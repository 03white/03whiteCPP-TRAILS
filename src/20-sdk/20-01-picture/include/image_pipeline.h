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
    // ---- 执行 ----
    // 同步：在调用线程上按拓扑序执行整张图，阻塞到所有步骤收敛后返回；
    // 回调也在调用线程触发。同一实例同时只允许一次执行。
    virtual bool Run()=0;

    // 异步：把可执行步骤投递到内部线程池后立即返回；回调可能在库内线程触发。
    // 同一实例同时只允许一次执行；已有执行未收敛时返回 false。
    // 线程池大小由 Config::maxConcurrency 控制。
    virtual bool RunAsync()=0;

    // 取消：已 Running 的步骤允许完成；未开始的步骤转为 Cancelled。
    // 可在任意线程、任意时刻调用，包括回调内部。
    virtual void Cancel()=0;

    // 阻塞直到当前执行（Run 或 RunAsync）收敛；无执行时立即返回。
    virtual void Wait()=0;
};

} //namespace imgpipe














