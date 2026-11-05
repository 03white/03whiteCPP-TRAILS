# 条件变量（`std::condition_variable`）

配套示例：本目录的 `main.cpp`（3 个 worker 全部完成后叫醒 main）。

## 1. 它解决什么问题

一个线程要等另一批线程完成某个条件（比如“计数归零”），有两种写法。

**轮询（坏）**：

```cpp
while (pending != 0) {
    // 空转，CPU 一直烧
}
```

**条件变量（好）**：

```cpp
std::unique_lock<std::mutex> lk(m);
cv.wait(lk, [&] { return pending == 0; });
```

线程在 `cv` 上睡眠，不占 CPU；条件成立时由改状态的一方 `notify` 唤醒。

一句话：**条件变量负责“等”，互斥量负责“改”。**

## 2. 三件套

| 组件 | 作用 |
| --- | --- |
| `std::mutex` | 保护共享状态，保证“读-改-写”不被别的线程插队 |
| `std::condition_variable` | 让线程睡眠/被唤醒 |
| 谓词（lambda） | 判断条件是否真的成立，避免误唤醒后瞎跑 |

三者缺一不可：没有 mutex 会有数据竞争；没有谓词会被假唤醒骗；没有条件变量就只能轮询。

## 3. 核心 API

```cpp
std::mutex m;
std::condition_variable cv;
bool done = false;

// 等待方
{
    std::unique_lock<std::mutex> lk(m);        // wait 只接受 unique_lock
    cv.wait(lk, [] { return done; });          // 放锁 → 睡 → 被唤醒 → 重新拿锁 → 检查谓词
}                                              // 谓词为真才返回

// 通知方
{
    std::lock_guard<std::mutex> lk(m);
    done = true;                               // 先改状态
}                                              // 先放锁
cv.notify_all();                               // 锁外按门铃
```

- `notify_one()`：叫醒一个等待者（适合“每来一个任务，唤醒一个消费者”）
- `notify_all()`：叫醒所有等待者（适合“状态变成全局可见”，比如全部完成）
- `wait(lk, pred)` 内部等价于：

```cpp
while (!pred()) {
    cv.wait(lk);   // 放锁并睡，醒来后重新拿锁
}
```

## 4. 三个必须理解的点

### 4.1 为什么 `wait` 要传锁

`wait` 需要**原子地**“检查条件 + 进入睡眠”。否则会出现丢唤醒：

```
等待方：检查 done == false
通知方：done = true; notify();     ← 在等待方睡下之前
等待方：睡下                          ← 永远醒不过来
```

传锁后，通知方改 `done` 必须拿同一把锁，等待方的“检查 + 睡下”不会被打断。

### 4.2 为什么必须带谓词

`wait` 允许**假唤醒**（没有 notify 也可能醒）。谓词会在每次醒来后重新检查，条件不成立就继续睡。写成不带谓词的形式，等于自找麻烦。

### 4.3 为什么 `notify` 放在锁外

被唤醒的线程会立刻尝试重新拿锁。如果通知方还握着锁，等待者只能干等，没有收益；严重时还会形成锁顺序问题。惯例：

> **锁内改状态，锁外 notify。**

## 5. demo 逐段对应

```cpp
std::mutex g_m;
std::condition_variable g_doneCv;
int g_pending = 3;
```

三个 worker 共享“还剩几个没完成”。

```cpp
void worker(int id) {
    // 干活...

    bool last = false;
    {
        std::lock_guard<std::mutex> lk(g_m);   // 锁内：读-减-判断不可分割
        --g_pending;
        last = (g_pending == 0);
    }
    if (last) {
        g_doneCv.notify_all();                 // 锁外：按门铃
    }
}
```

如果两个 worker 同时 `--g_pending` 而不加锁，可能都读到 `1`、都写回 `0`，实际只减了一次，主线程可能永远等不到。

```cpp
{
    std::unique_lock<std::mutex> lk(g_m);
    g_doneCv.wait(lk, [] { return g_pending == 0; });
}
```

- 用 `unique_lock`：`wait` 需要在睡眠时释放锁，`lock_guard` 不支持
- 用谓词：醒来后确认真的已经归零
- 睡眠期间不占 CPU

## 6. 常见错误

| 错误 | 后果 |
| --- | --- |
| 改共享状态不加锁 | 丢更新、计数错、可能永久等待 |
| `wait` 不带谓词 | 假唤醒导致条件没成立就继续跑 |
| `wait` 用 `lock_guard` | 编译不过（需要 `unique_lock`） |
| 持锁调用 `notify` | 被唤醒者立刻又阻塞在锁上 |
| 用 `notify_one` 唤醒多个等待者 | 只有一个能继续，其余继续睡 |
| 先 `notify` 后改状态 | 等待者可能被叫醒又睡回去，或丢唤醒 |

## 7. 与 SDK 的对应

`20-sdk/20-01-picture` 的异步调度里：

| demo | SDK |
| --- | --- |
| `g_m` | `m_` |
| `g_doneCv` | `doneCv_` |
| `g_pending == 0` | `running_ == false`（由 `pending_` 归零驱动） |
| `worker()` | `runStep(id)` |
| `main` 的 `wait` | `Wait()` |

SDK 只是把“一个计数器”扩展成了“入度表 + 已处理集合 + 未完成计数”，锁和条件变量的用法完全一致：

```cpp
// runStep 成功路径
{
    std::lock_guard<std::mutex> lk(m_);
    handled_.insert(id);
    for (down : next_) if (--indeg_[down] == 0) ready.push_back(down);
    if (--pending_ == 0) { running_ = false; finished = true; }
}
for (down : ready) post(...);              // 锁外
if (finished) doneCv_.notify_all();        // 锁外按门铃
```

```cpp
void Wait() {
    std::unique_lock<std::mutex> lk(m_);
    doneCv_.wait(lk, [this] { return !running_; });
}
```

## 8. 建议实验

把 `worker` 里的 `lock_guard` 去掉，连续跑多次：

```powershell
1..20 | ForEach-Object { src\06-concurrency\06-02-mutex-condvar\build\bin\06-02-mutex-condvar.exe }
```

观察是否出现 `main: all done` 不打印、进程挂住。亲眼看到一次数据竞争，比读十遍文档有效。
