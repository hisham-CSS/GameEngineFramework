#include "cse/game/WitnessCursor.h"

#include "cse/data/MatchBuilder.h"

namespace cse::game {
namespace {

// The stance-establishing direction for a built move: Down for a crouching
// move, the takeoff Up for an aerial, nothing for a move that states no
// posture. Read off MoveDef::stance -- the enforced bytes -- and nowhere else.
std::uint16_t stanceHold(const cse::kernel::MoveDef* m) {
    if (m == nullptr) return 0;
    if (m->stance == cse::kernel::kStanceCrouching) return cse::kernel::kInputDown;
    if (m->stance == cse::kernel::kStanceAir)       return cse::kernel::kInputUp;
    return 0;
}

} // namespace

WitnessCursor WitnessCursor::FromSlots(const std::vector<std::uint16_t>& slots,
                                       std::size_t loopStart,
                                       const cse::kernel::FighterData& data) {
    WitnessCursor c;
    c.loopStart_ = loopStart;
    for (const std::uint16_t slot : slots) {
        const cse::kernel::MoveDef* m = cse::kernel::MoveAt(data, slot);
        c.slots_.push_back(slot);
        c.buttons_.push_back(m != nullptr ? m->button : std::uint16_t{0});
        c.holds_.push_back(stanceHold(m));
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
        c.holds_.push_back(stanceHold(m));
        c.ids_.push_back(id);
    }
    return c;
}

std::uint16_t WitnessCursor::StanceHold(const cse::kernel::FighterData& data,
                                        std::uint16_t slot) {
    return stanceHold(cse::kernel::MoveAt(data, slot));
}

bool WitnessCursor::Usable(std::string& why) const {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
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
    if (s.release) return holds_[s.cursor];
    return static_cast<std::uint16_t>(buttons_[s.cursor] | holds_[s.cursor]);
}

WitnessCursor::StepResult WitnessCursor::Step(const State& s,
                                              std::uint16_t moveId,
                                              std::uint16_t moveFrame) const {
    StepResult r;
    r.next = s;
    if (slots_.empty() || s.cursor >= slots_.size()) return r;

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
