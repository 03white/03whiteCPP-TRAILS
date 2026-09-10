# 12-06-mt —— 多线程分块下载

对应 `downloader-roadmap.md` 阶段 5。

**目标**：N 个线程并发拉不同区间，写进同一个文件，最终校验和与单线程结果一致，
速度显著优于单线程。

**不做**：崩溃一致性 journal（阶段 6）、断点续传、重试与调度、进度汇总（阶段 7）。
这一阶段只解决一件事：**把一个文件切成 N 段，并发拉下来，拼对**。

**当前状态**：主路径已跑通并验证（见第 6 节）。`## 已知缺口` 一节记着还没做的事，
其中第 1 条是正确性缺口，不是功能缺口。

---

## 1. 这一阶段真正的难点

不是「怎么开线程」，也不是「怎么算区间」。难点只有一个：

> **并发写盘错了，不会有任何症状。**

文件大小是对的（预分配撑出来的），程序退出码是 0（没人检查 `CURLcode`），
用文本编辑器打开也看不出来——因为错的那一段要么是一片 0，要么是**别的偏移的数据**。
唯一能发现它的手段是拿完整文件对哈希。

这就是为什么阶段 4 要先把单线程续传做对：跳过那一步直接上多线程，
「续传逻辑错」和「并发写盘错」两类 bug 的症状完全一样，你会同时调试两个不可见的东西。

---

## 2. 线程模型

```
main
 └─ GlobalCurlGuard          curl_global_init 一次，早于任何线程
     └─ MultiDownLoader
         ├─ initTotalSize()   HEAD 请求拿 Content-Length（主线程，串行）
         ├─ preallocate_file()  把目标文件撑到最终大小（主线程，串行）
         ├─ 构造 N 个 SingleDownloader   每个持有自己的 CurlEasy + ofstream
         └─ 启动 N 个 std::thread        每个跑 loaders[i]->run()
```

**关键顺序**：`loaders` 全部构造完，才开始 `emplace_back` 线程。
两个循环不能合并——`loaders.reserve()` 虽然避免了 vector 扩容导致的迁移，
但把「构造」和「启动」分开写，意图更清楚，也留出了统一校验的位置。

libcurl 的线程规则（roadmap 5.1，违反了都是随机崩溃）：

| 规则 | 本阶段的落实 |
| --- | --- |
| `curl_global_init` 只在 main 调一次 | `GlobalCurlGuard` 在 `main` 第一行构造，早于一切 |
| 一个 easy handle 只属于一个线程 | 每个 `SingleDownloader` 自己 `curl_easy_init`，从不跨线程传递 |
| `CURLOPT_NOSIGNAL = 1L` | **没设**，见已知缺口 4 |
| 共享 DNS/连接缓存要用 `CURLSH` + 锁回调 | 没用。4 个连接各自解析 DNS，可接受 |

---

## 3. 区间切分

Range 是**闭区间**，这一点是所有 off-by-one 的来源。

```cpp
size_t chunk = (total_size + blockNum - 1) / blockNum;   // 向上取整
size_t start = i * chunk;
size_t end   = std::min(start + chunk - 1, total_size - 1);
if (start > end) break;                                   // 块数 > 文件字节数时
```

向上取整 + 末块夹到 `total_size - 1`，保证：

- 各块**不重叠**（下一块的 start 正好是上一块 end + 1）
- 并**覆盖到最后一个字节**
- 文件小于线程数时（如 302 字节 / 4 块），多余的块被 `break` 掉，不会发出 `start > end` 的非法 Range

> [Tips] `windows.h` 会污染 `std::min`
> `Multidownloader.h` 顶部那几行 `#ifdef max / #undef max` 不是装饰。
> `windows.h` 定义了 `min`/`max` 宏，会把 `std::min(a, b)` 展开成语法错误。
> 另一条路是在包含 `windows.h` 前 `#define NOMINMAX`——`Multidownloader.cpp` 里
> 已经有 `WIN32_LEAN_AND_MEAN` 了，`NOMINMAX` 本该放在一起。两种写法别混用。

---

## 4. 并发写盘：选了方案 B

roadmap 5.3 给了三个方案，这里选 **B：单文件 + 每线程独立句柄**。

| 方案 | 为什么没选 |
| --- | --- |
| A. 分片临时文件后拼接 | 要 2 倍磁盘空间，末尾一次全量拷贝。最简单，但学不到并发写的东西 |
| **B. 单文件 + 每线程独立句柄** | **选它**。无额外空间、无拼接，正好暴露 Windows 共享模式的问题 |
| C. 单句柄 + `WriteFile` + `OVERLAPPED` | 语义最干净，但绕开了 `std::ofstream`，留作对照实验 |

