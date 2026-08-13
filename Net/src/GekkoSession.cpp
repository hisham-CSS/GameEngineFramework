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
#include <vector>

namespace cse::net {
namespace {

class GekkoSessionImpl final : public ISession {
public:
    GekkoSessionImpl(GekkoSession* s, std::uint32_t stateBytes)
        : session_(s), stateBytes_(stateBytes) {}

    ~GekkoSessionImpl() override {
        if (session_) gekko_destroy(&session_);
    }

    void AddLocalInput(int player, const void* input) override {
        // GekkoNet's signature takes void* rather than const void*; it copies
        // immediately and does not retain or modify. const_cast is the honest
        // spelling of that rather than making every caller hold a mutable copy.
        gekko_add_local_input(session_, player, const_cast<void*>(input));
    }

    const SessionEvent* Update(int* count) override {
        events_.clear();

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
        int n = 0;
        GekkoSessionEvent** ev = gekko_session_events(session_, &n);
        for (int i = 0; i < n; ++i) {
            if (ev[i]->type != GekkoDesyncDetected) continue;
            if (out) {
                out->frame          = ev[i]->data.desynced.frame;
                out->localChecksum  = ev[i]->data.desynced.local_checksum;
                out->remoteChecksum = ev[i]->data.desynced.remote_checksum;
                out->remotePlayer   = ev[i]->data.desynced.remote_handle;
            }
            return true;
        }
        return false;
    }

private:
    GekkoSession*             session_ = nullptr;
    std::uint32_t             stateBytes_ = 0;
    std::vector<SessionEvent> events_;
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

    return new GekkoSessionImpl(s, cfg.stateBytes);
}

} // namespace

ISession* CreateGekkoLocalSession(const SessionConfig& cfg) {
    return create(GekkoGameSession, cfg);
}

ISession* CreateGekkoStressSession(const SessionConfig& cfg) {
    return create(GekkoStressSession, cfg);
}

void DestroySession(ISession* session) {
    delete session;
}

} // namespace cse::net
