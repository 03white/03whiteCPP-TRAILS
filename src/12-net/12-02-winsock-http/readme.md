# Winsock 裸 socket 手写 HTTP 客户端

> 阶段 1 的实验记录。目的不是造轮子，是为了之后读 libcurl 的 header 回调和 Range 语义时，
> 脑子里有具体的字节流。做完就扔，后续全程用 libcurl。
> 路线见 [downloader-roadmap.md](../downloader-roadmap.md)。

实验对象：`httpbin.org`，两个端点 —— `/range/1024`（定长体）和 `/stream-bytes/2048`（chunked 体）。

## 1. 环境与构建

MSVC 14.51 / Windows 11。链接 `ws2_32`（Winsock 2 的导入库）。

```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
```

写在 CMake 里而不是 `#pragma comment(lib, "ws2_32.lib")`：后者是 MSVC 私有扩展，MinGW 不认。

### 1.1 头文件顺序是有讲究的

```cpp
#include <winsock2.h>
#include <ws2tcpip.h>   // getaddrinfo / addrinfo，必须在 winsock2.h 之后
```

`windows.h` 会自动 include `winsock.h`（Winsock **1**），和 `winsock2.h` 里的同名结构体/宏大面积冲突，报一屏 `C2011 redefinition`。两道保险：

1. `winsock2.h` 必须排在 `windows.h` 之前；
2. `WIN32_LEAN_AND_MEAN` 让 `windows.h` 不要自动拉 `winsock.h` 进来。

`NOMINMAX` 挡掉 `windows.h` 里的 `min`/`max` 宏 —— 它们会破坏 `<algorithm>`，`std::min` 会被展开成一堆语法错误。

### 1.2 三个必须 RAII 的资源

| 类 | 包的是 | 要点 |
| --- | --- | --- |
| `WSAGuard` | `WSAStartup` / `WSACleanup` | 禁拷贝。Winsock 内部有引用计数，多次 `WSACleanup` 会破坏它 |
| `SockGuard` | `socket()` / `closesocket()` | 禁拷贝、**可移动**（`connect_to` 要值返回） |
| `AddrinfoPtr` | `getaddrinfo` / `freeaddrinfo` | `unique_ptr<addrinfo, AddrinfoDeleter>` |

`SockGuard` 的移动语义不是装饰。`connect_to()` 需要把连好的 socket 值返回出来：

```cpp
SockGuard sock8 = connect_to(host, port);   // 禁拷贝 + 有移动构造 = 合法
```

判失败要用 `INVALID_SOCKET`，不能写 `== -1`：

```cpp
SOCKET sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
if (sockfd == INVALID_SOCKET) continue;
```

`SOCKET` 是 `UINT_PTR`（无符号），`INVALID_SOCKET` 是 `(SOCKET)~0`。写 `== -1` 靠的是有符号→无符号的隐式转换才碰巧成立，语义上是错的。

## 2. 请求报文

```cpp
const std::string request = "GET /range/1024 HTTP/1.1\r\n"
                            "Host: httpbin.org\r\n"
                            "Connection:close\r\n"
                            "\r\n";
```

实际发出去 **65 字节**：

| 行 | 字节数 |
| --- | --- |
| `GET /range/1024 HTTP/1.1\r\n` | 26 |
| `Host: httpbin.org\r\n` | 19 |
| `Connection:close\r\n` | 18 |
| `\r\n`（空行，头部结束） | 2 |
| **合计** | **65** |

程序输出 `Sent 65 bytes`，对得上。

两处值得注意：

- **`Connection:` 后面没有空格。** RFC 9112 里冒号后的空白是 `OWS`（optional whitespace），省掉合法，服务端也确实接受了。但绝大多数实现都会写空格 —— 这一个字节的差别正好解释了为什么是 65 而不是 66。
- **`Host` 头在 HTTP/1.1 是强制的**，缺了服务端会回 400。虚拟主机时代一个 IP 上有多个站点，服务端靠它区分。

`send()` 可能只发出去一部分，正确写法是循环发：

