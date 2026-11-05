#pragma once
#include "../include/image_pipeline.h"

#include <boost/asio/thread_pool.hpp>

#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace imgpipe {

class PipelineImpl : public Pipeline {
public:
    explicit PipelineImpl(const Config& cfg);

    StepId AddStep(std::string name, Step fn) override;
    void AddDependency(StepId downstream, StepId upstream) override;
    void SetListener(Listener listener) override;
    bool Run() override;
    bool RunAsync() override;
    void Cancel() override;
    void Wait() override;

private:
    struct StepNode {
        StepId id_{0};
        std::string name_;
        Step fn_;
        std::vector<StepId> next_;      // 出边
        std::size_t indegree_{0};       // 入度
    };

    void notify(StepId id, StepState state, const Error& err);
    void runStep(StepId id);            // 在 worker 线程上执行一个步骤

    Config cfg_;
    Listener listener_;
    bool building_{true};               // 构图期；执行开始后冻结

    StepId nextId_{0};
    std::map<StepId, StepNode> nodes_;

    // 异步调度的共享状态
    std::mutex m_;
    std::condition_variable doneCv_;
    bool running_{false};
    std::map<StepId, std::size_t> indeg_;
    std::set<StepId> handled_;          // 已到达终态或被跳过
    std::size_t pending_{0};            // 尚未到达终态的步骤数
    // 最后声明：析构时最先销毁，析构函数里 join 所有 worker
    std::unique_ptr<boost::asio::thread_pool> pool_;
};

} // namespace imgpipe
