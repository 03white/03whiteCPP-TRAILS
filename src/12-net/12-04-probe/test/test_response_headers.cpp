#include "ResponseHeaders.hpp"
#include "check.hpp"

namespace {

// 模拟 libcurl 的 HEADERFUNCTION 回调：一次一行，行尾带 CRLF。
// 有了它，整个「收一次 HTTP 响应」的过程都不需要网络。
void feed(ResponseHeaders& h, const std::string& line) {
    const std::string raw = line + "\r\n";
    h.add_line(raw.data(), raw.size());
}

}   // namespace

void test_response_headers() {
    check::suite("ResponseHeaders");

    // ================= 示范：单跳响应 =================
    {
        ResponseHeaders h;
        feed(h, "HTTP/1.1 206 Partial Content");
        feed(h, "Content-Range: bytes 0-0/1234");
        feed(h, "Accept-Ranges: bytes");
        feed(h, "");                        // 头区结束的空行

        CHECK_EQ(h.status_code(), 206);
        CHECK_EQ(h.get("content-range"), std::optional<std::string>{"bytes 0-0/1234"});
        CHECK_EQ(h.get("Content-Range"), std::optional<std::string>{"bytes 0-0/1234"});
        CHECK_EQ(h.get("nonexistent"), std::optional<std::string>{});
        CHECK(h.has("accept-ranges"));
        CHECK_EQ(h.entries().size(), std::size_t{2});
    }
    // =================================================

    // ---- TODO: 大小写不敏感（RFC 9110 §5.1）----
    //
    //   喂 "CoNtEnT-LeNgTh: 5"，用 get("content-length") / get("CONTENT-LENGTH")
    //   / get("Content-Length") 都要拿到 "5"。
    //   顺带确认：entries() 里存的 key 是小写的吗？

    // ---- TODO: 值的前后空白 ----
    //
    //   "ETag:   \"abc\"   "  ->  get("etag") 应该是 "\"abc\""，不带空白。
    //   注意别把值**内部**的空格也吃掉："Last-Modified: Wed, 26 Aug 2026 09:15:33 GMT"

    // ---- TODO: 重定向多跳（这一条最重要）----
    //
    //   feed "HTTP/1.1 302 Found"
    //   feed "Content-Length: 0"
    //   feed "Location: /ranged/x.bin"
    //   feed ""
    //   feed "HTTP/1.1 206 Partial Content"
    //   feed "Content-Range: bytes 0-0/104857600"
    //   feed ""
    //
    //   断言：
    //     status_code() == 206                       不是 302
    //     get("content-length") == None              302 那跳的 0 必须消失
    //     get("location") == None                    302 那跳的头一个都不该留
    //     entries().size() == 1
    //
    //   这是整个类存在的理由。不重置的话 total_size 会变成 0，
    //   下载器认为文件是空的，安静地产出一个 0 字节文件。
    //
    //   多跳版本也测一下：302 -> 301 -> 200，只有最后一跳该留下。

    // ---- TODO: 同名头出现多次 ----
    //
    //   feed "Set-Cookie: a=1"
    //   feed "Set-Cookie: b=2"
    //   get("set-cookie")      -> 取哪一个？你的实现取最后一个，写用例固定它
    //   get_all("set-cookie")  -> 两个都要，顺序是喂进去的顺序

    // ---- TODO: 畸形输入不能崩 ----
    //
    //   ""                     空行（头区结束）
    //   "no-colon-here"        没有冒号
    //   ": empty-name"         冒号在最前面，字段名为空
    //   "X-Empty:"             有名无值 -> 值是空串，但这个头**存在**
    //   "  continued value"    obs-fold 续行（以空白开头）
    //   add_line(nullptr, 0)   空指针
    //
    //   "X-Empty:" 那条要想清楚：has("x-empty") 该返回 true 还是 false？
    //   HTTP 里「头存在但值为空」和「头不存在」是两回事。

    // ---- TODO: 状态行的变体 ----
    //
    //   "HTTP/1.0 200 OK"
    //   "HTTP/2 206"                   没有 reason phrase
    //   "HTTP/1.1 404 Not Found"
    //   "HTTP/1.1 500 Internal Server Error"
    //
    //   注意 HTTP/2 那条：版本段长度和 HTTP/1.1 不一样。
    //   如果你按固定偏移取状态码就会挂 —— 写个用例确认没踩这个坑。

    // ---- TODO: curl_callback 的返回值 ----
    //
    //   ResponseHeaders h;
    //   const char line[] = "X: y\r\n";
    //   CHECK_EQ(ResponseHeaders::curl_callback(const_cast<char*>(line),
    //                                           1, sizeof(line) - 1, &h),
    //            sizeof(line) - 1);
    //
    //   返回值 != size*nitems 时 libcurl 会让 perform 返回 CURLE_WRITE_ERROR。
    //   再测一次 userdata 传 nullptr：不能崩，且仍要返回全量。

    // ---- TODO: clear() ----
    //
    //   收完一堆头之后 clear()，status_code() 归零、entries() 空。
    //   Probe 复用同一个 ResponseHeaders 跑两枪，靠的就是它。
}
