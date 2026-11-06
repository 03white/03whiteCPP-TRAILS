// 取消与错误传播示例。
//
// 1) 在进度回调里调用 Cancel()：Running 的步骤跑完，被挡住的下游变 Skipped。
// 2) 步骤抛异常：该步 Failed，下游 Skipped，进程不会崩。

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "image_pipeline.h"

namespace {

using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;

const char* stateName(StepState state) {
    switch (state) {
        case StepState::Pending:   return "Pending";
        case StepState::Running:   return "Running";
        case StepState::Succeeded: return "Succeeded";
        case StepState::Failed:    return "Failed";
        case StepState::Skipped:   return "Skipped";
        case StepState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

void printEvent(StepId id, StepState state, const Error& err) {
    std::cout << "  step " << id << " -> " << stateName(state);
    if (!err.ok()) {
        std::cout << " (code " << err.code << ": " << err.message << ")";
    }
    std::cout << "\n";
}

void demoCancelFromCallback() {
    std::cout << "[cancel from callback]\n";

    auto pipeline = Pipeline::Create();
    bool cancelRequested = false;

    const StepId a = pipeline->AddStep("A", [] { std::cout << "  run A\n"; });
    const StepId b = pipeline->AddStep("B", [] { std::cout << "  run B\n"; });
    const StepId c = pipeline->AddStep("C", [] { std::cout << "  run C\n"; });
    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, b);

    pipeline->SetListener([&](StepId id, StepState state, const Error& err) {
        printEvent(id, state, err);
        if (id == a && state == StepState::Running && !cancelRequested) {
            cancelRequested = true;
            std::cout << "  -> request cancel while A is running\n";
            pipeline->Cancel();
        }
    });

    pipeline->RunAsync();
    pipeline->Wait();
}

void demoFailurePropagation() {
    std::cout << "[failure propagation]\n";

    auto pipeline = Pipeline::Create();

    const StepId a = pipeline->AddStep("A", [] { std::cout << "  run A\n"; });
    const StepId b = pipeline->AddStep("B", [] {
        std::cout << "  run B -> throw\n";
        throw std::runtime_error("decode failed");
    });
    const StepId c = pipeline->AddStep("C", [] { std::cout << "  run C\n"; });
    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, b);

    pipeline->SetListener(printEvent);
    pipeline->Run();
}

}  // namespace

int main() {
    demoCancelFromCallback();
    demoFailurePropagation();
    return 0;
}
