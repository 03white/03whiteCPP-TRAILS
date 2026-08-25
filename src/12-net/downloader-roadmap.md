# 多线程文件下载器：学习训练路线

终点是一个命令行下载器，支持 **HTTP Range 多线程分块**、**断点续传**、**chunked 编码回退**。

这份文档不给实现代码，只给：每阶段的目标、必须搞清楚的概念、**必须能自己回答的问题**、验收标准、以及已知的坑。
代码全部由你自己写。每个阶段建议独立成一个子目录（`12-02-*`、`12-03-*` …），保留过程产物，别一路重构掉。

前置：[curl.md](./curl.md)（libcurl 在 Windows 下的形态与构建）已经读过。

---

## 阶段 0：先把"分块"这个词拆开

这是全篇最容易埋雷的地方。你说的"分块编码"在实现里其实是**两件互不相干、甚至互相排斥**的事：

| | **HTTP 分块传输编码** | **下载器的分块并发** |
| --- | --- | --- |
| 英文 | `Transfer-Encoding: chunked` | range-based segmentation |
| 是谁的概念 | HTTP/1.1 协议的报文封装格式 | 你的下载器的调度策略 |
| 谁决定 | 服务端 | 客户端 |
| 目的 | 响应体长度事先未知时也能流式发送 | 用多个连接并发拉同一个文件 |
| 关键头 | `Transfer-Encoding: chunked`（且**无** `Content-Length`） | 请求 `Range:` / 响应 `206` + `Content-Range:` |
| libcurl 是否代劳 | **是**，写回调里看到的已经是解码后的纯数据 | **否**，完全靠你自己 |

**它们的关系是排斥的**：chunked 意味着服务端事先不知道总长度，那你也就拿不到总长度，
拿不到总长度就**无法预先切分区间**，多线程分块直接失效——只能退化成单连接顺序流式下载。

所以最终程序里，这两个词对应两条完全不同的代码路径：

```
探测响应
├─ 有 Content-Length 且 Accept-Ranges: bytes  →  多线程分块 + 断点续传
├─ 有 Content-Length 但不支持 Range           →  单线程，可续传（靠 Range 探试）
└─ Transfer-Encoding: chunked（无长度）        →  单线程流式，无进度百分比，不可续传
```

> [Tips]
> HTTP/2 里**没有** chunked 编码——分帧由 DATA 帧负责，`Transfer-Encoding: chunked` 在 HTTP/2 中是非法头。
> 但"长度未知"这个情况依然存在（只是不带 `content-length` 头而已）。
> 所以你的判断条件应该写成"**有没有拿到总长度**"，而不是"是不是 chunked"。这个抽象层次的选择直接决定代码能不能跨协议版本复用。

**必须能回答**：
1. 一个 `206 Partial Content` 的响应，可以同时是 `Transfer-Encoding: chunked` 的吗？如果可以，你怎么知道这一段有多少字节？
2. 为什么"无 `Content-Length`"不等价于"chunked"？至少举出两种其他情况。

---

## 阶段 1：Winsock 对照实验（1～2 天，只做一次）

**目的不是造轮子**，是让你之后读 libcurl 的 header 回调、Range 语义时，脑子里有具体的字节流，而不是抽象的 API。做完就扔，后面全程用 libcurl。

**目标**：用裸 Winsock 向一个 HTTP（不是 HTTPS）站点发一次 GET，把**原始响应字节**原样打到屏幕上，然后手写解析。

**要点**：

- `WSAStartup` / `getaddrinfo` / `socket` / `connect` / `send` / `recv` / `closesocket` / `WSACleanup`，链接 `ws2_32`
- 请求报文自己拼，注意 HTTP/1.1 **必须**带 `Host:`，行尾是 `\r\n`，头部以**空行**结束：

  ```
  GET /range/1024 HTTP/1.1\r\n
  Host: httpbin.org\r\n
  Connection: close\r\n
  \r\n
  ```

