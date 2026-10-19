# Windows 进程间通信（IPC）：命名管道

本项目用 **命名管道（Named Pipe）** 实现一个简化的输入法架构：
`client` 捕获按键发给 `service`，`service` 维护输入缓冲（模拟输入法引擎）再返回结果。
这就是小狼毫（Weasel）这类"重服务端、轻客户端"输入法的基本骨架。

## 1. 什么是 IPC

**进程间通信（Inter-Process Communication, IPC）** 指的是：操作系统让两个相互独立的进程能够交换数据、协同工作的机制。

每个进程有自己独立的**虚拟地址空间**，进程 A 的变量指针给进程 B 直接解引用就是非法访问。
所以进程之间不能共享内存、不能直接调用对方函数，必须通过内核提供的"通道"来传数据，这就是 IPC。

常见的 IPC 方式：

| 方式 | 特点 | 适用场景 |
| --- | --- | --- |
| 管道 / 命名管道 | 字节流或消息流，跨进程无亲缘要求 | 本地进程通信，最常用 |
| 共享内存 | 最快，需自己做同步 | 大数据量、高频传输 |
| 套接字 Socket | 可跨主机，通用 | 网络通信、本机也可用 |
| 消息队列 | 内核排队，异步 | 松散耦合的模块 |
| 邮槽 Mailslot | 单向广播 | 简单通知 |

本项目选择 **命名管道**，因为它：本地通信快、Windows 原生、支持全双工、有现成的消息模式。

### 为什么要"服务端/客户端"拆分

以输入法为例：

- 每个应用（记事本、浏览器）都需要输入法，但输入法引擎很"重"（词库、候选、算法）。
- 若把它塞进每个应用，资源浪费、升级困难。
- 于是做成 **一个常驻 service + 多个轻量 client**，client 只负责收集按键、显示候选，真正的逻辑全在 service。

这正是可插拔/微内核思想在进程层面的体现：**client 与 service 通过稳定的管道协议通信，接口不变，两侧可独立演进。**

## 2. Windows 命名管道基础与 API 详解

### 2.1 命名管道的本质

命名管道是由内核管理的**一块缓冲区 + 一个名字**，名字长这样：

```
\\.\pipe\SimpleInputServer
```

- `\\.\` 表示"本机"，`pipe\` 是固定前缀，后面是自定义名。
- 服务端用 `CreateNamedPipe` **创建**这个名字的管道。
- 客户端用 `CreateFile` **打开**这个名字，双方就连上了。

### 2.2 服务端 API

**`CreateNamedPipe` —— 创建管道实例**

```cpp
HANDLE CreateNamedPipe(
    LPCWSTR lpName,              // 管道名 \\.\pipe\xxx
    DWORD   dwOpenMode,          // 访问模式（方向、重叠、写透传）
    DWORD   dwPipeMode,          // 管道模式（类型、读模式、等待模式）
    DWORD   nMaxInstances,       // 最大实例数
    DWORD   nOutBufferSize,      // 输出缓冲区大小
    DWORD   nInBufferSize,       // 输入缓冲区大小
    DWORD   nDefaultTimeOut,     // 默认超时（毫秒）
    LPSECURITY_ATTRIBUTES lpSA   // 安全属性，NULL 为默认
);
```

本项目用到的参数含义：

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `dwOpenMode` | `PIPE_ACCESS_DUPLEX` | 双向通信（可读可写） |
| `dwPipeMode` | `PIPE_TYPE_MESSAGE` | **消息模式**：写一次读一次，边界保留；若是 `PIPE_TYPE_BYTE` 则是字节流 |
| `dwPipeMode` | `PIPE_READMODE_MESSAGE` | 读的时候也按消息边界读 |
| `dwPipeMode` | `PIPE_WAIT` | 阻塞模式，数据没到就等 |
| `nMaxInstances` | `PIPE_UNLIMITED_INSTANCES` | 允许多个同名实例（多客户端） |
| 缓冲区 | `BUF_SIZE` | 单次收发的缓冲区大小 |

**`ConnectNamedPipe` —— 等待客户端连上来**

```cpp
BOOL ConnectNamedPipe(HANDLE hNamedPipe, LPOVERLAPPED lpOverlapped);
```

- 阻塞直到有客户端 `CreateFile` 打开它。
- 返回值特殊：正常情况下阻塞等待成功返回非零；但如果客户端在调用前就已经连上了，会返回 0 且 `GetLastError() == ERROR_PIPE_CONNECTED`，此时其实也算连接成功。
- 所以代码里这么写：

```cpp
BOOL connected = ConnectNamedPipe(hPipe, NULL)
                     ? TRUE
                     : (GetLastError() == ERROR_PIPE_CONNECTED);
