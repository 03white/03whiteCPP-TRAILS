#include "pipeline_impl.h"

#include <boost/asio/post.hpp>

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <utility>

namespace imgpipe {

std::unique_ptr<Pipeline> Pipeline::Create(const Config& cfg) {
    return std::make_unique<PipelineImpl>(cfg);
}

PipelineImpl::PipelineImpl(const Config& cfg) : cfg_(cfg) {}

StepId PipelineImpl::AddStep(std::string name, Step fn) {
    if (!building_) throw std::logic_error("AddStep: graph is frozen after Run");

    const StepId id = nextId_++;
    nodes_.emplace(id, StepNode{id, std::move(name), std::move(fn), {}, 0});
    return id;
}

void PipelineImpl::AddDependency(StepId downstream, StepId upstream) {
    if (!building_)
        throw std::logic_error("AddDependency: graph is frozen after Run");

    const auto up = nodes_.find(upstream);
    const auto down = nodes_.find(downstream);
    if (up == nodes_.end() || down == nodes_.end())
        throw std::invalid_argument("AddDependency: unknown StepId");
    if (downstream == upstream)
        throw std::invalid_argument("AddDependency: self dependency");

    auto& next = up->second.next_;
    if (std::find(next.begin(), next.end(), downstream) != next.end())
        return;   // 重复边：忽略

    next.push_back(downstream);      // 边记在上游的出边表
    ++down->second.indegree_;        // 入度记在下游
}

void PipelineImpl::SetListener(Listener listener) {
    std::shared_ptr<const Listener> next;
    if (listener) {
        next = std::make_shared<const Listener>(std::move(listener));
    }

    std::lock_guard<std::mutex> lk(m_);
    listener_ = std::move(next);
}

bool PipelineImpl::notify(StepId id, StepState state, const Error& err) {
    // 先取一份强引用：即使此刻 SetListener(nullptr)，正在执行的回调也安全。
    std::shared_ptr<const Listener> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_);
        snapshot = listener_;
    }
    if (!snapshot) return true;

    try {
        (*snapshot)(id, state, err);
        return true;
    } catch (...) {
        // 契约：回调异常不得穿透；由 executeNode 负责把该 Step 转成 Failed。
        return false;
    }
}

// ---------------------------------------------------------------
// 执行一个节点：Pending → Running → fn → 终态。
// 同步循环和异步 runStep 共用，区别只在执行前后的调度状态维护。
// ---------------------------------------------------------------
bool PipelineImpl::executeNode(StepId id, Error& err) {
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
        nodes_.at(id).fn_();
    } catch (const std::exception& e) {
        return fail(Error{1, e.what()});
    } catch (...) {
        return fail(Error{1, "unknown exception"});
    }

    if (!notify(id, StepState::Succeeded, Error{}))
        return fail(Error{3, "listener exception"});
    return true;
}

// ---------------------------------------------------------------
// 同步执行：调用线程按拓扑序跑完整张图，回调也在调用线程触发。
// ---------------------------------------------------------------
bool PipelineImpl::Run() {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (running_) return false;
        running_ = true;
        asyncRun_ = false;
        cancelled_ = false;
        runningNodes_.clear();
        ++runGeneration_;   // 让上一轮可能残留的异步任务失效
    }

    building_ = false;   // 冻结图

    // 入度副本
    std::map<StepId, std::size_t> indeg;
    std::queue<StepId> ready;
    for (const auto& kv : nodes_) {
        indeg[kv.first] = kv.second.indegree_;
        if (kv.second.indegree_ == 0) ready.push(kv.first);
    }

    std::set<StepId> handled;

    auto skipDownstream = [&](StepId from, const Error& err) {
        std::queue<StepId> q;
        for (StepId v : nodes_.at(from).next_) q.push(v);
        while (!q.empty()) {
            const StepId v = q.front();
            q.pop();
            if (!handled.insert(v).second) continue;
            notify(v, StepState::Pending, Error{});
            notify(v, StepState::Skipped, err);
            for (StepId w : nodes_.at(v).next_) q.push(w);
        }
    };

    while (!ready.empty()) {
        const StepId id = ready.front();
        ready.pop();

        if (handled.count(id)) continue;
        handled.insert(id);

        Error err;
        if (!executeNode(id, err)) {
            skipDownstream(id, err);
            continue;
        }

        for (StepId down : nodes_.at(id).next_) {
            if (handled.count(down)) continue;
            if (--indeg[down] == 0) ready.push(down);
        }
    }

    // 环检测：正常 DAG 里每个节点要么执行、要么被跳过
    if (handled.size() != nodes_.size()) {
        Error cycleErr;
        cycleErr.code = 2;
        cycleErr.message = "cycle detected";
        for (const auto& kv : nodes_) {
            if (handled.count(kv.first)) continue;
            notify(kv.first, StepState::Pending, Error{});
            notify(kv.first, StepState::Failed, cycleErr);
        }
    }

    {
        std::lock_guard<std::mutex> lk(m_);
        running_ = false;
    }
    doneCv_.notify_all();
    return true;
}