- `recv` 返回的是**字节流不是消息**，一次调用可能只给你半个头。必须自己缓冲、找 `\r\n\r\n` 边界。这是 TCP 粘包/拆包最真实的一课。

**至少动手解析这三样**：

1. **状态行 + 头部**：切出 `200` / `206`，把头部解析成键值对（注意头名**大小写不敏感**，值前有可选空格）。
2. **`Content-Length` 定长体**：读满 N 字节就停。
3. **chunked 体**：亲手解一遍。格式是

   ```
   1a\r\n            <- 十六进制长度，可能带 ";ext=val" 扩展，要忽略
   <26 字节数据>\r\n
   0\r\n             <- 长度 0 表示结束
   \r\n              <- 可选 trailer 头，然后空行
   ```

   拿 `http://httpbin.org/stream-bytes/2048` 这类接口试。

**验收**：
- 同一个 URL，你手写的解析结果和 `curl -v` 的输出对得上。
- 你能说清楚 `Connection: close` 和 keep-alive 在"怎么知道体结束了"这件事上的区别。

**坑**：
- Winsock 的 `recv` 返回 `0` 表示对端正常关闭，`SOCKET_ERROR`(-1) 才是出错，用 `WSAGetLastError()` 取码。别照抄 Linux 教程的 `errno`。
- 别碰 HTTPS。裸 socket 上做 TLS（Schannel）是另一个量级的工作量，与本路线无关。找个 `http://` 的测试目标。

> [Tips]
> 这一阶段做完，你会理解 libcurl 的 `CURLOPT_HEADERFUNCTION` 为什么是**一行一次**回调、
> 为什么状态行也会被当成一次"头"回调传给你、以及重定向时为什么会收到**多组**头。

---

## 阶段 2：libcurl easy API 与三个回调

**目标**：`12-02-easy`，把一个 URL 下载到本地文件，带实时进度条。

**核心 API**：

| 回调 | 选项 | 签名要点 |
| --- | --- | --- |
| 写数据 | `CURLOPT_WRITEFUNCTION` + `CURLOPT_WRITEDATA` | `size_t f(char* p, size_t sz, size_t nmemb, void* ud)`，**返回值必须等于 `sz*nmemb`**，否则 libcurl 认为你写失败，中断传输并返回 `CURLE_WRITE_ERROR` |
| 读响应头 | `CURLOPT_HEADERFUNCTION` + `CURLOPT_HEADERDATA` | 同样签名，每行调用一次（含结尾 `\r\n`），重定向时每一跳都会来一组 |
| 进度 | `CURLOPT_XFERINFOFUNCTION` + `CURLOPT_XFERINFODATA` | 需同时 `CURLOPT_NOPROGRESS = 0L`；**返回非 0 会中止传输**（这就是"取消下载"的实现方式） |

`CURLOPT_PROGRESSFUNCTION` 是老接口（参数是 `double`），新代码一律用 `XFERINFO`（`curl_off_t`）。

**跟仓库里已有模块的连接点**（这是本阶段真正的价值）：

- **[03-callback](../03-callback/readme.md)**：libcurl 要的是**裸 C 函数指针**。你不能直接传捕获了变量的 lambda——捕获 lambda 没有到函数指针的隐式转换。
  标准解法是 **trampoline（蹦床）**：写一个 `static` 成员函数或无捕获 lambda 当入口，把 `this` 从 `void* userdata` 里 `static_cast` 回来再转发到成员函数。
  这正是 03-callback 那套东西的第一次真实应用，务必自己推一遍为什么必须这样。
- **[01-ptr](../01-ptr/)**：`CURL*` 和 `curl_slist*` 都是需要成对释放的裸资源。
  用 `std::unique_ptr<CURL, 自定义删除器>` 包一层。注意删除器要写成**无状态的函数对象**（struct + `operator()`），
  别用 lambda 存进 `unique_ptr` 的类型参数里——想想为什么前者不会让 `unique_ptr` 变大。
