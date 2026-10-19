#include "core/worker.h"
#include "net/httpsession.h"
#include "entity/piece.h"
#include "util/stringutil.h"
#include <filesystem>
#include <stdexcept>

Worker::Worker(PieceManagerPtr pieceManagerPtr,
    const std::string& target_url,
    const std::string& local_url,
    const std::string& file_name) 
    :pieceManagerPtr_(pieceManagerPtr)
    ,target_url_(target_url)
    ,local_url_(local_url)
    ,file_name_(file_name) {
    const auto partPath =
        std::filesystem::path(local_url_) / (file_name_ + ".part");
    f_.open(partPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!f_.is_open()) {
        throw std::runtime_error("Failed to open file: " + partPath.string());
    }
}
void Worker::run(){
    while (true) {
        auto acquired = pieceManagerPtr_->acquire();
        if (!acquired) {
            break;
        }
        currentPiece_ = *acquired;
        writeFailed_ = false;
        callbackError_ = nullptr;

        //定位本地文件分片偏移
        const auto start = currentPiece_->offset + currentPiece_->downloaded;
        const auto end = currentPiece_->offset + currentPiece_->length - 1;
        f_.clear();
        f_.seekp(static_cast<std::streamoff>(start));
        if (!f_) {
            throw std::runtime_error("seekp failed: " + this->local_url_);
        }
        //构造range头，发送网络请求
        auto range = util::StringUtil::buildRange(start, end);
        HttpSession httpsession;
        HttpHeaderlist headerList;
        headerList.append(range);
        httpsession.setopt(CURLOPT_HTTPHEADER, headerList.get());
        httpsession.setopt(CURLOPT_URL,target_url_.c_str());
        httpsession.setopt(CURLOPT_FOLLOWLOCATION, 1L);
        httpsession.setopt(CURLOPT_FAILONERROR, 1L);
        httpsession.setopt(CURLOPT_NOSIGNAL, 1L);
        httpsession.setopt(CURLOPT_WRITEFUNCTION, &Worker::write_trampoline);
        httpsession.setopt(CURLOPT_WRITEDATA, this);
        const CURLcode code = httpsession.perform();
        if (callbackError_) {
            std::rethrow_exception(callbackError_);
        }
        if (writeFailed_) {
            throw std::runtime_error("failed to write downloaded piece");
        }
        if (code != CURLE_OK) {
            throw std::runtime_error("download failed: "
                                     + std::string(curl_easy_strerror(code)));
        }
        if (currentPiece_->downloaded != currentPiece_->length) {
            throw std::runtime_error("downloaded piece size mismatch");
        }
    }
}
size_t Worker::write_trampoline(void* ptr, size_t size, size_t nmem, void* stream) {
    auto* self = static_cast<Worker*>(stream);
    return self->write_callback(ptr, size * nmem);
}
size_t Worker::write_callback(void* ptr, size_t size) noexcept {
    try {
        const uint64_t remaining = currentPiece_->length
                                 - currentPiece_->downloaded;
        if (size > remaining) {
            writeFailed_ = true;
            return 0;
        }

        f_.write(static_cast<char*>(ptr), static_cast<std::streamsize>(size));
        f_.flush();
        if (!f_) {
            writeFailed_ = true;
            return 0;
        }

        pieceManagerPtr_->advanceDownloaded(currentPiece_, size);
        return size;
    } catch (...) {
        callbackError_ = std::current_exception();
        return 0;
    }
}
