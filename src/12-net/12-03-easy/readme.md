# libcurl easy API：三个回调与 RAII

> 阶段 2 的实验记录。目标：把一个 URL 下载到本地文件，带实时进度条，可主动取消。
> 路线见 [downloader-roadmap.md](../downloader-roadmap.md)，构建与产物见 [12-01-curl/readme.md](../12-01-curl/readme.md)。

阶段 1 是**手写字节流**，这一阶段是**把同一件事交给库**。价值不在"会调 API"，
而在于看清楚 libcurl 替你做掉了哪几层（chunked 解码、keep-alive、重定向、TLS），
以及它把哪几层留给了你（写盘、进度、取消、生命周期）。

---

## 0. 本阶段的三道坎（写代码前先想清楚）

### 0.1 回调必须是裸 C 函数指针

`CURLOPT_WRITEFUNCTION` 要的类型是 `size_t(*)(char*, size_t, size_t, void*)`。
**捕获了变量的 lambda 没有到函数指针的隐式转换**——只有无捕获 lambda 才有。
所以上下文（目标文件、已下载字节数、进度观察者……）只能走 `void* userdata` 这条路进去，
入口处 `static_cast` 回来再转发到成员函数。这就是 trampoline（蹦床）。

对应 [03-callback](../../03-callback/)。别用全局变量绕过去——多线程阶段会立刻还债。

### 0.2 `curl_easy_setopt` 是变参函数，没有类型检查

变参函数不做隐式转换。文档里到处写 `1L` 而不是 `1`，是因为选项按 `long` 取值：
在 Windows x64 上 `long` 是 32 位、`int` 也是 32 位，传 `1` 侥幸能过；
但 `curl_off_t` 类的选项（`CURLOPT_RESUME_FROM_LARGE`、`CURLOPT_MAX_RECV_SPEED_LARGE`）是 64 位，
传错宽度就是**栈上读到垃圾值，编译器一声不吭**。养成习惯：`long` 类选项一律写 `L` 后缀。

### 0.3 `CURL*` 和 `curl_slist*` 是成对释放的裸资源

用 `std::unique_ptr` + **无状态删除器**（`struct` + `operator()`）包一层。
写成 `struct CurlDeleter { void operator()(CURL* h) const { curl_easy_cleanup(h); } };`，
`unique_ptr<CURL, CurlDeleter>` 的大小仍然是一个指针（空基类优化）；
若把删除器塞成函数指针或有捕获的 lambda，对象就会变大。对应 [01-ptr](../../01-ptr/)。

---

## 1. 环境与构建

MSVC 14.51 / vcpkg `x64-windows` / libcurl 8.21.0。

```cmake
find_package(CURL CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
```

`CURL::libcurl` 这个 imported target 自带头文件路径、库路径，以及 **Debug 配置下自动选 `libcurl-d.lib`**。不用手写 `target_include_directories`，也不用自己判断配置后缀。

DLL 的问题：vcpkg 的 toolchain 会在构建后自动把 `libcurl.dll` 及其依赖拷到 exe 旁边（`VCPKG_APPLOCAL_DEPS` 默认开）。所以 `build/bin/` 下能直接双击运行，不用改 `PATH`。

本目录有四个 target：

| target | 内容 |
| --- | --- |
| `12-03-easy` | **主程序**：Downloader 类，写回调 + 进度回调 + 取消 |
| `12-03-easy-lesson01` | 最小可用：只设 URL，body 直接吐到 stdout |
| `12-03-easy-lesson02` | 写进文件 —— **目前编不过，见 7.1** |
| `12-03-easy-lesson03` | 写进 `std::string` |

## 2. 三个回调

libcurl 把「数据往哪去」「元信息怎么看」「进度怎么报」三件事都交给回调。

| 选项对 | 触发时机 | 返回值语义 |
| --- | --- | --- |
| `CURLOPT_WRITEFUNCTION` / `WRITEDATA` | 收到一批响应体 | 消费了多少字节，`!= size*nmemb` 即中止 |
| `CURLOPT_HEADERFUNCTION` / `HEADERDATA` | **每收到一行**响应头 | 同上 |
| `CURLOPT_XFERINFOFUNCTION` / `XFERINFODATA` | 大约每秒若干次 | `0` 继续，**非 0 中止** |

