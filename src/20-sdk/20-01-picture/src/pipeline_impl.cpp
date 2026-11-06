#include "pipeline_impl.h"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace imgpipe {

// 所有实现细节都在这里：公共头与私有头都看不到锁、容器、Boost。
struct PipelineImpl::Impl {
    struct StepNode {
        StepId id{0};
        std::string name;
        Step fn;
        std::vector<StepId> next;      // 出边
        std::size_t indegree{0};       // 入度
    };

    struct Progress {
        std::vector<StepId> ready;     // 需要继续调度的下游
        std::vector<StepId> skipped;   // 需要发 Skipped 的下游
        bool finished = false;         // pending 是否归零
    };

    Impl(const Config& c, bool test) : cfg(c), testMode(test) {}

    // ---- 构图 ----
    StepId AddStep(std::string name, Step fn) {
        if (!building) throw std::logic_error("AddStep: graph is frozen after Run");

        const StepId id = nextId++;
        nodes.emplace(id, StepNode{id, std::move(name), std::move(fn), {}, 0});
        return id;
    }

    void AddDependency(StepId downstream, StepId upstream) {
        if (!building)
            throw std::logic_error("AddDependency: graph is frozen after Run");

        const auto up = nodes.find(upstream);
        const auto down = nodes.find(downstream);
        if (up == nodes.end() || down == nodes.end())
            throw std::invalid_argument("AddDependency: unknown StepId");
        if (downstream == upstream)
            throw std::invalid_argument("AddDependency: self dependency");

        auto& next = up->second.next;
        if (std::find(next.begin(), next.end(), downstream) != next.end())
            return;   // 重复边：忽略

        next.push_back(downstream);      // 边记在上游的出边表
        ++down->second.indegree;         // 入度记在下游
    }

    void SetListener(Listener value) {
        std::shared_ptr<const Listener> next;
        if (value) {
            next = std::make_shared<const Listener>(std::move(value));
        }

        std::lock_guard<std::mutex> lk(m);
        listener = std::move(next);
    }

    // ---- 通知 ----
    bool notify(StepId id, StepState state, const Error& err) {
        // 先取一份强引用：即使此刻 SetListener(nullptr)，正在执行的回调也安全。
        std::shared_ptr<const Listener> snapshot;
        {
            std::lock_guard<std::mutex> lk(m);
            snapshot = listener;
        }
        if (!snapshot) return true;

        try {
            (*snapshot)(id, state, err);
            return true;
        } catch (...) {
            return false;   // 回调异常由 executeNode 转成 Step Failed
        }
    }

    // ---- 执行一个节点 ----
    bool executeNode(StepId id, Error& err) {
        auto fail = [&](const Error& e) {
            err = e;
            notify(id, StepState::Failed, err);
            return false;
        };

        if (!notify(id, StepState::Pending, Error{}))
            return fail(Error{3, "listener exception"});
        if (!notify(id, StepState::Running, Error{}))
            return fail(Error{3, "listener exception"});

        err = Error{};
        try {
            nodes.at(id).fn();
        } catch (const std::exception& e) {
            return fail(Error{1, e.what()});
        } catch (...) {
            return fail(Error{1, "unknown exception"});
        }

        if (!notify(id, StepState::Succeeded, Error{}))
            return fail(Error{3, "listener exception"});
        return true;
    }

    // ---- 调度状态（调用方持锁） ----
    std::uint64_t resetSchedulerLocked() {
        indeg.clear();
        for (const auto& kv : nodes) {
            indeg[kv.first] = kv.second.indegree;
        }
        handled.clear();
        runningNodes.clear();
        pending = nodes.size();
        cancelled = false;
        return ++runGeneration;
    }

    bool beginNode(StepId id, std::uint64_t generation) {
        std::lock_guard<std::mutex> lk(m);
        if (generation != runGeneration) return false;   // 上一轮残留任务
        if (handled.count(id)) return false;             // 已被 Cancel 标记
        runningNodes.insert(id);
        return true;
    }

    Progress completeSuccess(StepId id) {
        Progress progress;
        std::lock_guard<std::mutex> lk(m);
        runningNodes.erase(id);
        handled.insert(id);

        if (!cancelled) {
            for (StepId down : nodes.at(id).next) {
                if (handled.count(down)) continue;
                if (--indeg[down] == 0) progress.ready.push_back(down);
            }
        }

        if (--pending == 0) {
            running = false;
            progress.finished = true;
        }
        return progress;
    }

