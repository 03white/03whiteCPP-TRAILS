#include"downloader.h"
#include<iomanip>
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <target_url> <local_url>"
                  << std::endl;
        return 1;
    }
    std::string target_url = argv[1];
    std::string local_url = argv[2];
    Downloader downloader;
    downloader.setProgressCallback([](curl_off_t dtotal, curl_off_t dnow) {
        const int barWidth = 50;

        if (dtotal <= 0) {
            // 总大小未知时，只显示已下载字节，末尾加空格覆盖旧输出
            std::cout << "\r loaded: " << dnow << " bytes    " << std::flush;
            return;
        }

        double progress = static_cast<double>(dnow) / dtotal;
        int pos = static_cast<int>(barWidth * progress);
        int percent = static_cast<int>(progress * 100.0);

        // 使用 string 构造函数生成固定宽度的进度条
        std::string bar(pos, '#');
        std::string space(barWidth - pos, ' ');

        // 一次性输出，末尾添加一些空格，确保覆盖上一行的多余字符
        std::cout << "\r[" << bar << space << "] " << std::setw(3) << percent
                  << "%   " << std::flush;
    });
    try {
        downloader.load(target_url, local_url);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}