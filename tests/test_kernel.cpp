// The gameplay kernel: the structural properties rollback depends on, and the
// determinism that makes crossplay free.
//
// These tests are unusual in that most of them assert about the TYPE rather than
// about behaviour. That is deliberate. ARCHITECTURE.md D4 makes the snapshot a
// memcpy, and a memcpy snapshot is correct only while GameState stays trivially
// copyable, pointer-free and fixed-size. Those are properties a well-meaning
// change breaks silently -- adding a std::string for a debug name, or a
// std::vector of active projectiles, compiles and passes every gameplay test and
// then corrupts a rollback. So they are asserted at compile time, where the
// failure lands on the person who wrote the line.
//
// This test links CseKernel and NOT the Engine, on purpose. If it ever needs the
// Engine to build, the kernel has stopped being independent and the whole
// argument in ADR-002 CHOICE D has quietly failed.
#include <gtest/gtest.h>

#include "cse/kernel/Simulate.h"

#include <cstring>
#include <type_traits>
#include <vector>

using namespace cse::kernel;

// --- The properties the memcpy snapshot rests on ---------------------------

static_assert(std::is_trivially_copyable_v<GameState>,
              "GameState must be trivially copyable: the rollback snapshot is a "
              "memcpy (ARCHITECTURE.md D4). Adding a std::string, std::vector, "
              "or any type with a user-provided copy constructor breaks rollback "
              "without breaking the build anywhere else.");

static_assert(std::is_standard_layout_v<GameState>,
              "GameState must be standard-layout: Checksum() hashes its object "
              "representation, and the desync detector compares those hashes "
              "across two machines built by two different compilers.");

static_assert(std::is_trivially_copyable_v<Fighter>, "see GameState");
static_assert(std::is_trivially_default_constructible_v<GameState>,
              "GameState must stay an aggregate -- adding a constructor is what "
              "would make it non-trivially-copyable.");

// No hidden indirection. A pointer inside the state would survive the memcpy as
// a dangling address, which is the worst possible failure: it works locally and
// corrupts on the remote peer.
// The header is tick, rng and roundTimer (uint32); roundNumber (uint16); and six
// bytes -- roundState, fighterCount, roundsWon[2], roundsToWin and one explicit
// pad. Written as a sum of the members rather than as a byte count so it keeps
// asking "did padding appear" rather than "is this the number I last wrote down".
static_assert(sizeof(GameState) == sizeof(std::uint32_t) * 3 +
                                       sizeof(std::uint16_t) +
                                       sizeof(std::uint8_t) * 6 +
                                       sizeof(Fighter) * kMaxFighters,
              "GameState has grown a member that is not accounted for, or the "
              "compiler inserted padding between the header and the fighters. "
              "Either way the byte layout changed and the checksum is no longer "
              "comparable to a peer built before the change.");

TEST(KernelLayout, StateIsSmallEnoughToSnapshotEveryTick) {
    // ARCHITECTURE.md D4 sizes the ring at 128 slots. The absolute number matters
    // less than it staying in this order of magnitude: the design's claim is that
    // a rollback of 8 ticks costs a few hundred microseconds, and that claim is a
    // function of this number.
    EXPECT_LT(sizeof(GameState), 4096u)
        << "GameState is " << sizeof(GameState) << " bytes. The 128-slot ring and "
           "the sub-millisecond rollback budget assume it stays small.";
}

// --- Determinism ------------------------------------------------------------

namespace {

// A scripted input sequence with enough variety to exercise every branch in
// stepFighter: walking both directions, a jump and its whole arc, and idle.
std::vector<InputPair> scriptedMatch(int ticks) {
    std::vector<InputPair> seq;
    seq.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        InputPair in{};
        if (t % 7 == 0)  in.p[0].bits |= kInputRight;
        if (t % 11 == 0) in.p[0].bits |= kInputUp;
        if (t % 5 == 0)  in.p[1].bits |= kInputLeft;
        if (t % 13 == 0) in.p[1].bits |= kInputLP;
        seq.push_back(in);
    }
    return seq;
}

GameState runFrom(const GameState& start, const std::vector<InputPair>& seq,
                  std::size_t from, std::size_t to) {
    GameState s = start;
    for (std::size_t i = from; i < to; ++i) Simulate(s, seq[i]);
    return s;
}

} // namespace

TEST(KernelDeterminism, SameInputsProduceByteIdenticalState) {
    const auto seq = scriptedMatch(600);

    GameState a{}, b{};
    ResetMatch(a, 12345u);
    ResetMatch(b, 12345u);

    for (const auto& in : seq) Simulate(a, in);
    for (const auto& in : seq) Simulate(b, in);

    // memcmp, not field-by-field: field comparison would silently pass if someone
    // added a member and forgot to compare it, and the network only ever sees
    // bytes.
    EXPECT_EQ(0, std::memcmp(&a, &b, sizeof(GameState)))
        << "Two runs of the same 600 inputs from the same seed diverged. The "
           "simulation is reading something outside GameState.";
    EXPECT_EQ(Checksum(a), Checksum(b));
}

