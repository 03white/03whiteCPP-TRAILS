// 最小线程池示例：看清楚“worker 到底是什么”。
//
// 线程池 = 提前开好的 N 根线程（worker），它们循环从任务队列里取活干。
// post() 就是“往队列里塞一个函数”，被叫醒的 worker 执行它。
// 于是任务的执行线程是 worker，而不是 main 所在的调用线程。

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

// 多个 worker 同时 std::cout 会把一行拆得七零八落（operator<< 不是一次原子写），
// 所以用一把锁保证每次打印是完整的一行。这本身就是“共享数据要同步”的例子。
std::mutex g_coutMutex;

void printLine(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_coutMutex);
    std::cout << line << "\n";
}

class ThreadPool {
public:
    explicit ThreadPool(int n) {
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(m_);
                        // 没活就睡；被 post/release 叫醒后重新检查条件。
                        cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();   // 注意：这行跑在 worker 线程上
                }
            });
        }
    }

    void post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lk(m_);
            tasks_.push(std::move(f));
        }
        cv_.notify_one();
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();   // 等所有 worker 退出
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};

int main() {
    {
        std::ostringstream oss;
        oss << "main thread id = " << std::this_thread::get_id();
        printLine(oss.str());
    }

    ThreadPool pool(2);   // 2 个 worker

    for (int i = 1; i <= 3; ++i) {
        pool.post([i] {
            std::ostringstream oss;
            oss << "task " << i << " runs on " << std::this_thread::get_id();
            printLine(oss.str());
        });
    }

    // pool 析构时会 join，所以 3 个任务都会打印完才退出。
}
