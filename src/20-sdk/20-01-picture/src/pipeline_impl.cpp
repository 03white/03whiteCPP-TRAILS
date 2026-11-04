#include "pipeline_impl.h"

#include <algorithm>
#include <queue>
#include <set>
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
        return;   
    next.push_back(downstream);    
    ++down->second.indegree_;       
}

void PipelineImpl::SetListener(Listener listener) {
    listener_ = std::move(listener);
}

void PipelineImpl::notify(StepId id, StepState state, const Error& err) {
    if (!listener_) return;
    try {
        listener_(id, state, err);
    } catch (...) {
        
    }
}

bool PipelineImpl::Run() {
    if (running_) return false;
    auto guard = std::unique_ptr<bool, void(*)(bool*)>(
    &running_, [](bool* p){ *p = false; });
    running_ = true;
    building_ = false;   // 冻结图
    for (const auto& kv : nodes_)
        notify(kv.first, StepState::Pending, Error{});
    std::map<StepId, std::size_t> indeg;//入度表
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
            if (!handled.insert(v).second) continue;   // 已处理过
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
        bool ok = true;
        notify(id, StepState::Running, Error{});
        try {
            nodes_.at(id).fn_();
        } catch (const std::exception& e) {
            ok = false;
            err.code = 1;
            err.message = e.what();
        } catch (...) {
            ok = false;
            err.code = 1;
            err.message = "unknown exception";
        }
        if (!ok) {
            notify(id, StepState::Failed, err);
            skipDownstream(id, err);
            continue;
        }
        notify(id, StepState::Succeeded, Error{});
        for (StepId down : nodes_.at(id).next_) {
            if (handled.count(down)) continue;
            if (--indeg[down] == 0) ready.push(down);
        }
    }

    if (handled.size() != nodes_.size()) {
        Error cycleErr;
        cycleErr.code = 2;
        cycleErr.message = "cycle detected";
        for (const auto& kv : nodes_) {
            if (handled.count(kv.first)) continue;
            notify(kv.first, StepState::Failed, cycleErr);
        }
    }
    return true;
}

void PipelineImpl::Cancel() {}

void PipelineImpl::Wait() {}

} // namespace imgpipe
