#include "ContentRange.hpp"
#include "check.hpp"

void test_content_range() {
    check::suite("ContentRange::parse");

    // ================= 示范：照这个格式往下填 =================
    {
        const auto cr = ContentRange::parse("bytes 0-0/1234");
        CHECK(cr.has_value());
        if (cr) {
            CHECK_EQ(cr->first, std::uint64_t{0});
            CHECK_EQ(cr->last, std::uint64_t{0});
            CHECK_EQ(cr->total, std::optional<std::uint64_t>{1234});
            CHECK(cr->has_range);
            CHECK(cr->matches(0, 0));
            CHECK_EQ(cr->length(), std::uint64_t{1});
        }
    }
    // =========================================================

    // ---- TODO: 合法输入 ----
    //
    //   输入                        first  last   total   备注
    //   "bytes 0-0/1234"            0      0      1234    ✔ 上面已示范
    //   "bytes 0-99/1234"           0      99     1234    多字节区间
    //   "bytes 500-999/1234"        500    999    1234    非零起点
    //   "bytes 0-0/*"               0      0      none    总长未知（动态内容）
    //   "bytes 1233-1233/1234"      1233   1233   1234    最后一个字节
    //   "  bytes 0-0/1234  "        0      0      1234    ← 你现在的实现能过吗？
    //
    // 最后一行是真问题：libcurl 喂给你的头值有没有可能带前后空白？
    // 先想，再写测试，最后决定要不要改 parse。

    // ---- TODO: 必须拒绝的输入 ----
    //
    //   ""                          空
    //   "bytes"                     没有区间
    //   "bytes 0-0"                 缺 /total
    //   "bytes /1234"               缺区间
    //   "0-0/1234"                  缺 "bytes " 前缀
    //   "items 0-0/1234"            单位不是 bytes（RFC 允许其它单位）
    //   "bytes 99-0/1234"           first > last
    //   "bytes a-b/c"               非数字
    //   "bytes -1-5/100"            负数
    //   "bytes 0-0/99999999999999999999999"   溢出 uint64
    //
    // 倒数第一条：stoull 溢出会抛 out_of_range，你的 catch(...) 接住了吗？
    // 接住之后返回的是 nullopt 还是一个半填充的 ContentRange？

    // ---- TODO: 416 的形式（已知缺口）----
    //
    //   "bytes */0"                 零长文件
    //   "bytes */1234"              请求区间越界
    //
    // 这两条是 RFC 9110 §14.4 明确规定的合法形式，专用于 416 响应，
    // 没有 '-'。你现在的实现会返回 nullopt —— Probe.cpp 里为此绕开了 parse，
    // 直接抠斜杠后面的数字。
    //
    // 先写这两个用例让它们**红**，再决定：
    //   (a) 扩展 parse 支持 has_range=false + total 的形式，Probe 那边的绕行删掉
    //   (b) 维持现状，但在 ContentRange.hpp 里写明「不支持 416 形式」
    // 两条路都行，别让缺口没有记录。

    // ---- TODO: matches / length / to_string ----
    //
    //   默认构造的 ContentRange（has_range=false）调 length() 返回什么？
    //   现在是 0-0+1 = 1。调用方拿到 1 不会察觉有问题。这是 bug 还是可接受？
    //   写个用例把你的决定固定下来。
    //
    //   to_string 应该是 parse 的逆：
    //     parse("bytes 0-99/1234")->to_string() == "bytes 0-99/1234"
    //     parse("bytes 0-0/*")->to_string()     == "bytes 0-0/*"
}