```

**`DisconnectNamedPipe` —— 断开当前客户端**

```cpp
BOOL DisconnectNamedPipe(HANDLE hNamedPipe);
```

一个管道实例同一时刻只服务一个客户端。客户端断开后用它可以复位实例，接着 `ConnectNamedPipe` 迎接下一个。

### 2.3 客户端 API

**`CreateFile` —— 打开命名管道**

命名管道也用 `CreateFile` 打开（和其它内核对象统一）：

```cpp
HANDLE hPipe = CreateFile(
    PIPE_NAME,                 // 与 CreateNamedPipe 相同的名字
    GENERIC_READ | GENERIC_WRITE,
    0,                         // 共享模式，管道必须为 0
    NULL,                      // 安全属性
    OPEN_EXISTING,             // 管道必须已存在
    0,                         // 属性
    NULL                       // 模板
);
```

- 打开失败返回 `INVALID_HANDLE_VALUE`，用 `GetLastError()` 看原因（服务端没启动时是 `ERROR_FILE_NOT_FOUND`）。

### 2.4 收发 API

| API | 方向 | 说明 |
| --- | --- | --- |
| `WriteFile(h, buf, len, &written, NULL)` | 写 | 把数据写进管道 |
| `ReadFile(h, buf, len, &read, NULL)` | 读 | 从管道读数据；返回 FALSE 且 `read==0` 表示对端已关闭 |
| `FlushFileBuffers(h)` | —— | 消息模式下等待对端读完 |

这两组函数对管道和文件是统一的，这也是 Windows 命名管道设计巧妙之处。

### 2.5 其它相关 API

| API | 作用 |
| --- | --- |
| `GetLastError()` | 取最近一次失败的错误码 |
| `CloseHandle(h)` | 关闭句柄，释放资源 |
| `WaitNamedPipe(name, timeout)` | 客户端等待管道可用（可选，替代轮询） |
| `PeekNamedPipe(...)` | 不取出数据地查看有没有数据可读 |

## 3. C++ 如何实现

整体流程：**service 先创建并阻塞等待 → client 打开管道连上 → 双方 `ReadFile`/`WriteFile` 往返 → 断开后 service 循环迎接下一个 client。**

```
service 启动 ──CreateNamedPipe──▶ ConnectNamedPipe(阻塞等待)
                                        │
client 启动 ──CreateFile───────────────▶ 连接建立
                                        │
        _getch 捕获按键 ──WriteFile────▶ ReadFile 收到
                                        │
                                        维护 inputBuffer
                                        │
       ReadFile 收到 ◀──WriteFile─────── 返回候选/缓冲
                                        │
client 按 ESC ──CloseHandle────────────▶ ReadFile 返回 0
                                        │
                                DisconnectNamedPipe + CloseHandle
                                        │
                                   回到 ConnectNamedPipe
```

### 3.1 服务端：创建管道 + 维护状态

```cpp
HANDLE hPipe = CreateNamedPipe(PIPE_NAME,
                               PIPE_ACCESS_DUPLEX,
                               PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                               PIPE_UNLIMITED_INSTANCES,
                               BUF_SIZE, BUF_SIZE, 0, NULL);

ConnectNamedPipe(hPipe, NULL);

std::string inputBuffer;                 // 模拟输入法引擎状态
while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
    std::string key(buffer, bytesRead);

    if (key == "BACKSPACE") {
        if (!inputBuffer.empty()) inputBuffer.pop_back();
    } else if (key == "ENTER") {
        inputBuffer = "(已提交)";
    } else if (key.size() == 1) {
        inputBuffer += key;
    }

    std::string output = "缓冲内容: [" + inputBuffer + "]";
    WriteFile(hPipe, output.c_str(), (DWORD)output.length(), &bytesWritten, NULL);
}
```

- **状态（`inputBuffer`）在服务端**：这正是"重服务端"的核心，所有输入法逻辑集中于此。
- 读循环结束条件：`ReadFile` 返回 FALSE 或读到 0 字节，说明客户端断开了。
- 断开后 `DisconnectNamedPipe` + `CloseHandle`，`while(true)` 回到开头，为下一个客户端重建实例。

### 3.2 客户端：捕获按键 + 往返通信

```cpp
int ch = _getch();
if (ch == 27) break;                     // ESC 退出

