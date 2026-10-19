#include "core/downloader.h"
#include "core/worker.h"
#include "net/httpsession.h"
#include "util/stringutil.h"
#include "entity/filemeta.h"
#include "core/piecemanager.h"
#include "repository/jsondownloadrepository.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;

Downloader::Downloader(const std::string& target_url,
                       const std::string& local_url,
                       size_t pieceSize,
                       size_t threadNum)
            : filedaoPtr_(std::make_shared<JSONDownloadRepository>(
              (fs::path(local_url)
               / (util::StringUtil::tailName(target_url) + ".meta.json")).string()))
            , threadNum_(threadNum) 
{
    if (pieceSize == 0) {
        throw std::invalid_argument("pieceSize must be greater than zero");
    }
    if (threadNum_ == 0) {
        throw std::invalid_argument("threadNum must be greater than zero");
    }
    if (!local_url.empty()) {
        fs::create_directories(local_url);
    }

    std::string metafile_name =
        util::StringUtil::tailName(target_url) + ".meta.json";
    if (util::StringUtil::hasFile(local_url, metafile_name)){
        fileMetaPtr_=filedaoPtr_->readFileMeta();//读取已有的元信息
    } else { //没有元信息
        fileMetaPtr_ = std::make_shared<FileMeta>();
        HttpSession httpSession;
        httpSession.setopt(CURLOPT_URL, target_url.c_str());
        httpSession.setopt(CURLOPT_NOBODY, 1L); // 只获取头部信息
        httpSession.setopt(CURLOPT_HEADERFUNCTION,
                           &Downloader::header_tramplie);
        httpSession.setopt(CURLOPT_HEADERDATA, this);
        httpSession.setopt(CURLOPT_FOLLOWLOCATION, 1L);
        httpSession.setopt(CURLOPT_FAILONERROR, 1L);
        const CURLcode code = httpSession.perform();
        if (code != CURLE_OK) {
            throw std::runtime_error("metadata request failed: "
                                     + std::string(curl_easy_strerror(code)));
        }
        fileMetaPtr_->name = util::StringUtil::tailName(target_url);
        fileMetaPtr_->target_url = target_url;
        fileMetaPtr_->local_url = local_url;
        fileMetaPtr_->pieceSize = pieceSize;

        fileMetaPtr_->pieceNum =
            (fileMetaPtr_->totalSize + fileMetaPtr_->pieceSize - 1)
            / fileMetaPtr_->pieceSize;
        for (size_t i = 0; i < fileMetaPtr_->pieceNum; i++) {
            PiecePtr piecePtr = std::make_shared<Piece>();
            piecePtr->index = i;
            piecePtr->downloaded = 0;
            piecePtr->state = PieceState::None;
            piecePtr->offset = i * pieceSize;
            piecePtr->length = std::min<uint64_t>(
                pieceSize, fileMetaPtr_->totalSize - piecePtr->offset);
            fileMetaPtr_->pieces.push_back(piecePtr);
        }
        filedaoPtr_->saveFileMeta(fileMetaPtr_); // 落盘
    }

    if (fileMetaPtr_->name.empty()) {
        fileMetaPtr_->name = util::StringUtil::tailName(fileMetaPtr_->target_url);
    }

    pieceManagerPtr_ = std::make_shared<PieceManager>(fileMetaPtr_);
    checkpointPtr_ = std::make_shared<Checkpoint>(
        pieceManagerPtr_, filedaoPtr_, std::chrono::seconds(1));
}

Downloader::~Downloader() {
    try {
        if (checkpointPtr_) {
            checkpointPtr_->stop();
        }
        for (auto& task : pieceTasks_) {
            if (task.joinable()) {
                task.join();
            }
        }
        if (checkpointTask_.joinable()) {
            checkpointTask_.join();
        }
    } catch (...) {
    }
}

void Downloader::preparePartFile(const fs::path& partPath) const {
    if (!fs::exists(partPath)) {
        const bool hasProgress = std::any_of(
            fileMetaPtr_->pieces.begin(),
            fileMetaPtr_->pieces.end(),
            [](const PiecePtr& piece) { return piece->downloaded != 0; });
        if (hasProgress) {
            throw std::runtime_error("metadata has progress but .part is missing: "
                                     + partPath.string());
        }

        std::ofstream createPart(partPath, std::ios::binary | std::ios::trunc);
        if (!createPart) {
            throw std::runtime_error("failed to create .part file: "
                                     + partPath.string());
        }
        createPart.close();
        fs::resize_file(
            partPath,
            fileMetaPtr_
                ->totalSize); // 不需要_WIN32来判断，resize_file在Windows和Linux下都可用
        return;
    }

    if (fs::file_size(partPath) != fileMetaPtr_->totalSize) {
        throw std::runtime_error(".part file size does not match metadata: "
                                 + partPath.string());
    }
}

