#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

#include "image_pipeline.h"

namespace {

using imgpipe::Config;
using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;

struct Recorder {
    std::mutex m;
    std::map<StepId, std::vector<StepState>> states;

    void on(StepId id, StepState state) {
        std::lock_guard<std::mutex> lk(m);
        states[id].push_back(state);
    }

    std::vector<StepState> get(StepId id) {
        std::lock_guard<std::mutex> lk(m);
        return states[id];
    }
};

StepState lastState(Recorder& r, StepId id) {
    const auto v = r.get(id);
    return v.empty() ? StepState::Pending : v.back();
}

}  // namespace

// 一个 worker：A 完成后 B 先被取走并阻塞，C 还留在池队列里。
// Cancel 时：C（就绪但未开始）→ Cancelled；D（被 B/C 挡住）→ Skipped；B 允许跑完。
TEST(Cancel, ReadyBecomesCancelledBlockedBecomesSkipped) {
    Config cfg;
    cfg.maxConcurrency = 1;
    auto pipeline = Pipeline::Create(cfg);
    ASSERT_NE(pipeline, nullptr);

    std::mutex m;
    std::condition_variable cv;
    bool bStarted = false;
    bool gate = false;

    Recorder recorder;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [&] {
        {
            std::lock_guard<std::mutex> lk(m);
            bStarted = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return gate; });
    });
    const StepId c = pipeline->AddStep("C", [] {});
    const StepId d = pipeline->AddStep("D", [] {});

    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, a);
    pipeline->AddDependency(d, b);
    pipeline->AddDependency(d, c);

    pipeline->SetListener([&recorder](StepId id, StepState state, const Error&) {
        recorder.on(id, state);
    });

    ASSERT_TRUE(pipeline->RunAsync());

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return bStarted; });
    }

    pipeline->Cancel();
    pipeline->Cancel();   // 幂等

    {
        std::lock_guard<std::mutex> lk(m);
        gate = true;
    }
    cv.notify_all();

    pipeline->Wait();

    EXPECT_EQ(recorder.get(a),
              (std::vector<StepState>{StepState::Pending, StepState::Running, StepState::Succeeded}));
    EXPECT_EQ(lastState(recorder, b), StepState::Succeeded);
    EXPECT_EQ(recorder.get(c),
              (std::vector<StepState>{StepState::Pending, StepState::Cancelled}));
    EXPECT_EQ(recorder.get(d),
              (std::vector<StepState>{StepState::Pending, StepState::Skipped}));
}

TEST(Cancel, CanBeCalledFromCallbackWithoutDeadlock) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    Recorder recorder;
    std::atomic<bool> cancelCalled{false};

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
    pipeline->AddDependency(b, a);

    pipeline->SetListener([&](StepId id, StepState state, const Error&) {
        recorder.on(id, state);
        if (id == a && state == StepState::Running && !cancelCalled.exchange(true)) {
            pipeline->Cancel();
        }
    });

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();

    EXPECT_EQ(lastState(recorder, a), StepState::Succeeded);
    EXPECT_EQ(lastState(recorder, b), StepState::Skipped);
}

TEST(Cancel, BeforeRunIsNoop) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    bool ran = false;
    pipeline->AddStep("A", [&ran] { ran = true; });

    pipeline->Cancel();   // 还没开始跑，应该是 no-op

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();
    EXPECT_TRUE(ran);
}

TEST(Cancel, StressRepeatedCancelThenRerun) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::atomic<int> executions{0};
    const StepId a = pipeline->AddStep("A", [&executions] { ++executions; });
    const StepId b = pipeline->AddStep("B", [&executions] { ++executions; });
    const StepId c = pipeline->AddStep("C", [&executions] { ++executions; });
    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, b);

    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(pipeline->RunAsync());
        pipeline->Cancel();
        pipeline->Wait();
    }

    // 取消之后必须还能完整再跑一轮，且每个节点只执行一次。
    const int before = executions.load();
    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();
    EXPECT_EQ(executions.load() - before, 3);
}
