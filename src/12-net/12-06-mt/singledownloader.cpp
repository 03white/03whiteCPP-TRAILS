#include "singledownloader.h"
#include <stdexcept>

SingleDownloader::SingleDownloader(std::string target_url,
                                   std::string local_url,
                                   size_t start_pos,
                                   size_t end_pos) :
    target_url(std::move(target_url)),
    local_url(std::move(local_url)),
    start_pos(start_pos),
    end_pos(end_pos) {
    headers_.append("Range: bytes=" + std::to_string(start_pos) + "-"
                    + std::to_string(end_pos));

    subCurl.setopt(CURLOPT_URL, this->target_url.c_str());
    subCurl.setopt(CURLOPT_HTTPHEADER, headers_.get());
    subCurl.setopt(CURLOPT_WRITEFUNCTION, &SingleDownloader::write_triple);
    subCurl.setopt(CURLOPT_WRITEDATA, this);
    fp.open(this->local_url, std::ios::binary | std::ios::in | std::ios::out);
    if (!fp.is_open()) {
        throw std::runtime_error("Failed to open file: " + this->local_url);
    }
    fp.seekp(static_cast<std::streamoff>(start_pos));
    if (!fp) {
        throw std::runtime_error("seekp failed: " + this->local_url);
    }
}

SingleDownloader::~SingleDownloader() {
    if (fp.is_open()) {
        fp.flush();
        fp.close();
    }
}

CURLcode SingleDownloader::run() {
    CURLcode code = subCurl.perform();
    if (code != CURLE_OK)
        return code;
    long http_code = 0;
    curl_easy_getinfo(subCurl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 206) {
        return CURLE_HTTP_RETURNED_ERROR;
    }
    return CURLE_OK;
}

size_t SingleDownloader::write_triple(void* ptr,
                                      size_t size,
                                      size_t nmem,
                                      void* stream) {
    auto* self = static_cast<SingleDownloader*>(stream);
    return self->write_callback(ptr, size * nmem);
}

size_t SingleDownloader::write_callback(void* ptr, size_t size) {
    fp.write(static_cast<const char*>(ptr), size);
    if (!fp)
        return 0; // 触发 CURLE_WRITE_ERROR
    return size;
}