### 4.1 预分配

```cpp
CreateFileA(path, GENERIC_WRITE, 0, ..., CREATE_ALWAYS, ...)
SetFilePointerEx(h, total_size, nullptr, FILE_BEGIN)
SetEndOfFile(h)
CloseHandle(h)                      // ← 必须关掉
```

`CREATE_ALWAYS` 会截断已存在的文件，所以重跑不会残留上次的数据。
`dwShareMode = 0` 是**独占**打开——这没问题，因为句柄在函数返回前就关掉了，
线程还没启动。但要意识到：**这个句柄多活一秒，后面所有 `ofstream::open` 都会失败**。

预分配的好处是提前暴露磁盘空间不足；代价是 NTFS 要把这块空间的旧数据清零，
10 MB 感觉不到，1 GB 会有明显停顿。

### 4.2 每线程一个 `ofstream`

`SingleDownloader` 各自 `fp.open(path, binary | in | out)`，然后 `seekp(start_pos)`。

- `in | out` 组合是**不截断**的（单独 `out` 会把文件清成 0，四个线程互相清，直接毁掉）
- `binary` 不能少：MSVC 文本模式会把 `0x0A` 写成 `0x0D 0x0A`，二进制文件必坏
- 四个流各自有独立的文件指针和缓冲区，写的区间互不重叠，所以不需要加锁

> [Tips] 实测：MSVC 的 `ofstream` 默认就允许多句柄共享
> roadmap 建议用 `_fsopen(path, "rb+", _SH_DENYNO)` 显式指定共享模式。
> 实测 MSVC 的 `basic_filebuf::open` 内部走的就是 `_Fiopen` → `_fsopen(..., _SH_DENYNO)`，
> 四个 `ofstream` 同时打开同一文件全部成功。
> **但别依赖这个**——它是实现细节，不是标准保证，换个 STL 实现就未必。
> 真要写产品代码，`_fsopen` 显式指定才是对的。

---

## 5. 踩过的三个坑

这一节是这份 readme 最值钱的部分。三个 bug 的共同点：**症状都指向错误的方向**。

### 5.1 头回调的 `size` 不是字节数

```cpp
size_t header_triple(void* ptr, size_t size, size_t nmemb, void* stream) {
    self->header_callback(ptr, size);        // ✗ 错
    self->header_callback(ptr, size * nmemb); // ✓ 对
    return size * nmemb;
}
```

libcurl 的回调签名里 `size` **恒为 1**，`nmemb` 才是这一行的长度。
写错了不会崩，只会让 `std::string line(ptr, 1)` 每次拿到一个字符，
于是永远匹配不上 `content-length:`，`total_size` 保持 0。

对照 `singledownloader.cpp` 的 `write_triple`——那边一开始就写对了 `size * nmem`。
**同一个坑在同一个项目里，一处对一处错。**

### 5.2 `std::transform` 的输出迭代器写错了目标

```cpp
std::string line_lower = line;
std::transform(line.begin(), line.end(), line.begin(), tolower);  // ✗ 小写化的是 line
size_t pos = line_lower.find("content-length:");                   // 在原始大小写里找小写串
```

变量名叫 `line_lower`，内容却是原样大小写；变量名叫 `line`，内容才是小写的。
服务端发的是 `Content-Length: 10485760`，`find` 永远返回 `npos`。

这个 bug 修了两次才对——第一次把 `std::string line_lower;`（空串）改成
`std::string line_lower = line;`，看起来「初始化了」，但 `transform` 的三个迭代器
还是指向 `line`，find 依旧失败，**现象一模一样**。
正确的改法是让 `transform` 的输入输出都指向 `line_lower`。

> [Tips] HTTP 头字段名大小写不敏感，但两种都会真实出现
> HTTP/1.1 用 `Content-Length`（首字母大写），HTTP/2 强制**全小写** `content-length`。
> 所以归一化不是洁癖，是必需。测试时两种都要覆盖：
> `proof.ovh.net` 走 HTTP/1.1，国内镜像多是 HTTP/2。

### 5.3 移动构造后，函数体里的形参是空壳

这个最阴：

```cpp
SingleDownloader::SingleDownloader(std::string target_url,
                                   std::string local_url, ...) :
    target_url(std::move(target_url)),
    local_url(std::move(local_url)),      // ← 形参被掏空
    ...
{
    subCurl.setopt(CURLOPT_URL, this->target_url.c_str());   // 加了 this->，对
    fp.open(local_url, ...);                                 // ✗ 没加，用的是空壳形参
    if (!fp.is_open())
        throw std::runtime_error("Failed to open file: " + this->local_url);
}
```

