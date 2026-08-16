// The authoritative fighting-game simulation state.
//
// WHY THIS LIVES OUTSIDE Engine/. This header and its target deliberately link
// nothing -- no Jolt, no EnTT, no Lua, no GLM, no libm, not even the engine.
// That is docs/ARCHITECTURE.md D2 made structural: the standing temptation the
// plan names is "just ask the physics world for the hitbox", and against a
// separate target that is not a bad idea someone has to talk you out of, it is
// an unresolved symbol. See docs/ADR-002-open-decisions.md CHOICE D. The same
// trick is why the fast-math CI gate works: a violation is a build failure
// rather than a review miss.
//
// THE RULES THIS FILE OBEYS, all from ARCHITECTURE.md D2/D4/D8:
//   * Integers only. No float, no double, anywhere in this target. Cross-platform
//     bit-identity is then a property of integer arithmetic rather than of a
//     compiler flag, which is what makes Windows<->Linux crossplay (NORTHSTAR Q1)
//     cost nothing to guarantee.
//   * Positions and velocities are SUB-UNITS: 1 pixel = 256 sub-units. Not a
//     general Fixed type with operator overloads -- D2 rejects that explicitly,
//     because this game almost never multiplies two positions and the overload
//     set buys an overflow surface and a mirror-asymmetry bug for nothing.
//   * Fixed size, no pointers, no heap, trivially copyable. Snapshot is memcpy
//     and restore is memcpy back. test_kernel.cpp asserts all of this, so the
//     property cannot rot silently.
//   * No hidden state. Anything the simulation reads must live in GameState,
//     including the PRNG. A static counter or a cached global is a desync that
//     only appears when someone rolls back.
#pragma once

#include <cstdint>

namespace cse::kernel {

// 1 pixel = 256 sub-units. Chosen so a pixel divides evenly by 2, 4, 8 and 16 --
// halving a velocity is exact, which matters because friction and knockback are
// authored as halvings and a rounding difference is a desync.
inline constexpr std::int32_t kSubUnitsPerPixel = 256;

// Ticks per second. The simulation is tick-driven, never delta-time driven:
// Application's FixedTimestep is explicitly NOT allowed to drive this (it caps
// at 8 steps then ZEROES the accumulator, dropping the backlog -- see
// ARCHITECTURE.md's rejected-ideas table -- and a dropped tick in lockstep is an
// unrecoverable desync).
inline constexpr std::int32_t kTicksPerSecond = 60;

// One player's authoritative state. Field order is chosen for packing, not for
// reading pleasure: the whole struct is memcpy'd 128 times a second in the worst
// rollback case, and it is hashed for the desync checksum.
struct Fighter {
    std::int32_t posX;      // sub-units from stage centre, +X is stage right
    std::int32_t posY;      // sub-units above the floor; 0 is grounded
    std::int32_t velX;      // sub-units per tick
    std::int32_t velY;      // sub-units per tick
    std::int32_t health;
    std::int32_t meter;     // 1 unit = 10 MUGEN power (ADR-001 section 3, gate 2)

    std::uint16_t moveId;   // index into the character's move table; 0 = idle.
                            // The table is Combat.h's FighterData::moves, which
                            // is passed to Simulate rather than stored here --
                            // no tick writes it, and D4's snapshot is for what
                            // ticks write. See the long note at the top of
                            // Combat.h for why that split is exactly there.
    std::uint16_t moveFrame;// ticks since this move started; the tick it starts
                            // on is frame 0
    std::uint16_t hitstun;  // ticks remaining; 0 = actionable
    std::uint16_t blockstun;

    std::uint8_t facing;    // 0 = facing +X, 1 = facing -X. Not a sign multiplier:
                            // a sign would invite `pos * facing` and the
                            // mirror-asymmetry bug D2 warns about.
    std::uint8_t airborne;
    std::uint8_t comboHits; // drives hitstun decay; see ADR-001 on decay.floor

    // Which fighters the CURRENT active window has already hit: bit i is set
    // once this attack has connected on slot i. Cleared whenever a move starts
    // or ends, so it describes one active window and never a whole match.
    //
    // This is D4's `alreadyHitBits`, narrowed to the two fighter slots that
    // exist today. It is the guard against the oldest bug in the genre: a hitbox
    // that stays live for three frames deals its damage on all three, and every
    // string in the game becomes an infinite for a reason that has nothing to do
    // with the character. A bitmask rather than a bool because the defender is
    // already not the only possible target -- D4's 32 projectile slots each need
    // their own bit, and widening a bool later would be a state-layout change on
    // the wire.
    //
    // It occupies the byte that used to be Fighter::pad_. That byte existed so
    // the struct had no INDETERMINATE padding, which is what makes hashing the
    // object representation sound -- and the property is unchanged, because
    // these four uint8s still fill a whole 4-byte group and the struct is still
    // a multiple of its 4-byte alignment. Note the standing obligation: a FIFTH
    // uint8 added here must come with three more explicit bytes, or the compiler
    // inserts tail padding nobody initialises and the desync checksum starts
    // comparing two machines' uninitialised memory.
    std::uint8_t alreadyHitBits;
};

// The complete authoritative state. Everything the simulation may read.
struct GameState {
    std::uint32_t tick;
    std::uint32_t rng;      // snapshot-OWNED PRNG. Rolling back the state rolls
                            // back the random stream, which is the only way a
                            // re-simulated tick can reproduce its first run.
    Fighter p[2];
};

// One player's inputs for one tick. A bitfield rather than an enum set, because
// this is the thing sent over the wire every tick and re-read on every rollback.
struct Input {
    std::uint16_t bits;
};
inline constexpr std::uint16_t kInputUp     = 1u << 0;
inline constexpr std::uint16_t kInputDown   = 1u << 1;
inline constexpr std::uint16_t kInputLeft   = 1u << 2;
inline constexpr std::uint16_t kInputRight  = 1u << 3;
inline constexpr std::uint16_t kInputLP     = 1u << 4;
inline constexpr std::uint16_t kInputMP     = 1u << 5;
inline constexpr std::uint16_t kInputHP     = 1u << 6;
inline constexpr std::uint16_t kInputLK     = 1u << 7;
inline constexpr std::uint16_t kInputMK     = 1u << 8;
inline constexpr std::uint16_t kInputHK     = 1u << 9;

struct InputPair {
    Input p[2];
};

} // namespace cse::kernel