TEST(KernelDeterminism, DifferentSeedsDiverge) {
    // Guards against the vacuous version of the test above: if Simulate ignored
    // the RNG entirely, SameInputsProduceByteIdenticalState would pass no matter
    // what. This asserts the seed is actually part of the state.
    const auto seq = scriptedMatch(60);
    GameState a{}, b{};
    ResetMatch(a, 1u);
    ResetMatch(b, 2u);
    for (const auto& in : seq) Simulate(a, in);
    for (const auto& in : seq) Simulate(b, in);
    EXPECT_NE(Checksum(a), Checksum(b));
}

// --- Rollback ---------------------------------------------------------------

TEST(KernelRollback, ResimulatingFromASnapshotReproducesTheStraightRun) {
    // This is the property the entire netcode rests on. Run 300 ticks straight
    // through. Separately, snapshot at tick 200, roll back, and re-simulate the
    // last 100. The two must be byte-identical -- that is what lets a client
    // predict, receive a correction, and rewind without the players seeing a
    // difference.
    const auto seq = scriptedMatch(300);

    GameState start{};
    ResetMatch(start, 0xC0FFEEu);

    const GameState straight = runFrom(start, seq, 0, 300);

    GameState snapshot = runFrom(start, seq, 0, 200);
    const GameState restored = snapshot;               // the "snapshot" is a copy
    const GameState resimulated = runFrom(restored, seq, 200, 300);

    EXPECT_EQ(0, std::memcmp(&straight, &resimulated, sizeof(GameState)))
        << "Re-simulating from a restored snapshot did not reproduce the "
           "straight-through run. Rollback netcode is impossible until this "
           "passes.";
    EXPECT_EQ(Checksum(straight), Checksum(resimulated));
}

TEST(KernelRollback, EightTickRewindIsExactAtEveryDepth) {
    // The plan budgets up to 8 ticks of re-simulation per rollback event. Check
    // every depth from 1 to 8 rather than only the deepest: an off-by-one in a
    // future prediction loop shows up at one specific depth.
    const auto seq = scriptedMatch(120);
    GameState start{};
    ResetMatch(start, 7u);

    for (std::size_t depth = 1; depth <= 8; ++depth) {
        const GameState straight = runFrom(start, seq, 0, 100);
        const GameState mid      = runFrom(start, seq, 0, 100 - depth);
        const GameState redone   = runFrom(mid, seq, 100 - depth, 100);
        EXPECT_EQ(0, std::memcmp(&straight, &redone, sizeof(GameState)))
            << "rewind depth " << depth << " diverged";
    }
}

TEST(KernelRollback, ChecksumDetectsASingleBitOfDivergence) {
    // ADR-002 CHOICE C: exchange a 4-byte checksum every 8 ticks, and on
    // mismatch STOP the match and name the frame. That is only useful if the
    // checksum actually notices a minimal difference.
    GameState a{};
    ResetMatch(a, 99u);
    GameState b = a;
    b.p[1].posY += 1;                     // one sub-unit: 1/256th of a pixel
    EXPECT_NE(Checksum(a), Checksum(b))
        << "The desync checksum missed a one-sub-unit difference.";
}

// --- The state is genuinely self-contained ----------------------------------

TEST(KernelPurity, SimulationReadsNothingOutsideTheState) {
    // If Simulate consulted a clock, a global, or a static counter, then running
    // the same tick from the same state at two different points in the process's
    // life would differ. Interleave two independent matches to make any shared
    // mutable state show up.
    const auto seq = scriptedMatch(50);

    GameState solo{};
    ResetMatch(solo, 4242u);
    for (const auto& in : seq) Simulate(solo, in);

    GameState interleaved{}, noise{};
    ResetMatch(interleaved, 4242u);
    ResetMatch(noise, 777u);
    for (std::size_t i = 0; i < seq.size(); ++i) {
        Simulate(interleaved, seq[i]);
        Simulate(noise, seq[i]);          // a second match sharing the process
    }

    EXPECT_EQ(0, std::memcmp(&solo, &interleaved, sizeof(GameState)))
        << "Running a second match alongside changed the first one's result. "
           "Something in the kernel is not per-state.";
}

TEST(KernelPurity, ResetMatchFullyClearsPriorState) {
    // Scene-swap state that is never reset has bitten this repository before
    // (see docs/MAINTENANCE.md). A match that starts differently depending on
    // what the last match did is the same bug with a worse blast radius.
    GameState dirty{};
    ResetMatch(dirty, 5u);
    const auto seq = scriptedMatch(200);
    for (const auto& in : seq) Simulate(dirty, in);

    GameState reused = dirty;
    ResetMatch(reused, 5u);

    GameState fresh{};
    ResetMatch(fresh, 5u);

    EXPECT_EQ(0, std::memcmp(&reused, &fresh, sizeof(GameState)))
        << "ResetMatch left something from the previous match behind.";
}
