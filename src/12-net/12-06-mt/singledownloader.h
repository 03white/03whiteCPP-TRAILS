#pragma once
#include <fstream>
#include <string>
#include "CurlEasy.hpp"

class SingleDownloader {
public:
    SingleDownloader(std::string target_url,
                     std::string local_url,
                     size_t start_pos,
                     size_t end_pos);
    ~SingleDownloader();

    CURLcode run();

private:
    static size_t write_triple(void* ptr, size_t size, size_t nmem, void* stream);
    size_t write_callback(void* ptr, size_t size);

    std::string target_url;
    std::string local_url;
    size_t start_pos{0};
    size_t end_pos{0};

    CurlSlist headers_; // 必须在 subCurl 之前，保证先构造后析构
    CurlEasy subCurl;
    std::ofstream fp;
};