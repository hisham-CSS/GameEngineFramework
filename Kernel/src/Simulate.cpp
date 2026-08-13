#include "cse/kernel/Simulate.h"

// <cstring> only. No <cmath>, no <algorithm> that would drag in a float
// overload, nothing from the engine. If this include list ever grows something
// that pulls libm, the kernel has stopped being portable-by-arithmetic and the
// crossplay guarantee (NORTHSTAR Q1) goes with it.
#include <cstring>

namespace cse::kernel {
namespace {

// Tuning constants. Sub-units per tick. These are placeholders for values that
// will come from character data once ADR-001's schema v2 lands -- the point of
// this file today is the SHAPE (integer, pure, snapshot-able), not the balance.
constexpr std::int32_t kWalkSpeed   = 2 * kSubUnitsPerPixel;      //  2 px/tick
constexpr std::int32_t kGravity     = kSubUnitsPerPixel / 4;      // .25 px/tick^2
constexpr std::int32_t kJumpImpulse = 5 * kSubUnitsPerPixel;
constexpr std::int32_t kStageHalfWidth = 480 * kSubUnitsPerPixel;

// A player is actionable when nothing is holding them still.
bool actionable(const Fighter& f) {
    return f.hitstun == 0 && f.blockstun == 0;
}

// Integer-only clamp. Deliberately not std::clamp: that would be fine here, but
// keeping <algorithm> out of this translation unit removes an entire category of
// "someone reaches for std::max and gets the double overload" accident.
std::int32_t clampInt(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// xorshift32. Chosen over anything from <random> because the standard library's
// engines are not specified to produce identical sequences across
// implementations, and libstdc++ and the MSVC STL genuinely differ. Nine lines
// we own beats a portability question we cannot close.
std::uint32_t nextRandom(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

void stepFighter(Fighter& f, Input in, const FighterData& data) {
    // Stun burns down first, and while it does the player has no agency.
    if (f.hitstun   > 0) --f.hitstun;
    if (f.blockstun > 0) --f.blockstun;

    const bool canAct = actionable(f);

    if (canAct) {
        std::int32_t wish = 0;
        if (in.bits & kInputLeft)  wish -= kWalkSpeed;
        if (in.bits & kInputRight) wish += kWalkSpeed;
        f.velX = wish;

        if ((in.bits & kInputUp) && !f.airborne) {
            f.velY    = kJumpImpulse;
            f.airborne = 1;
        }
    } else {
        f.velX = 0;
    }

    if (f.airborne) {
        f.velY -= kGravity;
    }

    f.posX = clampInt(f.posX + f.velX, -kStageHalfWidth, kStageHalfWidth);
    f.posY += f.velY;

    if (f.posY <= 0) {
        f.posY     = 0;
        f.velY     = 0;
        f.airborne = 0;
    }

    // The attack lifecycle: end a move that has run out, CANCEL one that is still
    // running into the follow-up the fighter is asking for, or start one from
    // idle. It replaces the bare `if (moveId != 0) ++moveFrame;` this file used to
    // end on, and for a moveId this character's table does not describe it does
    // exactly that and nothing more -- which is what keeps a state driven by a
    // harness behaving as it did before boxes existed.
    //
    // It runs AFTER movement so that a move started this tick sees the position
    // the fighter actually reached, and BEFORE hit resolution, which happens once
    // for both fighters below.
    //
    // THAT SECOND ORDERING IS WHAT SETS THE FASTEST CANCEL. A cancel is gated on
    // the source having connected, and connecting is decided by ResolveHits at
    // the bottom of this tick -- so a hit landing on tick N is first visible to a
    // cancel test on tick N+1, and an edge with an authored delay of zero fires
    // one tick after contact rather than on it. Moving the cancel test below
    // ResolveHits would buy that tick back and cost something much worse: a
    // fighter could then cancel a move on the very tick it started.
    StepAttack(f, data, in, canAct);
}

} // namespace

void Simulate(GameState& state, const InputPair& inputs, const MatchData& data) {
    // Fixed order, always. Iterating a container whose order can vary -- the
    // hash-ordering hazard that bit SimplePhysicsBackend and ScriptWorld in this
    // repository -- is how a simulation stops being deterministic without
    // anybody changing the arithmetic.
    stepFighter(state.p[0], inputs.p[0], data.p[0]);
    stepFighter(state.p[1], inputs.p[1], data.p[1]);

    // Facing is derived from relative position, evaluated AFTER both have moved
    // so it cannot depend on which player was stepped first.
    if (state.p[0].posX <= state.p[1].posX) {
        state.p[0].facing = 0;
        state.p[1].facing = 1;
    } else {
        state.p[0].facing = 1;
        state.p[1].facing = 0;
    }

    // Hits are resolved after facing, because every box is authored facing +X
    // and mirrored by facing when it is placed -- so a box built before facing
    // settled would be the box the fighter had LAST tick. One tick of a stale
    // mirror is a hit that lands behind a character who just turned around.
    ResolveHits(state, data);

    // Advance the stream every tick whether or not anything consumed it, so the
    // RNG position is a function of the tick count alone. A stream that advances
    // only on some ticks makes the sequence depend on gameplay history, which is
    // still deterministic but far harder to reason about when a desync appears.
    nextRandom(state.rng);

    ++state.tick;
}

void Simulate(GameState& state, const InputPair& inputs) {
    // kNoMoves has moveCount 0 and a degenerate hurtbox, so StepAttack can start
    // nothing, ActiveHitbox can return nothing, and ResolveHits can find no
    // overlap. This overload is therefore the pre-hitbox kernel exactly, not
    // approximately -- which is the point, because the cross-toolchain golden
    // hashes were recorded against it and re-recording a golden is how the
    // evidence that two platforms agree gets destroyed.
    Simulate(state, inputs, kNoMoves);
}

void ResetMatch(GameState& state, std::uint32_t seed) {
    std::memset(&state, 0, sizeof(GameState));

    // Never zero: xorshift is absorbing at zero and would return 0 forever.
    state.rng = seed != 0u ? seed : 0x9E3779B9u;

    state.p[0].posX   = -100 * kSubUnitsPerPixel;
    state.p[1].posX   =  100 * kSubUnitsPerPixel;
    state.p[0].health = 1000;
    state.p[1].health = 1000;
    state.p[0].facing = 0;
    state.p[1].facing = 1;
}

std::uint32_t Checksum(const GameState& state) {
    // Reading the object representation through unsigned char is the one aliasing
    // route the standard actually blesses.
    const auto* bytes = reinterpret_cast<const unsigned char*>(&state);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < sizeof(GameState); ++i) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

} // namespace cse::kernel
