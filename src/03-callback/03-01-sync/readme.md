# 同步回调经典练习：用户登录验证

## 背景

在同步调用场景中，一个函数执行完某个操作后，会立即调用调用者提供的回调函数来通知结果。这种回调方式称为同步回调。

同步回调的特点是：回调函数在原函数内部被直接调用，原函数会等待回调执行完毕后才返回。

## 任务

实现一个简单的用户认证系统，练习同步回调、`std::function` 和 `std::bind` 的使用。

## 题目要求

### 1. 定义回调类型

使用 `std::function` 定义两个回调类型：

```cpp
struct UserInfo {
    std::string username;
    std::string email;
};

using SuccessCallback = std::function<void(const UserInfo&)>;
using FailureCallback = std::function<void(const std::string& errorMsg)>;
```

### 2. 实现 `Authenticator` 类

```cpp
class Authenticator {
public:
    void login(const std::string& username,
               const std::string& password,
               SuccessCallback onSuccess,
               FailureCallback onFailure) const;
};
```

`login` 方法是同步的，它会立即检查用户名和密码是否合法。

要求如下：

- 预设合法用户名和密码为 `"admin" / "123456"`，也可以存储在成员变量中。
- 验证成功时，构造 `UserInfo` 对象，例如邮箱可设为 `"admin@example.com"`，然后立即调用 `onSuccess`。
- 验证失败时，立即调用 `onFailure`，并传入错误信息 `"Invalid username or password"`。
- 注意：回调函数在 `login` 内部被直接调用，回调结束后 `login` 才返回。

### 3. 编写客户端代码进行测试

至少演示以下三种回调形式：

- 普通函数：定义一个自由函数作为成功/失败回调。
- Lambda 表达式：直接在调用处传入 lambda。
- 成员函数：使用 `std::bind` 将某个类的成员函数绑定到对象上，作为回调。

同时需要分别测试登录成功和登录失败两种情况。

## 示例代码框架

```cpp
#include <iostream>
#include <functional>
#include <string>

// 定义 UserInfo 和回调类型
// ...

// 实现 Authenticator 类
// ...

// 测试用普通函数
void onLoginSuccess(const UserInfo& info) {
    std::cout << "Success: " << info.username << ", " << info.email << "\n";
}

void onLoginFailure(const std::string& error) {
    std::cout << "Failure: " << error << "\n";
}

// 测试用类（成员函数）
class Logger {
public:
    void logSuccess(const UserInfo& info) {
        std::cout << "[LOG] Login success: " << info.username << "\n";
    }

    void logFailure(const std::string& error) {
        std::cout << "[LOG] Login failure: " << error << "\n";
    }
};

int main() {
    Authenticator auth;

    // 1. 普通函数
    std::cout << "=== 普通函数测试 ===\n";
    auth.login("admin", "123456", onLoginSuccess, onLoginFailure);
    auth.login("user", "wrong", onLoginSuccess, onLoginFailure);

    // 2. Lambda 表达式
    std::cout << "\n=== Lambda 测试 ===\n";
    auth.login(
        "admin",
        "123456",
        [](const UserInfo& info) {
            std::cout << "Lambda Success: " << info.username << "\n";
        },
        [](const std::string& err) {
            std::cout << "Lambda Failure: " << err << "\n";
        });

    // 3. 成员函数 + std::bind
    std::cout << "\n=== 成员函数(std::bind)测试 ===\n";
    Logger logger;
    auto boundSuccess = std::bind(&Logger::logSuccess, &logger, std::placeholders::_1);
    auto boundFailure = std::bind(&Logger::logFailure, &logger, std::placeholders::_1);

    auth.login("admin", "123456", boundSuccess, boundFailure);
    auth.login("admin", "bad", boundSuccess, boundFailure);

    return 0;
}
```

## 扩展思考

### 异常安全

如果回调函数抛出异常，`login` 方法应该如何处理？

可以思考以下两种策略，并选择其中一种实现：

- 让异常继续传播给 `login` 的调用者。
- 在 `login` 内部捕获异常并进行处理。

### 支持多个成功回调

如何修改设计，使得一次登录成功可以通知多个观察者？

例如：登录成功后同时更新 UI、记录日志、发送通知。请讨论并尝试实现。