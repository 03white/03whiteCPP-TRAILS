#include <coroutine>
#include <iostream>
#include <thread>
#include <chrono>
#include <optional>
#include <exception>
#include <string>
#include <vector>
#include <future>
#include <memory>

// ==================== 示例1：生成器（Generator）- 用于生成序列 ====================
template<typename T>
class Generator {
public:
    struct promise_type {
        T current_value;
        
        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void unhandled_exception() {
            std::terminate();
        }
        
        std::suspend_always yield_value(T value) noexcept {
            current_value = value;
            return {};
        }
        
        void return_void() {}
    };
    
    Generator(std::coroutine_handle<promise_type> h) : coro(h) {}
    ~Generator() { if (coro) coro.destroy(); }
    
    Generator(const Generator&) = delete;
    Generator(Generator&& other) noexcept : coro(other.coro) {
        other.coro = nullptr;
    }
    
    class Iterator {
    public:
        Iterator(std::coroutine_handle<promise_type> h = nullptr) : coro(h) {}
        
        Iterator& operator++() {
            coro.resume();
            return *this;
        }
        
        T operator*() const {
            return coro.promise().current_value;
        }
        
        bool operator==(const Iterator& other) const {
            return coro.done() == other.coro.done();
        }
        
    private:
        std::coroutine_handle<promise_type> coro;
    };
    
    Iterator begin() {
        coro.resume();
        return Iterator(coro);
    }
    
    Iterator end() {
        return Iterator(nullptr);
    }
    
private:
    std::coroutine_handle<promise_type> coro;
};

// 斐波那契数列生成器
Generator<int> fibonacci(int max) {
    std::cout << "  生成器启动：生成斐波那契数列（最大 " << max << "）\n";
    
    int a = 0, b = 1;
    while (a <= max) {
        std::cout << "  生成器：产生 " << a << "\n";
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
    
    std::cout << "  生成器完成\n";
}

// ==================== 自定义Awaiter ====================
struct SleepAwaiter {
    std::chrono::milliseconds duration;
    
    bool await_ready() const { return false; }
    
    void await_suspend(std::coroutine_handle<> h) const {
        std::cout << "      休眠开始 (" << duration.count() << "ms)\n";
        std::thread([h, this] {
            std::this_thread::sleep_for(duration);
            h.resume();
        }).detach();
    }
    
    void await_resume() const {
        std::cout << "      休眠结束\n";
    }
};

SleepAwaiter sleep_for(std::chrono::milliseconds ms) {
    return SleepAwaiter{ms};
}

// ==================== 示例2：异步任务（Task）- 需要实现awaiter接口 ====================
template<typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    
    struct promise_type {
        std::optional<T> result;
        std::exception_ptr exception;
        
        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        template<typename U>
        void return_value(U&& value) {
            result = std::forward<U>(value);
        }
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };
    
    // 构造函数
    Task(handle_type h) : coro(h) {}
    ~Task() { if (coro) coro.destroy(); }
    
    // 禁止拷贝，允许移动
    Task(const Task&) = delete;
    Task(Task&& other) noexcept : coro(other.coro) {
        other.coro = nullptr;
    }
    
    // ========== 重要：实现awaiter接口，使Task可以被co_await ==========
    bool await_ready() const { 
        return false;  // 总是挂起，让协程调度
    }
    
    void await_suspend(std::coroutine_handle<> awaiting_coro) {
        // 当当前Task完成后，恢复等待的协程
        coro.resume();
        awaiting_coro.resume();  // 这行可能需要调整
    }
    
    T await_resume() {
        if (coro.promise().exception) {
            std::rethrow_exception(coro.promise().exception);
        }
        if constexpr (!std::is_void_v<T>) {
            return *coro.promise().result;
        }
    }
    
    // 等待任务完成并获取结果
    T get() {
        while (!coro.done()) {
            coro.resume();
        }
        
        if (coro.promise().exception) {
            std::rethrow_exception(coro.promise().exception);
        }
        
        if constexpr (!std::is_void_v<T>) {
            return *coro.promise().result;
        }
    }
    
private:
    handle_type coro;
};

// 异步任务 - 模拟耗时操作
Task<std::string> fetch_data(const std::string& url, int delay_ms) {
    std::cout << "  开始获取数据: " << url << "\n";
    
    co_await sleep_for(std::chrono::milliseconds(delay_ms));
    
    if (url.empty()) {
        throw std::runtime_error("URL不能为空！");
    }
    
    std::string result = "来自 " + url + " 的数据";
    std::cout << "  数据获取完成: " << result << "\n";
    
    co_return result;
}

// 组合多个异步任务
Task<std::vector<std::string>> fetch_multiple_data() {
    std::vector<std::string> results;
    
    // 顺序执行（等待每个任务完成）
    auto data1 = co_await fetch_data("api1.example.com", 1000);
    results.push_back(data1);
    
    auto data2 = co_await fetch_data("api2.example.com", 500);
    results.push_back(data2);
    
    auto data3 = co_await fetch_data("api3.example.com", 800);
    results.push_back(data3);
    
    co_return results;
}

// ==================== 主函数 ====================
int main() {
    std::cout << "========== C++20 协程示例 ==========\n";
    std::cout << "编译器: " << __cplusplus << "\n\n";
    
    // 示例1：使用生成器遍历斐波那契数列
    std::cout << "示例1：生成器（Generator）\n";
    std::cout << "------------------------\n";
    
    auto fib = fibonacci(20);
    std::cout << "斐波那契数列（<=20）:\n";
    for (int value : fib) {
        std::cout << "  获取到: " << value << "\n";
    }
    
    std::cout << "\n";
    
    // 示例2：异步任务
    std::cout << "示例2：异步任务（Task）\n";
    std::cout << "------------------------\n";
    
    // 单个异步任务
    std::cout << "单个异步任务：\n";
    auto task = fetch_data("example.com", 1000);
    auto result = task.get();
    std::cout << "最终结果: " << result << "\n\n";
    
    // 组合多个异步任务
    std::cout << "组合多个异步任务：\n";
    auto multi_task = fetch_multiple_data();
    auto results = multi_task.get();
    
    std::cout << "所有结果:\n";
    for (const auto& item : results) {
        std::cout << "  - " << item << "\n";
    }
    std::cout << "\n";
    
    // 示例3：错误处理
    std::cout << "示例3：错误处理\n";
    std::cout << "------------------------\n";
    
    try {
        auto error_task = fetch_data("", 500);
        error_task.get();
    } catch (const std::exception& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }
    
    std::cout << "\n所有示例执行完毕！\n";
    return 0;
}