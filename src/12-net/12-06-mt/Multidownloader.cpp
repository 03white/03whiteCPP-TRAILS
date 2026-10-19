#include "Multidownloader.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>
#endif
MultiDownLoader::MultiDownLoader(const std::string& target,
                                 const std::string& local,
                                 size_t blockNum) :
    target_url(target), local_url(local), blockNum(blockNum) {
}

void MultiDownLoader::start() {
    this->initTotalSize();
    progress.total_size = total_size; // 设置总大小
    this->preallocate_file();
    loaders.reserve(blockNum); // 预留，避免重新分配
    threads.reserve(blockNum);
    results.assign(blockNum, CURLE_OK);

    size_t chunk = (total_size + blockNum - 1) / blockNum;
    for (size_t i = 0; i < blockNum; ++i) {
        size_t start = i * chunk;
        size_t end = std::min(start + chunk - 1, total_size - 1);
        if (start > end)
            break;
        loaders.push_back(std::make_unique<SingleDownloader>(
            target_url, local_url, start, end, &this->progress));
    }

    for (size_t i = 0; i < loaders.size(); ++i) {
        threads.emplace_back(
            [this, i] { this->results[i] = this->loaders[i]->run(); });
    }
}

void MultiDownLoader::wait() {
    for (auto& t : threads) {
        if (t.joinable())
            t.join();
    }
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i] != CURLE_OK) {
            throw std::runtime_error("Block " + std::to_string(i)
                                     + " failed, CURLcode="
                                     + std::to_string(results[i]));
        }
    }

    if (this->sha256sum.empty()) {
        std::cout << "No SHA256 checksum provided by the server." << std::endl;
        return;
    }

    std::ifstream ifs(local_url, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("Cannot open file for SHA256: " + local_url);
    }
    std::vector<char> data((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());

    EVP evp;
    evp.init(EVP_sha256());
    evp.update(data.data(), data.size());

    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_len = 0;
    evp.final(digest, digest_len);
    static const char* hex = "0123456789abcdef";
    std::string computed;
    computed.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        computed.push_back(hex[digest[i] >> 4]);
        computed.push_back(hex[digest[i] & 0xF]);
    }
    std::string expected = this->sha256sum;
    std::transform(expected.begin(),
                   expected.end(),
                   expected.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (computed != expected) {
        throw std::runtime_error("SHA256 checksum mismatch: got " + computed
                                 + ", expected " + expected);
    }
    std::cout << "SHA256 checksum verified successfully." << std::endl;
}

void MultiDownLoader::initTotalSize() {
    CurlEasy headCurl;
    headCurl.setopt(CURLOPT_URL, target_url.c_str());
    headCurl.setopt(CURLOPT_NOBODY, 1L); // 只获取头部信息
    headCurl.setopt(CURLOPT_HEADERFUNCTION, &MultiDownLoader::header_triple);
    headCurl.setopt(CURLOPT_HEADERDATA, this);
    headCurl.perform();
    if (total_size == 0) {
        throw std::runtime_error(
            "Failed to get content length from the server.");
    }
}

void MultiDownLoader::preallocate_file() {
    if (total_size == 0)
        return;
#if defined(_WIN32)
    HANDLE h = ::CreateFileA(local_url.c_str(),
                             GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        throw std::system_error(::GetLastError(),
                                std::system_category(),
                                "CreateFile failed: " + local_url);
    }

    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(total_size);
    if (!::SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
        DWORD err = ::GetLastError();
        ::CloseHandle(h);
        throw std::system_error(err,
                                std::system_category(),
                                "SetFilePointerEx failed: " + local_url);
    }
    if (!::SetEndOfFile(h)) {
        DWORD err = ::GetLastError();
        ::CloseHandle(h);
        throw std::system_error(
            err, std::system_category(), "SetEndOfFile failed: " + local_url);
    }
    ::CloseHandle(h);
#else
    int fd = ::open(local_url.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        throw std::system_error(
            errno, std::generic_category(), "open failed: " + local_url);
    }

    if (::ftruncate(fd, static_cast<off_t>(total_size)) != 0) {
        int err = errno;
        ::close(fd);
        throw std::system_error(
            err, std::generic_category(), "ftruncate failed: " + local_url);
    }

    ::close(fd);

#endif
}

size_t MultiDownLoader::header_triple(void* ptr,
                                      size_t size,
                                      size_t nmemb,
                                      void* stream) {
    auto* self = static_cast<MultiDownLoader*>(stream);
    self->header_callback(ptr, size * nmemb);
    return size * nmemb;
}

void MultiDownLoader::header_callback(void* ptr, size_t size) {
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
    static const std::string DigestPrefix = "x-checksum-sha256:";

    // ---- content-length ----
    if (size_t pos = lower.find(LenPrefix); pos != std::string::npos) {
        std::string v = trim_value(lower, pos + LenPrefix.size());
        if (!v.empty()) {
            try {
                this->total_size = static_cast<size_t>(std::stoull(v));
            } catch (const std::exception&) {}
        }
        return;
    }

    // ---- x-checksum-sha256 ----
    if (size_t pos = lower.find(DigestPrefix); pos != std::string::npos) {
        std::string v = trim_value(lower, pos + DigestPrefix.size());
        if (!v.empty())
            this->sha256sum = v;
        return;
    }
}