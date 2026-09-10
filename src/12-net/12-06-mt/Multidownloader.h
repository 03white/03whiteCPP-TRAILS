#include<vector>
#include<thread>
#include<vector>
#include<memory>
#include<cmath>
#include<algorithm>
#include "CurlEasy.hpp"
#include "singledownloader.h"
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
class MultiDownLoader {
public:
    MultiDownLoader(const std::string& target, const std::string& local,size_t blockNum);
    void start();
    void wait(); // Wait for all threads to finish

private:
    // Initialize the total size of the file to be downloaded
    // by making a HEAD request to the target URL
    void initTotalSize(); 
    void preallocate_file();
    static size_t header_triple(void* ptr, size_t size, size_t nmemb, void* stream);
    void header_callback(void* ptr, size_t size);
    size_t blockNum{0};
    std::string target_url;
    std::string local_url;
    size_t total_size{0};
    std::vector<std::unique_ptr<SingleDownloader>> loaders; // 先建好
    std::vector<std::thread> threads;                       // 再启动
    std::vector<CURLcode> results;                          // 收集结果
};