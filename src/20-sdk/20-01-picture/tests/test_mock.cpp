#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "image_pipeline.h"

namespace {
using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;
}  // namespace

// CreateForTest 的关键契约：不开线程，RunAsync 同步跑完，回调在调用线程。
TEST(Mock, RunAsyncIsSynchronousAndOnCallerThread) {
    auto pipeline = Pipeline::CreateForTest();
    ASSERT_NE(pipeline, nullptr);

    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id callbackThread;
    bool ran = false;
    bool callbackAfterReturn = true;

    pipeline->AddStep("A", [&ran] { ran = true; });
    pipeline->SetListener([&](StepId, StepState state, const Error&) {
        if (state == StepState::Succeeded) callbackThread = std::this_thread::get_id();
    });

    ASSERT_TRUE(pipeline->RunAsync());
    callbackAfterReturn = ran;   // RunAsync 返回时工作必须已经完成

    EXPECT_TRUE(callbackAfterReturn);
    EXPECT_EQ(callbackThread, caller);
}

TEST(Mock, DiamondRunsInTopologicalOrder) {
    auto pipeline = Pipeline::CreateForTest();
    ASSERT_NE(pipeline, nullptr);

    std::vector<StepId> executed;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
    const StepId c = pipeline->AddStep("C", [] {});
    const StepId d = pipeline->AddStep("D", [] {});
    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, a);
    pipeline->AddDependency(d, b);
    pipeline->AddDependency(d, c);

    pipeline->SetListener([&executed](StepId id, StepState state, const Error&) {
        if (state == StepState::Succeeded) executed.push_back(id);
    });

    ASSERT_TRUE(pipeline->RunAsync());

    ASSERT_EQ(executed.size(), 4u);
    EXPECT_EQ(executed.front(), a);
    EXPECT_EQ(executed.back(), d);
}

TEST(Mock, SyncRunAlsoWorks) {
    auto pipeline = Pipeline::CreateForTest();
    ASSERT_NE(pipeline, nullptr);

    bool ran = false;
    pipeline->AddStep("A", [&ran] { ran = true; });

    ASSERT_TRUE(pipeline->Run());
    EXPECT_TRUE(ran);
}
