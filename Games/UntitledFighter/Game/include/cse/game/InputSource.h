// Where a tick's bits come from.
//
// This is the smallest header in the module and the one everything else is
// arranged around. A fight is a fold over inputs: given the initial conditions
// and the bits for tick 0..N, the whole match is determined. So "where the bits
// come from" is the only degree of freedom the Game module has, and three very
// different things have to fit through it without the session growing a branch
// for any of them:
//
//   LOCAL     a human on a keyboard or a pad. Lives ABOVE Engine and is not in
//             this library -- but LatchedInputSource below is its whole
//             implementation minus the hardware read, so the Engine-side file
//             is a poll and one call.
//   SCRIPTED  the tool-assisted player. A fixed list of per-tick bits, which is
//             how the combo prover's printed loop gets performed perfectly.
//   REPLAY    a loaded replay file (ReplayInputSource, declared in Replay.h so
//             that a host with no interest in files never sees the format).
//
// ---------------------------------------------------------------------------
// THE SIGNATURE IS `At(tick)` AND THAT IS THE WHOLE DESIGN
// ---------------------------------------------------------------------------
// The obvious interface is `Input Next()` -- a stream you pull from. It is
// wrong here, and it is wrong for the reason this entire project exists.
//
// A rollback re-simulates ticks it has already run. A replay scrubber seeks. A
// desync investigation re-runs tick 900 from a snapshot at tick 880. Every one
// of those asks the SAME QUESTION TWICE and must get the same answer, and a
// stream cannot promise that: `Next()` is a function of how many times it has
// been called, which is a function of the path taken through the match rather
// than of the match itself. The bug that produces is the worst kind -- the
// second run of tick 900 differs from the first, the checksum diverges, and the
// evidence points at the simulation, which is innocent.
//
// So a source is a PURE FUNCTION OF AN ABSOLUTE TICK INDEX. `At(t)` must return
// the same bytes every time it is called for the same `t`, for the whole life
// of the source. No source may return one thing on the first ask and another on
// the second. This is not a performance note or a style preference; it is the
// property that makes seeking, rollback and replay the same operation.
//
// The one implementation that cannot be pure by construction is the local pad,
// and it is made pure by LATCHING (see LatchedInputSource): the hardware is
// read exactly once per tick and the value is written down forever. Latching is
// a concrete detail of that one class and deliberately NOT a method on this
// interface -- an interface with a mutating `Latch()` on it is an interface
// every implementation has to no-op, and an interface every CALLER has to
// remember to drive in the right order.
//
// ---------------------------------------------------------------------------
// RUNNING OUT IS A NORMAL STATE, NOT AN ERROR
// ---------------------------------------------------------------------------
// "Demonstrate performs the loop and then hands control back so the playtester
// can try it themselves" is a product requirement, and it is exactly a scripted
// source reaching the end of its trace. So exhaustion is expressed as DATA --
// `InputSample::authored` goes false -- and never as a failure, a sentinel bit
// pattern or an out-of-band flag. Neutral input (bits 0) is a perfectly good
// thing to author and must not be confused with authoring nothing, which is why
// the sample carries a separate bool rather than overloading zero.
//
// What happens to an unauthored tick is FightSession's decision and is stated
// there; the composition that implements "hand control back" is
// FallbackInputSource below.
#pragma once

#include "cse/kernel/GameState.h"

#include <cstdint>
#include <vector>