实际 `open` 的是**空文件名**，但报错信息用的是 `this->local_url`，
于是屏幕上打出 `Failed to open file: out.bin`——而 `out.bin` 就躺在当前目录，
大小还完全正确（预分配出来的）。**报错信息主动把你引向了错误的方向。**

排查时先怀疑了 Windows 文件共享模式，专门写了个 probe 验证四个 `ofstream`
能否同时打开同一文件——结论是能（见 4.2 的 Tips），排除掉之后才回到代码本身。

顺带一提：`"out.bin"` 只有 7 个字符，落在 SSO（小字符串优化）范围内。
**别指望 SSO 让 move 变成 copy** —— MSVC 的 `basic_string` 移动构造对短串同样会
`_Tidy_init()` 把源置空。这个行为是实现相关的，正因如此才更不能依赖。

**规则**：构造函数初始化列表里 `std::move` 过的形参，在函数体里一律当作已销毁。
要用同名的成员，**必须**写 `this->`。

---

## 6. 构建与运行

```bash
# 必须在 x64 环境下，直接跑会因为 shell 是 x86 VS 环境而失败
call "路径\VC\Auxiliary\Build\vcvars64.bat"
cmake --build --preset msvc-debug --target 12-06-mt
```

产物落在 `12-06-mt/build/bin/`（沿用各专题的 `EXECUTABLE_OUTPUT_PATH` 约定）。

```
12-06-mt <url> <输出路径>
```

线程数目前**硬编码在 `main.cpp` 里的 `block_num = 4`**，还没做成命令行参数。

### 测试用 URL

都确认过 `Accept-Ranges: bytes`：

| URL | 大小 | 协议 | 用途 |
| --- | --- | --- | --- |
| `https://mirrors.aliyun.com/debian/dists/stable/Release` | 138 607 B | HTTP/2（小写头） | 日常迭代，<1 s |
| `https://mirrors.tuna.tsinghua.edu.cn/debian-cd/current/amd64/iso-cd/SHA256SUMS` | 302 B | HTTP/2 | 边界：4 块每块 76 B，末块被 `std::min` 夹住 |
| `https://proof.ovh.net/files/10Mb.dat` | 10 MB | HTTP/1.1（大写头） | 跨国长连接，验证多线程加速比 |

### 已验证的结果

```powershell
.\12-06-mt.exe https://proof.ovh.net/files/10Mb.dat out.bin
curl.exe -s -o ref.bin https://proof.ovh.net/files/10Mb.dat
(Get-FileHash out.bin).Hash -eq (Get-FileHash ref.bin).Hash
# True
```

10 MB / 4 线程，与完整下载**字节级一致**。

单连接跨国到 OVH 实测 **~220 KB/s**，4 线程接近 4 倍。
瓶颈不是本地带宽，是**单条 TCP 流**被 RTT × 窗口和运营商单流限速卡住——
这正是分块下载存在的意义。反过来，换成局域网或国内高速镜像，
瓶颈会立刻转移到总带宽和磁盘写入，多线程基本没有增益。
**加速比跟源站强相关，别当成普适结论。**

> [Tips] PowerShell 里 `curl` 不是 curl
> PowerShell 把 `curl` 别名成了 `Invoke-WebRequest`，参数完全不兼容，
> 会报「缺少参数 SessionVariable」这种莫名其妙的错。必须写 `curl.exe`。
> 同理 `cmp` / `md5sum` / `dd` 在 PowerShell 里都不存在，
> 用 `Get-FileHash` / `fc.exe /b`，或者切到 git bash。

---

## 7. 必须能回答的问题

来自 roadmap 5.4，**把答案写进下面的 `## 我的答案` 一节**。

1. 为什么线程数不是越多越好？至少说出三个限制因素。
   （提示：服务端并发连接限制、TCP 建连 + TLS 握手开销、磁盘随机写、句柄数）
2. 进度汇总用 `std::atomic<uint64_t>` 每次写回调都 `fetch_add`，会有什么性能问题？
   怎么改？（提示：cache line 争用；线程本地累加 + 定期提交）
3. 某个块的连接断了，怎么只重试那一块而不影响其他线程？
4. 本阶段的 `results` 数组为什么必须检查？不检查会产生什么样的坏文件，
   为什么文件大小仍然是对的？
5. 四个 `ofstream` 写同一个文件不加锁是安全的，前提条件是什么？
   哪一条前提被破坏之后就不安全了？

**roadmap 的验收标准**：8 线程下载 1 GB 文件，MD5 与单线程一致；
线程数从 1 调到 16 画速度曲线，能解释拐点在哪。
**这条还没做**——目前只验证了 4 线程 / 10 MB。

