// The kernel driven through the rollback session seam.
//
// This is ADR-003's building-spike harness, promoted from a scratch file into a
// test that runs on every build. It earned that: it is the only thing in the
// suite that exercises save, load and re-simulation together, and it is the
// property the entire netcode plan rests on.
//
// It deliberately includes cse/net/ISession.h and NOT gekkonet.h. If this file
// ever needs the latter, the seam has leaked and CseNet's PRIVATE link has
// stopped meaning anything.
#include <gtest/gtest.h>

#include "cse/kernel/Simulate.h"
#include "cse/net/ISession.h"
#include "cse/net/LoopbackTransport.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

using namespace cse::kernel;
using namespace cse::net;

namespace {

// Two bytes per player on the wire. Deliberately NOT cse::kernel::Input itself:
// the session's input size is a network concern and the kernel's input type is a
// simulation concern, and letting them be the same type by accident is how they
// end up coupled.
struct WireInput { std::uint16_t bits; };

SessionConfig defaultConfig() {
    SessionConfig cfg{};
    cfg.playerCount         = 2;
    cfg.inputBytesPerPlayer = sizeof(WireInput);
    cfg.stateBytes          = sizeof(GameState);
    cfg.predictionWindow    = 8;
    cfg.desyncDetection     = true;
    cfg.desyncCheckInterval = 8;
    return cfg;
}

InputPair scriptedTick(int t) {
    InputPair in{};
    if (t % 7 == 0)  in.p[0].bits |= kInputRight;
    if (t % 11 == 0) in.p[0].bits |= kInputUp;
    if (t % 5 == 0)  in.p[1].bits |= kInputLeft;
    return in;
}

std::vector<InputPair> scriptedMatch(int ticks) {
    std::vector<InputPair> seq;
    seq.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) seq.push_back(scriptedTick(t));
    return seq;
}

struct RunCounts {
    int advances = 0, saves = 0, loads = 0, rollbackAdvances = 0;
};

// Drive `ticks` of the scripted match through a session, returning the final
// state and what the session asked us to do along the way.
GameState runThroughSession(ISession& session,
                            const std::vector<InputPair>& seq,
                            RunCounts* counts) {
    GameState live{};
    ResetMatch(live, 0xC0FFEEu);

    for (std::size_t t = 0; t < seq.size(); ++t) {
        WireInput a{ seq[t].p[0].bits };
        WireInput b{ seq[t].p[1].bits };
        session.AddLocalInput(0, &a);
        session.AddLocalInput(1, &b);

        int n = 0;
        const SessionEvent* ev = session.Update(&n);
        for (int i = 0; i < n; ++i) {
            switch (ev[i].type) {
            case SessionEventType::Save:
                // ARCHITECTURE.md D4: the snapshot is a memcpy. No serializer,
                // no adapter, no field-by-field copy that could forget a member.
                EXPECT_GE(ev[i].saveCapacity, sizeof(GameState));
                *ev[i].saveLength   = sizeof(GameState);
                *ev[i].saveChecksum = Checksum(live);
                std::memcpy(ev[i].saveBuffer, &live, sizeof(GameState));
                ++counts->saves;
                break;

            case SessionEventType::Load:
                EXPECT_EQ(sizeof(GameState), ev[i].loadBytes);
                std::memcpy(&live, ev[i].loadBuffer, sizeof(GameState));
                ++counts->loads;
                break;

            case SessionEventType::Advance: {
                const auto* wire = reinterpret_cast<const WireInput*>(ev[i].inputs);
                InputPair pair{};
                pair.p[0].bits = wire[0].bits;
                pair.p[1].bits = wire[1].bits;
                Simulate(live, pair);
                ++counts->advances;
                if (ev[i].rollingBack) ++counts->rollbackAdvances;
                break;
            }
            }
        }
    }
    return live;
}

} // namespace

TEST(Session, CreatesAndTearsDown) {
    ISession* s = CreateGekkoLocalSession(defaultConfig());
    ASSERT_NE(nullptr, s);
    DestroySession(s);
}

TEST(Session, RejectsAnUnconfiguredState) {
    // A zero state size would mean the session allocates nothing and every Save
    // writes into a null buffer. Better to fail at creation than at tick 200.
    SessionConfig bad = defaultConfig();
    bad.stateBytes = 0;
    EXPECT_EQ(nullptr, CreateGekkoLocalSession(bad));
}

