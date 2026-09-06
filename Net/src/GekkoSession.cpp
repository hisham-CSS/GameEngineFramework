// ISession over GekkoNet.
//
// This is the ONLY translation unit in the project that includes gekkonet.h.
// That is the point of the seam: if GekkoNet is ever replaced, this file is
// replaced and nothing else changes. Net/CMakeLists.txt links GekkoNet PRIVATE
// to make that structural rather than aspirational.
//
// The translation is thin because ADR-003 measured that it could be: GekkoNet's
// event stream and ours are the same shape, deliberately, since our shape was
// derived from theirs after the spike found it was the constraint the plan had
// missed.
#include "cse/net/ISession.h"

#include "gekkonet.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cse::net {
namespace {

// THE TRANSPORT BRIDGE (ROADMAP M2.1). GekkoNet's adapter is three C function
// pointers with no user-data argument, so an ITransport can only be reached
// through a static: one Bridge<N> per online session in the process, each a
// distinct set of functions. Four slots is a ceiling for tests (two sessions
// talking to each other in one process); the shipped game holds one, and the
// built-in UDP adapter GekkoNet ships is process-global as well. Receive()
// hands GekkoNet malloc'd results it frees back through Free(), one call each
// for the result, its address and its data -- the built-in adapter's own
// convention (gekkonet.cpp asio_receive / backend.cpp), mirrored exactly.
template <int N>
struct Bridge {
    static ITransport*                   transport;
    static std::vector<GekkoNetResult*>  results;   // the array GekkoNet reads; its elements it frees

    static void Send(GekkoNetAddress* addr, const char* data, int length) {
        if (transport == nullptr || addr == nullptr || length < 0) return;
        transport->Send(std::string(static_cast<const char*>(addr->data), addr->size),
                        reinterpret_cast<const std::uint8_t*>(data), static_cast<std::uint32_t>(length));
    }
    static GekkoNetResult** Receive(int* length) {
        results.clear();
        if (transport != nullptr) {
            for (TransportPacket& p : transport->Receive()) {
                auto* res = static_cast<GekkoNetResult*>(std::malloc(sizeof(GekkoNetResult)));
                res->addr.size = static_cast<unsigned int>(p.from.size());
                res->addr.data = std::malloc(p.from.size() ? p.from.size() : 1);
                std::memcpy(res->addr.data, p.from.data(), p.from.size());
                res->data_len  = static_cast<unsigned int>(p.bytes.size());
                res->data      = std::malloc(p.bytes.size() ? p.bytes.size() : 1);
                std::memcpy(res->data, p.bytes.data(), p.bytes.size());
                results.push_back(res);
            }
        }
        *length = static_cast<int>(results.size());
        return results.empty() ? nullptr : results.data();
    }
    static void Free(void* p) { std::free(p); }

    static GekkoNetAdapter adapter;
};
template <int N> ITransport*                  Bridge<N>::transport = nullptr;
template <int N> std::vector<GekkoNetResult*> Bridge<N>::results;
template <int N> GekkoNetAdapter              Bridge<N>::adapter = { &Bridge<N>::Send, &Bridge<N>::Receive, &Bridge<N>::Free };

constexpr int kBridgeSlots = 4;

// Claim a free bridge for `transport`; -1 when all are held.
int claimBridge(ITransport* transport) {
    ITransport** slots[kBridgeSlots] = { &Bridge<0>::transport, &Bridge<1>::transport,
                                         &Bridge<2>::transport, &Bridge<3>::transport };
    for (int i = 0; i < kBridgeSlots; ++i)
        if (*slots[i] == nullptr) { *slots[i] = transport; return i; }
    return -1;
}
GekkoNetAdapter* bridgeAdapter(int slot) {
    GekkoNetAdapter* a[kBridgeSlots] = { &Bridge<0>::adapter, &Bridge<1>::adapter,
                                         &Bridge<2>::adapter, &Bridge<3>::adapter };
    return a[slot];
}
void releaseBridge(int slot) {
    ITransport** slots[kBridgeSlots] = { &Bridge<0>::transport, &Bridge<1>::transport,
                                         &Bridge<2>::transport, &Bridge<3>::transport };
    if (slot >= 0 && slot < kBridgeSlots) *slots[slot] = nullptr;
}

class GekkoSessionImpl final : public ISession {
public:
    GekkoSessionImpl(GekkoSession* s, std::uint32_t stateBytes, bool online, int bridgeSlot)
        : session_(s), stateBytes_(stateBytes), online_(online), bridgeSlot_(bridgeSlot) {}

