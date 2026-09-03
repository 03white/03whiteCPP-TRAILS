# 12-04-probe —— 下载前的探测

承接 [12-01-curl](../12-01-curl/readme.md) 的 libcurl 环境搭建。这一篇做的是下载器的**第一步**：在真正开始传输之前，先搞清楚「这个 URL 该怎么下」。

环境：Windows 11 / MSVC 14.51（VS 2026）/ vcpkg `x64-windows` / libcurl 8.21.0。

---

## 1. 为什么需要探测

下载一个文件，看起来只要 `curl_easy_perform` 就完事了。但只要想做得比 `curl -O` 好一点，立刻会撞上三个问题：

| 你想做的事 | 前提条件 |
| --- | --- |
| 显示进度百分比 | 得知道**总字节数** |
| 断点续传 | 服务端得支持 **Range 请求** |
| 多线程分块加速 | 上面两个**都**要满足 |

这三个前提没有一个是能假设的。同一个 URL，换个 CDN 节点、换个反向代理，能力就可能不一样。所以必须先发几个请求问一问 —— 这就是探测。

探测的产出是一份 `DownloadPlan`：一份「怎么下这个文件」的完整说明书。后续阶段（12-05 续传、12-06 多线程）**以它为唯一输入，不再自己碰网络**。这个边界很重要：探测把「网络世界的不确定性」收敛成一个确定的数据结构，下游只跟数据结构打交道。

### 1.1 三条下载路径

```cpp
enum class TransferMode {
    Unknown,            // 探测失败
    RangedParallel,     // 多线程分块 + 断点续传
    SingleResumable,    // 单线程
    StreamingChunked    // 单线程流式，无进度百分比，不可续传
};
```

四种「总长已知/未知 × 支持 Range/不支持」的组合，为什么只有三档？因为**总长未知时，支不支持 Range 已经不重要了** —— 算不出切点就分不了块，没有分母就没有百分比。那两种组合塌到同一档。

| `total_size` | `supports_range` | mode |
| --- | --- | --- |
| 已知（>0） | true | `RangedParallel` |
| 已知（>0） | false | `SingleResumable` |
| 已知（=0） | 任意 | `SingleResumable`（零长文件切不动） |
| 未知 | 任意 | `StreamingChunked` |

---

## 2. 使用

### 2.1 构建

```shell
# 需要从 x64 Native Tools Command Prompt 启动，或先 call vcvars64.bat
cmake --preset msvc-debug
cmake --build --preset msvc-debug --target 12-04-probe
```

产物在 `build/bin/12-04-probe.exe`。

### 2.2 运行

```shell
12-04-probe <url> [-v]
```

`-v` 打开 `CURLOPT_VERBOSE`，会打印完整的请求/响应头 —— 判断结果对不对时先看这个。

```
$ 12-04-probe http://127.0.0.1:8001/ranged/test_100m.bin
---- download plan ----
requested url : http://127.0.0.1:8001/ranged/test_100m.bin
effective url : http://127.0.0.1:8001/ranged/test_100m.bin
probe status  : 206
total size    : 104857600 bytes
range support : yes (206 verified)
accept-ranges : bytes   <- 仅供参考，不作判据
etag          : "6400000-6a8eaeb5"
last-modified : Wed, 26 Aug 2026 09:15:33 GMT
content-type  : application/octet-stream
filename      : test_100m.bin
mode          : RangedParallel
notes:
  206 已验证，总长来自 Content-Range: 104857600
-----------------------
```

退出码：`0` = 拿到结论，`1` = `Unknown` 或异常，`2` = 参数错误。

`notes` 字段是排查的主要线索 —— 它记录了「用了哪一枪、为什么这么判、有没有发现矛盾」。

### 2.3 测试服务器

Python 自带的 `http.server` **不支持 Range**，只能用来验「服务端忽略 Range」这一类。四类验收目标要用专门的测试床：

```shell
cd ../testdata
python probe_server.py 8001
```

