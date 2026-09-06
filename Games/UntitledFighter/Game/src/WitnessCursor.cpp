#include "cse/game/WitnessCursor.h"

#include "cse/data/MatchBuilder.h"

namespace cse::game {
namespace {

// The stance-establishing direction for a built move: Down for a crouching
// move, the takeoff Up for an aerial, nothing for a move that states no
// posture. Read off MoveDef::stance -- the enforced bytes -- and nowhere else.
//
// THE UP HOLD IS FOR FALLBACK CHARACTERS ONLY (ADR-018). Their takeoff is the
// level response a held Up trips, which is exactly what an aerial's witness
// entry needs. A character that authors a JUMP MOVE has no level response --
// and a held Up would BUFFER jump-move presses the witness never asked for
// (Up joins the buffable union the moment a move binds it) -- so the witness
// carries the jump move ITSELF as an ordinary entry and holds nothing.
std::uint16_t stanceHold(const cse::kernel::FighterData& data,
                         const cse::kernel::MoveDef* m) {
    if (m == nullptr) return 0;
    if (m->stance == cse::kernel::kStanceCrouching) return cse::kernel::kInputDown;
    if (m->stance == cse::kernel::kStanceAir)
        return data.jumpMoveSlot != 0 ? std::uint16_t{0} : cse::kernel::kInputUp;
    return 0;
}

} // namespace

WitnessCursor WitnessCursor::FromSlots(const std::vector<std::uint16_t>& slots,
                                       std::size_t loopStart,
                                       const cse::kernel::FighterData& data) {
    WitnessCursor c;
    c.loopStart_ = loopStart;
    for (const std::uint16_t slot : slots) {
        if (IsMacro(slot)) {
            // A movement macro (ADR-013 decision 6): an absolute direction
            // (or nothing) held for a counted number of ticks. No button, no
            // move to watch.
            const std::uint16_t kind = static_cast<std::uint16_t>(slot & 0xFF00);
            c.slots_.push_back(slot);
            c.buttons_.push_back(0);
            c.holds_.push_back(kind == kMacroWalkLeft    ? cse::kernel::kInputLeft
                               : kind == kMacroWalkRight ? cse::kernel::kInputRight
                                                         : std::uint16_t{0});
            c.macroTicks_.push_back(MacroTickCount(slot));
            c.ids_.push_back(
                (kind == kMacroWalkLeft    ? std::string("walk left ")
                 : kind == kMacroWalkRight ? std::string("walk right ")
                 : kind == kMacroWait      ? std::string("wait ")
                                           : std::string("unknown macro ")) +
                std::to_string(MacroTickCount(slot)));
            continue;
        }
        const cse::kernel::MoveDef* m = cse::kernel::MoveAt(data, slot);
        c.slots_.push_back(slot);
        c.buttons_.push_back(m != nullptr ? m->button : std::uint16_t{0});
        c.holds_.push_back(stanceHold(data, m));
        c.macroTicks_.push_back(0);
        c.ids_.push_back("slot " + std::to_string(slot));
    }
    return c;
}

WitnessCursor WitnessCursor::FromIds(const std::vector<std::string>& ids,
                                     std::size_t loopStart,
                                     const cse::data::MoveIndexMap& map,
                                     const cse::kernel::FighterData& data) {
    WitnessCursor c;
    c.loopStart_ = loopStart;
    for (const std::string& id : ids) {
        const std::uint16_t slot = map.Find(id);
        const cse::kernel::MoveDef* m = cse::kernel::MoveAt(data, slot);
        c.slots_.push_back(slot);
        c.buttons_.push_back(m != nullptr ? m->button : std::uint16_t{0});
        c.holds_.push_back(stanceHold(data, m));
        c.macroTicks_.push_back(0);   // an id names a move; macros are slot-coded
        c.ids_.push_back(id);
    }
    return c;
}

std::uint16_t WitnessCursor::StanceHold(const cse::kernel::FighterData& data,
                                        std::uint16_t slot) {
    return stanceHold(data, cse::kernel::MoveAt(data, slot));
}

bool WitnessCursor::Usable(std::string& why) const {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (IsMacro(slots_[i])) {
            const std::uint16_t kind =
                static_cast<std::uint16_t>(slots_[i] & 0xFF00);
            if (kind != kMacroWalkLeft && kind != kMacroWalkRight &&
                kind != kMacroWait) {
                why = "`" + ids_[i] + "` is not a macro this cursor knows";
                return false;
            }
            if (macroTicks_[i] == 0) {
                why = "`" + ids_[i] + "` lasts zero ticks, which performs "
                      "nothing and would advance the cursor for free";
                return false;
            }
            continue;
        }
        if (slots_[i] == 0) {
            why = "this character has no move called `" + ids_[i] +
                  "`, so the witness cannot be replayed against it";
            return false;
        }
        if (buttons_[i] == 0) {
            why = "`" + ids_[i] + "` was given no button, so nothing can ask for it";
            return false;
        }
    }
    return !slots_.empty();
}

