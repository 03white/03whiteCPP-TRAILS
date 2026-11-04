#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace imgpipe {
class PipelineImpl;
enum class StepState{
    Pending,
    Running,
    Succeeded,
    Failed,
    Skipped,
    Cancelled,
};
struct Error{
    int code=0;
    std::string message;
    bool ok()const {return code==0;}
};

struct Config{
    std::uint32_t maxConcurrency=2;
};

using StepId = std::uint64_t;
// 处理节点流：通过 lambda 闭包捕获上下文
using Step = std::function<void()>;
using Listener = std::function<void(StepId, StepState, const Error&)>;

class Pipeline{
public:
    static std::unique_ptr<Pipeline> Create(const Config& cfg={});
    virtual ~Pipeline()=default;
    virtual StepId AddStep(std::string name,Step fn)=0;
    virtual void   AddDependency(StepId downstream,StepId upstream)=0;
    virtual void SetListener(Listener listener)=0;
    virtual bool Run()=0;
    virtual void Cancel()=0;
    virtual void Wait()=0;
};

} //namespace imgpipe














