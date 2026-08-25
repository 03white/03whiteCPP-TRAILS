#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ============ 1. 两个角色接口：名字直接说明"我能收什么" ============

///< 数据接收方 —— 采集 → 上传 这条流的下游
struct IDataReceiver {
    virtual ~IDataReceiver() = default;
    virtual void onDataArrived(const std::string& data) = 0;
};

///< 配置接收方 —— 上传 → 采集 这条流的下游
struct IConfigReceiver{
    virtual ~IConfigReceiver() = default;
    virtual void onConfigUpdated(int intervalMs) = 0;
};

// ============ 2. 廣播基类：把"维护订阅者名单"这件事抽出来，代碼復用 ============

template <typename ReceiverT>
class Publisher {
public:
    void addReceiver(std::shared_ptr<ReceiverT> receiver) {
        std::lock_guard<std::mutex> lock(mLock);
        mReceivers.push_back(receiver); // 存 weak_ptr：只订阅，不持有
    }
protected:
    ///< 把 method(args...) 广播给所有还活着的接收方
    template <typename Method, typename... Args>
    void notifyReceivers(Method method, const Args&... args) {
        std::vector<std::shared_ptr<ReceiverT>> alive;
        {
            std::lock_guard<std::mutex> lock(mLock);
            std::vector<std::weak_ptr<ReceiverT>> kept;
            for (auto& weak : mReceivers)
                if (auto locked = weak.lock()) {
                    alive.push_back(locked); // 提升成 shared_ptr，回调期间保命
                    kept.push_back(weak);
                }
            mReceivers.swap(kept); // 顺手剔除已析构的接收方
        }

        for (auto& receiver :
             alive) // 锁外回调：避免回调里再 addReceiver 造成死锁
            std::invoke(method, receiver, args...);
    }
private:
    std::vector<std::weak_ptr<ReceiverT>> mReceivers;
    std::mutex mLock;//这个锁是干什么？
};

// ============ 3. 两个模块：各自"发一种、收一种" ============

///< 采集模块：产出数据，消费配置
class CaptureModule : public Publisher<IDataReceiver>, // 继承广播通知的内存结构和功能
                      public IConfigReceiver,          // 自己也是观察者
                      public std::enable_shared_from_this<CaptureModule> {
public:
    void capture() {
        notifyReceivers(&IDataReceiver::onDataArrived, std::string("cpu=42%"));
    }

    void onConfigUpdated(int intervalMs) override {
        mIntervalMs = intervalMs;
        std::cout << "[采集] 采样间隔更新为 " << intervalMs << "ms\n";
    }

    //< 把自己以"配置接收方"这个角色交出去，返回自己的向上轉型
    std::shared_ptr<IConfigReceiver> asConfigReceiver() {
        return shared_from_this();
    }

private:
    int mIntervalMs{1000};
};

///< 上传模块：产出配置，消费数据
class UploadModule : public Publisher<IConfigReceiver>, // 我发配置
                     public IDataReceiver,              // 我收数据
                     public std::enable_shared_from_this<UploadModule> {
public:
    void onDataArrived(const std::string& data) override {
        std::cout << "[上传] 收到 " << data << " -> 入库并上报\n";
    }
    void pullConfigFromServer() {
        notifyReceivers(&IConfigReceiver::onConfigUpdated, 5000);
    }

    //类型提升
    std::shared_ptr<IDataReceiver> asDataReceiver() {
        return shared_from_this();
    }
};

// ============ 4. 由 App 完成交叉注册 ============

int main() {
    auto capture = std::make_shared<CaptureModule>();
    auto upload = std::make_shared<UploadModule>();

    capture->addReceiver(upload->asDataReceiver());   // 数据流：采集 → 上传
    upload->addReceiver(capture->asConfigReceiver()); // 配置流：上传 → 采集

    capture->capture();             // [上传] 收到 cpu=42% -> 入库并上报
    upload->pullConfigFromServer(); // [采集] 采样间隔更新为 5000ms
}