| 路由 | 模拟的服务端行为 | 期望 mode |
| --- | --- | --- |
| `/ranged/<file>` | 正常支持 Range，返回 206 | `RangedParallel` |
| `/ignore/<file>` | 收到 Range 也返回 200 + 全文 | `SingleResumable` |
| `/chunked` | `Transfer-Encoding: chunked`，无总长 | `StreamingChunked` |
| `/redirect/<file>` | 302 跳到 `/ranged/<file>` | `RangedParallel` + effective_url 跟完 |
| `/empty` | 零长文件，Range 落空 → 416 | `SingleResumable` |
| `/liar/<file>` | 声称 `Accept-Ranges: bytes` 但返回 200 | `SingleResumable` |
| `/cd` | `Content-Disposition` 塞路径穿越 + 中文名 | 文件名 = `中文 报告.bin` |

可用文件：`test_100m.bin`、`test_1g.bin`（`make_testfile.py` 生成）。

### 2.4 单元测试

```shell
cmake --build --preset msvc-debug --target 12-04-probe-test
ctest --test-dir build/msvc-debug --output-on-failure
```

单测**全部不联网**，见第 6 节。

---

## 3. 设计

### 3.1 数据流

```
ProbeContext  ──►  Probe::run()  ──►  DownloadPlan
  纯输入参数        唯一有 I/O 的地方      纯输出结果
```

两头都是纯数据结构，无行为。中间那一层是唯一碰网络的地方。这个形状让下游能拿一份手工构造的 `DownloadPlan` 做测试，完全不需要服务端。

### 3.2 模块

| 文件 | 职责 | 依赖 curl？ |
| --- | --- | --- |
| `CurlEasy.hpp/cpp` | libcurl 的 RAII 包装（三件套） | ✔ |
| `ResponseHeaders.hpp/cpp` | 收集一次响应的头字段，处理多跳 | ✘ |
| `ContentRange.hpp/cpp` | 解析 `Content-Range`（RFC 9110 §14.4） | ✘ |
| `FilenameResolver.hpp/cpp` | 决定本地文件名 + 安全消毒 | ✘ |
| `Probe.hpp/cpp` | 编排两枪，把 HTTP 语义翻译成 `DownloadPlan` | ✔ |
| `DownloadPlan.hpp` / `ProbeContext.hpp` | 纯数据 | ✘ |

四个不依赖 curl 的模块是主要的 bug 温床，也正好是最好测的。

`Probe.hpp` 里用了 `class CurlEasy;` 前置声明，头文件不拖 `curl/curl.h` 进来 —— 包含 `Probe.hpp` 的翻译单元不必看见 curl 的几千行声明。

### 3.3 两枪结构

```
第一枪  GET + "Range: bytes=0-0"      主力，能一次拿到全部结论
第二枪  HEAD                          仅当第一枪没结论时才发
```

**为什么主力是 GET 而不是 HEAD？** 这是整个设计里最反直觉的一处。

HEAD 看起来才是「只问不取」的正确工具。但它有三个致命问题：

1. **HEAD 证明不了 Range 能力。** 它最多告诉你 `Accept-Ranges: bytes`，而那只是一句声明（见 4.2）。只有一次真实的 Range 请求返回 206，才算证据。
2. **HEAD 常走不同的代码路径。** 动态生成内容的接口在 HEAD 时往往不计算 `Content-Length`，或者干脆 405。CDN 也可能对 HEAD 和 GET 给出不同的缓存行为。
3. **HEAD 的答案可能和 GET 不一致。** 你按 HEAD 的结论制定计划，真下载时走的却是 GET 的那条路径。

`Range: bytes=0-0` 请求的是**一个字节** —— 开销和 HEAD 相当，但走的是和真实下载完全相同的代码路径，拿到的是行为而不是声明。

HEAD 保留为兜底：第一枪连不上、或返回 405/501/意外状态码时再试一次。

---

## 4. 关键判断（协议层）

这一节是整个专题的核心。下面每一条判断，**写错了都不会报错，只会安静地产出坏文件**。

### 4.1 206 和 200：哪个更危险？

答案是 **200**。

- `206 Partial Content` —— 服务端真的按我们给的区间切了。这是唯一可信的「支持 Range」证据。
- `200 OK` —— 请求成功了，看起来一切正常，但服务端**无视了** Range 头，正在把整个文件从头推给你。

如果把 200 误判成支持 Range，12-06 的多线程会给每个块发一个 Range 请求，每个都拿回从 0 开始的完整文件，然后按各自的偏移写进同一个文件 —— 产出一个**大小正确、内容全错**的文件，全程零报错。