- **[05-01-Observer](../05-oop/05-01-Observer/)**：进度上报做成观察者。
  现在只有一个"控制台进度条"观察者，但接口一旦定好，阶段 5 的多线程汇总进度可以直接复用。

**必须能回答**：
1. 写回调返回 0 会发生什么？返回比 `sz*nmemb` 大的数呢？
2. `CURLOPT_TIMEOUT` 为什么**不能**用在大文件下载上？该用哪两个选项替代？（提示：`LOW_SPEED_*`）
3. `curl_easy_setopt` 是变参函数，传 `0` 和传 `0L` 有区别吗？为什么文档里到处是 `1L`？

**验收**：下载一个 100 MB 文件，进度条流畅，Ctrl+C 之外还能通过进度回调返回非 0 来主动取消，且取消后 `curl_easy_perform` 返回 `CURLE_ABORTED_BY_CALLBACK`。

---

## 阶段 3：探测——决定走哪条路

**目标**：`12-03-probe`，输入 URL，输出一份"下载计划"。这是整个下载器最容易写错的一环。

需要探测出来的东西：

| 信息 | 来源 |
| --- | --- |
| 最终 URL | 跟完重定向后的 `CURLINFO_EFFECTIVE_URL` |
| 文件总长度 | 见下 |
| 是否支持 Range | 见下 |
| 校验标识 | `ETag` 或 `Last-Modified` 头 |
| 建议文件名 | `Content-Disposition: attachment; filename=` → 退回 URL 路径末段 |

**探测手法的选择**（这是本阶段的核心判断）：

- **不要只发 HEAD**。很多服务端对 HEAD 的支持是二等公民：可能返回 405，可能不给 `Content-Length`，可能给的头和 GET 的不一致（尤其是动态生成的内容和 CDN）。
- **推荐做法：发一个 `Range: bytes=0-0` 的 GET**。一石三鸟：
  - 返回 `206` → 服务端**真的**支持 Range（比看 `Accept-Ranges` 头可靠，那个头可以撒谎/缺失）
  - 响应头 `Content-Range: bytes 0-0/1234567` 里斜杠后面就是**总长度**
  - 只传 1 个字节，代价极小

  ```
  Content-Range: bytes 0-0/1234567
                          ^^^^^^^ 总长度；若为 * 表示服务端也不知道
  ```
- 返回 `200` 而不是 `206` → 服务端**忽略了** Range，把整个文件塞给你了。这是最危险的情况，见下方坑。

> [Tips] **必须处理的失败模式**
> 服务端忽略 `Range` 时不会报错，它返回 `200` + 完整文件体。
> 如果你的多线程代码没检查状态码，四个线程各自把**完整文件**写到自己的偏移上，
> 得到的是一个大小四倍、内容全错的文件，而且**没有任何报错**。
> 铁律：**每个分块请求都必须校验状态码是 206，并且校验 `Content-Range` 的起止和自己要的一致。**

**必须能回答**：
1. `Accept-Ranges: bytes` 存在，但实际请求 Range 返回 200——可能吗？你的程序信哪个？
2. 服务端返回 `Content-Range: bytes 0-0/*` 时，你怎么办？
3. `Content-Length` 在**压缩传输**（`Content-Encoding: gzip`）时描述的是压缩前还是压缩后的长度？这对按 Range 切分意味着什么？（想清楚这一条，你就知道下载器为什么**不该**开启 `CURLOPT_ACCEPT_ENCODING`）

**验收**：对以下四类目标都能输出正确的计划——支持 Range 的静态文件、忽略 Range 的服务器、chunked 接口、需要重定向的 URL。

---

## 阶段 4：单线程 + 断点续传

先把续传在单线程下做对，再谈并发。**不要跳过这一阶段直接上多线程**，否则你会同时调试两类 bug。

**目标**：`12-04-resume`。下载到一半 Ctrl+C，重跑命令能接着下，最终文件和完整下载的 MD5 一致。

**机制**：