namespace cse::game {

// One hour at 60 Hz. The bound every per-tick container in this module is sized
// against: a latched local source, a scripted trace, a decoded replay. It is a
// hard cap rather than a suggestion, because each of those is either a
// long-running accumulation or a count read out of an untrusted file, and D4's
// rule about unbounded growth applies to both.
//
// 216000 ticks is 432 KB of Input and 864 KB of InputPair, so the cap is chosen
// to make the worst case obviously affordable rather than to be generous.
inline constexpr std::uint32_t kMaxMatchTicks = 60u * 60u * 60u;

// Returned by AuthoredEndTick() for a source that never runs out of script --
// today only LatchedInputSource, which authors whatever it is handed. Chosen as
// the largest uint32 so that `tick < AuthoredEndTick()` is the exhaustion test
// for every source with no special case.
inline constexpr std::uint32_t kUnboundedTick = 0xFFFFFFFFu;

// One tick's answer from one source, for one player.
//
// `authored` false means "this source has no opinion about that tick" -- before
// a scripted trace begins, after it ends, past the end of a replay, or ahead of
// what a local pad has latched. `input` is then required to be zeroed, so that a
// caller who ignores the flag gets neutral rather than stale bits. That is a
// deliberate belt-and-braces: the flag is the contract, and the zero is what
// keeps a missed check from being a heisenbug.
struct InputSample {
    cse::kernel::Input input{};
    bool               authored = false;
};

// The seam.
//
// IMPLEMENTOR'S CONTRACT, all of it load-bearing:
//   * `At` is CONST, PURE and TOTAL. Same tick in, same bytes out, always. No
//     clock, no rand, no float, no filesystem, no logging, no allocation, and
//     no throw -- it is called once per player per tick and again for every
//     tick a rollback re-simulates.
//   * `At` must answer for ANY uint32, including 0, including kUnboundedTick,
//     including ticks far past the end. Out of range is `authored = false`, not
//     an assert and not an index off the end of a vector.
//   * `AuthoredEndTick` is one past the last tick this source can EVER author,
//     or kUnboundedTick. It is a bound, not a promise that every tick below it
//     is authored -- a source may have holes, and FightSession treats a hole the
//     same as the end. It exists so a host can tell that a demonstration is over
//     without polling, and so a HUD can draw a progress bar.
//   * `Name` is a short stable literal for the HUD and for diagnostics ("DEMO",
//     "YOU", "REPLAY"). It must not allocate and must not change between calls.
class IInputSource {
public:
    virtual ~IInputSource() = default;

    IInputSource(const IInputSource&)            = delete;
    IInputSource& operator=(const IInputSource&) = delete;

    virtual InputSample   At(std::uint32_t tick) const = 0;
    virtual std::uint32_t AuthoredEndTick() const      = 0;
    virtual const char*   Name() const                 = 0;

    // Convenience, non-virtual so it cannot be overridden into disagreeing with
    // At(). A source is exhausted at `tick` when it authors nothing there.
    bool Exhausted(std::uint32_t tick) const { return !At(tick).authored; }

protected:
    IInputSource() = default;
};

// --- SCRIPTED: the tool-assisted player -------------------------------------

// A fixed list of per-tick bits, beginning at an ABSOLUTE tick.
//
// WHY ABSOLUTE AND NOT RELATIVE. The playtester presses Demonstrate at some tick
// in the middle of a session that is already running. A trace numbered from zero
// would force either a match reset (which is not what "show me this combo" means
// when the player is standing somewhere specific) or an offset stored outside
// the source (which is a second thing to keep in sync, and the first thing to be
// forgotten by a seek). Carrying `firstTick` inside the source keeps `At` a pure
// function of the session's own tick index, which is the property the whole
// interface exists for.
//
// Authors [firstTick, firstTick + inputs.size()). Before and after: nothing.
//
// PRECONDITION: firstTick + inputs.size() <= kMaxMatchTicks. The constructor
// clamps the trace rather than trusting it, and Truncated() reports that it did;
// a source that silently kept a trace running past the tick cap would be the one
// container in this module without a bound.
class ScriptedInputSource final : public IInputSource {
public:
    ScriptedInputSource(std::vector<cse::kernel::Input> inputs,
                        std::uint32_t                   firstTick,
                        const char*                     name = "SCRIPT");

    InputSample   At(std::uint32_t tick) const override;
    std::uint32_t AuthoredEndTick() const override;
    const char*   Name() const override;