三个回调的返回值语义**不一样**：前两个回答「消费了多少」，第三个回答「要不要继续」。这个区别是 5 节的关键。

### 2.1 写回调

```cpp
size_t Downloader::write_member(char* ptr, size_t size, size_t nmemb) {
    const size_t total = size * nmemb;
    file_.write(ptr, static_cast<std::streamsize>(total));
    if (!file_) {
        return 0;      // 唯一该返回短计数的场合：真的没写进去
    }
    return total;
}
```

关于 `size` 和 `nmemb`：**`size` 恒等于 1**，实际字节数全在 `nmemb` 里。这是 libcurl 沿用 `fwrite(ptr, size, nmemb, stream)` 签名留下的历史包袱，它从来不用 `size != 1` 的形式。所以 `size * nmemb` 这个写法只是照着签名走，实质就是 `nmemb`。

单次上限由 `CURL_MAX_WRITE_SIZE` 决定：

```c
/* D:/vcpkg/installed/x64-windows/include/curl/curl.h:265 */
#define CURL_MAX_WRITE_SIZE 16384
```

也就是 16 KB。一次 `perform` 会把回调调用成百上千次 —— 100 MB 的文件至少 6400 次。**回调里别做慢操作**（别每次都 flush，别每次都刷屏），这是 2.3 节做节流的原因。

### 2.2 头回调

> ⚠️ **本目录的 `main.cpp` 没有实现头回调** —— 它只设了 `WRITEFUNCTION` 和 `XFERINFOFUNCTION`。
> 下面的结论来自 [12-04-probe](../12-04-probe/readme.md) 里 `ResponseHeaders` 的实测，那里完整实现了这个回调。要在本阶段亲手验证，加一个 `CURLOPT_HEADERFUNCTION` 把每次回调的内容原样打出来即可。

三条实测结论：

1. **一行一次，行尾带 `CRLF`。** 不用自己找 `\r\n` 边界 —— 阶段 1 手写的那套缓冲逻辑，libcurl 替你做了。

2. **状态行也会来。** 第一次回调收到的是 `HTTP/1.1 206 Partial Content\r\n`，它没有冒号。头部结束的空行 `\r\n` 也会作为一次回调送达。所以解析时必须处理「没有冒号的行」这种情况。

3. **重定向时，每一跳的头都会喂进来，中间不发任何通知。** 一次 302 → 200 的请求会收到**两组**头，而且 302 那一跳也有自己的 `Content-Length: 0`。两组混在一起的话，你查 `Content-Length` 会拿到 `0`，然后认为文件是空的。

   唯一可靠的分界就是状态行 —— 每跳响应必然以 `HTTP/` 开头。12-04 的做法是看到状态行就清空已收集的头：

   ```cpp
   if (line.rfind("HTTP/", 0) == 0) {
       clear();          // 上一跳的头全丢掉
       // ... 解析状态码
       return;
   }
   ```

   这是整个下载器里最隐蔽的一个坑：不处理不会报错，只会安静地产出 0 字节文件。

### 2.3 进度回调

```cpp
curl_easy_setopt(curl_.get(), CURLOPT_NOPROGRESS, 0L);   // ← 不设这个，回调根本不触发
curl_easy_setopt(curl_.get(), CURLOPT_XFERINFOFUNCTION, &Downloader::progress_trampoline);
curl_easy_setopt(curl_.get(), CURLOPT_XFERINFODATA, this);
```

**`CURLOPT_NOPROGRESS` 默认是 `1L`（关闭进度）**，必须显式设成 `0L` 才生效。设了回调却不设这个，是最常见的「回调没被调用」的原因。

用 `XFERINFOFUNCTION` 而不是老的 `PROGRESSFUNCTION`：后者参数是 `double`，大文件会丢精度；前者是 `curl_off_t`（64 位整数）。

