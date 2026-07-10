// FixedTimestep accumulator tests (pure CPU, no GL).
#include <gtest/gtest.h>
#include "Engine.h"

using MyCoreEngine::FixedTimestep;

TEST(FixedTimestep, ConsumesWholeStepsAndCarriesRemainder) {
    FixedTimestep ts(0.01f); // 100 Hz
    int calls = 0;
    float lastDt = 0.f;

    // 0.035s => 3 whole steps, 0.005 carried
    int steps = ts.advance(0.035f, [&](float dt) { ++calls; lastDt = dt; });
    EXPECT_EQ(steps, 3);
    EXPECT_EQ(calls, 3);
    EXPECT_FLOAT_EQ(lastDt, 0.01f);
    EXPECT_NEAR(ts.alpha(), 0.5f, 1e-3f); // 0.005 / 0.01

    // 0.005 more completes exactly one further step
    steps = ts.advance(0.005f, [&](float) { ++calls; });
    EXPECT_EQ(steps, 1);
    EXPECT_EQ(calls, 4);
    EXPECT_NEAR(ts.alpha(), 0.f, 1e-3f);
}

TEST(FixedTimestep, AccumulatesAcrossSmallFrames) {
    FixedTimestep ts(1.f / 60.f);
    int calls = 0;
    // 120 frames of 1/120s => exactly 60 fixed steps
    for (int i = 0; i < 120; ++i) {
        ts.advance(1.f / 120.f, [&](float) { ++calls; });
    }
    EXPECT_NEAR(calls, 60, 1); // float accumulation slack of one step
}

TEST(FixedTimestep, SpiralOfDeathGuardDropsBacklog) {
    FixedTimestep ts(0.01f);
    int calls = 0;
    // A 10-second stall would demand 1000 steps; the cap runs maxSteps and
    // drops the rest instead of freezing the app.
    int steps = ts.advance(10.f, [&](float) { ++calls; }, /*maxSteps=*/8);
    EXPECT_EQ(steps, 8);
    EXPECT_EQ(calls, 8);
    EXPECT_NEAR(ts.alpha(), 0.f, 1e-6f); // backlog dropped

    // ...and behaves normally afterwards
    steps = ts.advance(0.01f, [&](float) { ++calls; });
    EXPECT_EQ(steps, 1);
}

TEST(FixedTimestep, NegativeAndZeroDtAreSafe) {
    FixedTimestep ts(0.01f);
    int calls = 0;
    EXPECT_EQ(ts.advance(0.f, [&](float) { ++calls; }), 0);
    EXPECT_EQ(ts.advance(-5.f, [&](float) { ++calls; }), 0);
    EXPECT_EQ(calls, 0);
    EXPECT_NEAR(ts.alpha(), 0.f, 1e-6f);
}

TEST(FixedTimestep, SetStepClampsToMinimum) {
    FixedTimestep ts(0.01f);
    ts.setStep(0.f); // must not divide by zero / infinite-loop
    EXPECT_GT(ts.step(), 0.f);
    int calls = 0;
    ts.advance(0.001f, [&](float) { ++calls; }, 4);
    EXPECT_LE(calls, 4);
}
