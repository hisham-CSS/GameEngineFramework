// THE ONE WITNESS CURSOR (docs/adr/ADR-012 rule 4, ROADMAP M1.3g).
//
// A witness -- the prover's printed combo, or any scripted cycle -- is
// performed by pressing the button of the move that comes next and advancing
// when the attacker actually ENTERS it. That one sentence was implemented five
// times (BuildDemonstration and four test drivers), the copies drifted twice,
// and each drift cost a day: the seam test caught a copy missing the waiting
// rule, and the M1.3e review found four latent traps that had to be dodged in
// every copy at once. This file is where the sentence lives now; everything
// else adapts it.
//
// THE RULES, EACH LEARNED THE EXPENSIVE WAY:
//
// ADVANCE ON `moveFrame == 0` WITH THE EXPECTED moveId, never on a CHANGE of
// moveId. The witness for an infinite is a move cancelling into ITSELF, so the
// id never changes and a transition detector sees nothing; the frame counter
// is what resets. (ComboWatcher.h documents the same trap a third time.)
//
// THE START IS CHECKED BEFORE A RELEASE TICK IS SPENT. A buffered press is
// consumed the exact tick the fighter can act -- very often a tick this cursor
// is deliberately silent on, because it pressed two ticks ago and is releasing
// so the next press has an edge to be. A cursor that spent the release tick
// blind missed precisely the transitions buffering creates: it went on asking
// for the move already running, and the loop repeated one duration late while
// the observer reported decay.
//
// A RELEASE BETWEEN REPEATS OF THE SAME BUTTON. Holding a bit is ONE press
// however long it lasts, and a self-cancel asks for the same bit twice
// running; without the gap the second press never happens and "the kernel
// refused the link" is a sentence about the trace. A DIFFERENT button is
// already an edge, and a gap there would cost a tick for nothing -- compared
// on BUTTONS, never on the emitted bits, because the stance hold rides along.
//
// THE RELEASE IS OF THE BUTTON, NOT THE POSTURE. Each entry's
// stance-establishing direction (Down for a crouching move, the takeoff Up an
// aerial needs -- read off the BUILT MoveDef::stance, the bytes the kernel
// enforces) is pressed WITH the button and KEPT through release ticks. Dropped
// there, a buffered press consumed on the silent tick asks for a crouching
// move from a stand, is refused, and every loop repeats one tick late. The
// hold is a SEPARATE table from the buttons: folded in, it would break the
// zero-button Usable check and make the same-button release predicate compare
// composites that never match.
//
// A STALL RE-PRESSES AFTER TWO WAITING TICKS. A cursor that stalls holding a
// button feeds the kernel nothing at all -- "the string stopped" would mean
// "the cursor went quiet", not "the game refused" -- and since M1.3e a landing
// between turns makes waiting ROUTINE: an aerial loop touches down and needs a
// free tick to take off again. Two ticks: long enough not to interrupt a move
// in progress with a pointless re-press, short enough to catch the actionable
// tick.
//
// A MOVEMENT MACRO ADVANCES BY COUNTING, NOT BY OBSERVING (ADR-013 decision
// 6). Witness entries at or above kMacroBase are not move slots: a walk holds
// one ABSOLUTE direction, a wait holds nothing, and the entry completes after
// its encoded tick count -- there is no move start to watch for. Directions
// are absolute (left/right, never toward/away) so the emitted bits are
// replay-stable: the same witness fed to the same opening walks the same
// pixels. The press/release cycling does not apply to a held direction -- a
// direction is a LEVEL to the kernel, and releasing it mid-walk would just
// walk less than the entry says.
#pragma once

#include "cse/kernel/Combat.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cse::data { struct MoveIndexMap; }

namespace cse::game {

// The immutable half: what to press for each witness entry. Built from the
// BUILT fighter so the buttons and stance holds are the bytes the kernel will
// actually consult -- a table built from the authored CharacterData could
// agree with a mis-mapped bridge and disagree with the game.
class WitnessCursor {
public:
    // Ticks a stalled cursor waits before it releases to re-press.
    static constexpr int kRepressAfter = 2;

