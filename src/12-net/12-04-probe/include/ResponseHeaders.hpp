#pragma once

// 收集一次 HTTP 响应的头字段。
//
// 两件事想清楚再动手：
//   1. 头字段名大小写不敏感（RFC 9110 §5.1）。这对内部怎么存、怎么查有什么要求？
//   2. 跟随重定向时，CURLOPT_HEADERFUNCTION 会把 **每一跳** 的头都喂进来。
//      30x 那一跳也有 Content-Length。不处理的话会发生什么？

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ResponseHeaders {
    using Entry = std::pair<std::string, std::string>;   // {小写名, 原始值}
public:
    std::optional<std::string> get(const std::string& name) const;
    std::vector<std::string>   get_all(const std::string& name) const;
    bool                       has(const std::string& name) const;
    int status_code() const noexcept { return status_code_; }
    const std::vector<Entry>& entries() const noexcept { return entries_; }
    void clear() noexcept;
    //static method
    static std::size_t curl_callback(char* buffer, std::size_t size,
                                     std::size_t nitems, void* userdata);
    static std::string to_lower(std::string s);
    static std::string trim(const std::string& s);
private:
    std::vector<Entry> entries_;
    int                status_code_ = 0;
    void add_line(const char* data, std::size_t size);
};