std::string key;
if (ch == 8)        key = "BACKSPACE";
else if (ch == 13)  key = "ENTER";
else if (ch >= 32 && ch < 127) key = std::string(1, (char)ch);
else continue;                           // 忽略其它特殊键

WriteFile(hPipe, key.c_str(), (DWORD)key.length(), &bytesWritten, NULL);

if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
    buffer[bytesRead] = '\0';
    std::cout << "[Client] 服务端返回: " << buffer << std::endl;
}
```

- `_getch()` 来自 `<conio.h>`，无回显地读一个按键，不用按回车。
- 客户端把按键**编码成字符串协议**（`"BACKSPACE"`、`"ENTER"`、单字符），服务端按协议解析。
- **客户端不保存任何输入法状态**，它只是"眼睛和手"，逻辑都在服务端。

### 3.3 为什么这样设计

| 设计点 | 目的 |
| --- | --- |
| `PIPE_TYPE_MESSAGE` | 每次 WriteFile 对应一次 ReadFile，天然对齐一条"按键消息"，不会粘包 |
| 服务端持有 `inputBuffer` | 状态集中，多个 client 可共享同一套引擎逻辑 |
| `PIPE_UNLIMITED_INSTANCES` + 外层 `while` | 一个 client 走了，服务端立刻迎接下一个 |
| 客户端只发按键 | 客户端极轻，易于注入、易于适配不同应用 |
| 先建管道再连 | 客户端必须先创建成功才能连，保证连接有序 |

### 3.4 局限与进阶

`doc/扩展.md` 里列了把它做成真正 Weasel 的方向，这里对应说明：

1. **二进制协议**：定义 `struct KeyEvent { UINT keycode; UINT mask; };`，
   客户端发结构体、服务端解析，替代字符串协议，更高效。
2. **模拟输入法逻辑**：服务端维护映射表，如 `nihao → ["你好","你号","泥壕"]`。
3. **候选列表**：服务端返回候选数组，客户端显示并支持数字选词。
4. **DLL 注入**：把客户端做成 DLL，用 `SetWindowsHookEx` 挂全局键盘钩子注入到记事本，捕获真实按键。
5. **多客户端**：服务端用**多线程**，每个客户端一个管道实例（`CreateThread` + 每线程一份 `inputBuffer`）。
6. **超时与重连**：`CreateFile` 失败时用 `WaitNamedPipe` 等待，或用重叠 I/O 做超时检测。

其他工程化改进：

- **安全**：`CreateNamedPipe` 的安全属性可设置只允许特定用户/完整性级别连接，避免被恶意进程冒充。
- **同步**：多线程服务端对共享状态要加锁（`CRITICAL_SECTION`）。
- **协议版本**：在消息头带版本号，服务端据此兼容新旧客户端。

## 4. 编译与运行

本示例是 Windows 专用（依赖 `<windows.h>`、`<conio.h>`），使用 MSVC：

```bat
cl /nologo /EHsc /std:c++17 service\service.cpp /Fe:service\service.exe
cl /nologo /EHsc /std:c++17 client\client.cpp  /Fe:client\client.exe
```

先启动服务端（保持运行），再启动客户端：

```bat
service\service.exe
:: 另开一个终端
client\client.exe
```

预期效果：

```
[Server] 启动，监听管道...
[Server] 等待客户端连接...
[Server] 客户端已连接
[Server] 收到按键: n
[Server] 收到按键: i
[Server] 收到按键: BACKSPACE
[Server] 收到按键: ENTER
[Server] 客户端断开
[Server] 等待客户端连接...
```

```
[Client] 连接服务端...
[Client] 已连接。输入字符，ESC 退出。
[Client] 按键: n
[Client] 服务端返回: 缓冲内容: [n]
[Client] 按键: i
[Client] 服务端返回: 缓冲内容: [ni]
```
