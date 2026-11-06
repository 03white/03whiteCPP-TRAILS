// 链式图完整示例：load -> decode -> resize -> watermark -> encode -> save
// 用同步 Run() 演示，回调在调用线程触发。

#include <iostream>
#include <string>
#include <vector>

#include "image_pipeline.h"

namespace {

const char* stateName(imgpipe::StepState state) {
    using imgpipe::StepState;
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

}  // namespace

int main() {
    using namespace imgpipe;

    auto pipeline = Pipeline::Create();
    if (!pipeline) {
        std::cerr << "create pipeline failed\n";
        return 1;
    }

    std::vector<std::string> executed;

    auto addStep = [&](const char* name) {
        return pipeline->AddStep(name, [&executed, name] { executed.push_back(name); });
    };

    const StepId load = addStep("load");
    const StepId decode = addStep("decode");
    const StepId resize = addStep("resize");
    const StepId watermark = addStep("watermark");
    const StepId encode = addStep("encode");
    const StepId save = addStep("save");

    pipeline->AddDependency(decode, load);
    pipeline->AddDependency(resize, decode);
    pipeline->AddDependency(watermark, resize);
    pipeline->AddDependency(encode, watermark);
    pipeline->AddDependency(save, encode);

    pipeline->SetListener([](StepId id, StepState state, const Error& err) {
        std::cout << "step " << id << " -> " << stateName(state);
        if (!err.ok()) {
            std::cout << " (code " << err.code << ": " << err.message << ")";
        }
        std::cout << "\n";
    });

    if (!pipeline->Run()) {
        std::cerr << "pipeline is already running\n";
        return 1;
    }

    std::cout << "executed order:";
    for (const auto& name : executed) {
        std::cout << " " << name;
    }
    std::cout << "\n";
    return 0;
}
