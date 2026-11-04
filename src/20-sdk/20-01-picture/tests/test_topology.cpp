#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "image_pipeline.h"

namespace {

using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;

using Event = std::pair<StepId, StepState>;

std::vector<StepState> statesFor(const std::vector<Event>& events, StepId id) {
    std::vector<StepState> states;
    for (const auto& e : events) {
        if (e.first == id) states.push_back(e.second);
    }
    return states;
}

}  // namespace

TEST(Smoke, PipelineCanBeCreated) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);
}

TEST(Topology, ChainRunsInTopologicalOrder) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<StepId> executed;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
    const StepId c = pipeline->AddStep("C", [] {});

    pipeline->AddDependency(b, a);   // B 依赖 A
    pipeline->AddDependency(c, b);   // C 依赖 B

    pipeline->SetListener(
        [&executed](StepId id, StepState state, const Error&) {
            if (state == StepState::Succeeded) executed.push_back(id);
        });

    ASSERT_TRUE(pipeline->Run());

    ASSERT_EQ(executed.size(), 3u);
    EXPECT_EQ(executed, (std::vector<StepId>{a, b, c}));
}

TEST(Topology, DiamondRunsJoinAfterBothBranches) {
    auto pipeline = Pipeline::Create();
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

    pipeline->SetListener(
        [&executed](StepId id, StepState state, const Error&) {
            if (state == StepState::Succeeded) executed.push_back(id);
        });

    ASSERT_TRUE(pipeline->Run());

    ASSERT_EQ(executed.size(), 4u);
    EXPECT_EQ(executed.front(), a);
    EXPECT_EQ(executed.back(), d);

    // B、C 的先后不固定，只要求都排在 A 之后、D 之前
    const auto posB = std::find(executed.begin(), executed.end(), b);
    const auto posC = std::find(executed.begin(), executed.end(), c);
    ASSERT_NE(posB, executed.end());
    ASSERT_NE(posC, executed.end());
    EXPECT_LT(std::distance(executed.begin(), posB), 3);
    EXPECT_LT(std::distance(executed.begin(), posC), 3);
}

TEST(Topology, MultiRootMultiSinkRunsAllNodes) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<StepId> executed;

    const StepId r1 = pipeline->AddStep("R1", [] {});
    const StepId s1 = pipeline->AddStep("S1", [] {});
    const StepId r2 = pipeline->AddStep("R2", [] {});
    const StepId s2 = pipeline->AddStep("S2", [] {});

    pipeline->AddDependency(s1, r1);
    pipeline->AddDependency(s2, r2);

    pipeline->SetListener(
        [&executed](StepId id, StepState state, const Error&) {
            if (state == StepState::Succeeded) executed.push_back(id);
        });

    ASSERT_TRUE(pipeline->Run());

    ASSERT_EQ(executed.size(), 4u);
    for (StepId id : {r1, s1, r2, s2}) {
        EXPECT_NE(std::find(executed.begin(), executed.end(), id), executed.end());
    }
}

TEST(Topology, CycleIsReportedAsFailedWithoutHanging) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<Event> events;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});

    pipeline->AddDependency(b, a);
    pipeline->AddDependency(a, b);   // 环

    pipeline->SetListener([&events](StepId id, StepState state, const Error&) {
        events.emplace_back(id, state);
    });

    ASSERT_NO_THROW(pipeline->Run());

    EXPECT_EQ(statesFor(events, a), (std::vector<StepState>{StepState::Pending, StepState::Failed}));
    EXPECT_EQ(statesFor(events, b), (std::vector<StepState>{StepState::Pending, StepState::Failed}));
}

TEST(Topology, UpstreamSucceededPrecedesDownstreamRunning) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<Event> events;

    const StepId a = pipeline->AddStep("A", [] {});
    const StepId b = pipeline->AddStep("B", [] {});
    pipeline->AddDependency(b, a);

    pipeline->SetListener([&events](StepId id, StepState state, const Error&) {
        events.emplace_back(id, state);
    });

    ASSERT_TRUE(pipeline->Run());

    const auto aSucceeded = std::find(events.begin(), events.end(), Event{a, StepState::Succeeded});
    const auto bRunning = std::find(events.begin(), events.end(), Event{b, StepState::Running});
    ASSERT_NE(aSucceeded, events.end());
    ASSERT_NE(bRunning, events.end());
    EXPECT_LT(aSucceeded, bRunning);
}

TEST(Topology, EmptyGraphRunsSuccessfully) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    int callbacks = 0;
    pipeline->SetListener([&callbacks](StepId, StepState, const Error&) { ++callbacks; });

    EXPECT_TRUE(pipeline->Run());
    EXPECT_EQ(callbacks, 0);
}
