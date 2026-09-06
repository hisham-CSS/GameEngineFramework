// A desync, named (ROADMAP M2.3; DETERMINISM.md T6, S8; ADR-002 CHOICE C).
//
// The session reports the frame at which two peers' checksums disagreed and
// nothing more (ISession::PollDesync). This is the rest of the sentence: hold
// the last frames of your own state (StateHistory), obtain the peer's state at
// that frame (cse::net::BlobExchange carries the bytes), and name the FIRST
// FIELD the two disagree about through the kernel's reflection table
// (FirstDivergence: "p[1].health", the offset, both values). The artifact is
// one flat JSON object a person or a script can read; writing it is the host's
// last act before it stops the match -- a desync is never corrected.
//
// Headless, integers and strings only, no session or transport type: the host
// owns those, and this library answers one question about two states.
#pragma once
#include "cse/kernel/GameState.h"

#include <cstdint>
#include <string>

namespace cse::game {

struct Divergence {
    bool          found        = false;
    std::uint32_t tick         = 0;      // the tick the two states carry (equal when found)
    std::string   field;                 // "p[1].health", "ev[3].a", "roundsWon[1]", "rng"
    std::uint32_t offset       = 0;      // of that scalar, from the GameState's start
    std::uint32_t width        = 0;      // bytes
    std::int64_t  local        = 0;      // the two values, read with the field's width and sign
    std::int64_t  remote       = 0;
};

// The first field, in the reflection table's order, whose bytes differ. False
// with `found` clear when the states are byte-identical.
bool FirstDivergence(const cse::kernel::GameState& local, const cse::kernel::GameState& remote, Divergence* out);

// One flat JSON object: the frame the session reported, both checksums, the
// remote player, and the divergence (or "state not held" when the frame had
// left the history). Never throws; the text is the artifact.
std::string DesyncArtifactJson(std::uint32_t reportedFrame, std::uint32_t localChecksum, std::uint32_t remoteChecksum,
                               int remotePlayer, const Divergence* divergenceOrNull);

// Writes `json` to `path`, creating the directory; false with `error` on failure.
bool WriteDesyncArtifact(const std::string& path, const std::string& json, std::string* error);

// The last kCapacity states by tick, so the state at the frame a desync names
// is still here when the report arrives. Push after every Advance; a rollback
// re-pushes the corrected state for the same tick. THE FRAME IS NOT THE TICK:
// the session reports the frame whose ADVANCE produced the differing state
// (measured against GekkoNet: a drift that begins on frame 100 is reported at
// frame 100), so the state to compare is the one after it -- Find(frame + 1).
// And the peer that detected first must keep its session pumping a little
// longer before it tears down, so the other side receives the checksum that
// lets it detect too; a session destroyed at once leaves the peer finishing
// the match alone after the disconnect timeout (seen over UDP).
class StateHistory {
public:
    static constexpr int kCapacity = 128;
    void Push(const cse::kernel::GameState& state);
    const cse::kernel::GameState* Find(std::uint32_t tick) const;

private:
    cse::kernel::GameState ring_[kCapacity]{};
    bool                   held_[kCapacity]{};
};

} // namespace cse::game