    ~GekkoSessionImpl() override {
        if (session_) gekko_destroy(&session_);
        releaseBridge(bridgeSlot_);
    }

    void AddLocalInput(int player, const void* input) override {
        // GekkoNet's signature takes void* rather than const void*; it copies
        // immediately and does not retain or modify. const_cast is the honest
        // spelling of that rather than making every caller hold a mutable copy.
        gekko_add_local_input(session_, player, const_cast<void*>(input));
    }

    const SessionEvent* Update(int* count) override {
        events_.clear();

        if (online_) {
            // Move packets first, then count the peers the move connected or
            // lost, so ConnectedPeers() after Update() reflects this pump.
            gekko_network_poll(session_);
            int m = 0;
            GekkoSessionEvent** se = gekko_session_events(session_, &m);
            for (int i = 0; i < m; ++i) {
                if (se[i]->type == GekkoPlayerConnected)    ++connected_;
                if (se[i]->type == GekkoPlayerDisconnected) --connected_;
                if (se[i]->type == GekkoDesyncDetected) {
                    desync_.frame          = se[i]->data.desynced.frame;
                    desync_.localChecksum  = se[i]->data.desynced.local_checksum;
                    desync_.remoteChecksum = se[i]->data.desynced.remote_checksum;
                    desync_.remotePlayer   = se[i]->data.desynced.remote_handle;
                    desynced_ = true;
                }
            }
        }

        int n = 0;
        GekkoGameEvent** raw = gekko_update_session(session_, &n);

        events_.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            SessionEvent e{};
            switch (raw[i]->type) {
            case GekkoAdvanceEvent:
                e.type         = SessionEventType::Advance;
                e.frame        = raw[i]->data.adv.frame;
                e.inputs       = raw[i]->data.adv.inputs;
                e.inputBytes   = raw[i]->data.adv.input_len;
                e.rollingBack  = raw[i]->data.adv.rolling_back;
                e.runningAhead = raw[i]->data.adv.running_ahead;
                break;
            case GekkoSaveEvent:
                e.type         = SessionEventType::Save;
                e.frame        = raw[i]->data.save.frame;
                e.saveBuffer   = raw[i]->data.save.state;
                e.saveCapacity = stateBytes_;
                e.saveLength   = raw[i]->data.save.state_len;
                e.saveChecksum = raw[i]->data.save.checksum;
                break;
            case GekkoLoadEvent:
                e.type       = SessionEventType::Load;
                e.frame      = raw[i]->data.load.frame;
                e.loadBuffer = raw[i]->data.load.state;
                e.loadBytes  = raw[i]->data.load.state_len;
                break;
            default:
                continue;   // GekkoEmptyGameEvent and anything added upstream
            }
            events_.push_back(e);
        }

        *count = static_cast<int>(events_.size());
        return events_.empty() ? nullptr : events_.data();
    }

    int FramesAhead() const override {
        // THE FLOAT STOPS HERE. gekko_frames_ahead returns f32, computed from an
        // i8 history and consumed by nobody inside GekkoNet (ADR-003 traced it:
        // GetAverageAdvantage -> FramesAhead -> this public function, and no
        // internal caller). Rounding at this boundary means the rule "no float
        // reaches the simulation" is enforced by our signature rather than by
        // their implementation continuing to behave.
        //
        // std::lround, not a cast: a cast truncates toward zero, so -0.6 would
        // become 0 and a client that is behind would be told it is level.
        return static_cast<int>(std::lround(gekko_frames_ahead(session_)));
    }

    bool PollDesync(DesyncReport* out) override {
        // An online session read the session events in Update() (they are
        // consumed by the read), so a desync is remembered there and reported
        // from here for the rest of the session's life -- a desync ends the
        // match (ADR-002 CHOICE C), it is not a transient to miss.
        if (desynced_) {
            if (out) *out = desync_;
            return true;
        }
        int n = 0;
        GekkoSessionEvent** ev = gekko_session_events(session_, &n);
        for (int i = 0; i < n; ++i) {
            if (ev[i]->type != GekkoDesyncDetected) continue;
            desync_.frame          = ev[i]->data.desynced.frame;
            desync_.localChecksum  = ev[i]->data.desynced.local_checksum;
            desync_.remoteChecksum = ev[i]->data.desynced.remote_checksum;
            desync_.remotePlayer   = ev[i]->data.desynced.remote_handle;
            desynced_ = true;
            if (out) *out = desync_;
            return true;
        }
        return false;
    }

