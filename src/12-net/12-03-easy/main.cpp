#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>
#include <curl/curl.h>


class GlobalCurlGuard {
public:
    GlobalCurlGuard() {
        CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed: " +
                                     std::string(curl_easy_strerror(code)));
        }
    }
    ~GlobalCurlGuard() { curl_global_cleanup(); }
    GlobalCurlGuard(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard& operator=(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard(GlobalCurlGuard&&) = delete;
    GlobalCurlGuard& operator=(GlobalCurlGuard&&) = delete;
};
struct CurlDeleter {
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};
using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;

class Downloader {
public:
    Downloader() : curl_(curl_easy_init()) {
        if (!curl_) {
            throw std::runtime_error("curl_easy_init failed");
        }
    }
    bool download(const std::string& url, const std::string& output_path) {
        file_.open(output_path, std::ios::binary);
        if (!file_.is_open()) {
            std::cerr << "can't open file: " << output_path << std::endl;
            return false;
        }
        // ----- 3b. 设置 libcurl 选项 -----
        curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_.get(), CURLOPT_FOLLOWLOCATION, 1L);   // 自动跟随重定向

        // ---- 写回调（数据体） ----
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, &Downloader::write_trampoline);
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, this);
        // ---- 进度回调 ----
        // 注意：必须将 NOPROGRESS 设为 0L，否则进度回调不会触发！
        curl_easy_setopt(curl_.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_.get(), CURLOPT_XFERINFOFUNCTION, &Downloader::progress_trampoline);
        curl_easy_setopt(curl_.get(), CURLOPT_XFERINFODATA, this);

        // ----- 3c. 执行下载 -----
        CURLcode res = curl_easy_perform(curl_.get());

        // ----- 3d. 关闭文件（flush 并释放资源） -----
        // close() 内部会 flush，flush 失败同样会置 badbit——最后一批数据可能
        // 还压在缓冲里没落盘，所以要在 close 之后再判一次流状态。
        file_.close();
        const bool disk_ok = static_cast<bool>(file_);

        if (res == CURLE_OK) {
            if (!disk_ok) {
                std::cerr << "\n\n:transfer ok but flush failed, file is INCOMPLETE: "
                          << output_path << std::endl;
                return false;
            }
            std::cout << "\n\ncomplete!: file is in: " << output_path<< std::endl;
            return true;
        } else if (res == CURLE_ABORTED_BY_CALLBACK) {
            // 42：来自进度回调返回非 0，也就是用户主动取消
            std::cout << "\n\n:download is canceled by user"
                      << " (CURLE_ABORTED_BY_CALLBACK=" << res << ")" << std::endl;
            // 删除未完成的文件（可选）
            // std::remove(output_path.c_str());
            return false;
        } else if (res == CURLE_WRITE_ERROR) {
            // 23：来自写回调返回短计数，现在它只可能意味着一件事——本地写盘失败
            std::cerr << "\n\n:local write failed (CURLE_WRITE_ERROR=" << res
                      << ") — 磁盘满 / 路径不可写？" << std::endl;
            return false;
        } else {
            std::cerr << "\n\n:download is failure: " << curl_easy_strerror(res)
                      << " (code=" << res << ")" << std::endl;
            return false;
        }
    }
    void cancel() {
        cancelled_ = true;
        std::cout << "\n[cancel request is sending]" << std::endl;
    }

private:
    CurlPtr curl_;
    std::ofstream file_;
    std::atomic<bool> cancelled_{false};
    int last_percent_ = -1;

    static size_t write_trampoline(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* self = static_cast<Downloader*>(userdata);
        return self->write_member(ptr, size, nmemb);
    }

    size_t write_member(char* ptr, size_t size, size_t nmemb) {
        // 这里【不】检查 cancelled_。
        //
        // 写回调的返回值在 libcurl 眼里只回答一个问题：“这块数据你消费了多少字节”。
        // 返回值 != size*nmemb 的唯一含义是“没接住”，perform 会返回 CURLE_WRITE_ERROR(23)。
        // 如果让它兼职表达“用户想取消”，那么“磁盘满”和“用户按了取消”会得到同一个
        // 错误码，事后无法区分；而且写回调触发频率远高于进度回调，取消几乎总是先撞
        // 到这里，perform 返回 23 而不是验收要求的 CURLE_ABORTED_BY_CALLBACK(42)。
        //
        // 取消统一走进度回调（见 progress_member）：一个信号只表达一个含义。
        const size_t total = size * nmemb;

        file_.write(ptr, static_cast<std::streamsize>(total));
        if (!file_) {
            // 唯一该返回短计数的场合：真的没写进去（磁盘满、句柄失效）。
            return 0;
        }
        return total;
    }
    static int progress_trampoline(void* userdata,
                                   curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
        auto* self = static_cast<Downloader*>(userdata);
        return self->progress_member(dltotal, dlnow);
    }
    int progress_member(curl_off_t dltotal, curl_off_t dlnow) {
        // 如果外部调用了 cancel()，则返回 1 中止传输
        if (cancelled_) return 1;
        if (dltotal > 0) {
            int percent = static_cast<int>((dlnow * 100) / dltotal);
            // 防止刷屏：只有百分比变化时才打印（或每 1% 打印）
            if (percent != last_percent_) {
                last_percent_ = percent;
                std::cerr << "\r download ...: " << percent << "%  (" 
                          << (dlnow / 1024) << " KB / " 
                          << (dltotal / 1024) << " KB)   " << std::flush;
            }
        } else {
            // 服务器没返回 Content-Length，只显示已下载字节数
            std::cerr << "\r done: " << (dlnow / 1024) << " KB" << std::flush;
        }
        return 0;   // 0 表示继续
    }
};

// 用法: 12-03-easy [url] [输出路径] [取消秒数]
//
//   12-03-easy
//   12-03-easy http://127.0.0.1:8000/test_100m.bin out.bin
//   12-03-easy http://127.0.0.1:8000/test_100m.bin out.bin 2
//     └ 第三个参数 > 0 时，另起一个线程在 N 秒后调用 cancel()。
//       用来验收“主动取消 → CURLE_ABORTED_BY_CALLBACK(42)”这一条。
//       测试服务器: cd src/12-net/testdata && python -m http.server 8000
int main(int argc, char* argv[]) {
    const std::string url    = (argc > 1) ? argv[1] : "http://127.0.0.1:8000/test_1g.bin";
    const std::string output = (argc > 2) ? argv[2] : "downloaded.bin";
    int cancel_after = 0;
    if (argc > 3) {
        try { cancel_after = std::stoi(argv[3]); } catch (...) { cancel_after = 0; }
    }

    try {
        GlobalCurlGuard curl_guard;
        Downloader downloader;

        // 取消是跨线程的：主线程阻塞在 curl_easy_perform 里，
        // 只能由另一个线程翻 cancelled_ 这个 atomic<bool>。
        std::thread canceller;
        if (cancel_after > 0) {
            std::cout << "[will cancel after " << cancel_after << "s]" << std::endl;
            canceller = std::thread([&downloader, cancel_after] {
                std::this_thread::sleep_for(std::chrono::seconds(cancel_after));
                downloader.cancel();
            });
        }

        const bool success = downloader.download(url, output);

        // 下载可能先于计时结束，这里必须 join，别让线程带着 downloader 的
        // 引用活过 main 的作用域。
        if (canceller.joinable()) canceller.join();

        return success ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }
}