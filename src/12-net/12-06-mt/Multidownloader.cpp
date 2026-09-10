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
                       size_t blockNum) 
                    :target_url(target)
                    ,local_url(local)
                    ,blockNum(blockNum) {
    
}

void MultiDownLoader::start() {
    this->initTotalSize();
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
            target_url, local_url, start, end));
    }

    for (size_t i = 0; i < loaders.size(); ++i) {
        threads.emplace_back([this, i] { this->results[i] = this->loaders[i]->run(); });
    }

}

void MultiDownLoader::wait() {
    for (auto& t : threads) {
        if (t.joinable())
            t.join();
    }
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

size_t MultiDownLoader::header_triple(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* self = static_cast<MultiDownLoader*>(stream);
    self->header_callback(ptr, size * nmemb);
    return size * nmemb;    
}

void MultiDownLoader::header_callback(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return;
    }
    std::string line(static_cast<char*>(ptr), size);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    };
    std::string line_lower = line;
    std::transform(line_lower.begin(),line_lower.end(),line_lower.begin(),
                        [](unsigned char c){
                        return std::tolower(c);});
    const std::string prefix = "content-length:";
    size_t pos = line_lower.find(prefix);
    if (pos == std::string::npos) {
        return;
    }
    std::string value_str = line_lower.substr(pos + prefix.length());                      
    size_t start = value_str.find_first_not_of(" \t");
    size_t end = value_str.find_last_not_of(" \t");
    if (start == std::string::npos) {
        return; // 没有值
    }
    value_str = value_str.substr(start, end - start + 1);
    try {
        this->total_size = std::stoll(value_str);
    } catch (const std::exception& e) {
    }

}