void Downloader::start() {
    if (started_) {
        throw std::logic_error("Downloader::start may only be called once");
    }
    started_ = true;

    const fs::path outputDir(
        fileMetaPtr_
            ->local_url); // 使用fs::path替代std::string处理路径，避免手动拼接路径分隔符
    const fs::path partPath = outputDir / (fileMetaPtr_->name + ".part");
    const fs::path finalPath = outputDir / fileMetaPtr_->name;
    const fs::path metaPath = outputDir / (fileMetaPtr_->name + ".meta.json");

    if (!outputDir.empty()) {
        fs::create_directories(outputDir);
    }
    if (fs::exists(finalPath)) {
        throw std::runtime_error("target file already exists: "
                                 + finalPath.string());
    }

    preparePartFile(partPath);

    std::mutex errorMutex;
    std::exception_ptr firstError;
    auto recordError = [&](std::exception_ptr error) {
        {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (!firstError) {
                firstError = error;
            }
        }
        pieceManagerPtr_->cancel();
    }; // 一个记录异常的函数对象，线程安全地记录第一个异常，并取消下载任务。

    try {
        checkpointTask_ = std::thread([&] {
            try {
                checkpointPtr_->run();
            } catch (...) {
                recordError(std::current_exception());
            }
        });

        pieceTasks_.reserve(threadNum_);
        for (size_t i = 0; i < threadNum_; ++i) {
            pieceTasks_.emplace_back([&] {
                try {
                    Worker worker(pieceManagerPtr_,
                                  fileMetaPtr_->target_url,
                                  fileMetaPtr_->local_url,
                                  fileMetaPtr_->name);
                    worker.run();
                } catch (...) {
                    recordError(std::current_exception());
                }
            });
        }
    } catch (...) {
        recordError(std::current_exception());
    }

    for (auto& task : pieceTasks_) {
        if (task.joinable()) {
            task.join();
        }
    }

    checkpointPtr_->stop();
    if (checkpointTask_.joinable()) {
        checkpointTask_.join();
    }

    if (firstError) {
        std::rethrow_exception(firstError);
    }
    if (!pieceManagerPtr_->all_done()) {
        throw std::runtime_error("download stopped before all pieces completed");
    }

    fs::rename(partPath, finalPath);
    if (!fs::remove(metaPath)) {
        throw std::runtime_error("failed to remove metadata file: "
                                 + metaPath.string());
    }
}

size_t Downloader::header_tramplie(void* ptr,
                                 size_t nmem,
                                 size_t size,
                                 void* stream) {
    auto* self = static_cast<Downloader*>(stream);
    self->header_callback(ptr, nmem * size);
    return nmem * size;
}

void Downloader::header_callback(void* ptr, size_t size) {
    if (!ptr || size == 0)
        return;

    std::string line(static_cast<char*>(ptr), size);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();

    if (line.empty())
        return;

    std::string lower = line;
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto trim_value = [](const std::string& s, size_t from) -> std::string {
        std::string v = s.substr(from);
        size_t b = v.find_first_not_of(" \t");
        if (b == std::string::npos)
            return {};
        size_t e = v.find_last_not_of(" \t");
        return v.substr(b, e - b + 1);
    };

    static const std::string LenPrefix = "content-length:";
  /*  static const std::string DigestPrefix = "x-checksum-sha256:";*/

    // ---- content-length ----
    if (lower.rfind(LenPrefix, 0) == 0) {
        std::string v = trim_value(lower, LenPrefix.size());
        if (!v.empty()) {
            try {
                this->fileMetaPtr_->totalSize = static_cast<size_t>(std::stoull(v));
            } catch (const std::exception&) {}
        }
        return;
    }

    //// ---- x-checksum-sha256 ----
    //if (size_t pos = lower.find(DigestPrefix); pos != std::string::npos) {
    //    std::string v = trim_value(lower, pos + DigestPrefix.size());
    //    if (!v.empty())
    //        this->sha256sum = v;
    //    return;
    //}

}