```cpp
int send_all(SOCKET sock, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int n = send(sock, buf + total_sent, len - total_sent, 0);
        if (n == SOCKET_ERROR || n == 0) return -1;
        total_sent += n;
    }
    return total_sent;
}
```

65 字节的短报文基本不会遇到部分发送，但要知道正确写法 —— 大报文（POST body）一定会遇到。

## 3. 原始响应

`GET /range/1024` 的实际响应头（和 `curl -v --http1.1 http://httpbin.org/range/1024` 逐行对照过，一致）：

```
HTTP/1.1 200 OK
Date: Fri, 28 Aug 2026 09:30:14 GMT
Content-Type: application/octet-stream
Content-Length: 1024
Connection: close
Server: gunicorn/19.9.0
ETag: range1024
Accept-Ranges: bytes
Content-Range: bytes 0-1023/1024
Access-Control-Allow-Origin: *
Access-Control-Allow-Credentials: true
```

体是 1024 字节的 `abcdefg...` 循环。

> **一个反常之处**：状态码是 `200`，却带了 `Content-Range`。
>
> 严格说这是 httpbin 的毛病 —— `Content-Range` 只在 `206` 和 `416` 里有意义。我们没发 `Range` 请求头，服务端本就该只回 200 + Content-Length。
>
> 这正好是 12-04 探测阶段的核心教训的预演：**头字段的存在不等于语义成立**。如果按「有 `Content-Range` 就说明支持 Range」来判断，这里就会误判。12-04 的结论是只信 `206` 状态码本身。

## 4. 头部解析

### 4.1 边界是 `\r\n\r\n`，四个字节

```cpp
if (buffer.find("\r\n\r\n") != std::string::npos) break;
```

头部结束的标志是**空行**，也就是 `\r\n\r\n`。找 `\r\n` 的话状态行一读完就退出了，头部根本没收全。

### 4.2 必须自己缓冲

```cpp
while (buffer.find("\r\n\r\n") == std::string::npos) {
    char temp[1024];
    int n = recv(sock, temp, sizeof(temp), 0);
    if (n > 0) buffer.append(temp, n);
    else if (n == 0) throw std::runtime_error("Connection closed before header complete");
    else throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
}
```

一次 `recv` 给你多少字节完全看 TCP，可能是半个头，也可能是头 + 一截体。这是 TCP **字节流**的本质：它不保留任何消息边界。详见 Q2。

### 4.3 头名大小写不敏感（RFC 9110 §5.1）

`read_head()` 里存进 map 之前统一转小写：

```cpp
std::transform(key.begin(), key.end(), key.begin(),
    [](unsigned char c) -> unsigned char { return std::tolower(c); });
```

`func_TODO_7` 里的早期版本**没有**转小写，直接 `headers.find("Content-Length")`。它能跑通纯属运气 —— httpbin 恰好发的是规范大小写。换一个发 `content-length` 的服务端就会查不到，然后当成「没有长度」走错分支。

> 这个 map 现在的做法是「存进去时转小写」。更好的做法是用带大小写不敏感比较器的 `map`：查一次 `O(logN)`，而不是每次查都线性扫一遍。

### 4.4 用 `find` 而不是 `operator[]`

```cpp
const std::string* find_header(const std::map<std::string, std::string>& headers,
                               const std::string& name) {
    auto it = headers.find(name);
    return it != headers.end() ? &it->second : nullptr;
}
```

两个理由：

1. `map::operator[]` 查不到会**插入**一个空串，静默给你一个「存在但为空」的头。
2. 返回指针比返回 `std::string` 诚实 —— 「头不存在」和「头的值是空串」是两件不同的事，后者在 HTTP 里合法。

## 5. 定长体 vs chunked 体

### 5.1 定长：按 `Content-Length` 读满

```cpp
size_t content_length = 0;
if (auto it = headers.find("Content-Length"); it != headers.end()) {
    content_length = std::stoull(it->second);   // stoull 自己跳过前导空格
}
while (body.size() < content_length) {
    size_t remain = content_length - body.size();
    int want = static_cast<int>(std::min(remain, sizeof(temp)));
    int n = recv(sock, temp, want, 0);
    if (n > 0)       body.append(temp, n);
    else if (n == 0) throw std::runtime_error("Connection closed before body complete");
    else             throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
}
```