进度条的两个要点：

```cpp
int Downloader::progress_member(curl_off_t dltotal, curl_off_t dlnow) {
    if (cancelled_) return 1;                    // ← 取消走这里，见 5 节
    if (dltotal > 0) {
        int percent = static_cast<int>((dlnow * 100) / dltotal);
        if (percent != last_percent_) {          // ← 节流：只在百分比变化时才画
            last_percent_ = percent;
            std::cerr << "\r download ...: " << percent << "%  ("
                      << (dlnow / 1024) << " KB / " << (dltotal / 1024) << " KB)   "
                      << std::flush;
        }
    } else {
        std::cerr << "\r done: " << (dlnow / 1024) << " KB" << std::flush;
    }
    return 0;
}
```

- **`\r` 回车不换行** + `std::flush`，让新的一行盖掉旧的，不刷屏。行尾多打几个空格是为了盖掉上一次更长的输出残留。
- **节流**：`percent != last_percent_` 保证最多画 101 次。不节流的话回调频率远高于人眼刷新率，光是 I/O 就会拖慢下载。
- 打到 `cerr` 而不是 `cout`：进度是状态信息，不该混进可能被重定向的正常输出。

实测输出（把 `\r` 转成换行才看得到，正常终端上是原地刷新）：

```
 done: 0 KB
 done: 0 KB
 download ...: 41%  (41984 KB / 102400 KB)
 download ...: 42%  (43008 KB / 102400 KB)
 download ...: 43%  (44127 KB / 102400 KB)
```

> 注意开头那两行 `done: 0 KB` —— **进度回调在响应头解析完之前就开始触发了**，那时 `dltotal` 还是 0，走的是 else 分支。这不是 bug，但如果你的 UI 拿 `dltotal == 0` 当「服务端没给 Content-Length」的判据，开头这几次就会误判。稳妥的做法是等 `dltotal > 0` 出现过一次再下结论。

## 3. Trampoline 的写法

```cpp
class Downloader {
    // ---- 静态入口：签名和 C 函数指针兼容 ----
    static size_t write_trampoline(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* self = static_cast<Downloader*>(userdata);
        return self->write_member(ptr, size, nmemb);
    }
    // ---- 成员函数：能访问 file_ / cancelled_ / last_percent_ ----
    size_t write_member(char* ptr, size_t size, size_t nmemb);
};

// 注册时把 this 塞进 userdata
curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, &Downloader::write_trampoline);
curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, this);
```

**为什么必须这样，不能直接传捕获 lambda：**

普通成员函数有隐式的 `this` 参数，签名是 `size_t(Downloader::*)(char*, size_t, size_t)` —— 和 `size_t(*)(char*, size_t, size_t, void*)` 是两个不相干的类型，没有转换。

有捕获的 lambda 同理：它是一个带成员变量的匿名类，`operator()` 也有隐式 `this`。只有**无捕获** lambda 才有到函数指针的隐式转换，但无捕获就意味着拿不到上下文，等于没解决问题。

静态成员函数是唯一的出路：它**没有**隐式 `this`（签名和 C 兼容），但**是**类的成员（能访问私有成员）。上下文靠 `void* userdata` 手动传递 —— 这正是 C 语言回调 API 里 `void* user_data` 参数存在的全部意义。

> 12-04 里 `Probe::discard_body` 和 `ResponseHeaders::curl_callback` 用的是同一套手法。

## 4. RAII 包装

两层：全局的 init/cleanup，和每个句柄的 init/cleanup。

```cpp
class GlobalCurlGuard {                          // 全进程一次
public:
    GlobalCurlGuard() {
        CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) throw std::runtime_error(...);
    }
    ~GlobalCurlGuard() { curl_global_cleanup(); }
    GlobalCurlGuard(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard& operator=(const GlobalCurlGuard&) = delete;
    GlobalCurlGuard(GlobalCurlGuard&&) = delete;        // ← 连移动也禁掉
    GlobalCurlGuard& operator=(GlobalCurlGuard&&) = delete;
};
```

