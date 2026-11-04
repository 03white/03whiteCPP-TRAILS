#pragma once
#include "../include/image_pipeline.h"

#include <cstddef>
#include <map>
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

    Config cfg_;
    Listener listener_;
    bool building_{true};               // 构图期；Run 开始后冻结
    bool running_{false};               // 防止回调里重入 Run
    StepId nextId_{0};
    std::map<StepId, StepNode> nodes_;
};

} // namespace imgpipe