`recv` 第三参是 `int`，`std::min` 的结果是 `size_t` —— 显式收窄消掉 `C4267`。

### 5.2 chunked：自己解帧

`GET /stream-bytes/2048` 的响应头里有 `Transfer-Encoding: chunked` 且**没有** `Content-Length`。

用 `curl --raw`（关闭自动解码）抓到的真实字节，为了看得清用的是 64 字节版本：

```
$ curl -s --raw --http1.1 http://httpbin.org/stream-bytes/64 | xxd

00000000: 3430 0d0a d067 31df 2fa3 bc5c 231d ed03  40...g1./..\#...
00000010: 3c8c 51dc dc77 3021 4ea2 d732 661f 0718  <.Q..w0!N..2f...
00000020: 0f61 062d 1513 e1e1 ac3d c6bf be3a c884  .a.-.....=...:..
00000030: e92e 5ac5 8252 4da3 1ea5 645b bb5a 83c9  ..Z..RM...d[.Z..
00000040: b72d cbf5 0d0a 300d 0a0d 0a              .-....0....
```

拆开看：

| 偏移 | 字节 | 含义 |
| --- | --- | --- |
| `0x00` | `34 30 0d 0a` | `"40\r\n"` —— chunk 长度行，**十六进制** 0x40 = 64 |
| `0x03` | （64 字节数据） | chunk 数据 |
| `0x44` | `0d 0a` | 数据后的 `\r\n`（不算在长度里） |
| `0x46` | `30 0d 0a` | `"0\r\n"` —— 长度 0 的 chunk = 结束标记 |
| `0x49` | `0d 0a` | trailer 区结束的空行 |

三个容易错的点：

1. **长度是十六进制**，不是十进制。`"40"` 是 64 不是 40。
2. **数据后面那个 `\r\n` 不算在长度里**，读完 N 字节数据还要再吃掉 2 字节。
3. **`0\r\n` 之后还有一个 `\r\n`**（可能夹着 trailer 头）。少读这 2 字节，下一个请求的解析就全乱了。

### 5.3 `leftover_body` —— 本阶段最经典的 bug

读头的循环是 `recv` 进 buffer 找 `\r\n\r\n`，而 **`recv` 不会好心地在头部结束处停下**。那一次 `recv` 极可能已经把头之后的一批 body 字节一起读进来了：

```cpp
struct ResponseHead {
    // ...
    std::string leftover_body;   // 读头时顺带多读进来的体字节
};
```

这些字节必须原样交给解码器，一个都不能丢：

```cpp
ChunkDecoder decoder;
std::string body;
decoder.feed(head.leftover_body.data(), head.leftover_body.size(), body);   // ← 别漏
while (!decoder.done()) { /* recv -> feed */ }
```

丢了会怎样：第一个 chunk 的长度行没了，解码器一上来就在数据中间读「长度」，随机解出一个巨大的十六进制数，然后卡住等一个永远收不满的 chunk。**现象是「程序卡死在 recv」，看着像网络问题，其实是自己把长度行吃掉了。**

`func_TODO_7`（定长那条路）没被咬到，是因为那段代码碰巧把 leftover 留在了 `body` 变量里。

### 5.4 实际运行结果

```
Connected successfully!
Sent 65 bytes
====================Header================
HTTP/1.1 200 OK
...
Content-Length: 1024
====================Body==================
abcdefghijklmnopqrstuvwxyzabcdefghij...   (1024 字节)

Connected successfully!
Body size: 2048
First 32 bytes: 86 8B 3C 7A A0 21 65 FF 76 58 22 5F 60 49 A6 18 FE 4D 2C 57 0F C5 91 11 AA BB 91 5D AA E4 2C 6D
```

`Body size: 2048` —— 验收通过。

chunked 的体是随机二进制，**不要 `cout << body`**：终端会被控制字符搞乱，Windows 文本模式下 `0x1A` 还可能被当 EOF 截断。打字节数 + 前 32 字节 hex 就够。

### 5.5 两个实验必须开两条连接