这就是 `main.cpp` 里那句「绝不能报 RangedParallel」的分量。

而且 206 也不能闭眼信，必须验 `Content-Range` 确实是我们要的 `0-0`：

```cpp
const auto cr = ContentRange::parse(*cr_raw);
if (!cr || !cr->matches(0, 0)) {
    note(plan, "206 但 Content-Range 不是 bytes 0-0/*: " + *cr_raw);
    return false;
}
```

见过返回 206 却把整个文件塞过来的实现。

### 4.2 `Accept-Ranges` 是声明，206 是行为

`DownloadPlan` 里这两个字段是**分开的，不是冗余**：

```cpp
bool                       supports_range = false;      // 实测结论
std::optional<std::string> accept_ranges_header;        // 服务端的声明
```

矛盾时**信实测**。中间任何一层（CDN、反向代理、压缩中间件）都可能透传了声明却吃掉了行为。测试床的 `/liar/` 路由专门模拟这种情况：

```
notes:
  200 —— 服务端忽略了 Range，总长来自 Content-Length: 104857600
  Accept-Ranges 声称支持 bytes，但实测返回 200 —— 以实测为准
```

把矛盾记进 `notes` 而不是默默丢掉 —— 那条记录是将来排查用户报障时唯一的线索。

同理，HEAD 那一枪即使看到 `Accept-Ranges: bytes`，也坚持填 `supports_range = false`。宁可退化成单线程慢一点，也不能误判。

### 4.3 206 响应的 `Content-Length` 是陷阱

```
HTTP/1.1 206 Partial Content
Content-Range: bytes 0-0/104857600
Content-Length: 1                     ← 这是本次区间的长度，不是文件总长
```

拿 `Content-Length` 当总长，你会得到一个 1 字节的文件。**206 的总长只能从 `Content-Range` 的斜杠后面取。**

反过来，200 响应的 `Content-Length` 就是全文长度，可以直接用。两种状态码下同名头的含义完全不同 —— 这是 HTTP 里最容易踩的语义陷阱之一。

### 4.4 `Content-Range` 的 total 是 `*`

```
Content-Range: bytes 0-0/*
```

服务端支持 Range，但自己也不知道总长（动态生成的内容）。这种情况**能续传，不能分块** —— 分块需要先知道总长才能算切点。落到 `StreamingChunked`。

### 4.5 416 是「理解了 Range」的证据

`416 Range Not Satisfiable` 说明服务端**理解** Range 语义，只是 `0-0` 落在了文件之外 —— 唯一可能是零长文件。响应形如：

```
HTTP/1.1 416 Range Not Satisfiable
Content-Range: bytes */0
```

注意 `bytes */0` 这个形式没有 `-`（RFC 9110 §14.4 专门为 416 规定的合法形式），`ContentRange::parse` 目前处理不了，`Probe.cpp` 里为此直接抠了斜杠后面的数。见第 7 节。

### 4.6 重定向：每一跳的头都会喂进来

`CURLOPT_HEADERFUNCTION` 在跟随重定向时，会把**每一跳**的响应头都回调给你，中间不发任何「换跳了」的通知。而 302 那一跳也有自己的 `Content-Length: 0`。

不处理的话，`get("content-length")` 会拿到 `0`，下载器认为文件是空的，安静地产出一个 0 字节文件。

唯一可靠的分界是**状态行** —— 每跳响应必然以 `HTTP/x.y NNN` 开头：

```cpp
if (line.rfind("HTTP/", 0) == 0) {
    clear();            // 把上一跳的头全丢掉
    // ... 解析状态码
    return;
}
```

收集结束时 `entries_` 里剩下的必然只属于最后一跳。

验证方法：`/redirect/test_100m.bin` 的 `total size` 必须是 `104857600` 而不是 `0`。

### 4.7 **不开** `CURLOPT_ACCEPT_ENCODING`

`ProbeContext` 里故意没有「开启压缩」的开关。原因：

一旦开了 gzip，服务端返回的 `Content-Length` 描述的是**压缩后**的字节数，而 Range 请求的字节偏移针对的也是压缩后的字节流。但最终要写到磁盘上的是**解压后**的内容 —— 两者长度对不上，按 `Content-Length` 切出来的块拼起来长度不对，而且没有任何一层会报错。

