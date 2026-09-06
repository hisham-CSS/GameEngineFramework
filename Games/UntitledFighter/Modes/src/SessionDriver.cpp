// The session owns the tick count; see the header.
#include "SessionDriver.h"

#include <cstring>

namespace untitledfighter {

cse::net::SessionConfig SessionDriver::WireConfig(int playerCount) {
    cse::net::SessionConfig cfg{};
    cfg.playerCount         = playerCount;
    cfg.inputBytesPerPlayer = static_cast<std::uint32_t>(sizeof(WireInput));
    cfg.stateBytes          = static_cast<std::uint32_t>(sizeof(cse::kernel::GameState));
    cfg.predictionWindow    = 8;
    cfg.desyncDetection     = true;
    cfg.desyncCheckInterval = 8;
    return cfg;
}

void SessionDriver::Bind(cse::net::ISession* session, cse::game::FightSession* fight,
                         std::uint8_t localSlots, int padSlot) {
    session_      = session;
    fight_        = fight;
    localSlots_   = localSlots;
    padSlot_      = padSlot;
    currentFrame_ = 0;
    offeredFrame_ = -1;
    counts_       = DriverCounts{};
    fatal_.clear();
}

void SessionDriver::Unbind() {
    session_ = nullptr;
    fight_   = nullptr;
}

bool SessionDriver::AcceptsInput() const {
    return Bound() && offeredFrame_ != currentFrame_;
}

void SessionDriver::Offer(int slot, cse::kernel::Input in) {
    if (!Bound()) return;
    WireInput w{ in.bits };
    session_->AddLocalInput(slot, &w);
    offeredFrame_ = currentFrame_;
    ++counts_.offers;
}

int SessionDriver::Frame(cse::kernel::Input pad) {
    if (!Bound()) return 0;
    if (AcceptsInput()) {
        for (int slot = 0; slot < playerCount_; ++slot) {
            if ((localSlots_ & (1u << slot)) == 0) continue;
            Offer(slot, slot == padSlot_ ? pad : cse::kernel::Input{});
        }
    }
    return Pump();
}

int SessionDriver::Pump() {
    if (!Bound()) return 0;
    ++counts_.framesPumped;
    int ticks = 0;
    int n = 0;
    const cse::net::SessionEvent* ev = session_->Update(&n);
    // EVERY EVENT, IN ORDER (ISession.h: valid until the next Update, handled
    // in the order given). A Load followed by three Advances is a rollback and
    // all three ticks run; an Update with no Advance is a stall and no tick
    // runs. Nothing here decides; it does what the session says.
    for (int i = 0; i < n; ++i) {
        const cse::net::SessionEvent& e = ev[i];
        switch (e.type) {
        case cse::net::SessionEventType::Save: {
            if (e.saveCapacity < sizeof(cse::kernel::GameState)) {
                fatal_ = "the session offered " + std::to_string(e.saveCapacity) +
                         " bytes for a snapshot of " + std::to_string(sizeof(cse::kernel::GameState)) +
                         "; it was not created with SessionDriver::WireConfig.";
                return ticks;
            }
            // ARCHITECTURE.md D4: the snapshot is a memcpy of the POD.
            cse::kernel::GameState snap{};
            fight_->Snapshot(snap);
            std::memcpy(e.saveBuffer, &snap, sizeof(snap));
            *e.saveLength   = static_cast<std::uint32_t>(sizeof(snap));
            *e.saveChecksum = fight_->Checksum();
            ++counts_.saves;
            break;
        }
        case cse::net::SessionEventType::Load: {
            if (e.loadBytes != sizeof(cse::kernel::GameState)) {
                fatal_ = "the session handed back " + std::to_string(e.loadBytes) +
                         " bytes to restore; a GameState is " + std::to_string(sizeof(cse::kernel::GameState)) + ".";
                return ticks;
            }
            cse::kernel::GameState snap{};
            std::memcpy(&snap, e.loadBuffer, sizeof(snap));
            fight_->Restore(snap);
            ++counts_.loads;
            break;
        }
        case cse::net::SessionEventType::Advance: {
            // Every player's WireInput back to back (ISession.h); the kernel's
            // InputPair has a slot for each, neutral for the ones not on the
            // wire.
            cse::kernel::InputPair pair{};
            const int players = static_cast<int>(e.inputBytes / sizeof(WireInput));
            for (int p = 0; p < players && p < cse::kernel::kMaxFighters; ++p) {
                WireInput w{};
                std::memcpy(&w, e.inputs + static_cast<std::size_t>(p) * sizeof(WireInput), sizeof(w));
                pair.p[p].bits = w.bits;
            }
            fight_->Tick(pair);
            ++counts_.advances;
            ++counts_.ticksRun;
            ++ticks;
            if (e.rollingBack) {
                ++counts_.rollbackTicks;
            } else {
                // The next new Advance carries the frame after this one; a
                // rollback re-advance carries a frame already counted and
                // leaves the session where it was.
                currentFrame_ = e.frame + 1;
            }
            break;
        }
        }
    }
    return ticks;
}

} // namespace untitledfighter
