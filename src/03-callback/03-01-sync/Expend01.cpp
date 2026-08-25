#include <iostream>
#include<vector>
#include <string>
#include <functional>
/*
思路1：
1.在Authenticator内部维护一个观察者列表（可以存储抽象接口指针，也可以存储std::function对象）。
2.提供registerObserver方法，让外部可以注册观察者。
3.在login方法中，根据登录结果调用所有注册的观察者的回调方法。
*/ 
struct UserInfo {
    std::string username_;
    std::string email_;
};
using SuccessCallback = std::function<void(const UserInfo&)>;
using FailureCallback = std::function<void(const std::string&)>;

class LoginObserver{
public:
    virtual void onSuccess(const UserInfo& user) = 0;
    virtual void onFailure(const std::string& error) = 0;
};
class LogObserver : public LoginObserver {
public:
    void onSuccess(const UserInfo& user) override {
        std::cout << "Log: User " << user.username_
                  << " logged in successfully." << std::endl;
    }
    void onFailure(const std::string& error) override {
        std::cout << "Log: Login failed with error: " << error << std::endl;
    }

};
class UIObserver : public LoginObserver {
public:
    void onSuccess(const UserInfo& user) override {
        std::cout << "UI: Welcome, " << user.username_ << "!" << std::endl;
    }
    void onFailure(const std::string& error) override {
        std::cout << "UI: Login failed: " << error << std::endl;
    }

};
class NotificationObserver : public LoginObserver {
public:
    void onSuccess(const UserInfo& user) override {
        std::cout << "Notification: User " << user.username_
                  << " logged in successfully." << std::endl;
    }
    void onFailure(const std::string& error) override {
        std::cout << "Notification: Login failed with error: " << error
                  << std::endl;
    }
};
class Authenticator {
public:
    void login(const std::string& name,
               const std::string& password) const {
        if (name == name_ && password == password_) {
            UserInfo user{name_, name_ + "@example.com"};
            notifySuccess(user);
        } else {
            notifyFailure("Invalid username or password");
        }
    }
    void registerObserver(LoginObserver* observer) {
        callbacks_.push_back(observer);
    }

private:
    void notifySuccess(const UserInfo& user) const {
        for (auto* callback : callbacks_) {
            callback->onSuccess(user);
        }
    }
    void notifyFailure(const std::string& error) const {
        for (auto* callback : callbacks_) {
            callback->onFailure(error);
        }
    }
    std::string name_{"admin"};
    std::string password_{"123456"};
    std::vector<LoginObserver*> callbacks_;
};
int main() {
    Authenticator auth;
    std::string username, password;
    std::cout << "Enter username: ";
    std::cin >> username;
    std::cout << "Enter password: ";
    std::cin >> password;
    LogObserver logger;
    UIObserver userInterface;
    NotificationObserver notifier;
    auth.registerObserver(&logger);
    auth.registerObserver(&userInterface);
    auth.registerObserver(&notifier);
    auth.login(username, password);
    return 0;
}