下载器要的是**字节透传**，不是内容协商。

---

## 5. 实现技巧

### 5.1 探测请求的「刹车」

第一枪是 GET。如果服务端忽略了 Range，它会开始把一个 1GB 的文件推给你 —— 「探测」会把整个文件拉完。

刹车做在写回调里：

```cpp
std::size_t Probe::discard_body(char* ptr, std::size_t size,
                                std::size_t nmemb, void* userdata) {
    const std::size_t n = size * nmemb;
    auto* self = static_cast<Probe*>(userdata);
    self->body_bytes_seen_ += n;
    if (self->body_bytes_seen_ > kMaxProbeBody) {   // 64 KB
        return 0;                                   // ← 中止传输
    }
    return n;
}
```

返回一个 `!= size*nmemb` 的值，libcurl 判定回调失败并中止传输，`curl_easy_perform` 返回 `CURLE_WRITE_ERROR`。

**关键：调用方不能把它当错误。** 这是我们主动踩的刹车，而且此时状态行和响应头早就收齐了，探测结论完全成立：

```cpp
if (code != CURLE_OK && code != CURLE_WRITE_ERROR) {
    // 只有这里才是真的网络出事
}
```

实测效果 —— 对 `/ignore/` 路由（服务端无视 Range 猛推）：

| 文件 | 探测耗时 |
| --- | --- |
| `test_100m.bin`（100 MB） | 0.73 s |
| `test_1g.bin`（1 GB） | 0.73 s |

耗时和文件大小无关，证明没有把文件拉完。

### 5.2 回调返回值的语义

libcurl 的两个回调，返回值回答的都是同一个问题：**你消费了多少字节**。

```cpp
std::size_t ResponseHeaders::curl_callback(char* buffer, std::size_t size,
                                           std::size_t nitems, void* userdata) {
    const std::size_t total = size * nitems;
    if (auto* self = static_cast<ResponseHeaders*>(userdata)) {
        self->add_line(buffer, total);
    }
    return total;      // 哪怕这行头我们不要，也必须报告全量消费
}
```

不相等 = 失败 = `CURLE_WRITE_ERROR`。写回调把这个特性当刹车用（5.1），头回调则必须老老实实返回全量。

### 5.3 静态成员函数当 C 回调的蹦床

libcurl 是 C 库，只能接受自由函数指针。要在回调里访问对象状态，标准手法是「静态成员函数 + `userdata` 传 `this`」：

```cpp
curl.setopt(CURLOPT_HEADERFUNCTION, &ResponseHeaders::curl_callback);
curl.setopt(CURLOPT_HEADERDATA, &headers_);        // ← 对象地址

curl.setopt(CURLOPT_WRITEFUNCTION, &Probe::discard_body);
curl.setopt(CURLOPT_WRITEDATA, this);              // ← 对象地址
```

静态成员函数没有隐式 `this` 参数，签名和 C 函数指针兼容；同时它又是类的成员，能访问私有成员（`discard_body` 里直接改 `self->body_bytes_seen_`）。

**注意不能用捕获了变量的 lambda** —— 有捕获的 lambda 不能转成函数指针。

### 5.4 RAII 三件套

libcurl 有三种必须成对的资源，每一种包一个类：

| 类 | 包的是 | 要点 |
| --- | --- | --- |
| `GlobalCurlGuard` | `curl_global_init/cleanup` | 全进程一次，在 `main` 里最早构造最晚析构；禁拷贝**禁移动** |
| `CurlEasy` | `curl_easy_init/cleanup` | `unique_ptr<CURL, Deleter>` + 自定义删除器 |
| `CurlSlist` | `curl_slist_append/free_all` | 可移动（`std::exchange`），不可拷贝 |

`CurlEasy` 用 `unique_ptr` 配自定义删除器，移动语义直接白拿：

```cpp
struct Deleter {
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};
std::unique_ptr<CURL, Deleter> handle_;
```

`setopt` 用模板包住 varargs 并统一检查返回码：

```cpp
template <typename T>
void setopt(CURLoption option, T value) {
    CURLcode code = curl_easy_setopt(handle_.get(), option, value);
    if (code != CURLE_OK) throw_setopt_failed(option, code);
}
```