    Progress completeFailure(StepId id) {
        Progress progress;
        std::lock_guard<std::mutex> lk(m);
        runningNodes.erase(id);

        if (handled.insert(id).second) --pending;

        if (!cancelled) {
            std::queue<StepId> q;
            for (StepId v : nodes.at(id).next) q.push(v);
            while (!q.empty()) {
                const StepId v = q.front();
                q.pop();
                if (!handled.insert(v).second) continue;
                progress.skipped.push_back(v);
                --pending;
                for (StepId w : nodes.at(v).next) q.push(w);
            }
        }

        if (pending == 0) {
            running = false;
            progress.finished = true;
        }
        return progress;
    }

    void emitSkipped(const std::vector<StepId>& skipped, const Error& err) {
        for (StepId v : skipped) {
            notify(v, StepState::Pending, Error{});
            notify(v, StepState::Skipped, err);
        }
    }

    // ---- 同步执行 ----
    bool Run() {
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lk(m);
            if (running) return false;
            running = true;
            generation = resetSchedulerLocked();
        }
        building = false;   // 冻结图

        std::queue<StepId> ready;
        {
            std::lock_guard<std::mutex> lk(m);
            for (const auto& kv : indeg) {
                if (kv.second == 0) ready.push(kv.first);
            }
        }

        while (!ready.empty()) {
            const StepId id = ready.front();
            ready.pop();

            if (!beginNode(id, generation)) continue;

            Error err;
            const bool ok = executeNode(id, err);
            Progress progress = ok ? completeSuccess(id) : completeFailure(id);

            if (!ok) emitSkipped(progress.skipped, err);
            for (StepId down : progress.ready) ready.push(down);
            if (progress.finished) doneCv.notify_all();
        }

        // 环检测：正常 DAG 里每个节点要么执行、要么被跳过。
        std::vector<StepId> cycle;
        bool superseded = false;
        {
            std::lock_guard<std::mutex> lk(m);
            superseded = (generation != runGeneration);
            if (!superseded) {
                for (const auto& kv : nodes) {
                    if (!handled.count(kv.first)) cycle.push_back(kv.first);
                }
                running = false;
            }
        }