**为什么连移动都禁**：这个类不管理任何可转移的资源，它代表的是「进程级初始化已完成」这个状态。允许移动就意味着允许存在一个「被掏空」的实例，而它的析构函数照样会调 `curl_global_cleanup` —— 直接 double cleanup。禁掉最省心。

它必须在 `main` 里**最早构造、最晚析构**，所有 `CURL*` 都要活在它的生命期内。

```cpp
struct CurlDeleter {
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};
using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;
```

### sizeof 验证

`unique_ptr` 对**无状态**删除器会做空基类优化（EBO），不占额外空间。实测（MSVC 14.51 / x64）：

```
void*                                     = 8
unique_ptr<CURL, CurlDeleter>（无状态）   = 8     ← 和裸指针一样大
unique_ptr<CURL, void(*)(CURL*)>（函数指针）= 16   ← 多了一个指针
```

所以删除器要写成 `struct` + `operator()`，别图省事写 `unique_ptr<CURL, decltype(&curl_easy_cleanup)>` —— 那会让每个句柄多带 8 字节，而且没法默认构造。

## 5. 取消传输

取消是**跨线程**的：主线程阻塞在 `curl_easy_perform` 里，只能由另一个线程翻标志位。

```cpp
std::atomic<bool> cancelled_{false};

void cancel() { cancelled_ = true; }
```

```cpp
canceller = std::thread([&downloader, cancel_after] {
    std::this_thread::sleep_for(std::chrono::seconds(cancel_after));
    downloader.cancel();
});
// ...
if (canceller.joinable()) canceller.join();      // 必须 join
```

`join` 不能省：下载可能先于计时结束，不 join 的话线程会带着 `downloader` 的引用活过 `main` 的作用域。

### 5.1 为什么取消走进度回调，不走写回调

主程序的写回调里有一段注释专门说这件事，值得抄出来：

```cpp
size_t Downloader::write_member(char* ptr, size_t size, size_t nmemb) {
    // 这里【不】检查 cancelled_。
    // ...
}
```

三个理由：

1. **错误码会混淆。** 写回调返回短计数 → `CURLE_WRITE_ERROR(23)`。如果让它兼职表达「用户取消」，那么「磁盘满」和「用户按了取消」会得到同一个错误码，事后无法区分。
2. **验收要求的是 42。** 进度回调返回非 0 → `CURLE_ABORTED_BY_CALLBACK(42)`，这才是「主动取消」的专用码。
3. **写回调触发频率远高于进度回调**（每 16 KB 一次 vs 每秒几次），取消几乎总是先撞到写回调，于是 `perform` 返回 23 而不是 42。

一句话：**一个信号只表达一个含义。**

### 5.2 三种返回码分别对应什么

```cpp
if (res == CURLE_OK)                     { /* 传输成功，还要再查落盘 */ }
else if (res == CURLE_ABORTED_BY_CALLBACK) { /* 42：用户主动取消 */ }
else if (res == CURLE_WRITE_ERROR)         { /* 23：本地写盘失败 */ }
else                                       { /* 网络层出事 */ }
```

`CURLE_OK` 还不够，落盘也要查：

```cpp
file_.close();                              // close 内部会 flush
const bool disk_ok = static_cast<bool>(file_);
if (res == CURLE_OK && !disk_ok) {
    // 传输成功但 flush 失败 —— 最后一批数据还压在缓冲里没落盘，文件是残的
}
```

`flush` 失败同样会置 `badbit`，所以要在 `close` **之后**再判一次流状态。只看 `perform` 的返回码会漏掉这一类。

### 5.3 实测

```
$ 12-03-easy http://127.0.0.1:8000/test_100m.bin out.bin
 download ...: 100%  (102400 KB / 102400 KB)

complete!: file is in: out.bin
$ ls -l out.bin
-rw-r--r-- 104857600 out.bin          ← 字节数精确
```