---

## 8. 验收清单

| # | 场景 | 期望 | 状态 |
| --- | --- | --- | --- |
| 1 | 10 MB 文件 4 线程下载 | 哈希与完整下载一致 | ✅ 已验证 |
| 2 | 302 字节文件 4 线程下载 | 哈希一致，多余的块被 `break` 掉不发非法 Range | ⬜ |
| 3 | HTTP/2 源站（小写头） | 能正确解析 `content-length` | ⬜ |
| 4 | 某块返回非 206 | 程序**报错退出**，不产出损坏文件 | ❌ 见缺口 1 |
| 5 | 1 GB / 8 线程，与单线程对比 MD5 | 一致 | ⬜ |
| 6 | 线程数 1→16 速度曲线 | 能画出来并解释拐点 | ⬜ |
| 7 | 服务端不支持 Range | 识别并报错，或退化为单线程 | ❌ 见缺口 2 |
| 8 | URL 带 302 重定向 | 能跟随 | ❌ 见缺口 5 |

校验和（Windows 自带，不用加依赖）：

```bash
certutil -hashfile out.bin MD5
```

---

## 我的答案

（第 7 节的五个问题，写在这里）

---

## 已知缺口

1. **`results` 收集了每个线程的 `CURLcode`，但 `wait()` 之后没人检查**——这是正确性缺口。
   某块失败时，那段区间在预分配文件里是一片 0，**文件大小照样正确，程序静默返回 0**。
   `run()` 里已经判了 `http_code != 206` 并返回 `CURLE_HTTP_RETURNED_ERROR`，
   但这个返回值最终被丢掉了。下一步就是修它：让 `wait()` 返回聚合结果，或有失败即抛。
   在这之前，**每次都必须靠哈希对比才敢说下载成功**。

2. **没有检查服务端是否支持 Range**。`initTotalSize()` 只读了 `Content-Length`，
   没看 `Accept-Ranges`，也没做阶段 3 的 probe。碰上不支持 Range 的服务端，
   四个线程都会拿到 200 + 全量数据，`run()` 会因为 `http_code != 206` 全部失败——
   但因为缺口 1，这个失败传不出来。

3. **没有任何超时**。没设 `CURLOPT_TIMEOUT` / `CONNECTTIMEOUT` /
   `LOW_SPEED_LIMIT` + `LOW_SPEED_TIME`。连接僵死时 `curl_easy_perform` 会长时间阻塞，
   `join()` 跟着一起等，进程看起来就是挂死。
   注意真实下载**不能**用 `CURLOPT_TIMEOUT`（会掐断大文件），要用低速阈值。

4. **没设 `CURLOPT_NOSIGNAL = 1L`**。roadmap 5.1 明确要求多线程下必设。
   Windows 上影响小，但这是要养成的习惯，且阶段 7 上更多线程时会真的咬人。

5. **没有 `CURLOPT_FOLLOWLOCATION`**。遇到 302 直接把重定向响应体当内容写进文件。
   注意多线程下跟随重定向还有个额外问题：四个线程各自跟随，
   可能被负载均衡分到**不同的后端**，拿到不同版本的文件——
   正确做法是主线程 probe 出最终 URL，四个线程用同一个固定 URL。

6. **`header_callback` 用 `find` 而不是前缀匹配**。
   `line_lower.find("content-length:")` 会误匹配形如
   `Access-Control-Expose-Headers: content-length` 的头。
   应该判断 `pos == 0`，或者按冒号切分后比对字段名。

7. **线程数硬编码 `block_num = 4`**，没做成命令行参数——
   第 8 节验收 #6 的速度曲线做不了，先补这个。

8. **`std::ofstream` 的偏移用的是 `seekp(std::streamoff)`**。
   MSVC 上 `streamoff` 是 64 位，>4 GB 安全；但 `start_pos` / `end_pos` /
   `total_size` 全是 `size_t`，64 位构建下没问题，**32 位构建会溢出**。
   本项目只做 x64，记一笔即可。

9. **等分切块，没有负载均衡**。最慢的那块决定总时长，其余线程早早空转。
   roadmap 5.4 的小块 + 任务队列 / 工作窃取是阶段 7 的事，这里先知道有这个问题。

10. **没有 `.part` + rename 的原子完成**，直接写目标文件名。
    中途失败会留下一个大小正确、内容残缺的文件，用户无法区分「下完了」和「下了一半」。
    阶段 4 已经做过这套，这里为了聚焦并发写盘暂时省了，阶段 6 必须补回来。