TEST(Session, FramesAheadIsAnIntegerAtTheBoundary) {
    // ADR-003's mitigation, as a type. GekkoNet computes frame advantage in f32;
    // the trace proved it never re-enters their library, and this signature
    // means it can never enter OURS regardless of what upstream does later.
    ISession* s = CreateGekkoLocalSession(defaultConfig());
    ASSERT_NE(nullptr, s);
    static_assert(std::is_same_v<decltype(s->FramesAhead()), int>,
                  "FramesAhead must return int. A float here would put a "
                  "platform-variable quantity one step from simulation timing, "
                  "which is the thing NORTHSTAR Q1 (crossplay) cannot survive.");
    EXPECT_EQ(0, s->FramesAhead());   // no remote peer in a local session
    DestroySession(s);
}

TEST(Session, LocalSessionMatchesTheKernelRunningAlone) {
    const auto seq = scriptedMatch(240);

    GameState reference{};
    ResetMatch(reference, 0xC0FFEEu);
    for (const auto& in : seq) Simulate(reference, in);

    ISession* s = CreateGekkoLocalSession(defaultConfig());
    ASSERT_NE(nullptr, s);

    RunCounts c{};
    const GameState viaSession = runThroughSession(*s, seq, &c);
    DestroySession(s);

    EXPECT_EQ(240, c.advances);
    EXPECT_EQ(0, std::memcmp(&reference, &viaSession, sizeof(GameState)))
        << "the session layer changed the result of a run with no rollbacks in it";
    EXPECT_EQ(Checksum(reference), Checksum(viaSession));
}

TEST(Session, SurvivesHundredsOfRealRollbacks) {
    // THE TEST. A stress session rolls back continuously to hunt divergence, so
    // this drives the kernel through hundreds of save/load/re-simulate cycles
    // and demands the end state be byte-identical to a straight run.
    //
    // It is simultaneously two proofs. That the session integration is correct
    // (state survives the round trip), and that the KERNEL is deterministic
    // under real rollback rather than only under test_kernel.cpp's synthetic
    // rewind — because a kernel that read a clock, a global, or an unseeded RNG
    // would diverge here and nowhere else.
    const auto seq = scriptedMatch(240);

    GameState reference{};
    ResetMatch(reference, 0xC0FFEEu);
    for (const auto& in : seq) Simulate(reference, in);

    ISession* s = CreateGekkoStressSession(defaultConfig());
    ASSERT_NE(nullptr, s);

    RunCounts c{};
    const GameState viaSession = runThroughSession(*s, seq, &c);
    DestroySession(s);

    // Guard against the vacuous version: if the stress session stopped rolling
    // back, this test would pass while proving nothing at all.
    EXPECT_GT(c.loads, 50)
        << "the stress session performed almost no rollbacks (" << c.loads
        << "), so this test is not exercising what it claims to";
    EXPECT_GT(c.rollbackAdvances, 100)
        << "almost nothing was re-simulated (" << c.rollbackAdvances << ")";
    EXPECT_GT(c.advances, 240)
        << "advances (" << c.advances << ") should exceed the tick count, "
           "because re-simulated ticks are advances too";

    EXPECT_EQ(0, std::memcmp(&reference, &viaSession, sizeof(GameState)))
        << "state diverged after " << c.loads << " rollbacks and "
        << c.rollbackAdvances << " re-simulated ticks";
    EXPECT_EQ(Checksum(reference), Checksum(viaSession));
}