```
$ 12-03-easy http://127.0.0.1:8000/test_1g.bin out2.bin 2
[will cancel after 2s]
 download ...: 33%  (...)
[cancel request is sending]

:download is canceled by user (CURLE_ABORTED_BY_CALLBACK=42)
$ ls -l out2.bin
-rw-r--r-- 355647488 out2.bin         ← 半个文件留在盘上
```

**取消后残文件是留着的。** 代码里那行删除是注释掉的：

```cpp
// std::remove(output_path.c_str());
```

这是个待决策项，不是 bug —— 留着才能做断点续传（12-05），删掉才符合「取消了就当没发生」的直觉。但**现在这个状态最糟**：文件既不完整，也没有任何标记说明它不完整，下次运行会被当成正常文件。

12-05 的标准做法：下到 `xxx.part`，完成后才 `rename` 成正式名。

## 6. 必须能回答的问题

**Q1. 写回调返回 0 会发生什么？返回比 `sz*nmemb` 大的数呢？**

libcurl 的判断只有一条：**返回值 `!= size*nmemb` 就算失败**，立刻中止传输，`curl_easy_perform` 返回 `CURLE_WRITE_ERROR(23)`。

所以：

- **返回 0** → 中止，`CURLE_WRITE_ERROR`。这是「我一个字节都没接住」。
- **返回比 `sz*nmemb` 小的数**（短计数）→ 同样中止，同样是 23。libcurl 不会「把剩下的再喂一次」。
- **返回比 `sz*nmemb` 大的数** → 也是 `!=`，**同样中止**，同样是 23。不存在「多消费」这种语义。

换句话说，这个返回值不是「我处理了多少」的统计，而是一个**二值信号**：等于 = 继续，不等于 = 出错停。

例外是几个特殊哨兵值：`CURL_WRITEFUNC_PAUSE`（暂停传输，之后用 `curl_easy_pause` 恢复），以及新版本的 `CURL_WRITEFUNC_ERROR`（显式报错）。它们是特意选的、不可能与真实字节数冲突的大数。

12-04 的探测器故意利用了这个机制来踩刹车 —— 服务端无视 Range 猛推 1 GB 时，写回调收够 64 KB 就 `return 0` 中止，然后把 `CURLE_WRITE_ERROR` 当成正常情况处理。实测 1 GB 的探测只花 0.73 秒。

**Q2. `CURLOPT_TIMEOUT` 为什么不能用在大文件下载上？该用哪两个选项替代？**

因为它是**整个传输的硬性总时限**，从 `perform` 开始计时，到时就杀，不管当时传得多顺利。

一个 1 GB 的文件在 2 MB/s 的线路上正常需要 500 秒。设 `CURLOPT_TIMEOUT` 为 300 就会在 60% 处被杀掉 —— 而这次传输**没有任何问题**，只是文件大、线路慢。要设一个「一定够用」的值，就得按最坏情况估，那这个超时也就失去了意义（真卡死时要等好几个小时才发现）。

根本问题：**「慢」和「卡死」是两回事，总时限区分不了。**

替代方案是这两个，专门检测「卡死」：

```cpp
curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);   // 低于 1 KB/s
curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);     // 持续 30 秒 → 中止
```

语义是「速度连续 30 秒低于 1 KB/s 就放弃」。传得慢但一直在传 → 不中止；真断了 → 30 秒后报 `CURLE_OPERATION_TIMEDOUT`。和文件大小完全解耦。

另外 `CURLOPT_CONNECTTIMEOUT` 只约束**连接建立**阶段，和传输时长无关，任何场景都该设。

> **对比 12-04**：那里设了 `CURLOPT_TIMEOUT = 30L`，而且是对的 —— 探测请求只传 1 个字节，30 秒还不完成必然是出事了。同一个选项，在「探测」和「下载」两个场景下一个合适一个有害，取决于传输量是否有界。

**Q3. `curl_easy_setopt` 传 `0` 和传 `0L` 有区别吗？为什么文档里到处是 `1L`？**

有区别，而且是**平台相关**的区别 —— 这正是它危险的地方。