std::uint16_t WitnessCursor::Bits(const State& s) const {
    if (buttons_.empty() || s.cursor >= buttons_.size()) return 0;
    // A movement macro's direction is a LEVEL: held every tick of the entry,
    // no press/release cycling to apply.
    if (IsMacro(slots_[s.cursor])) return holds_[s.cursor];
    if (s.release) return holds_[s.cursor];
    return static_cast<std::uint16_t>(buttons_[s.cursor] | holds_[s.cursor]);
}

WitnessCursor::StepResult WitnessCursor::Step(const State& s,
                                              std::uint16_t moveId,
                                              std::uint16_t moveFrame) const {
    StepResult r;
    r.next = s;
    if (slots_.empty() || s.cursor >= slots_.size()) return r;

    // A movement macro advances by COUNTING (the header says why): `waiting`
    // is its elapsed-tick counter -- an entry is either watched or counted,
    // never both, so the field cannot mean two things at once on one entry.
    //
    // ONLY FREE TICKS COUNT. A fighter mid-move cannot walk (commitment) and
    // a frozen one cannot either (hitstop), so "walk 8" means eight ticks OF
    // WALKING: the held direction rides silently through a move's tail or a
    // freeze and the count begins when the fighter can act on it. Without
    // this, a walk macro issued right after a connect spent most of itself
    // committed and the microwalk link was never performable -- the probe
    // that measured it is in the slice commit.
    if (IsMacro(slots_[s.cursor])) {
        if (moveId == 0) ++r.next.waiting;
        if (r.next.waiting >= static_cast<int>(macroTicks_[s.cursor])) {
            r.advanced     = true;
            r.next.waiting = 0;
            if (s.cursor + 1 < slots_.size()) {
                r.next.cursor = s.cursor + 1;
            } else {
                r.next.cursor = loopStart_;
                r.wrapped     = true;
            }
            // No button was down, so the next entry never needs a release
            // gap for an edge; a fresh press is one either way.
            r.next.release = false;
        }
        return r;
    }

    // The start, FIRST -- the header says why a release tick may not be spent
    // blind. Everything about the rules lives up there, once.
    if (moveId == slots_[s.cursor] && moveFrame == 0) {
        const std::uint16_t justUsed = buttons_[s.cursor];
        r.advanced = true;
        r.next.waiting = 0;
        if (s.cursor + 1 < slots_.size()) {
            r.next.cursor = s.cursor + 1;
        } else {
            r.next.cursor = loopStart_;
            r.wrapped     = true;
        }
        r.next.release = (buttons_[r.next.cursor] == justUsed);
        return r;
    }
    if (s.release) {
        r.next.release = false;
        return r;
    }
    ++r.next.waiting;
    if (r.next.waiting >= kRepressAfter) {
        r.next.release = true;
        r.next.waiting = 0;
    }
    return r;
}

} // namespace cse::game
