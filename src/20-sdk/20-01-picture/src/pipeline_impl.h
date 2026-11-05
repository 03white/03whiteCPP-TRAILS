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
    ~PipelineImpl() override;

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

    // 通知监听器；返回 false 表示监听器抛了异常（异常已被捕获）。
    bool notify(StepId id, StepState state, const Error& err);

    // 执行一个节点并发出 Pending/Running/终态通知；返回是否成功。
    // 同步与异步只在“执行前的调度状态”和“执行后的入度维护”上不同。
    bool executeNode(StepId id, Error& err);

    void runStep(StepId id, std::uint64_t generation);   // 在 worker 线程上执行一个步骤

    Config cfg_;
    // 用 shared_ptr 持有监听器：注销时即使有回调正在执行，也不会销毁它。
    std::shared_ptr<const Listener> listener_;
    bool building_{true};               // 构图期；执行开始后冻结

    StepId nextId_{0};
    std::map<StepId, StepNode> nodes_;

    // 异步调度的共享状态
    std::mutex m_;
    std::condition_variable doneCv_;
    bool running_{false};               // 是否有一轮执行未收敛
    bool asyncRun_{false};              // 当前这一轮是否由 RunAsync 发起
    bool cancelled_{false};             // 本轮是否已请求取消
    std::map<StepId, std::size_t> indeg_;
    std::set<StepId> handled_;          // 已到达终态或被跳过
    std::set<StepId> runningNodes_;     // 正在执行的节点（取消时跳过它们）
    std::size_t pending_{0};            // 尚未到达终态的步骤数
    std::uint64_t runGeneration_{0};    // 每轮自增，丢弃上一轮残留的排队任务
    // 最后声明：析构时最先销毁，析构函数里 join 所有 worker
    std::unique_ptr<boost::asio::thread_pool> pool_;
};

} // namespace imgpipe
