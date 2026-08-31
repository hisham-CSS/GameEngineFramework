// COMBO VERDICTS BY EXECUTION (docs/adr/ADR-013, ROADMAP M1.4a+M1.4).
//
// The question the paper asks -- what can this character actually perform, and
// does any string run forever? -- answered by SEARCHING THE GAME: a bounded
// exploration over macro-actions ("ask for move M next"), each performed the
// way a player performs it and executed on the real kernel. The prover answers
// the same question about the FILE, soundly and conservatively; this is the
// executed counterpart its verdicts are compared against, one implementation
// for tests, cooker, showcase and panel.
//
// THE THREE RULES THAT MAKE A VERDICT MEAN SOMETHING:
//
// A MACRO-ACTION IS PERFORMED, NOT PROJECTED. Asking for M presses M's button
// with its stance-establishing hold, releases between repeats and re-presses
// on a stall -- WitnessCursor's rules, reused not restated -- and the KERNEL
// decides whether that becomes a cancel, a buffered link, a restart or a jump
// into an aerial. The search never re-implements a window; teaching a second
// model the kernel's arithmetic is what ADR-012 rule 4 exists to stop, and
// what test_gap_extent's section 3 died of.
//
// A STRING LIVES WHILE THE DEFENDER IS NEVER ACTIONABLE -- the direct reading
// off the stun clocks at the end of the previous tick, the same rule
// ComboWatcher and the gap sweep use. The defender is the silent training
// dummy, which is the recipe the ground-truth files themselves prescribe. A
// macro during which the defender becomes actionable ends the string; the
// path is recorded and pruned.
//
// AN INFINITE IS A STATE THAT RETURNS. Node keys are the state's checksum
// with tick, the RNG, both healths and the round fields masked out --
// everything else, positions and velocities and pushback and scaling and
// combo accumulators included, stays in the key, so "the key repeated along
// one path with the defender never free" really is induction: the same bytes
// meet the same inputs and continue identically, forever. The witness is the
// move sequence between the repeats, replayable by the cursor that found it.
//
// THE BUDGET ONLY EVER PRODUCES `Unresolved`. Exhausting the search space is
// TERMINATING; finding a returning state is INFINITE; running out of ticks or
// nodes is evidence of nothing and is never promoted.
#pragma once

#include "cse/kernel/Combat.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cse::game {

enum class ComboVerdict : std::uint8_t {
    Unresolved,    // the budget ran out first; not a verdict about the character
    Terminating,   // every string the search could form ends with the defender free
    Infinite,      // one path returned to its own state with the defender never free
};

struct ComboSearchRequest {
    // The BUILT match data; the attacker's move table is what is searched.
    const cse::kernel::MatchData* data = nullptr;

    // The opening state, placed by the caller -- "from here", exactly like a
    // demonstration. The ground-truth benches open in the corner because that
    // is the stage the shipped verdicts are computed for.
    cse::kernel::GameState from{};

    int attackerSlot = 0;

    // Budgets. Ticks is total SIMULATED ticks across the whole search; nodes
    // is connect-states expanded. Hitting either yields Unresolved, never a
    // verdict -- size them so the character resolves or say that it did not.
    // The defaults resolve the two shipped characters in seconds: a kernel
    // tick is integer arithmetic over 728 bytes, and two million of them cost
    // less than a renderer frame is worth of debugging.
    std::uint32_t maxTicks      = 2000000;
    std::uint32_t maxNodes      = 50000;
    // Per-macro bound: how long "ask for M" may take before it is abandoned.
    // A jump plus the longest move plus re-press slack fits comfortably.
    std::uint32_t maxMacroTicks = 120;
};

struct ComboSearchResult {
    ComboVerdict verdict = ComboVerdict::Unresolved;

    // INFINITE: the performed move sequence; entries from `loopStart` onward
    // are the cycle that returned to its own state, entries before it the
    // prefix that reached it. Kernel move slots, ready for a WitnessCursor.
    std::vector<std::uint16_t> witness;
    std::size_t                loopStart = 0;

    // The longest string measured (hits, and the moves that landed them) --
    // TERMINATING's headline, the executed counterpart of the prover's
    // maxHits. Also filled for the other verdicts with the best seen so far.
    std::int32_t               maxHits = 0;
    std::vector<std::uint16_t> longestString;

    std::uint32_t ticksUsed     = 0;
    std::uint32_t nodesExpanded = 0;
    std::string   note;   // one human sentence saying why this verdict
};

// Deterministic by construction: fixed expansion order (move slots ascending),
// integer state, no clock, no randomness -- the same request produces the same
// result on every machine, so a verdict can be a golden the way a hash can.
ComboSearchResult RunComboSearch(const ComboSearchRequest& request);

} // namespace cse::game
