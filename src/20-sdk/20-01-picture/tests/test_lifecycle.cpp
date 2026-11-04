#include <gtest/gtest.h>

#include <stdexcept>

#include "image_pipeline.h"

namespace {
using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;
}  // namespace

TEST(Lifecycle, GraphIsFrozenAfterRun) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    pipeline->AddStep("A", [] {});
    ASSERT_TRUE(pipeline->Run());

    EXPECT_THROW(pipeline->AddStep("B", [] {}), std::logic_error);
    EXPECT_THROW(pipeline->AddDependency(0, 0), std::logic_error);
}

TEST(Lifecycle, NullListenerStopsNewNotifications) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    int callbacks = 0;
    pipeline->AddStep("A", [] {});
    pipeline->SetListener([&callbacks](StepId, StepState, const Error&) { ++callbacks; });

    ASSERT_TRUE(pipeline->Run());
    ASSERT_GT(callbacks, 0);

    pipeline->SetListener(nullptr);
    const int before = callbacks;
    ASSERT_TRUE(pipeline->Run());
    EXPECT_EQ(callbacks, before);
}

TEST(Lifecycle, ListenerExceptionDoesNotEscapeRun) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    bool ran = false;
    pipeline->AddStep("A", [&ran] { ran = true; });
    pipeline->SetListener([](StepId, StepState, const Error&) {
        throw std::runtime_error("listener boom");
    });

    ASSERT_NO_THROW(pipeline->Run());
    EXPECT_TRUE(ran);
}

TEST(Lifecycle, ReentrantRunReturnsFalse) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    bool innerResult = true;
    bool innerCalled = false;

    pipeline->AddStep("A", [] {});
    pipeline->SetListener([&](StepId, StepState state, const Error&) {
        if (state == StepState::Running && !innerCalled) {
            innerCalled = true;
            innerResult = pipeline->Run();
        }
    });

    ASSERT_TRUE(pipeline->Run());
    EXPECT_TRUE(innerCalled);
    EXPECT_FALSE(innerResult);
}

TEST(Lifecycle, NoListenerIsAllowed) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    bool ran = false;
    pipeline->AddStep("A", [&ran] { ran = true; });

    ASSERT_NO_THROW(pipeline->Run());
    EXPECT_TRUE(ran);
}