`curl_easy_setopt` 是变参函数，声明是 `curl_easy_setopt(CURL*, CURLoption, ...)`。变参部分**没有形参类型**，编译器不做任何隐式转换，只做默认实参提升（`char`/`short` → `int`，`float` → `double`）。libcurl 内部按选项类别用 `va_arg(param, long)` 取值。

于是：

| 平台 | 数据模型 | `long` | 传 `0`（`int`）的后果 |
| --- | --- | --- | --- |
| Windows x64 | LLP64 | 32 位 | `int` 也是 32 位，**侥幸正确** |
| Linux/macOS x64 | LP64 | **64 位** | 只推了 32 位，`va_arg` 读 64 位 → **高 32 位是垃圾** |

所以在 Windows 上写 `0` 能跑，代码一挪到 Linux 就出现「选项设了但没生效」或者更诡异的行为，而且**编译器一句警告都没有**。

`curl_off_t` 类的选项更糟 —— 它们在**所有**平台上都是 64 位：

```cpp
curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, 1000000);        // ✗ int，读到垃圾
curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)1000000);  // ✓
```

续传时断点位置读成垃圾值 = 从错误的偏移开始下载 = 文件损坏且不报错。12-05 会直接踩到这个。

结论：`long` 类选项一律写 `L` 后缀，`curl_off_t` 类选项显式强转。这不是风格洁癖，是变参函数唯一的自保方式。

**Q4. 阶段 1 手写的 chunked 解码，在 libcurl 里去哪了？写回调收到的是解码前还是解码后的数据？
你怎么验证这个结论？**

**libcurl 内部做掉了。写回调收到的是解码后的数据** —— chunk 长度行、每块后面的 `\r\n`、结束标记 `0\r\n\r\n` 全部被剥掉，回调只看到纯粹的实体内容。

阶段 1 手写的那个 `ChunkDecoder`，对应的就是 libcurl 里 `Curl_httpchunk_read` 那一层。同时被做掉的还有：keep-alive 连接管理、重定向跟随、TLS 握手、`Content-Encoding` 解压（如果开了 `CURLOPT_ACCEPT_ENCODING`）。

**怎么验证 —— 两个独立的办法：**

*方法一：数字节。* 请求 `http://httpbin.org/stream-bytes/2048`，在写回调里累加所有 `nmemb`：

- 如果总和**正好是 2048** → 收到的是解码后的数据。
- 如果是 2048 + 若干（多出来的是 `"800\r\n"` 这类长度行和分隔符）→ 是原始数据。

实测结果是 2048。阶段 1 手写解码器得到的也是 2048，两边对得上。

*方法二：看头几个字节。* 用 `curl --raw`（关闭自动解码）抓同一个端点：

```
$ curl -s --raw --http1.1 http://httpbin.org/stream-bytes/64 | xxd
00000000: 3430 0d0a d067 31df 2fa3 bc5c 231d ed03  40...g1./..\#...
                ^^^^^^^^ 数据从这里才开始
          ^^^^^^^ "40\r\n" —— 长度行，0x40 = 64
```

原始流的头 4 个字节是 `"40\r\n"`。如果写回调第一次拿到的数据也以 `34 30 0d 0a` 开头，说明是原始的；实测拿到的第一个字节直接就是 `d0`（数据本身），说明已解码。

> 顺带一个推论，对 12-04 很重要：**`Content-Encoding`（gzip）的解压也在这一层做掉**。一旦开了 `CURLOPT_ACCEPT_ENCODING`，写回调看到的是解压后的字节，而 `Content-Length` 头描述的是**压缩后**的字节数 —— 两个数对不上。按 `Content-Length` 切块的下载器会因此产出坏文件。所以 12-04 的探测器**故意不开**压缩。

## 7. 踩过的坑

### 7.1 `lesson02.cpp` 编不过 —— 把 `std::ofstream` 塞进变参

**现象**

```
lesson02.cpp(26): error C4839: 使用类 "std::basic_ofstream<...>" 作为可变参数函数的参数的非标准用法
lesson02.cpp(26): error C2280: "std::basic_ofstream<...>::basic_ofstream(const basic_ofstream &)":
                               尝试引用已删除的函数
```