        if (!superseded) {
            Error cycleErr{2, "cycle detected"};
            for (StepId id : cycle) {
                notify(id, StepState::Pending, Error{});
                notify(id, StepState::Failed, cycleErr);
            }
            doneCv.notify_all();
        }
        return true;
    }

    // ---- 异步执行 ----
    bool RunAsync() {
        // 测试模式：不启动线程，退化为同步执行
        if (testMode) return Run();

        {
            std::lock_guard<std::mutex> lk(m);
            if (running) return false;
        }

        building = false;   // 冻结图

        // 1) 静态环检测
        std::map<StepId, std::size_t> indegCopy;
        std::queue<StepId> seed;
        for (const auto& kv : nodes) {
            indegCopy[kv.first] = kv.second.indegree;
            if (kv.second.indegree == 0) seed.push(kv.first);
        }

        auto work = indegCopy;
        std::set<StepId> reachable;
        while (!seed.empty()) {
            const StepId id = seed.front();
            seed.pop();
            reachable.insert(id);
            for (StepId down : nodes.at(id).next) {
                if (--work[down] == 0) seed.push(down);
            }
        }

        if (reachable.size() != nodes.size()) {
            std::vector<StepId> cycle;
            for (const auto& kv : nodes) {
                if (!reachable.count(kv.first)) cycle.push_back(kv.first);
            }

            Error cycleErr{2, "cycle detected"};
            for (StepId id : cycle) {
                notify(id, StepState::Pending, Error{});
                notify(id, StepState::Failed, cycleErr);
            }
            return false;
        }

        // 2) 初始化状态 + 懒创建线程池
        std::uint64_t generation = 0;
        std::vector<StepId> roots;
        bool emptyGraph = false;
        {
            std::lock_guard<std::mutex> lk(m);
            if (running) return false;
            running = true;
            generation = resetSchedulerLocked();
            emptyGraph = nodes.empty();

            if (!emptyGraph && !pool) {
                const auto threads = cfg.maxConcurrency == 0 ? 1u : cfg.maxConcurrency;
                pool = std::make_unique<boost::asio::thread_pool>(
                    static_cast<std::size_t>(threads));
            }

            for (const auto& kv : indeg) {
                if (kv.second == 0) roots.push_back(kv.first);
            }

            if (emptyGraph) running = false;
        }

        if (emptyGraph) {
            doneCv.notify_all();
            return true;
        }

        // 3) 点火
        for (StepId id : roots)
            boost::asio::post(*pool, [this, id, generation] { runStep(id, generation); });
        return true;
    }

    void runStep(StepId id, std::uint64_t generation) {
        if (!beginNode(id, generation)) return;

        Error err;
        const bool ok = executeNode(id, err);
        Progress progress = ok ? completeSuccess(id) : completeFailure(id);

        if (!ok) emitSkipped(progress.skipped, err);

        for (StepId down : progress.ready)
            boost::asio::post(*pool, [this, down, generation] { runStep(down, generation); });

        if (progress.finished) doneCv.notify_all();
    }

    void Cancel() {
        std::vector<StepId> cancelledList;
        std::vector<StepId> skippedList;
        bool finished = false;

        {
            std::lock_guard<std::mutex> lk(m);
            if (!running || cancelled) return;
            cancelled = true;

            for (const auto& kv : nodes) {
                const StepId id = kv.first;
                if (handled.count(id) || runningNodes.count(id)) continue;

                // 入度已归零说明上游全部成功，本来马上能跑 → Cancelled。
                // 否则被运行中/失败/取消的上游挡住，永远不会再跑 → Skipped。
                const bool ready = (indeg[id] == 0);
                handled.insert(id);
                --pending;
                (ready ? cancelledList : skippedList).push_back(id);
            }

            if (pending == 0) {
                running = false;
                finished = true;
            }
        }

        const Error cancelErr{4, "cancelled"};
        for (StepId id : cancelledList) {
            notify(id, StepState::Pending, Error{});
            notify(id, StepState::Cancelled, cancelErr);
        }
        for (StepId id : skippedList) {
            notify(id, StepState::Pending, Error{});
            notify(id, StepState::Skipped, cancelErr);
        }

        if (finished) doneCv.notify_all();
    }

    void Wait() {
        std::unique_lock<std::mutex> lk(m);
        doneCv.wait(lk, [this] { return !running; });
    }

    Config cfg;
    bool testMode{false};
    std::shared_ptr<const Listener> listener;
    bool building{true};

    StepId nextId{0};
    std::map<StepId, StepNode> nodes;

    std::mutex m;
    std::condition_variable doneCv;
    bool running{false};
    bool cancelled{false};
    std::map<StepId, std::size_t> indeg;   // 入度信息表，随节点处理动态变化
    std::set<StepId> handled;              // 已到达终态或被跳过的节点
    std::set<StepId> runningNodes;
    std::size_t pending{0};
    std::uint64_t runGeneration{0};

    // 最后声明：析构时最先销毁，join 所有 worker
    std::unique_ptr<boost::asio::thread_pool> pool;
};

// ---------------------------------------------------------------
// PipelineImpl：只做转发，没有数据
// ---------------------------------------------------------------
PipelineImpl::PipelineImpl(const Config& cfg, bool testMode)
    : impl_(std::make_unique<Impl>(cfg, testMode)) {}

PipelineImpl::~PipelineImpl() {
    impl_->Cancel();
    impl_->Wait();
}

StepId PipelineImpl::AddStep(std::string name, Step fn) {
    return impl_->AddStep(std::move(name), std::move(fn));
}

void PipelineImpl::AddDependency(StepId downstream, StepId upstream) {
    impl_->AddDependency(downstream, upstream);
}

void PipelineImpl::SetListener(Listener listener) {
    impl_->SetListener(std::move(listener));
}

bool PipelineImpl::Run() {
    return impl_->Run();
}

bool PipelineImpl::RunAsync() {
    return impl_->RunAsync();
}

void PipelineImpl::Cancel() {
    impl_->Cancel();
}

void PipelineImpl::Wait() {
    impl_->Wait();
}

std::unique_ptr<Pipeline> Pipeline::Create(const Config& cfg) {
    return std::make_unique<PipelineImpl>(cfg);
}

std::unique_ptr<Pipeline> Pipeline::CreateForTest() {
    return std::make_unique<PipelineImpl>(Config{}, true);
}

} // namespace imgpipe