TEST(Session, NoDesyncInASingleProcess) {
    // Two local players in one process share one simulation, so a reported
    // desync would mean our save/load is lossy rather than that the peers
    // disagree. This is the cheapest possible check that the memcpy round-trip
    // is faithful, and it is free given desync detection is already on.
    const auto seq = scriptedMatch(120);
    ISession* s = CreateGekkoStressSession(defaultConfig());
    ASSERT_NE(nullptr, s);

    RunCounts c{};
    runThroughSession(*s, seq, &c);

    DesyncReport report{};
    const bool desynced = s->PollDesync(&report);
    DestroySession(s);

    EXPECT_FALSE(desynced)
        << "desync at frame " << report.frame << ": local "
        << report.localChecksum << " vs remote " << report.remoteChecksum;
}
// ---------------------------------------------------------------------------
// ONLINE (ROADMAP M2.1): two sessions, one match, through the transport seam.
//
// Each peer owns one player, one GameState and one endpoint of an in-process
// LoopbackNetwork; the other player arrives through the session as a remote
// actor. What the tests hold is the property the netcode plan rests on, now
// between two kernels rather than inside one: every frame both peers have
// confirmed carries the same checksum on both sides -- through latency, which
// forces prediction and rollback, and through loss, which the session's own
// input redundancy must survive -- and a peer whose kernel diverges is named.
// No socket: LoopbackNetwork is deterministic, so a pass here passes every
// time, and the two-process UDP run (tests/two_peers.py) is the transport's
// own test.
// ---------------------------------------------------------------------------
namespace {

struct Peer {
    int        slot = 0;
    ISession*  session = nullptr;
    GameState  live{};
    RunCounts  counts{};
    // The checksum after each frame was simulated, overwritten as rollbacks
    // re-simulate it: the last value is the corrected one.
    std::map<int, std::uint32_t> checksumAfterFrame;
    int        confirmedFrames = 0;
    // A peer that starts cheating on this iteration nudges its own posX after
    // every advance from then on -- a kernel that diverges, for the desync test.
    int        cheatFromIteration = -1;
};

SessionConfig onlineConfig(int localSlot, const char* remoteName) {
    SessionConfig cfg = defaultConfig();
    cfg.localDelay    = 2;
    cfg.peerAddresses = { localSlot == 0 ? std::string() : std::string(remoteName),
                          localSlot == 1 ? std::string() : std::string(remoteName) };
    return cfg;
}

void pumpPeer(Peer& p, int iteration) {
    WireInput w{ scriptedTick(iteration).p[p.slot].bits };
    p.session->AddLocalInput(p.slot, &w);
    int n = 0;
    const SessionEvent* ev = p.session->Update(&n);
    for (int i = 0; i < n; ++i) {
        switch (ev[i].type) {
        case SessionEventType::Save:
            *ev[i].saveLength   = sizeof(GameState);
            *ev[i].saveChecksum = Checksum(p.live);
            std::memcpy(ev[i].saveBuffer, &p.live, sizeof(GameState));
            ++p.counts.saves;
            break;
        case SessionEventType::Load:
            std::memcpy(&p.live, ev[i].loadBuffer, sizeof(GameState));
            ++p.counts.loads;
            break;
        case SessionEventType::Advance: {
            const auto* wire = reinterpret_cast<const WireInput*>(ev[i].inputs);
            InputPair pair{};
            pair.p[0].bits = wire[0].bits;
            pair.p[1].bits = wire[1].bits;
            Simulate(p.live, pair);
            if (p.cheatFromIteration >= 0 && iteration >= p.cheatFromIteration) p.live.p[p.slot].posX += 1;
            p.checksumAfterFrame[ev[i].frame] = Checksum(p.live);
            ++p.counts.advances;
            if (ev[i].rollingBack) ++p.counts.rollbackAdvances;
            if (!ev[i].rollingBack && !ev[i].runningAhead) p.confirmedFrames = ev[i].frame + 1;
            break;
        }
        }
    }
}

// Both peers for `iterations` frames of the network's clock; the frames both
// have confirmed by the end.
int runPeers(Peer& a, Peer& b, LoopbackNetwork& net, int iterations, int firstIteration = 0) {
    for (int t = firstIteration; t < firstIteration + iterations; ++t) {
        pumpPeer(a, t);
        pumpPeer(b, t);
        net.Tick();
    }
    return std::min(a.confirmedFrames, b.confirmedFrames);
}

// Every frame both peers have simulated, up to the confirmed horizon less the
// prediction window (frames still open to revision), carries one checksum.
void expectAgreement(const Peer& a, const Peer& b, int confirmed, int predictionWindow) {
    const int safe = confirmed - predictionWindow - 1;
    ASSERT_GT(safe, 32) << "the peers barely advanced (" << confirmed << " confirmed frames)";
    for (int f = 0; f < safe; ++f) {
        const auto ia = a.checksumAfterFrame.find(f);
        const auto ib = b.checksumAfterFrame.find(f);
        ASSERT_NE(ia, a.checksumAfterFrame.end()) << "peer A never simulated frame " << f;
        ASSERT_NE(ib, b.checksumAfterFrame.end()) << "peer B never simulated frame " << f;
        EXPECT_EQ(ia->second, ib->second) << "the peers disagree after frame " << f;
    }
}

struct TwoPeers {
    LoopbackNetwork net;
    Peer a, b;
    TwoPeers() {
        a.slot = 0; b.slot = 1;
        ResetMatch(a.live, 0xC0FFEEu);
        ResetMatch(b.live, 0xC0FFEEu);
        a.session = CreateGekkoOnlineSession(onlineConfig(0, "B"), &net.Endpoint("A"));
        b.session = CreateGekkoOnlineSession(onlineConfig(1, "A"), &net.Endpoint("B"));
    }
    ~TwoPeers() {
        if (a.session) DestroySession(a.session);
        if (b.session) DestroySession(b.session);
    }
    void expectNoDesync() {
        DesyncReport r{};
        EXPECT_FALSE(a.session->PollDesync(&r)) << "peer A reported a desync at frame " << r.frame;
        EXPECT_FALSE(b.session->PollDesync(&r)) << "peer B reported a desync at frame " << r.frame;
    }
};

} // namespace

