#pragma once

// 解析 Content-Range 响应头（RFC 9110 §14.4）。
//
//   Content-Range: bytes 0-0/1234567 
//
#include <cstdint>
#include <optional>
#include <string>

struct ContentRange {
    bool          has_range = false;
    std::uint64_t first     = 0;
    std::uint64_t last      = 0;
    std::optional<std::uint64_t> total;

    static std::optional<ContentRange> parse(const std::string& value);//一个静态工厂方法
    std::uint64_t length() const noexcept;
    bool matches(std::uint64_t expect_first, std::uint64_t expect_last) const noexcept;
    std::string to_string() const;
};
