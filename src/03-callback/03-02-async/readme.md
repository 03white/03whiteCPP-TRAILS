# 经典异步回调场景：图片下载完成后自动处理

## 场景

你正在实现一个图片处理工具。

用户输入一张图片的 URL 后，程序不能一直卡住等待下载完成，而是应该立即返回，让主线程继续做其他事情。

当图片下载完成后，程序需要通过回调函数通知调用者，并继续执行后续处理逻辑，例如：

- 打印下载结果
- 校验图片是否下载成功
- 如果成功，继续执行图片压缩
- 如果失败，打印错误信息

这就是一个非常经典的异步回调场景：

> 发起一个耗时任务时不阻塞当前流程，任务完成后再通过回调通知结果。

## 你的任务

请你使用 C++ 实现一个异步图片下载模拟器。

不需要真的访问网络，只需要用 `sleep_for` 模拟下载耗时即可。

## 实现要求

1. 定义一个异步函数 `downloadImageAsync`。
2. `downloadImageAsync` 接收两个参数：
   - 图片地址 `url`
   - 下载完成后的回调函数 `callback`
3. `downloadImageAsync` 内部启动一个新线程模拟下载。
4. 新线程中等待 2 秒，表示图片正在下载。
5. 下载完成后，通过回调函数返回下载结果。
6. 主线程调用 `downloadImageAsync` 后，不能被下载过程阻塞。
7. 主线程需要先打印 `main thread is still running`。
8. 回调函数中根据下载结果打印：
   - 下载成功：`download success: xxx`
   - 下载失败：`download failed: xxx`

## 建议的数据结构

你可以定义一个结构体表示下载结果：

```cpp
struct DownloadResult {
    bool success;
    std::string url;
    std::string message;
};
```

## 示例调用方式

```cpp
downloadImageAsync("https://example.com/avatar.png", [](const DownloadResult& result) {
    if (result.success) {
        std::cout << "download success: " << result.url << std::endl;
    } else {
        std::cout << "download failed: " << result.message << std::endl;
    }
});

std::cout << "main thread is still running" << std::endl;
```

## 期望输出示例

输出顺序应体现异步效果，例如：

```text
start download: https://example.com/avatar.png
main thread is still running
download success: https://example.com/avatar.png
```

## 思考问题

1. 为什么 `main thread is still running` 会先于下载结果输出？
2. 如果主线程提前结束，回调函数还有机会执行吗？
3. 如果有 3 张图片同时下载，应该如何复用这个异步函数？
4. 如果下载完成后还要压缩图片，你会把压缩逻辑写在哪里？

## `std::thread` 速记

`std::thread` 可以把一个函数放到新的线程里执行。

最基本写法：

```cpp
#include <thread>

void task() {
    // 这里的代码会在新线程里执行
}

int main() {
    std::thread t(task);
    t.join(); // 等待线程执行结束
    return 0;
}
```

如果不想让主线程等待它，可以使用 `detach()`：

```cpp
std::thread t(task);
t.detach(); // 线程在后台自己运行，主线程继续往下走
```

在异步回调场景里，常见写法是直接创建线程，并传入 lambda：

```cpp
std::thread([=]() {
    // 这里是子线程执行的逻辑
}).detach();
```

其中：

- `[=]` 表示把外部变量按值拷贝进线程，避免外部变量提前失效。
- `detach()` 表示不阻塞主线程，让线程自己在后台运行。
- 如果使用 `detach()`，要确保主线程不要太早结束，否则后台线程可能还没执行完。

## 实现提示

你可以先按下面的骨架完成：

```cpp
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

struct DownloadResult {
    bool success;
    std::string url;
    std::string message;
};

void downloadImageAsync(
    const std::string& url,
    std::function<void(const DownloadResult&)> callback
) {
    std::cout << "start download: " << url << std::endl;

    std::thread([url, callback]() {
        // 1. 用 sleep_for 模拟下载耗时 2 秒

        // 2. 构造 DownloadResult result

        // 3. 调用 callback(result)
    }).detach();
}

int main() {
    downloadImageAsync("https://example.com/avatar.png", [](const DownloadResult& result) {
        // 4. 在这里判断 result.success
        // 5. 成功就打印 download success
        // 6. 失败就打印 download failed
    });

    std::cout << "main thread is still running" << std::endl;

    // 7. 这里先让主线程等一下，避免程序太快结束
    std::this_thread::sleep_for(std::chrono::seconds(3));

    return 0;
}
```

## 关键点

这道题最重要的不是线程本身，而是这一行：

```cpp
callback(result);
```

它的意思是：

> 下载线程完成任务后，主动调用别人传进来的函数，把结果通知出去。

这就是异步回调的核心。

## 03-03-async：`std::async` 和 `std::future` 版本

上一节使用 `std::thread` 和回调函数实现异步通知。本节可以换成 `std::async` 和 `std::future` 来实现：异步任务负责返回结果，主线程在需要结果时再调用 `future.get()` 获取。

这种写法更适合“异步任务有返回值”的场景，因为结果会被保存在 `std::future` 中，不需要手动设计回调函数来传回结果。

### 基本用法

下面这个函数返回 `std::future`，表示“将来会得到一个下载结果”。

使用前需要包含 `future`、`chrono`、`thread`、`string`、`iostream` 这些头文件。

    struct DownloadResult {
        bool success;
        std::string url;
        std::string message;
    };

    auto downloadImageAsync(std::string url) {
        return std::async(std::launch::async, [url]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            DownloadResult result;
            result.success = true;
            result.url = url;
            result.message = url;
            return result;
        });
    }

### 示例调用方式

    auto task = downloadImageAsync(url);

    // 主线程可以先继续做其他事情

    DownloadResult result = task.get();

    // get 会等待异步下载结束，然后拿到返回结果

### 关键点

- `std::async(std::launch::async, task)`：强制把任务放到异步线程中执行。
- `std::future`：表示一个未来才会得到的结果。
- `future.get()`：等待异步任务完成，并取出返回值。
- 同一个 `future` 只能调用一次 `get()`。
- 如果异步任务里抛出异常，异常会在调用 `get()` 时重新抛出。

### 和回调版本的区别

- 回调版本：任务完成后主动调用 `callback(result)` 通知调用方。
- `future` 版本：任务完成后把结果保存起来，调用方主动通过 `get()` 获取。
- 如果只是通知事件，回调更自然；如果需要拿到一个明确的返回值，`std::async` 和 `std::future` 更方便。
