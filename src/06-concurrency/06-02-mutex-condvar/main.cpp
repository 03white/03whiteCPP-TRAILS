// 演示「锁」和「条件变量」各自解决什么问题。
//
// 场景：3 个 worker 各干一点活，全部完成后叫醒 main。
//
// - 锁 g_m：保护 g_pending，防止两个 worker 同时“读-改-写”导致少减一次。
// - 条件变量 g_doneCv：让 main 睡觉，等最后一个 worker 来按门铃。
//
// 可以试着把 worker 里的 lock_guard 去掉跑几次，观察 main 是否会偶尔卡住：
// 那就是两个线程同时 --g_pending 丢了更新，条件永远不成立。

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

std::mutex g_m;
std::condition_variable g_doneCv;
int g_pending = 3;   // 还有几个 worker 没完成

void worker(int id) {
    // 模拟干活
    volatile long long sum = 0;
    for (long long i = 0; i < 20'000'000; ++i) sum += i;
    (void)sum;

    bool last = false;
    {
        // 锁内：读、减、判断必须是一个不可分割的整体
        std::lock_guard<std::mutex> lk(g_m);
        --g_pending;
        last = (g_pending == 0);
    }

    // 锁外按门铃：被叫醒的线程会立刻来抢 g_m
    if (last) {
        std::cout << "worker " << id << " is the last one\n";
        g_doneCv.notify_all();
    }
}

int main() {
    std::vector<std::thread> workers;
    for (int i = 1; i <= 3; ++i) {
        workers.emplace_back(worker, i);
    }

    {
        std::unique_lock<std::mutex> lk(g_m);
        // wait 会先放锁再睡，被叫醒后重新拿锁，并重新检查条件
        g_doneCv.wait(lk, [] { return g_pending == 0; });
    }

    std::cout << "main: all done\n";

    for (auto& t : workers) t.join();
}