### 5.5 `CurlSlist` 的生命期陷阱

**libcurl 只保存链表的指针，不拷贝内容。** 所以 slist 必须活到 `perform()` 返回：

```cpp
bool Probe::probe_range_get(CurlEasy& curl, DownloadPlan& plan) {
    CurlSlist req_headers;                          // ← 函数作用域
    req_headers.append("Range: bytes=0-0");
    curl.setopt(CURLOPT_HTTPHEADER, req_headers.get());

    const CURLcode code = curl.perform();           // ← 析构在这之后
    ...
}
```

常见错法：在 `if` 块里建 slist，出块就析构，`perform` 时读到野指针。

### 5.6 复用句柄：`curl_easy_reset` 保留连接池

两枪共用同一个 `CurlEasy`。第二枪前必须 `reset()`：

```cpp
curl.reset();       // 清掉上一枪的 HTTPHEADER（那个 slist 已经析构了）
                    // 也清掉 HTTPGET，否则和 NOBODY 打架
```

**`curl_easy_reset` 只清选项，不清连接池** —— 上一枪建立的 TCP/TLS 连接还在，这一枪能直接复用。这正是复用句柄而不是新建一个的理由：省掉一次三次握手 + TLS 握手。

### 5.7 `CURLOPT_NOSIGNAL` 要现在就设

```cpp
curl.setopt(CURLOPT_NOSIGNAL, 1L);
```

libcurl 默认用 `SIGALRM` + `siglongjmp` 实现域名解析超时。信号是**进程级**的，在多线程里会打到随机一个线程上，栈跳飞。

12-06 才会开多线程，但这个选项现在就得设上 —— 等出问题再加会非常难查（表现是随机崩溃，且和你正在改的代码毫无关系）。

代价：DNS 解析不再受超时约束（除非 libcurl 编译时带了 c-ares）。

### 5.8 文件名是不可信输入

服务端给的文件名可能是 `../../autoexec.bat`、`C:\Windows\x`、`NUL`、带控制字符的名字。`sanitize` 里有一处顺序是关键：

> **先剥目录成分，再过滤字符。**

反过来的话，`..%2F..%2Fx` 解码出的斜杠会被先删成 `....x` —— 路径穿越是挡住了，但名字面目全非；而且一旦以后有人在过滤表里漏掉某个分隔符，穿越就直接漏过去了。

其它几条容易漏的：

- **Windows 静默吃掉结尾的点和空格**：你写 `a.txt.`，系统实际创建的是 `a.txt`。续传时按 `a.txt..part` 去找就找不到 —— 断点续传失效，且不报错。
- **设备名判断只看第一个点之前**：`NUL.txt` 在 Windows 上一样是设备。
- **按字节截断会切断 UTF-8 多字节序列**，要退回到最近的字符边界。

优先级 `filename*` > `filename` > URL 末段 > 兜底常量，而且**每一级 sanitize 后为空都要退到下一级** —— 服务端发个 `filename=".."` 不该让你直接掉到 fallback，URL 里往往有个好名字。

### 5.9 「探不出来」不是一种 mode

```cpp
if (conclusive) {
    plan.mode = decide_mode(plan);
} else {
    note(plan, "两枪都没有拿到结论，mode 保持 Unknown");
}
```

少了这个门，两枪全失败（DNS 挂了、连不上）时 `total_size` 是空的，`decide_mode` 会照着「总长未知」这条规则给出 `StreamingChunked` —— 把「我探不出来」谎报成「这是个流式接口」，上层会真的去发起下载。

同一个「无值」，在不同的上下文里含义完全不同。控制流必须把它们分开。

另外，`run()` 只在**输入非法**（空 URL）时抛异常。网络挂了不抛 —— 探测的产出是一份**如实**的计划，包括「我探不出来」这个结论本身。上层看到 `Unknown` 自己决定重试、换 URL 还是放弃。

---

## 6. 测试

三层，按性价比排序。

### 第一层：纯函数表驱动（零依赖）

`ContentRange` / `ResponseHeaders` / `FilenameResolver` / `decide_mode` 都是纯函数，不联网就能测。

