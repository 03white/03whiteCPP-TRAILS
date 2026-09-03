#pragma once

// 极简断言。故意不用 Catch2/gtest —— 先用够裸的东西，等你嫌它不好用了，
// 那份「不好用」就是你选框架时的判据。
//
// 和 assert 的区别（也是 assert 不适合做测试的原因）：
//   - 失败不中止，跑完全部用例再汇总。assert 挂在第一个失败上，
//     你永远只能看到一个错，修一个跑一次。
//   - 打印实际值和期望值。assert 只告诉你「表达式为假」。
//   - 计数，main 靠失败数决定退出码，CTest 才能判定通过与否。
//
// 用法：
//   CHECK(cr.has_value());
//   CHECK_EQ(cr->first, 0ull);
//   CHECK_EQ(name, std::string("a.bin"));

#include "DownloadPlan.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

namespace check {

// C++17 inline 变量：头文件里定义全局状态而不违反 ODR。
inline int g_checks   = 0;
inline int g_failures = 0;

// ---- 把值转成可打印的字符串 ----
// 声明全部前置，因为 optional 的版本要调用其它版本。
template <typename T> std::string show(const T& v);
inline std::string show(bool b);
inline std::string show(const std::string& s);
inline std::string show(const char* s);
inline std::string show(TransferMode m);
template <typename T> std::string show(const std::optional<T>& o);

template <typename T> std::string show(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else {
        return std::to_string(v);
    }
}
inline std::string show(bool b)                { return b ? "true" : "false"; }
inline std::string show(const std::string& s)  { return "\"" + s + "\""; }
inline std::string show(const char* s)         { return s ? ("\"" + std::string(s) + "\"") : "(null)"; }
inline std::string show(TransferMode m)        { return to_string(m); }

template <typename T> std::string show(const std::optional<T>& o) {
    return o ? ("Some(" + show(*o) + ")") : "None";
}

inline void pass() { ++g_checks; }

inline void fail(const char* file, int line, const std::string& expr,
                 const std::string& detail) {
    ++g_checks;
    ++g_failures;
    std::cout << "  FAIL  " << file << ":" << line << "\n"
              << "        " << expr << "\n";
    if (!detail.empty()) {
        std::cout << "        " << detail << "\n";
    }
}

// 每个用例文件开头调一次，输出里好分段。
inline void suite(const char* name) {
    std::cout << "[" << name << "]" << std::endl;
}

inline int summary() {
    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed";
    if (g_failures > 0) {
        std::cout << "  (" << g_failures << " FAILED)";
    }
    std::cout << std::endl;
    return g_failures == 0 ? 0 : 1;
}

}   // namespace check

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            ::check::pass();                                                   \
        } else {                                                               \
            ::check::fail(__FILE__, __LINE__, "CHECK(" #cond ")", "");         \
        }                                                                      \
    } while (false)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const auto& a_ = (actual);                                             \
        const auto& e_ = (expected);                                           \
        if (a_ == e_) {                                                        \
            ::check::pass();                                                   \
        } else {                                                               \
            ::check::fail(__FILE__, __LINE__,                                  \
                          "CHECK_EQ(" #actual ", " #expected ")",              \
                          "got " + ::check::show(a_) +                         \
                              ", want " + ::check::show(e_));                  \
        }                                                                      \
    } while (false)
