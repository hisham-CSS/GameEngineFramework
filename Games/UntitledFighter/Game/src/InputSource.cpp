// The three input sources, out of line.
//
// Every method here is on the path `At` describes in InputSource.h: const, pure,
// total, allocation-free and non-throwing. That contract is not decoration --
// `At` is called twice per tick in a local match and again for every tick a
// rollback re-simulates, and a source that answered differently the second time
// would desync two peers who pressed the same buttons.
//
// So the shape below is uniform and deliberately dull: bounds-check the tick
// against a half-open range, subtract, index. No branch reads a clock, no branch
// allocates, and the one piece of arithmetic that could overflow is done in
// 64 bits at construction time where a throw would be survivable.
#include "cse/game/InputSource.h"

#include <cstddef>
#include <utility>

namespace cse::game {
namespace {

// The unauthored answer, in one place. `input` is zeroed rather than left
// indeterminate because InputSample's own comment promises it: the flag is the
// contract and the zero is what keeps a caller who forgot to check the flag from
// getting stale bits instead of a neutral stick.
InputSample nothing() {
    return InputSample{};
}

// Half-open membership without the subtraction that could wrap.
//
// `tick - first` is only formed after `tick >= first` is known, so the unsigned
// wrap that would turn "one tick before the trace" into "four billion ticks in"
// cannot happen. That bug is the reason this is a named helper rather than three
// copies of the same expression.
bool covers(std::uint32_t tick, std::uint32_t first, std::size_t count,
            std::size_t& indexOut) {
    if (tick < first) return false;
    const std::uint32_t offset = tick - first;
    if (offset >= count) return false;
    indexOut = static_cast<std::size_t>(offset);
    return true;
}

} // namespace

// --- ScriptedInputSource ----------------------------------------------------

// THE CLAMP IS IN THE CONSTRUCTOR AND NOWHERE ELSE.
//
// The header states the precondition `firstTick + inputs.size() <= kMaxMatchTicks`
// and then says the constructor clamps rather than trusts. Doing it here means
// `At` never has to know the cap exists: whatever survives construction is
// already inside it, and the hot path is one comparison shorter for it.
//
// The arithmetic is done in 64 bits because it is the one place in this file
// where a sum can exceed uint32 -- a caller can legitimately hold a large
// `firstTick` late in a match and hand over a long trace, and computing the end
// in 32 bits would wrap it to a small number and silently keep a trace that
// should have been cut.
ScriptedInputSource::ScriptedInputSource(std::vector<cse::kernel::Input> inputs,
                                         std::uint32_t                   firstTick,
                                         const char*                     name)
    : inputs_(std::move(inputs)),
      firstTick_(firstTick),
      name_(name != nullptr ? name : "SCRIPT") {
    if (firstTick_ >= kMaxMatchTicks) {
        // The trace begins after the match can possibly still be running, so
        // none of it is reachable. Dropped rather than kept-and-never-asked,
        // because a source holding ticks it can never author is a source whose
        // AuthoredEndTick lies to the progress bar.
        truncated_ = !inputs_.empty();
        inputs_.clear();
        return;
    }

    const std::uint64_t room = static_cast<std::uint64_t>(kMaxMatchTicks) -
                               static_cast<std::uint64_t>(firstTick_);
    if (static_cast<std::uint64_t>(inputs_.size()) > room) {
        inputs_.resize(static_cast<std::size_t>(room));
        truncated_ = true;
    }
}

InputSample ScriptedInputSource::At(std::uint32_t tick) const {
    std::size_t index = 0;
    if (!covers(tick, firstTick_, inputs_.size(), index)) return nothing();
    InputSample s;
    s.input    = inputs_[index];
    s.authored = true;
    return s;
}

std::uint32_t ScriptedInputSource::AuthoredEndTick() const {
    // Cannot overflow: the constructor guarantees the sum is <= kMaxMatchTicks.
    return firstTick_ + static_cast<std::uint32_t>(inputs_.size());
}

const char*   ScriptedInputSource::Name() const      { return name_; }
std::uint32_t ScriptedInputSource::FirstTick() const { return firstTick_; }

std::uint32_t ScriptedInputSource::TickCount() const {
    return static_cast<std::uint32_t>(inputs_.size());
}

bool ScriptedInputSource::Truncated() const { return truncated_; }

// --- LatchedInputSource -----------------------------------------------------

LatchedInputSource::LatchedInputSource(std::uint32_t firstTick, const char* name)
    : firstTick_(firstTick), name_(name != nullptr ? name : "LOCAL") {}

InputSample LatchedInputSource::At(std::uint32_t tick) const {
    std::size_t index = 0;
    if (!covers(tick, firstTick_, inputs_.size(), index)) return nothing();
    InputSample s;
    s.input    = inputs_[index];
    s.authored = true;
    return s;
}

// ALWAYS UNBOUNDED, and the header is emphatic about why: a live player never
// runs out of script. An unlatched tick still answers `authored = false`, but
// that means the HOST FAILED TO POLL rather than "the input ended" -- two states
// that a single number could not tell apart, and which want opposite responses
// (keep playing versus hand control back).
std::uint32_t LatchedInputSource::AuthoredEndTick() const { return kUnboundedTick; }

const char* LatchedInputSource::Name() const { return name_; }

// MONOTONIC AND IMMUTABLE, enforced by the two conditions below rather than by
// documentation. `tick != NextTick()` covers both directions at once: a tick in
// the past is a rewrite and a tick in the future is a hole, and either one makes
// `At` stop being a pure function of the match's own history.
//
// Returning false rather than asserting is deliberate. This is called from a
// host's frame loop, and the header tells that host what to do about it -- stop
// the match. A crash would deny it the chance.
bool LatchedInputSource::Latch(std::uint32_t tick, cse::kernel::Input in) {
    const std::uint32_t next = NextTick();
    if (tick != next) return false;
    if (next >= kMaxMatchTicks) return false;
    inputs_.push_back(in);
    return true;
}

std::uint32_t LatchedInputSource::NextTick() const {
    return firstTick_ + static_cast<std::uint32_t>(inputs_.size());
}

std::uint32_t LatchedInputSource::FirstTick() const { return firstTick_; }

void LatchedInputSource::Reset(std::uint32_t firstTick) {
    inputs_.clear();
    firstTick_ = firstTick;
}

// --- FallbackInputSource ----------------------------------------------------

FallbackInputSource::FallbackInputSource(const IInputSource* primary,
                                         const IInputSource* secondary)
    : primary_(primary), secondary_(secondary) {}

// THIS IS THE WHOLE "DEMONSTRATE, THEN YOU TRY" FEATURE.
//
// The demonstration authors its ticks and answers them; the moment it runs out,
// the very next tick falls through to the pad with nothing reset and nothing
// notified. There is no state to get wrong because there is no state.
InputSample FallbackInputSource::At(std::uint32_t tick) const {
    if (primary_ != nullptr) {
        const InputSample s = primary_->At(tick);
        if (s.authored) return s;
    }
    if (secondary_ != nullptr) {
        const InputSample s = secondary_->At(tick);
        if (s.authored) return s;
    }
    return nothing();
}

std::uint32_t FallbackInputSource::AuthoredEndTick() const {
    const std::uint32_t a = primary_   != nullptr ? primary_->AuthoredEndTick()   : 0u;
    const std::uint32_t b = secondary_ != nullptr ? secondary_->AuthoredEndTick() : 0u;
    return a > b ? a : b;
}

const char* FallbackInputSource::Name() const { return "FALLBACK"; }

// The pure question a HUD asks instead of the impure one it wants to.
//
// `Active(tick)->Name()` is how a caller prints DEMO or YOU. Note it re-asks the
// children rather than caching: caching is what would need the mutable member
// this class exists to avoid, and the call is two virtual dispatches over data
// that is already in cache from the At() a moment earlier.
const IInputSource* FallbackInputSource::Active(std::uint32_t tick) const {
    if (primary_ != nullptr && primary_->At(tick).authored)   return primary_;
    if (secondary_ != nullptr && secondary_->At(tick).authored) return secondary_;
    return nullptr;
}

} // namespace cse::game
