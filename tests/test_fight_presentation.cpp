// THE PRESENTATION LIBRARY, LINKED WITH NO WINDOW (ROADMAP M3.4a; ADR-019 D9).
//
// This executable exists twice over. Once for what it asserts -- the stateless
// cycle phases floor rather than truncate, so a fighter left of stage centre
// never hands the sampler a negative frame -- and once for what it PROVES BY
// LINKING: UntitledFighterPresentation builds and links into a test with no GL
// context in every CI job, before M3.4b-c put the clip table, the matrices and
// the P4 acceptance tests into it. Five later Done-whens ride on this link; the
// plan's critic asked for it to be shown rather than assumed.
#include <gtest/gtest.h>

#include "cse/presentation/CycleFrame.h"

#include <cstdint>

using cse::presentation::CycleFrame;
using cse::presentation::FloorDiv;
using cse::presentation::WalkCycleFrame;

// Half the stage is negative. `%` truncates toward zero, so the naive index goes
// 2, 1, 0, -1, -2 across centre and hands a sampler a frame that does not exist.
TEST(CycleFrame, FloorModKeepsANegativePositionOnTheCycle) {
    constexpr std::uint32_t n = 8;
    EXPECT_EQ(CycleFrame(0, n), 0u);
    EXPECT_EQ(CycleFrame(7, n), 7u);
    EXPECT_EQ(CycleFrame(8, n), 0u);
    EXPECT_EQ(CycleFrame(-1, n), 7u) << "one step left of zero must be the LAST frame, never -1";
    EXPECT_EQ(CycleFrame(-8, n), 0u);
    EXPECT_EQ(CycleFrame(-9, n), 7u);

    // Walking across stage centre: contiguous frames, no jump and no negative.
    std::uint32_t prev = CycleFrame(-3, n);
    for (std::int64_t phase = -2; phase <= 3; ++phase) {
        const std::uint32_t now = CycleFrame(phase, n);
        EXPECT_LT(now, n);
        EXPECT_EQ(now, (prev + 1) % n) << "phase " << phase << " skipped a frame";
        prev = now;
    }
}

TEST(CycleFrame, AZeroLengthCycleHasExactlyOneFrame) {
    EXPECT_EQ(CycleFrame(0, 0), 0u);
    EXPECT_EQ(CycleFrame(12345, 0), 0u);
    EXPECT_EQ(CycleFrame(-12345, 0), 0u);
}

TEST(CycleFrame, FloorDivRoundsTowardNegativeInfinity) {
    EXPECT_EQ(FloorDiv(7, 2), 3);
    EXPECT_EQ(FloorDiv(-7, 2), -4) << "truncation would say -3";
    EXPECT_EQ(FloorDiv(-8, 2), -4);
    EXPECT_EQ(FloorDiv(-1, 256), -1) << "one sub-unit left of centre is already stride -1";
    EXPECT_EQ(FloorDiv(0, 256), 0);
    EXPECT_EQ(FloorDiv(255, 256), 0);
}

// The walk keys on posX so the feet do not slide: one frame per stride of ground
// covered. Crossing centre from -1 to +1 sub-unit must read n-1, 0, 0.
TEST(CycleFrame, TheWalkCycleCrossesStageCentreWithoutAJump) {
    constexpr std::int32_t  stride = 256;   // one kernel pixel
    constexpr std::uint32_t n      = 6;
    EXPECT_EQ(WalkCycleFrame(-1, stride, n), n - 1);
    EXPECT_EQ(WalkCycleFrame(0, stride, n), 0u);
    EXPECT_EQ(WalkCycleFrame(1, stride, n), 0u);
    EXPECT_EQ(WalkCycleFrame(256, stride, n), 1u);
    EXPECT_EQ(WalkCycleFrame(-256, stride, n), n - 1);
    EXPECT_EQ(WalkCycleFrame(-257, stride, n), n - 2);
    EXPECT_EQ(WalkCycleFrame(1000, 0, n), 0u) << "no stride authored means frame 0, never a divide by zero";
    EXPECT_EQ(WalkCycleFrame(1000, -5, n), 0u);
}
