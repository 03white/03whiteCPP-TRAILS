#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
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

TEST(Error, StepExceptionMarksFailedWithError) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<Event> events;
    Error failedError;

    const StepId a = pipeline->AddStep("A", [] { throw std::runtime_error("boom"); });

    pipeline->SetListener([&](StepId id, StepState state, const Error& err) {
        events.emplace_back(id, state);
        if (state == StepState::Failed) failedError = err;
    });

    ASSERT_NO_THROW(pipeline->Run());

    EXPECT_EQ(statesFor(events, a),
              (std::vector<StepState>{StepState::Pending, StepState::Running, StepState::Failed}));
    EXPECT_EQ(failedError.code, 1);
    EXPECT_NE(failedError.message.find("boom"), std::string::npos);
}

TEST(Error, DownstreamOfFailedStepIsSkipped) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    std::vector<Event> events;
    bool bRan = false;
    bool cRan = false;

    const StepId a = pipeline->AddStep("A", [] { throw std::runtime_error("boom"); });
    const StepId b = pipeline->AddStep("B", [&bRan] { bRan = true; });
    const StepId c = pipeline->AddStep("C", [&cRan] { cRan = true; });

    pipeline->AddDependency(b, a);
    pipeline->AddDependency(c, b);

    pipeline->SetListener([&events](StepId id, StepState state, const Error&) {
        events.emplace_back(id, state);
    });

    ASSERT_NO_THROW(pipeline->Run());

    EXPECT_FALSE(bRan);
    EXPECT_FALSE(cRan);
    EXPECT_EQ(statesFor(events, b), (std::vector<StepState>{StepState::Pending, StepState::Skipped}));
    EXPECT_EQ(statesFor(events, c), (std::vector<StepState>{StepState::Pending, StepState::Skipped}));
}

TEST(Error, IndependentBranchStillRunsAfterFailure) {
    auto pipeline = Pipeline::Create();
    ASSERT_NE(pipeline, nullptr);

    bool xRan = false;

    const StepId a = pipeline->AddStep("A", [] { throw std::runtime_error("boom"); });
    const StepId x = pipeline->AddStep("X", [&xRan] { xRan = true; });

    pipeline->SetListener([](StepId, StepState, const Error&) {});

    ASSERT_NO_THROW(pipeline->Run());
    EXPECT_TRUE(xRan);
    (void)a;
}