TEST(Session, TwoPeersOverALoopbackTransportAgreeOnEveryChecksum) {
    TwoPeers t;
    ASSERT_NE(nullptr, t.a.session) << "the online session could not be created (a bridge slot, a config?)";
    ASSERT_NE(nullptr, t.b.session);
    const int confirmed = runPeers(t.a, t.b, t.net, 400);
    EXPECT_GE(t.a.session->ConnectedPeers(), 1) << "peer A never saw B connect";
    EXPECT_GE(t.b.session->ConnectedPeers(), 1) << "peer B never saw A connect";
    t.expectNoDesync();
    expectAgreement(t.a, t.b, confirmed, 8);
    EXPECT_GT(t.net.PacketsSent(), 100u) << "the peers did not talk";
}

TEST(Session, LatencyForcesRollbacksAndBothPeersConverge) {
    TwoPeers t;
    ASSERT_NE(nullptr, t.a.session);
    ASSERT_NE(nullptr, t.b.session);
    t.net.SetLatencyFrames(4);   // beyond the local delay of 2: inputs arrive after the frame was predicted
    const int confirmed = runPeers(t.a, t.b, t.net, 400);
    t.expectNoDesync();
    EXPECT_GT(t.a.counts.loads + t.b.counts.loads, 0)
        << "four frames of latency produced no rollback; the test is not exercising what it claims";
    expectAgreement(t.a, t.b, confirmed, 8);
}

TEST(Session, LossIsSurvivedByTheSessionsOwnRedundancy) {
    TwoPeers t;
    ASSERT_NE(nullptr, t.a.session);
    ASSERT_NE(nullptr, t.b.session);
    // Connect first: the handshake is a handful of packets, and a network that
    // eats one of the first four never lets the match start -- which is a
    // finding about the handshake, not about the redundancy under test here.
    runPeers(t.a, t.b, t.net, 60);
    ASSERT_GE(t.a.session->ConnectedPeers(), 1);
    t.net.SetLatencyFrames(1);
    t.net.SetDropEvery(4);       // from here, one packet in four vanishes, every direction
    const int confirmed = runPeers(t.a, t.b, t.net, 400, 60);
    EXPECT_GT(t.net.PacketsDropped(), 50u) << "the network dropped almost nothing";
    t.expectNoDesync();
    expectAgreement(t.a, t.b, confirmed, 8);
}

// ADR-002 CHOICE C, seen firing between two kernels: a peer whose simulation
// diverges is reported with the frame, not corrected. Peer B starts nudging
// its own position after every advance; the periodic checksum exchange catches
// it on one side or the other within a few check intervals.
TEST(Session, ADivergentPeerIsReportedAndNamed) {
    TwoPeers t;
    ASSERT_NE(nullptr, t.a.session);
    ASSERT_NE(nullptr, t.b.session);
    runPeers(t.a, t.b, t.net, 120);
    t.expectNoDesync();
    t.b.cheatFromIteration = 120;
    runPeers(t.a, t.b, t.net, 200, 120);
    DesyncReport ra{}, rb{};
    const bool a = t.a.session->PollDesync(&ra), b = t.b.session->PollDesync(&rb);
    EXPECT_TRUE(a || b) << "a diverging peer went unreported for 200 frames";
    const DesyncReport& r = a ? ra : rb;
    EXPECT_GT(r.frame, 100) << "the desync is dated before the divergence began";
    EXPECT_NE(r.localChecksum, r.remoteChecksum);
}
