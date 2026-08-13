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

void stepFighter(Fighter& f, Input in) {
    // Stun burns down first, and while it does the player has no agency.
    if (f.hitstun   > 0) --f.hitstun;
    if (f.blockstun > 0) --f.blockstun;

    if (actionable(f)) {
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

    if (f.moveId != 0) {
        ++f.moveFrame;
    }
}

} // namespace

void Simulate(GameState& state, const InputPair& inputs) {
    // Fixed order, always. Iterating a container whose order can vary -- the
    // hash-ordering hazard that bit SimplePhysicsBackend and ScriptWorld in this
    // repository -- is how a simulation stops being deterministic without
    // anybody changing the arithmetic.
    stepFighter(state.p[0], inputs.p[0]);
    stepFighter(state.p[1], inputs.p[1]);

    // Facing is derived from relative position, evaluated AFTER both have moved
    // so it cannot depend on which player was stepped first.
    if (state.p[0].posX <= state.p[1].posX) {
        state.p[0].facing = 0;
        state.p[1].facing = 1;
    } else {
        state.p[0].facing = 1;
        state.p[1].facing = 0;
    }

    // Advance the stream every tick whether or not anything consumed it, so the
    // RNG position is a function of the tick count alone. A stream that advances
    // only on some ticks makes the sequence depend on gameplay history, which is
    // still deterministic but far harder to reason about when a desync appears.
    nextRandom(state.rng);

    ++state.tick;
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