    // Movement-macro codes (ADR-013 decision 6): kind in the high byte, tick
    // count 1..255 in the low. Far above kMaxMovesPerFighter on purpose -- a
    // witness entry is either a move slot or one of these, and the gap makes
    // a confusion loud.
    static constexpr std::uint16_t kMacroBase      = 0x8000;
    static constexpr std::uint16_t kMacroWalkLeft  = 0x8000;   // + ticks
    static constexpr std::uint16_t kMacroWalkRight = 0x8100;   // + ticks
    static constexpr std::uint16_t kMacroWait      = 0x8200;   // + ticks
    static constexpr bool IsMacro(std::uint16_t entry) {
        return entry >= kMacroBase;
    }
    static constexpr std::uint16_t MacroTickCount(std::uint16_t entry) {
        return static_cast<std::uint16_t>(entry & 0x00FF);
    }

    // The mutable half, a value: copy it, snapshot it, resume it. Rollback and
    // rehearsal both depend on nothing hiding outside it.
    struct State {
        std::size_t cursor  = 0;
        bool        release = false;   // this tick drops the button, keeps the hold
        int         waiting = 0;       // ticks spent waiting; kRepressAfter re-presses
    };

    // What one observed tick did to the cursor.
    struct StepResult {
        State next;
        bool  advanced = false;   // the expected move began this tick
        bool  wrapped  = false;   // ...and the cursor returned to loopStart
    };

    WitnessCursor() = default;

    // From kernel move ids -- BuildDemonstration's construction.
    static WitnessCursor FromSlots(const std::vector<std::uint16_t>& slots,
                                   std::size_t loopStart,
                                   const cse::kernel::FighterData& data);

    // From witness id strings through the build's own name table -- the test
    // drivers' construction. An id the map does not know becomes slot 0 and is
    // reported by Usable rather than silently skipped.
    static WitnessCursor FromIds(const std::vector<std::string>& ids,
                                 std::size_t loopStart,
                                 const cse::data::MoveIndexMap& map,
                                 const cse::kernel::FighterData& data);

    // The stance-establishing direction for one built move, exposed because a
    // MASHER needs it too: a bare button whose only binding is a crouching or
    // air move starts nothing since M1.3e, and "the defender never acted"
    // becomes a fact about the harness rather than about the combo.
    static std::uint16_t StanceHold(const cse::kernel::FighterData& data,
                                    std::uint16_t slot);

    // Every entry names a real move with a real button, or the reason nothing
    // can perform this witness -- checked up front, so a forty-tick rehearsal
    // is never spent on a trace whose sixth move nothing can ask for.
    bool Usable(std::string& why) const;

    // The bits to feed the attacker this tick.
    std::uint16_t Bits(const State& s) const;

    // Observe the attacker AFTER the tick ran. Pure: (table, state, observed)
    // in, new state out, nothing else read or written.
    StepResult Step(const State& s, std::uint16_t moveId,
                    std::uint16_t moveFrame) const;

    bool                empty() const { return slots_.empty(); }
    std::size_t         size() const { return slots_.size(); }
    std::size_t         LoopStart() const { return loopStart_; }
    std::size_t         LoopLength() const { return slots_.size() - loopStart_; }
    const std::vector<std::uint16_t>& Slots() const { return slots_; }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::uint16_t> holds_;   // stance direction; never folded into buttons_
    // Movement-macro tick counts; 0 for move entries. A separate table for
    // the same reason holds_ is: an entry is EITHER watched (a move) or
    // counted (a macro), and overloading `waiting` across both meanings in
    // one untagged field is how the next drift starts.
    std::vector<std::uint16_t> macroTicks_;
    std::vector<std::string>   ids_;     // for Usable's messages only
    std::size_t                loopStart_ = 0;
};

// The stateful convenience the test drivers are: one cursor, one State, the
// Bits/Observe surface their drive loops already speak.
class WitnessDriver {
public:
    WitnessDriver() = default;
    explicit WitnessDriver(WitnessCursor cursor) : cursor_(std::move(cursor)) {}

    bool Usable(std::string& why) const { return cursor_.Usable(why); }
    std::uint16_t Bits() const { return cursor_.Bits(state_); }
    void Observe(std::uint16_t moveId, std::uint16_t moveFrame) {
        state_ = cursor_.Step(state_, moveId, moveFrame).next;
    }

    const WitnessCursor& Cursor() const { return cursor_; }
    const std::vector<std::uint16_t>& Slots() const { return cursor_.Slots(); }
    std::size_t LoopStart() const { return cursor_.LoopStart(); }
    std::size_t LoopLength() const { return cursor_.LoopLength(); }

private:
    WitnessCursor        cursor_;
    WitnessCursor::State state_;
};

} // namespace cse::game