- 已下载 N 字节 → 请求头 `Range: bytes=N-`（开区间，一直到结尾），本地文件以追加模式打开。
- **文件变了怎么办**：这是续传的正确性核心。用 `If-Range`：

  ```
  If-Range: "686897696a7c876b7e"      <- 阶段 3 存下来的 ETag
  Range: bytes=1048576-
  ```

  服务端语义：ETag 匹配 → 返回 `206` 只给你要的部分；**不匹配 → 返回 `200` 和整个新文件**。
  一个头搞定"校验 + 回退"，不需要你先发一次条件请求再判断。但你的代码必须能处理返回 200 的情况：丢弃本地进度，从头开始。

- 用 `.part` 临时文件 + 完成后 rename。别直接写目标文件名——否则用户无法区分"下完了"和"下了一半"。

**必须能回答**：
1. `If-Range` 里可以放 `Last-Modified` 的日期吗？和放 ETag 相比有什么风险？（提示：秒级精度）
2. 弱 ETag（`W/"xxx"`）能用于 `If-Range` 吗？为什么？
3. 断点在文件**最后一个字节之后**（即已经下完了）时，`Range: bytes=N-` 会得到什么响应？（`416` 意味着什么，怎么和"真的出错了"区分开）

**验收**：用 `curl --limit-rate` 或本地限速服务器制造慢速环境，中断 5 次以上再续传，最终校验和正确。

---

## 阶段 5：多线程分块

**目标**：`12-05-mt`，N 个线程并发拉不同区间，汇总进度，速度显著优于单线程。

### 5.1 libcurl 的线程规则（先背下来，违反了都是随机崩溃）

| 规则 | 说明 |
| --- | --- |
| `curl_global_init` 在 main 里调一次 | 它**不是**线程安全的，绝不能让多个线程各自调用；`curl_easy_init` 内部会隐式调它，所以更要抢在建线程之前显式调好 |
| 一个 easy handle 只能属于一个线程 | 不是"加锁就能共享"，是根本不支持。每个线程自己 `curl_easy_init` |
| `CURLOPT_NOSIGNAL = 1L` | 多线程下必设。libcurl 默认可能用信号做 DNS 超时，在多线程里是灾难。Windows 上影响小，但保持习惯 |
| 共享 DNS/连接缓存要用 `CURLSH` | `curl_share_*` 系列，且**必须**给它装锁回调（`CURLSHOPT_LOCKFUNC`/`UNLOCKFUNC`），否则等于没加锁 |

### 5.2 区间切分

总长 `L`，`n` 个线程。注意 Range 是**闭区间**：

```
第 i 块: [i*L/n, (i+1)*L/n - 1]
最后一块的右端 = L-1
```

先用等分把功能跑通，但要知道等分是有缺陷的——见 5.4。

### 5.3 并发写盘（本阶段最容易出错的地方）

三种方案，各有取舍：

| 方案 | 做法 | 优点 | 缺点 |
| --- | --- | --- | --- |
| A. 分片临时文件 | 每块写自己的 `.part0`/`.part1`…，全部完成后顺序拼接 | 实现最简单，无并发写问题，续传时每块进度就是文件大小 | 需要 2 倍磁盘空间，末尾有一次全量拷贝 |
| B. 单文件 + 每线程独立句柄 | 预分配目标文件，每个线程**自己** `fopen`/`CreateFile` 同一路径，`seek` 到自己的偏移写 | 无额外空间，无拼接 | 要处理 Windows 共享模式；每个句柄有独立文件指针，这是它能工作的前提 |
| C. 单文件 + 定位写 | 一个句柄，Windows 用 `WriteFile` + `OVERLAPPED` 指定偏移（POSIX 对应 `pwrite`） | 句柄唯一，语义最干净 | Windows 上 API 略绕，且要理解非异步句柄也能用 OVERLAPPED 传偏移 |

建议：**先 A 跑通，再改 B 或 C 作为对照实验**，正好能量出拼接那一遍拷贝的代价。