`ResponseHeaders::add_line` 收原始字节，意味着「收一次 HTTP 响应」可以完全手工模拟：

```cpp
void feed(ResponseHeaders& h, const std::string& line) {
    const std::string raw = line + "\r\n";
    h.add_line(raw.data(), raw.size());
}
```

4.6 那个重定向污染的端到端测试，在这一层压缩成 8 行、零网络、微秒级。

### 第二层：解读逻辑（需要重构）

`probe_range_get()` 目前把「设选项 + perform」和「200/206/416 各自意味着什么」焊在一个函数里，后半段测不了。要摘成纯函数：

```cpp
static bool interpret_range_get(long status, const ResponseHeaders&, DownloadPlan&);
```

摘出来之后，`test_probe_interpret.cpp` 里那两张表的每一行都变成不联网的用例。其中三行是**端到端测试跑不到**的分支（206 但区间不对、`Content-Length: -1`、`Content-Length: abc`），而它们恰恰最危险。

建议在写 12-05 之前做完 —— 续传要大量复用这套判断。

### 第三层：端到端（`probe_server.py`）

抓前两层抓不到的问题：curl 选项设错了、`NOBODY` 忘了清、`FOLLOWLOCATION` 没开。这层是回归网，不是主力。

### 关于框架

`test/check.hpp` 是一个 20 行的 `CHECK`/`CHECK_EQ`，故意不用 Catch2/gtest。和 `assert` 的区别就是测试框架存在的全部理由：

| | `assert` | `CHECK` |
| --- | --- | --- |
| 失败后 | 中止，只能看到第一个错 | 继续，一次看完全部 |
| 输出 | 「表达式为假」 | `file:line` + 实际值 vs 期望值 |
| 计数 | 无 | 有，`main` 靠失败数决定退出码，CTest 才判得了 |

等这套裸的用起来嫌麻烦了，那份「麻烦」就是选框架时的判据。

---

## 7. 已知缺口

1. **`ContentRange::parse` 不支持 `bytes */N`**（416 的合法形式，无 `-`）。`Probe.cpp` 里绕开了，直接抠斜杠后的数字。两条路：扩展 parse 并删掉绕行，或者在 `ContentRange.hpp` 里写明不支持。别让缺口没有记录。

2. **`SingleResumable` 这个名字在 `supports_range == false` 时名不副实** —— 服务端不支持 Range，断了根本续不上。写 12-05 时要么改名 `SingleStream`，要么这一档再分「可续/不可续」。目前按 `main.cpp` 的验收口径走，不提前发明抽象。

3. **`main.cpp` 打印的 `"yes (206 verified)"` 在 416 路径上措辞不准** —— 那是 416 验证的。

4. **非 UTF-8 的 `filename*` charset 不转码**（ISO-8859-1 等）。转码要引 iconv/ICU，不值得为一个文件名加依赖。

5. **中文文件名在 Windows 上落盘还没解决。** `from_content_disposition` 交出的是正确的 UTF-8 字节，但 `std::ofstream(const char*)` 走的是 ANSI 代码页。真要正确落盘得转 UTF-16 走 `_wfopen`/`wofstream` —— 那是 14-io 的事。

---

## 8. 验收清单

| 场景 | URL | probe_status | 期望 mode |
| --- | --- | --- | --- |
| 支持 Range 的静态文件 | `/ranged/test_100m.bin` | 206 | `RangedParallel` |
| 忽略 Range 的服务器 | `/ignore/test_100m.bin` | 200 | `SingleResumable` |
| chunked 接口 | `/chunked` | 200 | `StreamingChunked` |
| 需要重定向的 URL | `/redirect/test_100m.bin` | 206 | `RangedParallel`，`effective_url` 跟完，总长非 0 |
| 声明与行为矛盾 | `/liar/test_100m.bin` | 200 | `SingleResumable` + notes 记矛盾 |
| 零长文件 | `/empty` | 416 | `SingleResumable` |
| 不存在的资源 | `/ranged/nope.bin` | 404 | `Unknown`，退出码 1 |
| 文件名安全 | `/cd` | 200 | 文件名 = `中文 报告.bin` |
| 刹车 | `/ignore/test_1g.bin` | 200 | 耗时与 100MB 相当（不拉完整个文件） |
