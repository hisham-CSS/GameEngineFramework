// The simulation entry points. This is the whole public surface of the kernel:
// advance one tick, hash a state, seed a match.
//
// `Simulate` is a PURE FUNCTION of (state, inputs). It reads no clock, no
// filesystem, no globals, and no RNG other than the one inside GameState. That
// is what makes rollback possible at all -- re-running tick N with the same
// inputs must produce byte-identical output, on both machines, on every replay.
#pragma once

#include "Combat.h"
#include "GameState.h"

namespace cse::kernel {

// Advance the simulation by exactly one tick.
//
// Deterministic by construction: same (state, inputs, data) in, same bytes out,
// on every platform this compiles for. test_kernel.cpp proves it by running a
// sequence twice and memcmp'ing, and by the rollback round-trip that
// re-simulates a resumed prefix and compares against the straight-through run.
//
// `data` is the read-only character data both fighters are fought with. It is a
// PARAMETER rather than part of GameState because no tick writes it -- see the
// note at the top of Combat.h, which is where that decision is argued. Purity is
// unaffected: the data cannot change during a match, so a re-simulated tick
// reads the same bytes it read the first time. What it does mean is that a
// GameState alone no longer describes a match, and the connect handshake
// (ARCHITECTURE.md 4.8) is what proves both peers hold the same MatchData.
void Simulate(GameState& state, const InputPair& inputs, const MatchData& data);

// The same tick with no characters loaded: nothing can start a move, no box can
// be live, and nothing can hit. Provided so that a caller with no character data
// yet gets EXACTLY the pre-hitbox kernel rather than a subtly different one --
// which is also what keeps the cross-toolchain golden hashes in
// test_determinism_crossplat.cpp meaningful across this change.
void Simulate(GameState& state, const InputPair& inputs);

// Put a match at its opening position. Separate from a constructor because
// GameState must stay an aggregate with no user-provided constructors -- that is
// what keeps it trivially copyable, and trivially copyable is what makes the
// snapshot a memcpy.
//
// The setup form is the general one: per-slot team, active flag, start position
// and start health, plus round length and set length. It is what makes a fight
// startable from something other than "both fighters at +/-100 pixels with 1000
// health", which every fight in this game was, because the only way to start one
// said so. See docs/ADR-009 section 5.
void ResetMatch(GameState& state, const MatchSetup& setup);

// The 1v1 opening this project shipped with, as data. Exposed rather than hidden
// inside ResetMatch so a caller can take it and change one field -- a handicap, a
// cornered story fight, a longer round -- without restating the parts it does not
// care about.
MatchSetup DefaultMatchSetup(std::uint32_t seed);

// The seed-only form, kept because most callers and nearly every test want
// exactly the default match. It is IMPLEMENTED in terms of the setup form, so
// the default and the general path cannot drift apart.
void ResetMatch(GameState& state, std::uint32_t seed);

// FNV-1a over the raw bytes. This is the desync checksum from
// ADR-002 CHOICE C -- exchanged every 8 ticks, and on mismatch the match STOPS
// and names the frame rather than silently resyncing.
//
// Hashing raw bytes is only sound because GameState has no padding holes with
// indeterminate values (Fighter::pad_ is explicit) and no pointers. The static
// asserts in test_kernel.cpp are what keep that true.
std::uint32_t Checksum(const GameState& state);

} // namespace cse::kernel