> [Tips] **B 方案在 Windows 上的具体坑**
> - 多个句柄打开同一文件需要共享模式允许写。用 `_fsopen(path, "rb+", _SH_DENYNO)` 明确指定，
>   不要依赖 `fopen` 的默认行为（不同 CRT 版本不一致）。
> - 大文件必须用 `_fseeki64`/`_ftelli64`，`fseek` 的 `long` 在 Windows 上是 32 位，**4 GB 以上直接溢出**。
> - 绝对不要多个线程共享同一个 `FILE*`：`FILE` 内部有位置和缓冲区，"seek 完再写"这两步之间会被别的线程插入。

**预分配**：开工前把文件撑到最终大小（`SetFilePointerEx` + `SetEndOfFile`，或 `_chsize_s`）。
好处是提前暴露磁盘空间不足，并减少碎片。代价是 NTFS 上这块空间的旧数据需要清零，可能有一次停顿。

### 5.4 分块调度

等分的问题：连接质量不均时，最慢的那块决定总时长，其余线程早早空转。

改进方向（阶段 7 再做，现在先知道）：
- **小块 + 任务队列**：切成远多于线程数的小块（如每块 4 MB），线程从队列里取。天然负载均衡。
- **工作窃取**：快线程接管慢线程尚未下载的尾部区间，需要能安全地"缩短"一个进行中的块。

**必须能回答**：
1. 为什么线程数不是越多越好？（至少说出三个限制因素：服务端并发限制、TCP 建连开销、磁盘随机写、`ulimit`/句柄数）
2. 进度汇总用 `std::atomic<uint64_t>` 每次回调都加，会有什么性能问题？怎么改？（提示：线程本地累加 + 定期提交）
3. 如果某个块的连接断了，怎么只重试那一块而不影响其他线程？

**验收**：8 线程下载 1 GB 文件，MD5 与单线程结果一致；把线程数从 1 调到 16，画出速度曲线，能解释拐点在哪。

---

## 阶段 6：多线程续传的持久化与崩溃一致性

**目标**：`12-06-journal`。多线程下载途中**强杀进程**（不是优雅退出），重启后能从各块的正确位置继续，最终校验和正确。

### 6.1 元数据文件

在 `.part` 旁边放一个 `.part.meta`，至少记录：

```
url                 最终 URL（重定向后的）
total_size          总字节数
etag / last_modified  用于 If-Range 校验
chunk_count
chunks[i] = { start, end, downloaded }   每块的区间和已完成字节数
```

格式随便（JSON / 自定义二进制都行），但**必须能原子替换**：写到 `.meta.tmp` 再 rename，
否则崩在写元数据的中途会得到一个半截的、解析不了的元数据文件，续传直接废掉。

### 6.2 唯一的正确性铁律

> **元数据里记录的 `downloaded`，必须永远 ≤ 实际已经落盘的字节数。**

顺序必须是：**数据落盘（flush）→ 再更新元数据**。

反过来（先更新元数据再写数据）如果崩在中间，元数据声称第 3 块已完成 10 MB，但盘上只有 8 MB，
续传时从 10 MB 处接着写，中间 2 MB 永远是垃圾数据——而且**校验和不对但你查不出原因**。

推论：元数据可以**低频**更新（比如每 2 秒或每 8 MB 一次），代价只是崩溃时多重下几 MB。
这是"性能 vs 丢失窗口"的经典权衡，不是可以省掉的步骤。

> [Tips]
> "flush 到盘"这件事本身有层次：`fflush` 只是把 CRT 缓冲交给 OS，
> 真正落盘要 `FlushFileBuffers`（POSIX 的 `fsync`）。
> 对下载器来说，进程崩溃用 `fflush` 就够（OS 缓冲还在），但**掉电**要 `FlushFileBuffers`。
> 想清楚你要防哪一种，别无脑每次都 flush——那会让速度掉一个数量级。

### 6.3 恢复流程

```
读 .meta
  ├─ 解析失败 / 与当前 URL 不符  → 放弃，从头下载
  └─ 成功 → 用存下的 ETag 发 If-Range 探测
              ├─ 206 → 逐块用 Range: (start+downloaded)-(end) 恢复
              └─ 200 → 服务端文件已变，删除 .part 和 .meta，从头开始
```

