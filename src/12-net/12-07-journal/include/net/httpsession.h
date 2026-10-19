#ifndef HTTPSESSION_H
#define HTTPSESSION_H
#include <curl/curl.h>
#include <string>
#include <memory>
// curl_global_init / curl_global_cleanup 必须成对，且全进程只做一次。
// 在 main 里最早构造、最晚析构（所有 CurlEasy 都要活在它的生命期内）。
class GlobalCurlGuard {
public:
    GlobalCurlGuard();
    ~GlobalCurlGuard();

    GlobalCurlGuard(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard& operator=(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard(GlobalCurlGuard&&) = delete;
    GlobalCurlGuard& operator=(GlobalCurlGuard&&) = delete;
};

// curl_slist 的 RAII 包装。加自定义请求头（Range: bytes=0-0 等）时用。
// 想清楚：libcurl 对这个链表是拷贝还是只存指针？它必须活多久？
class HttpHeaderlist {
public:
    HttpHeaderlist() = default;
    ~HttpHeaderlist();

    HttpHeaderlist(const HttpHeaderlist&) = delete;
    HttpHeaderlist& operator=(const HttpHeaderlist&) = delete;
    HttpHeaderlist(HttpHeaderlist&& other) noexcept;
    HttpHeaderlist& operator=(HttpHeaderlist&& other) noexcept;

    void append(const std::string& header); // 形如 "Range: bytes=0-0"
    curl_slist* get() const noexcept {
        return list_;
    }
    void reset() noexcept;

private:
    curl_slist* list_{nullptr};
};

class HttpSession {
public:
    HttpSession();
    ~HttpSession() = default;
    HttpSession(const HttpSession&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;
    HttpSession(HttpSession&&) = default;
    HttpSession& operator=(HttpSession&&) = default;

    CURL* get() const noexcept {
        return httpSession_.get();
    }
    template <typename T>
    void setopt(CURLoption option, T value) {
        CURLcode code = curl_easy_setopt(httpSession_.get(), option, value);
        if (code != CURLE_OK) {
            throw_setopt_failed(code);
        }
    }
    CURLcode perform() noexcept;
    void reset() noexcept;

private:
    struct DeleteCurl {
        void operator()(CURL* curl) const {
            if (curl) {
                curl_easy_cleanup(curl);
            }
        }
    };
    [[noreturn]] static void throw_setopt_failed(CURLcode code);
    std::unique_ptr<CURL, DeleteCurl> httpSession_;
    std::unique_ptr<HttpHeaderlist> curlslist_;
};
#endif // !HTTPSESSION_H