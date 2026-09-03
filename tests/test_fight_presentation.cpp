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


// ============================================================================
// The clip table (ROADMAP M3.4b; ADR-019 D9)
// ============================================================================

#include "cse/presentation/FighterClips.h"

using cse::data::CharacterData;
using cse::data::ClipLength;
using cse::data::Move;
using cse::game::PoseKind;
using cse::presentation::ClipRef;
using cse::presentation::FighterClips;
using cse::presentation::MoveSlot;

namespace {

Move moveOf(const char* id, int startup, int active, int recovery, const char* clip = "") {
    Move m{};
    m.id = id;
    m.startup = startup;
    m.active = active;
    m.recovery = recovery;
    m.anim3dClip = clip;
    return m;
}

// A character as CseData would hand it over after A21/A22 passed: three moves,
// one with a clip override, and every reserved cycle in the sidecar.
CharacterData threeMoveCharacter() {
    CharacterData c{};
    c.anim3dModel = "Characters/fighter_a/model/fighter_a.gltf";
    c.moves = { moveOf("a", 3, 2, 9), moveOf("b", 4, 2, 10), moveOf("c", 5, 3, 12, "c_alt") };
    c.anim3dClips = { { "a", 14 }, { "b", 16 }, { "c_alt", 20 } };
    std::int32_t n = 4;
    for (const char* cycle : cse::data::kReservedCycleNames) c.anim3dClips.push_back({ cycle, n++ });
    c.RebuildIndices();
    return c;
}

} // namespace

// BuildMatchData numbers moves in file order, so a reload that reorders them
// renumbers every slot. The table is keyed by slot for the hot path but BOUND
// by id, so after a reorder each slot still wears its own move's clip.
TEST(FighterClips, AReloadThatReordersMovesRebindsEveryClipByName) {
    const CharacterData c = threeMoveCharacter();
    FighterClips clips;

    clips.Rebuild(c, { { 1, "a" }, { 2, "b" }, { 3, "c" } });
    ASSERT_EQ(clips.MoveClipCount(), 3u);
    ASSERT_NE(clips.Find(PoseKind::Move, 1), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "a");
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->frames, 14u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->name, "c_alt") << "the per-move override names the clip";
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->frames, 20u);

    // the reload: c moved to the front, everything renumbered
    clips.Rebuild(c, { { 1, "c" }, { 2, "a" }, { 3, "b" } });
    ASSERT_EQ(clips.MoveClipCount(), 3u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "c_alt") << "slot 1 is c now; a table keyed by the old slots would say a";
    EXPECT_EQ(clips.Find(PoseKind::Move, 2)->name, "a");
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->name, "b");
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->frames, 16u);

    // a move this build did not give a slot has no clip; slot 0 never does
    clips.Rebuild(c, { { 0, "a" }, { 1, "b" } });
    EXPECT_EQ(clips.MoveClipCount(), 1u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 0), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Move, 2), nullptr) << "no slot 2 was handed in";
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "b");
}

// The reserved cycles are keyed by kind alone and are the same fourteen names
// in the same order as the loader's list; None and Move are never cycles.
TEST(FighterClips, EveryReservedCycleIsFoundByItsKind) {
    const CharacterData c = threeMoveCharacter();
    FighterClips clips;
    clips.Rebuild(c, { { 1, "a" } });
    for (std::size_t i = 0; i < cse::data::kReservedCycleNames.size(); ++i) {
        const PoseKind kind = static_cast<PoseKind>(static_cast<int>(PoseKind::Idle) + static_cast<int>(i));
        const ClipRef* r = clips.Find(kind, 0);
        ASSERT_NE(r, nullptr) << cse::data::kReservedCycleNames[i];
        EXPECT_EQ(r->name, cse::data::kReservedCycleNames[i]);
        EXPECT_EQ(r->frames, 4u + static_cast<std::uint32_t>(i));
    }
    EXPECT_EQ(clips.Find(PoseKind::None, 0), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Idle, 7), clips.Find(PoseKind::Idle, 0)) << "a cycle ignores the move slot";
}

// Off by default: a character without engine.anim3d.model has no table, so the
// mode keeps drawing the 2D placeholders and asks nothing of a renderer.
TEST(FighterClips, ACharacterWithNoModelHasAnEmptyTable) {
    CharacterData c = threeMoveCharacter();
    c.anim3dModel.clear();
    FighterClips clips;
    clips.Rebuild(c, { { 1, "a" }, { 2, "b" }, { 3, "c" } });
    EXPECT_TRUE(clips.Empty());
    EXPECT_EQ(clips.Find(PoseKind::Move, 1), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Idle, 0), nullptr);
}
