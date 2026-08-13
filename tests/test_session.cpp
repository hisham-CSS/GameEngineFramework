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

#include <cstring>
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

std::vector<InputPair> scriptedMatch(int ticks) {
    std::vector<InputPair> seq;
    seq.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        InputPair in{};
        if (t % 7 == 0)  in.p[0].bits |= kInputRight;
        if (t % 11 == 0) in.p[0].bits |= kInputUp;
        if (t % 5 == 0)  in.p[1].bits |= kInputLeft;
        seq.push_back(in);
    }
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