**排查**

第 26 行是：

```cpp
curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);   // fp 是 std::ofstream
```

**根因**

两个错误其实是一件事的两面。变参函数只能接受「可平凡拷贝」的类型；`std::ofstream` 有已删除的拷贝构造函数，按值传不进去。C4839 是「你在变参里传了个类类型」的警告，C2280 是它试图拷贝时撞上 `= delete`。

这正是 0.2 节说的「变参函数没有类型检查」的另一面 —— 它不做转换，但对**能不能传**这件事编译器还是管的。

**结论**

`CURLOPT_WRITEDATA` 要的是 `void*`，传地址：

```cpp
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fp);
```

同时写回调里要 `static_cast<std::ofstream*>(stream)` 取回来。

> `lesson02.cpp` 里 `write_data` 的函数体目前也是空的（声明了返回 `size_t` 却没有 `return`）。就算修好了上面那行，运行时也会因为返回值是栈上垃圾而立刻 `CURLE_WRITE_ERROR`。两处一起改。

### 7.2 其余

| 现象 | 根因 | 结论 |
| --- | --- | --- |
| 进度回调完全不触发 | `CURLOPT_NOPROGRESS` 默认是 `1L` | 显式设 `0L` |
| 进度条刷屏、下载变慢 | 回调频率远超人眼刷新率 | 按百分比变化节流 |
| 进度条残留上一次的字符 | 新内容比旧的短，`\r` 只回车不清行 | 行尾补空格 |
| 开头几次进度显示 `0 KB` | 头还没解析完，`dltotal` 还是 0 | 等 `dltotal > 0` 出现过再下结论 |
| 取消返回 23 而不是 42 | 在写回调里检查了取消标志 | 取消只走进度回调（5.1） |
| `perform` 成功但文件是残的 | 只查了返回码，没查 `flush` | `close()` 之后再判一次流状态 |
| 取消后留下半个文件 | 没有 `.part` 机制 | 12-05 处理，见 5.3 |
| 传 `0` 在 Linux 上行为诡异 | 变参 + `long` 宽度差异 | 一律写 `L` 后缀（Q3） |
| 大文件下载被超时杀掉 | `CURLOPT_TIMEOUT` 是总时限 | 换 `LOW_SPEED_LIMIT` + `LOW_SPEED_TIME`（Q2） |
| 想在回调里用捕获 lambda | 有捕获 = 不能转函数指针 | trampoline（3 节） |

---

## 验收

- [x] 下载一个 100 MB 文件，进度条流畅、不刷屏 —— 实测字节数 104857600 精确
- [x] 进度回调返回非 0 能主动取消，且 `curl_easy_perform` 返回 `CURLE_ABORTED_BY_CALLBACK` —— 实测返回 42
- [x] 所有 libcurl 资源走 RAII，代码里没有裸 `curl_easy_cleanup`（`main.cpp`）
- [x] 回调统一走 trampoline，没有用全局变量传上下文
- [x] `curl_global_init` 只在 `main` 里调一次（`GlobalCurlGuard`）
- [ ] **`lesson02.cpp` 修好**（见 7.1）
- [ ] **头回调补上**（2.2 的结论目前借自 12-04，本目录没有亲手验证）

## 这一阶段留给后面的东西

| 这里建立的 | 后面用在哪 |
| --- | --- |
| trampoline（静态入口 + `this` 走 userdata） | 12-04 的 `Probe::discard_body`、`ResponseHeaders::curl_callback` |
| `GlobalCurlGuard` / `CurlPtr` | 12-04 的 `CurlEasy.hpp`，原样搬过去又加了 `CurlSlist` |
| 写回调返回值 = 中止信号 | 12-04 拿它当探测的刹车（Q1） |
| 「`perform` 成功 ≠ 文件完整」 | 12-05 的 `.part` + rename |
| 变参函数的宽度陷阱 | 12-05 的 `CURLOPT_RESUME_FROM_LARGE`（Q3） |
