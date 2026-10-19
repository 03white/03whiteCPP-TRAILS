#include "Multidownloader.h"
#include "CurlEasy.hpp"
#include <iostream>
int main(int argc, char* argv[]) {
    GlobalCurlGuard globalCurlGuard; // Ensure curl_global_init is called before
                                     // any other curl functions
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <target_url> <local_path>"
                  << std::endl;
        return 1;
    }
    std::string target_url = argv[1];
    std::string local_path = argv[2];
    size_t block_num = 4; // Number of threads to use
    try {
        MultiDownLoader downloader(target_url, local_path, block_num);
        downloader.setProgressCallback([](size_t total, size_t now) {
            std::cout << "\rProgress: " << now << "/" << total << " bytes ("
                      << (total > 0 ? (now * 100 / total) : 0) << "%)"
                      << std::flush;
        });
        downloader.start();
        downloader.wait();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}