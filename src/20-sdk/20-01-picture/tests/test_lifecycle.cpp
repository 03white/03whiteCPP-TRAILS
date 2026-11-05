#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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

TEST(Lifecycle, ListenerExceptionMarksStepFailed) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<StepState> states;
    bool ran = false;

    pipeline->AddStep("A", [&ran] { ran = true; });
    pipeline->SetListener([&states](StepId, StepState state, const Error&) {
        states.push_back(state);
        if (state == StepState::Running) throw std::runtime_error("listener boom");
    });

    ASSERT_NO_THROW(pipeline->Run());
    EXPECT_FALSE(ran);
    ASSERT_FALSE(states.empty());
    EXPECT_EQ(states.back(), StepState::Failed);
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

TEST(Lifecycle, AsyncNullListenerStopsNotifications) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::atomic<int> callbacks{0};
    pipeline->AddStep("A", [] {});
    pipeline->SetListener([&callbacks](StepId, StepState, const Error&) { ++callbacks; });

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();
    ASSERT_GT(callbacks.load(), 0);

    pipeline->SetListener(nullptr);
    const int before = callbacks.load();

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();
    EXPECT_EQ(callbacks.load(), before);
}

TEST(Lifecycle, AsyncListenerExceptionMarksStepFailed) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::mutex m;
    std::vector<StepState> states;
    bool ran = false;

    pipeline->AddStep("A", [&ran] { ran = true; });
    pipeline->SetListener([&](StepId, StepState state, const Error&) {
        {
            std::lock_guard<std::mutex> lk(m);
            states.push_back(state);
        }
        if (state == StepState::Running) throw std::runtime_error("listener boom");
    });

    ASSERT_TRUE(pipeline->RunAsync());
    ASSERT_NO_THROW(pipeline->Wait());

    EXPECT_FALSE(ran);
    std::lock_guard<std::mutex> lk(m);
    ASSERT_FALSE(states.empty());
    EXPECT_EQ(states.back(), StepState::Failed);
}

TEST(Lifecycle, DestructorWaitsForRunningStep) {
    std::mutex m;
    std::condition_variable cv;
    bool started = false;
    bool gate = false;
    bool finished = false;

    {
        auto pipeline = Pipeline::Create();
        ASSERT_NE(pipeline, nullptr);

        pipeline->AddStep("A", [&] {
            {
                std::lock_guard<std::mutex> lk(m);
                started = true;
            }
            cv.notify_all();

            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return gate; });
            finished = true;
        });

        ASSERT_TRUE(pipeline->RunAsync());

        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return started; });
        }

        std::thread releaser([&] {
            {
                std::lock_guard<std::mutex> lk(m);
                gate = true;
            }
            cv.notify_all();
        });

        pipeline.reset();   // 析构：Cancel + Wait，必须等 A 结束
        releaser.join();
    }

    EXPECT_TRUE(finished);
}