请求头里写了 `Connection: close`，服务端发完 `/range/1024` 的响应就把连接关了。第一个实验用的那个 `sock` 已经是死 fd，不可能拿去发第二个请求 —— 所以有了 `connect_to()`。

抽出这个函数还有个副作用是好的：两次实验共用同一段连接代码，观察到的差异只可能来自「响应体编码方式不同」，而不是别处。

## 6. 必须能回答的问题

**Q1. `Connection: close` 和 keep-alive 在"怎么知道响应体结束了"这件事上有什么区别？**

`Connection: close` 下，**连接关闭本身就是结束标志** —— 一直 `recv` 到返回 0 即可，服务端不需要提前告诉你长度。

代价是**区分不了「发完了」和「网断了」**。对方 FIN 和中途掉线在 `recv` 返回值上都是 0，你拿到的半个文件看起来和完整文件一样正常。对下载器来说这是致命的。

keep-alive（HTTP/1.1 默认）下，连接要留给下一个请求复用，所以**必须有独立的定帧机制**，二选一：

- `Content-Length: N` —— 读满 N 字节
- `Transfer-Encoding: chunked` —— 读到 `0\r\n\r\n`

两者都比「等对端关连接」可靠：读不满 N / 没等到结束 chunk 就断了，你**知道**这是截断的响应，可以抛异常。这就是为什么下载器宁可要长度也不要「读到关闭为止」。

**Q2. 一次 `recv` 返回的数据，为什么不能假设它正好是一个完整的 HTTP 头部？**

因为 **TCP 是字节流协议，不是消息协议**。它只保证字节的顺序和完整，不保留任何发送侧的边界信息。

`recv` 返回的是「此刻内核缓冲区里有的、且不超过你给的 buffer 大小的字节数」，这个数取决于：MTU 分片、Nagle 算法、对端的 `send` 调用方式、网络拥塞、接收窗口……全都和 HTTP 的语义无关。

具体会遇到的三种情况：

1. **只收到半个头** —— 必须继续 `recv` 并追加到缓冲区，直到找到 `\r\n\r\n`。
2. **头 + 一截体一起来** —— 多出来的字节就是 `leftover_body`，必须留着（见 5.3）。
3. **一次收到多个响应** —— pipelining 时会遇到，本阶段没做。

结论：**自己维护缓冲区 + 自己找边界**，是所有基于 TCP 的协议解析的通用范式。HTTP 如此，Redis 协议、MySQL 协议都一样。

**Q3. 一个 `206 Partial Content` 的响应，可以同时是 `Transfer-Encoding: chunked` 的吗？
如果可以，你怎么知道这一段有多少字节？**

**可以。** 这两个头处在**不同的层次**，互相正交：

- `Content-Range: bytes 500-999/10000` 回答「这是文件的**哪一段**」—— 语义层。
- `Transfer-Encoding: chunked` 回答「这段字节在连接上**怎么定帧**」—— 传输层。

服务端动态生成部分内容（比如按 Range 实时转码）时，它知道自己在发哪一段，但发之前算不出这段压缩后有多长 —— 这时就是 206 + chunked。

怎么知道有多少字节，两条路都行：

1. **从 `Content-Range` 算**：`last - first + 1`。上例是 `999 - 500 + 1 = 500`。
2. **解码到结束 chunk**：`0\r\n\r\n` 之前累计的解码后字节数。

两者应该相等，不等就是服务端有问题 —— 下载器可以拿它当一致性校验。

**注意 206 + chunked 时不会有 `Content-Length`**，别去找它。这正是 12-04 里「206 的 `Content-Length` 是本段长度不是总长」那条陷阱的近亲：**同一个概念在不同层次有不同的数**。

**Q4. 为什么"无 `Content-Length`"不等价于"chunked"？至少举出两种其他情况。**

`Content-Length` 缺失只说明「长度不是用这个头表达的」，定帧方式还有好几种：

