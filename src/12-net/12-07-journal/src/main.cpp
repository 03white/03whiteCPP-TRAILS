#include "core/downloader.h"
#include "net/httpsession.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        std::cerr << "Usage: 12-07-journal <url> <output-dir> "
                     "[piece-size] [thread-count]\n";
        return 2;
    }

    try {
        const uint64_t pieceSize = argc >= 4
            ? std::stoull(argv[3])
            : 4ULL * 1024 * 1024;
        const unsigned int detectedThreads = std::thread::hardware_concurrency();
        const size_t threadCount = argc >= 5
            ? static_cast<size_t>(std::stoull(argv[4]))
            : (detectedThreads == 0 ? 1U : detectedThreads);

        GlobalCurlGuard curlGuard;
        Downloader downloader(argv[1], argv[2], pieceSize, threadCount);
        downloader.start();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Download failed: " << error.what() << '\n';
        return 1;
    }
}
