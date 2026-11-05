#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "image_pipeline.h"

namespace {

using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;

using Event = std::pair<StepId, StepState>;

struct Recorder {
    std::mutex m;
    std::vector<Event> events;

    void on(StepId id, StepState state) {
        std::lock_guard<std::mutex> lk(m);
        events.emplace_back(id, state);
    }

    std::vector<Event> snapshot() {
        std::lock_guard<std::mutex> lk(m);
        return events;
    }
};

std::vector<StepState> statesFor(const std::vector<Event>& events, StepId id) {
    std::vector<StepState> states;
    for (const auto& e : events) {
        if (e.first == id) states.push_back(e.second);
    }
    return states;
}

}  // namespace

TEST(Async, RunAsyncReturnsBeforeCompletion) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::mutex m;
    std::condition_variable startedCv;
    std::condition_variable returnCv;
    std::condition_variable gateCv;
    bool started = false;
    bool returned = false;
    bool gate = false;

    const StepId a = pipeline->AddStep("A", [&] {
        {
            std::lock_guard<std::mutex> lk(m);
            started = true;
        }
        startedCv.notify_all();

        std::unique_lock<std::mutex> lk(m);
        gateCv.wait(lk, [&] { return gate; });
    });

    std::thread runner([&] {
        pipeline->RunAsync();
        {
            std::lock_guard<std::mutex> lk(m);
            returned = true;
        }
        returnCv.notify_all();
    });

    {
        std::unique_lock<std::mutex> lk(m);
        startedCv.wait(lk, [&] { return started; });
    }

    bool returnedBeforeCompletion = false;
    {
        std::unique_lock<std::mutex> lk(m);
        returnedBeforeCompletion = returnCv.wait_for(
            lk, std::chrono::seconds(2), [&] { return returned; });
    }

    {
        std::lock_guard<std::mutex> lk(m);
        gate = true;
    }
    gateCv.notify_all();
    runner.join();
    pipeline->Wait();

    EXPECT_TRUE(returnedBeforeCompletion);
    (void)a;
}

TEST(Async, ChainRunsInTopologicalOrder) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    Recorder recorder;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
    const StepId c = pipeline->AddStep("C", [] {});
    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, b);

    pipeline->SetListener([&recorder](StepId id, StepState state, const Error&) {
        recorder.on(id, state);
    });

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();

    const auto events = recorder.snapshot();
    EXPECT_EQ(statesFor(events, a),
              (std::vector<StepState>{StepState::Pending, StepState::Running, StepState::Succeeded}));
    EXPECT_EQ(statesFor(events, b),
              (std::vector<StepState>{StepState::Pending, StepState::Running, StepState::Succeeded}));
    EXPECT_EQ(statesFor(events, c),
              (std::vector<StepState>{StepState::Pending, StepState::Running, StepState::Succeeded}));

    const auto aSucceeded = std::find(events.begin(), events.end(), Event{a, StepState::Succeeded});
    const auto bRunning = std::find(events.begin(), events.end(), Event{b, StepState::Running});
    ASSERT_NE(aSucceeded, events.end());
    ASSERT_NE(bRunning, events.end());
    EXPECT_LT(aSucceeded, bRunning);
}

TEST(Async, FailureSkipsDownstream) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    Recorder recorder;
    bool bRan = false;

    const StepId a = pipeline->AddStep("A", [] { throw std::runtime_error("boom"); });
    const StepId b = pipeline->AddStep("B", [&bRan] { bRan = true; });
    pipeline->AddDependency(b, a);

    pipeline->SetListener([&recorder](StepId id, StepState state, const Error&) {
        recorder.on(id, state);
    });

    ASSERT_TRUE(pipeline->RunAsync());
    pipeline->Wait();

    EXPECT_FALSE(bRan);
    const auto events = recorder.snapshot();
    EXPECT_EQ(statesFor(events, b),
              (std::vector<StepState>{StepState::Pending, StepState::Skipped}));
}

TEST(Async, DiamondRunsJoinAfterBothBranches) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    Recorder recorder;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
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
    pipeline->Wait();

    const auto events = recorder.snapshot();
    const auto dRunning = std::find(events.begin(), events.end(), Event{d, StepState::Running});
    const auto bSucceeded = std::find(events.begin(), events.end(), Event{b, StepState::Succeeded});
    const auto cSucceeded = std::find(events.begin(), events.end(), Event{c, StepState::Succeeded});
    ASSERT_NE(dRunning, events.end());
    ASSERT_NE(bSucceeded, events.end());
    ASSERT_NE(cSucceeded, events.end());
    EXPECT_LT(bSucceeded, dRunning);
    EXPECT_LT(cSucceeded, dRunning);
}
