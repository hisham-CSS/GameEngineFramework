// The session owns the tick count (ROADMAP M2.4; DETERMINISM.md T1, T2, T3).
//
// A rollback session (cse::net::ISession) speaks in three events -- Save this
// frame, Load that one, Advance with these inputs -- and a FightSession answers
// each with one call: Snapshot, Restore, Tick(inputs). This is the glue, and
// the whole of what it decides is NOTHING: the kernel runs exactly as many
// ticks as the session says, in the order it says them. Zero on a frame the
// peer has not answered is legal; more than one when the session rolls back
// is normal; one fewer is never allowed. The host's fixed step pumps this once
// per step at the real rate and asks how many ticks ran; pause, slow motion,
// frame step and the fixed step's own backlog rule have no say (T2, T3).
//
// It lives in the Modes library, not in CseGame: CseGame's link line is an
// exact whitelist that keeps the session seam out of the simulation libraries,
// and CseNet knows no GameState. The two meet here, in the host, the way the
// spike harness (tests/test_session.cpp, tests/online_peer.cpp) always joined
// them -- this file is that harness with a name.
//
// ONE INPUT PER SESSION FRAME. The session takes the local input FOR ITS
// CURRENT FRAME and ignores a second offer for the same frame (ISession.h,
// rule 5: sequential insertion, measured against GekkoNet). A host that offers
// the pad every fixed step while the session stalls therefore has its later
// offers dropped -- and anything it spent into them, like the tap accumulator's
// pending presses, dropped with them. AcceptsInput() is the question to ask
// first: true while no input has been offered for the frame the session is on.
// Frame() asks it itself and offers nothing when the answer is no, so the
// mode's taps stay pending until a frame that will carry them.
#pragma once
#include "cse/game/FightSession.h"
#include "cse/kernel/GameState.h"
#include "cse/net/ISession.h"

#include <cstdint>
#include <string>

namespace untitledfighter {

// Two bytes per player on the wire (tests/test_session.cpp's idiom). Not
// cse::kernel::Input itself: the session's input size is a network concern and
// the kernel's input type is a simulation concern, and letting them be one
// type by accident is how they end up coupled.
struct WireInput { std::uint16_t bits; };

struct DriverCounts {
    std::uint32_t framesPumped  = 0;   // Pump() calls
    std::uint32_t offers        = 0;   // inputs handed to the session
    std::uint32_t advances      = 0;   // Advance events (== ticksRun unless something is very wrong)
    std::uint32_t ticksRun      = 0;   // FightSession::Tick calls
    std::uint32_t rollbackTicks = 0;   // of those, re-simulations
    std::uint32_t saves         = 0;
    std::uint32_t loads         = 0;
};

class SessionDriver {
public:
    // The configuration a session must be created with to drive a FightSession:
    // sizeof(WireInput) per player and sizeof(GameState) of state. The host
    // fills in the peers and the delay.
    static cse::net::SessionConfig WireConfig(int playerCount);

    // Both borrowed. `localSlots` is a bit per player slot this host supplies
    // (bit 0 = player 0); Frame() offers the pad to `padSlot` and neutral to
    // every other local slot -- a local session has two, an online one has one.
    void Bind(cse::net::ISession* session, cse::game::FightSession* fight,
              std::uint8_t localSlots, int padSlot);
    void Unbind();
    bool Bound() const { return session_ != nullptr && fight_ != nullptr; }

    // True while the session will take an input for the frame it is on (see
    // the header comment). Always true for a session that never stalls.
    bool AcceptsInput() const;

    // Hand `in` to the session as `slot`'s input for the current frame. The
    // raw offer, for a host that supplies more than one slot itself; it does
    // not consult AcceptsInput().
    void Offer(int slot, cse::kernel::Input in);

    // One frame of the session's clock: Update, then do EXACTLY what it says.
    // Returns the ticks run this frame; zero is a legal answer.
    int Pump();

    // The mode's step: if AcceptsInput(), offer `pad` to the pad slot and
    // neutral to the other local slots; then Pump().
    int Frame(cse::kernel::Input pad);

    // The session frame the next Advance will carry: the frame of the last
    // confirmed-or-ahead Advance plus one, 0 before any.
    std::int32_t CurrentFrame() const { return currentFrame_; }

    const DriverCounts& Counts() const { return counts_; }

    // Set once and kept when the session asked for something this driver
    // could not do -- a Save into a buffer smaller than a GameState, a Load of
    // bytes that are not one. The host stops the match; nothing here corrects.
    const std::string& Fatal() const { return fatal_; }

private:
    cse::net::ISession*      session_      = nullptr;
    cse::game::FightSession* fight_        = nullptr;
    std::uint8_t             localSlots_   = 0;
    int                      padSlot_      = 0;
    int                      playerCount_  = 2;
    std::int32_t             currentFrame_ = 0;
    std::int32_t             offeredFrame_ = -1;
    DriverCounts             counts_{};
    std::string              fatal_;
};

} // namespace untitledfighter
