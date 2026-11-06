#pragma once
#include "../include/image_pipeline.h"

#include <memory>

namespace imgpipe {

class PipelineImpl : public Pipeline {
public:
    explicit PipelineImpl(const Config& cfg, bool testMode = false);
    ~PipelineImpl() override;

    StepId AddStep(std::string name, Step fn) override;
    void AddDependency(StepId downstream, StepId upstream) override;
    void SetListener(Listener listener) override;
    bool Run() override;
    bool RunAsync() override;
    void Cancel() override;
    void Wait() override;

private:
    struct Impl;                 // 全部数据结构、锁、线程池都藏在 cpp 里
    std::unique_ptr<Impl> impl_;
};

} // namespace imgpipe
