#include"CurlEasy.hpp"
#include<fstream>
#include <filesystem>
#include<iostream>
#include<functional>
class Downloader{
    using ProgressCallback=std::function<void(size_t total, size_t now)>;
public:
    void setProgressCallback(ProgressCallback callback) {
        this->callback = std::move(callback);
    }
    void load(const std::string target_url,const std::string local_url);
    static size_t write_triple(void*ptr,size_t size,size_t nmemb,void*stream);
    void write_callback(void*ptr,size_t size);
    static int progress_triple(void* clientp,
                               curl_off_t dltotal,
                               curl_off_t dlnow,
                               curl_off_t ultotal,
                               curl_off_t ulnow);

    int progress_callback(curl_off_t dltotal, curl_off_t dlnow);

private:
    ProgressCallback callback;
    std::string target_url;
    std::string local_url;
    size_t downloaded_size{0};
    std::ofstream output_file;
    GlobalCurlGuard global_curl_guard;
    CurlSlist header_list;
    CurlEasy curl;
};