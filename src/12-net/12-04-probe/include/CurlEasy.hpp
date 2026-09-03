#pragma once
#include <curl/curl.h>

#include <memory>
#include <string>
#include <cstdint>

// curl_global_init / curl_global_cleanup 必须成对，且全进程只做一次。
// 在 main 里最早构造、最晚析构（所有 CurlEasy 都要活在它的生命期内）。
class GlobalCurlGuard {
public:
    GlobalCurlGuard();
    ~GlobalCurlGuard();

    GlobalCurlGuard(const GlobalCurlGuard&)            = delete;
    GlobalCurlGuard& operator=(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard(GlobalCurlGuard&&)                 = delete;
    GlobalCurlGuard& operator=(GlobalCurlGuard&&)      = delete;
};

// curl_slist 的 RAII 包装。加自定义请求头（Range: bytes=0-0 等）时用。
// 想清楚：libcurl 对这个链表是拷贝还是只存指针？它必须活多久？
class CurlSlist {
public:
    CurlSlist() = default;
    ~CurlSlist();

    CurlSlist(const CurlSlist&)            = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
    CurlSlist(CurlSlist&& other) noexcept;
    CurlSlist& operator=(CurlSlist&& other) noexcept;

    void append(const std::string& header);   // 形如 "Range: bytes=0-0"
    curl_slist* get() const noexcept { return list_; }
    void reset() noexcept;

private:
    curl_slist* list_{nullptr};
};

class CurlEasy {
public:
    CurlEasy();
    ~CurlEasy() = default;
    CurlEasy(const CurlEasy&)            = delete;
    CurlEasy& operator=(const CurlEasy&) = delete;
    CurlEasy(CurlEasy&&)                 = default;
    CurlEasy& operator=(CurlEasy&&)      = default;

    CURL* get() const noexcept { return handle_.get(); }
    template <typename T>
    void setopt(CURLoption option, T value) {
        CURLcode code = curl_easy_setopt(handle_.get(), option, value);
        if (code != CURLE_OK) {
            throw_setopt_failed(option, code);
        }
    }
    long        get_info_long(CURLINFO info) const;
    std::string get_info_string(CURLINFO info) const;
    curl_off_t  get_info_offt(CURLINFO info) const;
    CURLcode perform() noexcept;
    void reset() noexcept;
private:
    struct Deleter {
        void operator()(CURL* handle) const noexcept {
            if (handle) {
                curl_easy_cleanup(handle);
            }
        }
    };
    [[noreturn]] static void throw_setopt_failed(CURLoption option, CURLcode code);
    std::unique_ptr<CURL, Deleter> handle_;
};
