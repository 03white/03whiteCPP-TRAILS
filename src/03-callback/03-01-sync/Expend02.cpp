#include <iostream>
#include <vector>
#include <string>
#include <functional>
/*
思路1需要我们定义一个抽象的观察者接口，并为每个观察者实现该接口。
这种方式虽然清晰，但在实际应用中可能会显得繁琐，尤其是当我们有多个不同类型的观察者时。
为了简化代码，我们可以使用std::function来存储回调函数，这样我们就不需要为每个观察者定义一个类，
只需要传入一个函数或lambda表达式即可。
思路2：
1.在Authenticator内部维护两个回调列表，一个用于成功回调，一个用于失败回
好处：
1. 简化代码结构：不需要为每个观察者定义一个类，只需传入函数或lambda表达式。
2. 成功和失败的回调分开注册，观察者可以根据自己的需求选择注册哪种回调。
3. 仍然一次性通知所有注册的回调，保持了观察者模式的核心思想。
*/

struct UserInfo {
    std::string username_;
    std::string email_;
};
class Log {
public:
    void handleSuccess(const UserInfo& user) {
        std::cout << hint_ << "User " << user.username_
                  << " logged in successfully." << std::endl;
    }
    void handleFailure(const std::string& error) {
    std::cout << hint_ << "Login failed with error : " << error << std::endl;
    }
private:
    const std::string hint_ = "Log: ";
};
class Authenticator {
using SuccessCallback = std::function<void(const UserInfo&)>;
using FailureCallback = std::function<void(const std::string&)>;
public:
    void login(const std::string& name, const std::string& password) const {
        if (name == name_ && password == password_) {
            UserInfo user{name_, name_ + "@example.com"};
            notifySuccess(user);
        } else {
            notifyFailure("Invalid username or password");
        }
    }
    void addSuccessCallback(SuccessCallback callback) {
        successCallbacks_.push_back(std::move(callback));
    }
    void addFailureCallback(FailureCallback callback) {
        failureCallbacks_.push_back(std::move(callback));
    }
private:
    void notifySuccess(const UserInfo& user) const {
        for (const auto& callback : successCallbacks_) {
            callback(user);
        }
    }
    void notifyFailure(const std::string& error) const {
        for (const auto& callback : failureCallbacks_) {
            callback(error);
        }
    }
    std::string name_{"admin"};
    std::string password_{"123456"};
    std::vector<SuccessCallback> successCallbacks_;
    std::vector<FailureCallback> failureCallbacks_;
};
int main() {
    Authenticator auth;
    std::string username, password;
    std::cout << "Enter username: ";
    std::cin >> username;
    std::cout << "Enter password: ";
    std::cin >> password;
    Log logger;
    auth.addSuccessCallback([](const UserInfo& user) {
        std::cout << "Login successful! Welcome, " << user.username_
                  << std::endl;
    });
    auth.addFailureCallback([](const std::string& error) {
        std::cout << "Login failed: " << error << std::endl;
    });
    auth.addSuccessCallback(
        [&logger](const UserInfo& user) { logger.handleSuccess(user); });
    auth.addFailureCallback(std::bind(&Log::handleFailure, &logger, std::placeholders::_1));
    auth.login(username, password);
    return 0;
}