**必须能回答**：
1. 为什么恢复时要以**元数据里的 `downloaded`** 为准，而不是去看 `.part` 文件的实际大小？（多线程 + 预分配的情况下想一想）
2. 用户下载途中改了线程数（8 → 4），你的恢复逻辑怎么处理？是重新切分还是沿用旧的块划分？
3. 元数据文件被用户手动删了，但 `.part` 还在——怎么办才对用户最友好？

**验收**：写个脚本，在下载过程中随机时刻 `taskkill /F`，重启续传，循环 10 次，最终 MD5 依然正确。这个测试跑通了，续传才算真的做对了。

---

## 阶段 7：健壮性与调度

到这一步功能齐了，剩下的是让它在真实网络里活下来。

- **重试与退避**：区分可重试错误（`CURLE_OPERATION_TIMEDOUT`、`CURLE_PARTIAL_FILE`、`CURLE_RECV_ERROR`、5xx）和不可重试错误（404、403）。指数退避 + 抖动，设最大重试次数。
- **停滞检测**：`CURLOPT_LOW_SPEED_LIMIT` + `CURLOPT_LOW_SPEED_TIME`（如"30 秒内低于 1 KB/s 就断开重连"）。这比给大文件设 `CURLOPT_TIMEOUT` 正确得多。
- **限速**：`CURLOPT_MAX_RECV_SPEED_LARGE`，注意它是**每个 handle** 的，全局限速要自己把总额分给各线程。
- **连接复用**：分块间复用连接（`CURLSH` 共享连接缓存），或干脆一个线程一个 handle 循环取任务，让 libcurl 自动复用。
- **动态调度**：实现 5.4 里的小块任务队列，对比等分方案在"一个块特别慢"时的总耗时差异。
- **完整性校验**：如果服务端给了 `Content-MD5` 或 `Digest` 头就核对；没有的话至少核对最终文件大小 == `total_size`。

**验收**：拔网线 / 切 Wi-Fi 10 秒再恢复，下载能自动接上而不是失败退出。

---

## 阶段 8（可选）：multi API 对照

写完多线程版之后，用 `curl_multi_*` 在**单线程**里实现同样的并发下载，然后对比。

核心 API：`curl_multi_init` / `curl_multi_add_handle` / `curl_multi_perform` / `curl_multi_poll` / `curl_multi_info_read`。

对比维度：

| | 多线程 + easy | 单线程 + multi |
| --- | --- | --- |
| 并发模型 | 抢占式，OS 调度 | 事件驱动，你自己驱动循环 |
| 心智负担 | 每个线程逻辑是顺序的，但要处理同步 | 无锁无竞态，但逻辑被拆成回调 |
| 扩展到几百并发 | 线程开销明显 | 几乎无额外开销 |
| 写盘阻塞 | 只阻塞该线程 | **阻塞整个事件循环**——这是 multi 版最关键的陷阱 |

对下载器（并发数只有个位数、瓶颈在网络和磁盘）而言两者差别不大；
但理解"为什么 multi 版里绝不能在写回调里做同步磁盘 IO"，是理解事件驱动模型的关键一课，和 [03-02-async](../03-callback/03-02-async/readme.md) 直接对应。

---

## 测试环境

不要拿公网大文件当主要测试目标——慢、不稳定、行为不可控。

**本地服务器**（关键是它们对 Range 的支持差异，正好覆盖不同代码路径）：

| 工具 | 命令 | Range 支持 |
| --- | --- | --- |
| Python 内置 | `python -m http.server 8000` | **不支持**——会忽略 Range 返回 200 全文件。**正好用来测你的忽略检测逻辑** |
| rangehttpserver | `pip install rangehttpserver` → `python -m RangeHTTPServer 8000` | 支持 206 |
| nginx / caddy | `caddy file-server --listen :8000` | 支持，且行为接近生产环境 |