// ---------------------------------------------------------------
// 异步执行：把根节点投递到线程池后立即返回，后续由 worker 接力。
// ---------------------------------------------------------------
bool PipelineImpl::RunAsync() {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (running_) return false;
        running_ = true;
        asyncRun_ = true;
        cancelled_ = false;
        runningNodes_.clear();
        generation = ++runGeneration_;
    }
    building_ = false;   // 冻结图
    // 1) 静态环检测：异步路径没有“自然结束”，有环就会永远等不到 pending 归零
    std::map<StepId, std::size_t> indeg;
    std::queue<StepId> seed;
    for (const auto& kv : nodes_) {
        indeg[kv.first] = kv.second.indegree_;
        if (kv.second.indegree_ == 0) seed.push(kv.first);
    }
    auto work = indeg;
    std::set<StepId> reachable;
    while (!seed.empty()) {
        const StepId id = seed.front();
        seed.pop();
        reachable.insert(id);
        for (StepId down : nodes_.at(id).next_) {
            if (--work[down] == 0) seed.push(down);
        }
    }
    if (reachable.size() != nodes_.size()) {
        std::vector<StepId> cycle;
        for (const auto& kv : nodes_) {
            if (!reachable.count(kv.first)) cycle.push_back(kv.first);
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            running_ = false;
            asyncRun_ = false;
        }
        doneCv_.notify_all();
        Error cycleErr;
        cycleErr.code = 2;
        cycleErr.message = "cycle detected";
        for (StepId id : cycle) {
            notify(id, StepState::Pending, Error{});
            notify(id, StepState::Failed, cycleErr);
        }
        return false;
    }
    // 2) 初始化本轮调度状态，线程池懒创建
    std::vector<StepId> roots;
    bool emptyGraph = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        indeg_ = std::move(indeg);
        handled_.clear();
        runningNodes_.clear();
        pending_ = nodes_.size();
        emptyGraph = nodes_.empty();
        if (!emptyGraph && !pool_) {
            const auto threads = cfg_.maxConcurrency == 0 ? 1u : cfg_.maxConcurrency;
            pool_ = std::make_unique<boost::asio::thread_pool>(
                static_cast<std::size_t>(threads));
        }
        for (const auto& kv : indeg_) {
            if (kv.second == 0) roots.push_back(kv.first);
        }
        if (emptyGraph) {
            running_ = false;
            asyncRun_ = false;
        }
    }
    if (emptyGraph) {
        doneCv_.notify_all();
        return true;
    }
    // 3) 点火：提交根节点后立即返回
    for (StepId id : roots)
        boost::asio::post(*pool_, [this, id, generation] { runStep(id, generation); });
    return true;
}
// ---------------------------------------------------------------
// worker 上执行一个步骤，并在结束时把下一批 ready 的步骤提交回池子。
// ---------------------------------------------------------------
void PipelineImpl::runStep(StepId id, std::uint64_t generation) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (generation != runGeneration_) return;   // 上一轮残留的排队任务
        if (handled_.count(id)) return;             // 已被 Cancel 标记，不再执行
        runningNodes_.insert(id);
    }

    Error err;
    const bool ok = executeNode(id, err);

    if (ok) {
        std::vector<StepId> ready;
        bool finished = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            runningNodes_.erase(id);
            handled_.insert(id);
            if (!cancelled_) {
                for (StepId down : nodes_.at(id).next_) {
                    if (handled_.count(down)) continue;
                    if (--indeg_[down] == 0) ready.push_back(down);
                }
            }
            if (--pending_ == 0) {
                running_ = false;
                asyncRun_ = false;
                finished = true;
            }
        }
        for (StepId down : ready)
            boost::asio::post(*pool_, [this, down, generation] { runStep(down, generation); });

        if (finished) doneCv_.notify_all();
        return;
    }
    // 下游全部跳过；pending 必须照常递减，否则 Wait() 永远醒不来
    std::vector<StepId> skipped;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        runningNodes_.erase(id);
        if (handled_.insert(id).second) --pending_;
        if (!cancelled_) {
            std::queue<StepId> q;
            for (StepId v : nodes_.at(id).next_) q.push(v);
            while (!q.empty()) {
                const StepId v = q.front();
                q.pop();
                if (!handled_.insert(v).second) continue;
                skipped.push_back(v);
                --pending_;
                for (StepId w : nodes_.at(v).next_) q.push(w);
            }
        }
        if (pending_ == 0) {
            running_ = false;
            asyncRun_ = false;
            finished = true;
        }
    }
    for (StepId v : skipped) {
        notify(v, StepState::Pending, Error{});
        notify(v, StepState::Skipped, err);
    }
    if (finished) doneCv_.notify_all();
}

void PipelineImpl::Cancel() {
    std::vector<StepId> cancelledList;
    std::vector<StepId> skippedList;
    bool finished = false;

    {
        std::lock_guard<std::mutex> lk(m_);
        // 同步 Run 的调度状态是本地的，暂不支持在其执行期间取消。
        if (!running_ || !asyncRun_ || cancelled_) return;
        cancelled_ = true;

        for (const auto& kv : nodes_) {
            const StepId id = kv.first;
            if (handled_.count(id) || runningNodes_.count(id)) continue;

            // 入度已归零说明上游全部成功，它本来马上就能跑 → Cancelled。
            // 否则它被运行中/已失败/已取消的上游挡住，永远不会再跑 → Skipped。
            const bool ready = (indeg_[id] == 0);
            handled_.insert(id);
            --pending_;
            (ready ? cancelledList : skippedList).push_back(id);
        }

        if (pending_ == 0) {
            running_ = false;
            asyncRun_ = false;
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

    if (finished) doneCv_.notify_all();
}

PipelineImpl::~PipelineImpl() {
    Cancel();
    Wait();
}

void PipelineImpl::Wait() {
    std::unique_lock<std::mutex> lk(m_);
    doneCv_.wait(lk, [this] { return !running_; });
}

} // namespace imgpipe