    std::uint32_t FirstTick() const;
    std::uint32_t TickCount() const;
    bool          Truncated() const;

private:
    std::vector<cse::kernel::Input> inputs_;
    std::uint32_t                   firstTick_ = 0;
    const char*                     name_      = "SCRIPT";
    bool                            truncated_ = false;
};

// --- LOCAL: a human, made pure by writing it down ---------------------------

// A source that remembers what it was told, tick by tick.
//
// This is the LOCAL player's source with the hardware removed. The Engine-side
// host polls its pad once per tick and calls Latch(tick, bits) BEFORE asking the
// session to run that tick; from then on the value is immutable and `At` is as
// pure as a scripted trace. That is what makes a live match recordable and a
// rollback correct: re-simulating tick 900 asks this object for tick 900 and
// gets the bits the player actually pressed, not the bits they are pressing now.
//
// It lives here, in a library that links no Engine, precisely so that the pure
// half is testable with no window: a test latches a sequence by hand and the
// object behaves identically to the one a pad drives.
//
// LATCHING IS MONOTONIC AND IMMUTABLE. Latch(t) is legal only for
// t == NextTick(); anything else returns false and changes nothing. A value once
// latched is never rewritten. Both rules exist for the same reason: the moment a
// source can revise the past, "same tick in, same bytes out" stops being true
// and every downstream guarantee -- replay, rollback, the desync checksum --
// goes with it. A caller that gets `false` back has a sequencing bug and must
// stop the match rather than continue with a hole in the input log.
class LatchedInputSource final : public IInputSource {
public:
    explicit LatchedInputSource(std::uint32_t firstTick = 0,
                                const char*   name      = "LOCAL");

    InputSample   At(std::uint32_t tick) const override;
    // Always kUnboundedTick: this source never runs out of script. It still
    // answers `authored = false` for a tick it has not been told about yet,
    // which is a HOST SEQUENCING BUG rather than a normal end-of-input.
    std::uint32_t AuthoredEndTick() const override;
    const char*   Name() const override;

    // False when `tick` is not NextTick(), or when the cap would be exceeded.
    bool          Latch(std::uint32_t tick, cse::kernel::Input in);
    std::uint32_t NextTick() const;
    std::uint32_t FirstTick() const;

    // Throw the history away and begin again at `firstTick`. For a new match
    // only -- calling it mid-match is exactly the "revise the past" this class
    // is built to prevent.
    void Reset(std::uint32_t firstTick);

private:
    std::vector<cse::kernel::Input> inputs_;
    std::uint32_t                   firstTick_ = 0;
    const char*                     name_      = "LOCAL";
};

// --- The composition that hands control back --------------------------------

// Primary if it authors this tick, otherwise secondary.
//
// This is the entire "Demonstrate" feature, expressed once so that three hosts
// do not each write their own if-statement:
//
//     ScriptedInputSource demo(BuildDemonstration(...).inputs, session.CurrentTick());
//     FallbackInputSource p0(&demo, &localPad);
//     session.SetInputSource(0, &p0);
//
// The trace plays perfectly, runs out, and the pad takes over on the very next
// tick with nothing to reset and nothing to notice. `Active(tick)` tells a HUD
// which of the two is speaking so it can say DEMO or YOU.
//
// Both pointers are BORROWED and may be null; a null primary means the secondary
// answers everything, and two nulls author nothing. Neither is owned and both
// must outlive this object. Nesting is fine and is how a third source would be
// added -- the fallback of a fallback -- so this stays two-way rather than
// growing a list.
class FallbackInputSource final : public IInputSource {
public:
    FallbackInputSource(const IInputSource* primary, const IInputSource* secondary);

    InputSample   At(std::uint32_t tick) const override;
    // The LARGER of the two ends, because either may still speak up to its own.
    std::uint32_t AuthoredEndTick() const override;
    // Always "FALLBACK", deliberately. The tempting alternative -- returning
    // whichever child spoke most recently -- needs a mutable member written from
    // a const method, which would make the one method this interface promises is
    // pure the one method with a side effect. A HUD that wants to print DEMO or
    // YOU asks Active(tick)->Name(), which is a pure question with a pure answer.
    const char*   Name() const override;

    // Which source authors `tick`: the primary, or the secondary, or null when
    // neither does.
    const IInputSource* Active(std::uint32_t tick) const;

private:
    const IInputSource* primary_   = nullptr;
    const IInputSource* secondary_ = nullptr;
};

} // namespace cse::game