**公网测试接口**：

| 用途 | URL |
| --- | --- |
| Range / 206 | `https://httpbin.org/range/102400` |
| chunked 无长度 | `https://httpbin.org/stream-bytes/102400` |
| 重定向 | `https://httpbin.org/redirect/3` |
| 指定状态码 | `https://httpbin.org/status/416` |

**造测试文件**（Windows）：

```shell
fsutil file createnew test_1g.bin 1073741824
certutil -hashfile test_1g.bin MD5
```

**排查工具**：
- `curl -v -r 0-0 <url>` —— 一行命令看服务端到底支不支持 Range，比写代码试快得多
- `curl -sI <url>` —— 看头部
- `CURLOPT_VERBOSE = 1L` + `CURLOPT_DEBUGFUNCTION` —— 在自己程序里看完整的请求/响应报文
- Wireshark 过滤 `http` —— 只对明文 HTTP 有用，HTTPS 看不到内容

---

## 最终验收清单

功能：

- [ ] 支持 Range 的服务器 → 多线程分块，速度明显快于单线程
- [ ] 忽略 Range 的服务器 → **正确检测到并回退单线程**，文件不损坏
- [ ] chunked 响应 → 单线程流式下载成功，进度显示为"已下载 X MB（总量未知）"
- [ ] 重定向 → 跟随后对最终 URL 分块
- [ ] 中断续传 → 强杀 10 次，最终 MD5 正确
- [ ] 服务端文件变更 → `If-Range` 检出，自动重新下载而不是产出损坏文件
- [ ] 磁盘空间不足 → 预分配阶段就报错，不是下到 90% 才失败
- [ ] 4 GB 以上大文件 → 无整数溢出（全程 `curl_off_t` / `uint64_t`，杜绝 `long`/`int`）

工程：

- [ ] 所有 libcurl 资源用 RAII 包装，无裸 `curl_easy_cleanup`
- [ ] 写/头/进度回调统一走 trampoline，无全局变量传递上下文
- [ ] 进度上报走观察者接口，控制台输出只是其中一个实现
- [ ] `curl_global_init` 只在 main 调用一次，且在任何线程创建之前
- [ ] 分块请求校验 `206` + `Content-Range` 一致性
- [ ] 元数据写入是"数据先落盘、元数据后更新"，且原子替换

---

## 每阶段的目录建议

```
12-net/
├── curl.md                      已完成：libcurl 的构建与产物
├── downloader-roadmap.md        本文件
├── 12-01-curl/                  已完成：版本探针
├── 12-02-winsock-http/          阶段 1：裸 socket 对照实验（一次性）
├── 12-03-easy/                  阶段 2：easy API + 三回调 + RAII
├── 12-04-probe/                 阶段 3：探测与下载计划
├── 12-05-resume/                阶段 4：单线程续传
├── 12-06-mt/                    阶段 5：多线程分块
├── 12-07-journal/               阶段 6：崩溃一致性
├── 12-08-downloader/            阶段 7：最终成品
└── 12-09-multi/                 阶段 8（可选）：multi API 对照
```

每个子目录一个 `readme.md`，按 `curl.md` 的路数写：**结论先行 + 用工具验证 + 记下踩过的坑**。
阶段 3 和阶段 6 那两组"必须能回答"的问题，把你的答案写进去——半年后回头看，这些比代码有价值。

---

## 参考资料

| 主题 | 出处 |
| --- | --- |
| Range 请求 | RFC 9110 §14（`Range` / `Content-Range` / `If-Range` / `206` / `416`） |
| chunked 编码 | RFC 9112 §7.1 |
| 条件请求 | RFC 9110 §13（ETag 强弱、`Last-Modified` 精度问题） |
| libcurl 选项全表 | `curl_easy_setopt(3)` 手册页 |
| 线程安全规则 | libcurl 官方 "libcurl-thread" 文档 |
| 各选项的示例 | curl 源码 `docs/examples/`（尤其 `multithread.c`、`chkspeed.c`、`resume.c`） |