1. **`Connection: close` + 读到连接关闭。** HTTP/1.0 的默认做法，HTTP/1.1 里显式声明也合法。本实验的 `/range/1024` 就带了 `Connection: close`，只不过它同时也给了 `Content-Length`。
2. **响应本身就没有体。** `204 No Content`、`304 Not Modified`、所有 `1xx` —— 规范规定它们不能有 body，自然也不需要长度。
3. **`HEAD` 请求的响应。** 它**有** `Content-Length`（描述 GET 会返回多长），但**没有** body。「有长度」和「有体」在这里是分开的。
4. **`multipart/byteranges`。** 一次请求多个 Range 时，206 的体是 MIME 多段格式，靠 `Content-Type` 里的 `boundary` 字符串定帧。

所以判断逻辑不能写成 `if (!has_content_length) → chunked`。正确的优先级（RFC 9112 §6）是：

```
状态码/方法决定有没有体  →  Transfer-Encoding  →  Content-Length  →  读到连接关闭
```

`Transfer-Encoding` 排在 `Content-Length` **前面**，两个都在时必须忽略 `Content-Length`。这条规则的由来是 **HTTP 请求走私**（request smuggling）：前后两个代理如果对「信哪个」判断不一致，攻击者就能在一个请求里藏进第二个请求。

## 7. 踩过的坑

| 现象 | 根因 | 结论 |
| --- | --- | --- |
| 一屏 `C2011 redefinition` | `windows.h` 自动拉了 Winsock 1 进来 | `winsock2.h` 排在前面 + `WIN32_LEAN_AND_MEAN` |
| `std::min` 报语法错误 | `windows.h` 的 `min`/`max` 宏 | `NOMINMAX` |
| `std::main` 找不到 | 手滑，取小值的是 `std::min`，在 `<algorithm>` 里 | —— |
| `Content-Length` 永远查不到 | 写成了 `Content_Length`（下划线） | 头名是连字符 |
| 长度莫名其妙是 0 | `map::operator[]` 查不到会**插入**空串 | 用 `find`，返回指针 |
| 换个服务端就查不到头 | map 区分大小写，httpbin 恰好发规范大小写 | 存进去时转小写（RFC 9110 §5.1） |
| 头部只收到一半 | 找的是 `\r\n`，状态行读完就退出了 | 边界是 `\r\n\r\n`，四个字节 |
| **程序卡死在 `recv`** | 读头时多读进来的体字节没喂给解码器，长度行被吃掉了 | `leftover_body` 必须原样 feed（5.3） |
| 第二个请求发不出去 | `Connection: close`，第一个 socket 已经是死 fd | 抽 `connect_to()`，开新连接 |
| 错误码取不到 | Winsock 的错误不走 `errno` | `WSAGetLastError()` |
| 判失败写成 `== -1` | `SOCKET` 是无符号，靠隐式转换碰巧成立 | 用 `INVALID_SOCKET` |
| 终端输出乱码/被截断 | chunked 体是随机二进制，`0x1A` 在文本模式下当 EOF | 打 hex，别直接 `cout` |
| `C4267` 收窄警告 | `size_t`(8) → `int`(4)，`recv`/`connect` 的参数 | 显式 `static_cast<int>` |

---

## 验收

- [x] `/range/1024` 定长体读满 1024 字节，头部解析出状态行 + 键值对
- [x] `/stream-bytes/2048` chunked 手工解码，`body.size() == 2048`
- [x] 原始响应头与 `curl -v --http1.1` 的输出逐行一致
- [x] `WSAStartup` / `socket` / `getaddrinfo` 全部 RAII，无裸释放
- [x] `send` 走循环，`recv` 走缓冲，不假设单次调用的边界

## 这一阶段留给后面的东西

做完就扔，但有四个概念会在 12-03 / 12-04 反复出现：

| 这里手写的 | 后面对应的 |
| --- | --- |
| 缓冲 + 找 `\r\n\r\n` + 逐行切头 | libcurl 的 `CURLOPT_HEADERFUNCTION`（一行一次回调） |
| `ChunkDecoder` 手工解帧 | libcurl 内部做掉了，写回调收到的是**解码后**的数据 |
| 头名大小写不敏感 | `ResponseHeaders::to_lower`（12-04） |
| `Content-Range` / `Accept-Ranges` 头的存在 ≠ 语义成立（见 3 节） | 12-04 的核心结论：只信 `206` 状态码 |
