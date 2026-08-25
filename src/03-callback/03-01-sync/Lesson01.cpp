#include<iostream>
#include<string>
#include<functional>
struct UserInfo{
    std::string username_;
    std::string email_;
};
using SuccessCallback=std::function<void(const UserInfo&)>;
using FailureCallback =std::function<void(const std::string&)>;

class Authenticator {
public:
    void login(const std::string& name,
               const std::string& password,
               SuccessCallback onSuccess,
               FailureCallback onFailue) const {
        if (name == name_ && password == password_) {
            UserInfo user{name_, name_ + "@example.com"};
            onSuccess(user);
        } else {
            onFailue("Invalid username or password");
        }
    }

private:
    std::string name_{"admin"};
    std::string password_{"123456"};
};
void onLoginSuccess(const UserInfo& user) {
    std::cout << "Login successful! Welcome, " << user.username_ << std::endl;
}
void onLoginFailure(const std::string& error) {
    std::cout << "Login failed: " << error << std::endl;
}
class Log {
public:
    void logSuccess(const UserInfo& user) {
        std::cout << "Log: User " << user.username_
                  << " logged in successfully." << std::endl;
    }
    void logFailure(const std::string& error) {
        std::cout << "Log: Login failed with error: " << error << std::endl;
    }
};
int main() {
    Authenticator auth;
    std::string username, password;
    std::cout << "Enter username: ";
    std::cin >> username;
    std::cout << "Enter password: ";
    std::cin >> password;
    // Using lambda functions for callbacks
    auth.login(username, password, [](const UserInfo& user) {
        std::cout << "Login successful! Welcome, " << user.username_
                  << std::endl;}, [](const std::string& error) {
            std::cout << "Login failed: " << error << std::endl;
        });
    // Using member functions of Log class for callbacks
    Log logger;
    auth.login(username, password, 
               [&logger](const UserInfo& user) {
                   logger.logSuccess(user);
               },
               [&logger](const std::string& error) {
                   logger.logFailure(error);
               });
    // Using std::bind for callbacks
    auth.login(username,
               password,
               std::bind(&Log::logSuccess, &logger, std::placeholders::_1),
               std::bind(&Log::logFailure, &logger, std::placeholders::_1));
    // Using std::function for callbacks
    auth.login(username, password, onLoginSuccess, onLoginFailure);
    return 0;
}