    int ConnectedPeers() const override { return connected_ < 0 ? 0 : connected_; }

private:
    GekkoSession*             session_ = nullptr;
    std::uint32_t             stateBytes_ = 0;
    std::vector<SessionEvent> events_;
    bool                      online_ = false;
    int                       bridgeSlot_ = -1;
    int                       connected_ = 0;
    bool                      desynced_ = false;
    DesyncReport              desync_{};
};

ISession* create(GekkoSessionType type, const SessionConfig& cfg) {
    if (cfg.stateBytes == 0 || cfg.inputBytesPerPlayer == 0 || cfg.playerCount == 0) {
        return nullptr;
    }

    GekkoSession* s = nullptr;
    if (!gekko_create(&s, type)) return nullptr;

    GekkoConfig gc{};
    gc.num_players             = cfg.playerCount;
    gc.input_size              = cfg.inputBytesPerPlayer;
    gc.state_size              = cfg.stateBytes;
    gc.input_prediction_window = cfg.predictionWindow;
    gc.desync_detection        = cfg.desyncDetection;
    gc.check_distance          = cfg.desyncCheckInterval;
    gekko_start(s, &gc);

    for (std::uint8_t i = 0; i < cfg.playerCount; ++i) {
        gekko_add_actor(s, GekkoLocalPlayer, nullptr);
    }

    return new GekkoSessionImpl(s, cfg.stateBytes, /*online*/ false, /*bridge*/ -1);
}

} // namespace

ISession* CreateGekkoLocalSession(const SessionConfig& cfg) {
    return create(GekkoGameSession, cfg);
}

ISession* CreateGekkoStressSession(const SessionConfig& cfg) {
    return create(GekkoStressSession, cfg);
}

ISession* CreateGekkoOnlineSession(const SessionConfig& cfg, ITransport* transport) {
    if (cfg.stateBytes == 0 || cfg.inputBytesPerPlayer == 0 || cfg.playerCount == 0) return nullptr;
    if (cfg.peerAddresses.size() != cfg.playerCount) return nullptr;
    bool anyLocal = false;
    for (const std::string& a : cfg.peerAddresses) anyLocal = anyLocal || a.empty();
    if (!anyLocal) return nullptr;
    // A transport is REQUIRED. GekkoNet's own asio adapter is not compiled in
    // this tree (ThirdParty/CMakeLists.txt builds it NO_ASIO); the transport of
    // record is UdpTransport, ours, over the platform's sockets (ADR-021).
    if (transport == nullptr) return nullptr;

    const int bridge = claimBridge(transport);
    if (bridge < 0) return nullptr;

    GekkoSession* s = nullptr;
    if (!gekko_create(&s, GekkoGameSession)) { releaseBridge(bridge); return nullptr; }

    GekkoConfig gc{};
    gc.num_players             = cfg.playerCount;
    gc.input_size              = cfg.inputBytesPerPlayer;
    gc.state_size              = cfg.stateBytes;
    gc.input_prediction_window = cfg.predictionWindow;
    gc.desync_detection        = cfg.desyncDetection;
    gc.check_distance          = cfg.desyncCheckInterval;
    gekko_start(s, &gc);

    // The adapter before the actors: a remote actor's first handshake packet
    // leaves the moment it is added (the online example's order).
    gekko_net_adapter_set(s, bridgeAdapter(bridge));
    // Before the actors too, so the first remote actor is timed from the
    // configured silence, not the library's default. 0 would mean "never";
    // the config's comment forbids it, and the minimum of 1 ms keeps a caller
    // who passed 0 from switching disconnection off by accident.
    gekko_set_disconnect_timeout(s, cfg.disconnectTimeoutMs == 0u ? 1u : cfg.disconnectTimeoutMs);

    // Slot order, so the handle GekkoNet returns is the slot the caller will
    // name in AddLocalInput and read in the packed Advance inputs.
    for (std::uint8_t i = 0; i < cfg.playerCount; ++i) {
        const std::string& address = cfg.peerAddresses[i];
        if (address.empty()) {
            const int handle = gekko_add_actor(s, GekkoLocalPlayer, nullptr);
            gekko_set_local_delay(s, handle, cfg.localDelay);
        } else {
            GekkoNetAddress addr{};
            addr.data = const_cast<char*>(address.data());   // copied by GekkoNet on add
            addr.size = static_cast<unsigned int>(address.size());
            gekko_add_actor(s, GekkoRemotePlayer, &addr);
        }
    }

    return new GekkoSessionImpl(s, cfg.stateBytes, /*online*/ true, bridge);
}

void DestroySession(ISession* session) {
    delete session;
}

} // namespace cse::net
