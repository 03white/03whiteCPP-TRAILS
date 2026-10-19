#pragma once
#include <fstream>
#include <string>
#include "CurlEasy.hpp"
#include "progress.h"
class SingleDownloader {
public:
    SingleDownloader(std::string target_url,
                     std::string local_url,
                     size_t start_pos,
                     size_t end_pos,
                     Progress* progress);
    ~SingleDownloader();
    CURLcode run();

private:
    static size_t
    write_triple(void* ptr, size_t size, size_t nmem, void* stream);
    size_t write_callback(void* ptr, size_t size);
    static size_t progress_triple(void* clientp,
                                  curl_off_t dltotal,
                                  curl_off_t dlnow,
                                  curl_off_t ultotal,
                                  curl_off_t ulnow);
    size_t progress_callback(curl_off_t dltotal, curl_off_t dlnow);
    std::string target_url;
    std::string local_url;
    size_t start_pos{0};       // range开始字节
    size_t end_pos{0};         // range结束字节
    size_t downloaded_size{0}; // 已下载的字节数
    Progress* progress;        // 指向Progress对象的指针
    CurlSlist headers_;        // 必须在 subCurl 之前，保证先构造后析构
    CurlEasy subCurl;
    std::ofstream fp; // 文件流对象，用于写入下载的数据
};