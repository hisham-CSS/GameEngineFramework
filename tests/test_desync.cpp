// A DESYNC, NAMED (ROADMAP M2.3; DETERMINISM.md S8, T6; ADR-002 CHOICE C).
//
// S8: the reflection table covers every byte of GameState -- the static_asserts
// in StateReflection.h are the compile-time half, and here every byte is
// located to a field. Then the first divergent field is named from two states,
// and the whole flow between two kernels: one peer's simulation drifts, the
// session reports the frame, the peers swap their states at that frame over the
// transport the session used, and the artifact names the tick AND the field.
//
// Links CseGame (the naming, the history, the artifact), CseKernel, CseNet (the
// session, the exchange, the loopback). No Engine.
#include <gtest/gtest.h>

#include <cse/game/Desync.h>
#include <cse/kernel/Simulate.h>
#include <cse/kernel/StateReflection.h>
#include <cse/net/BlobExchange.h>
#include <cse/net/ISession.h>
#include <cse/net/LoopbackTransport.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace cse::kernel;
using namespace cse::net;
using cse::game::Divergence;
using cse::game::StateHistory;

TEST(StateReflection, TheTableNamesEveryByteOfGameState) {
    // The static_asserts already hold or this file would not compile; the
    // runtime half walks every byte to a field and checks the field contains it.
    EXPECT_EQ(CoveredBytes(kGameStateFields), sizeof(GameState));
    EXPECT_EQ(CoveredBytes(kFighterFields), sizeof(Fighter));
    EXPECT_EQ(CoveredBytes(kEventFields), sizeof(Event));
    std::map<std::string, int> tops;
    for (std::uint32_t byte = 0; byte < sizeof(GameState); ++byte) {
        FieldPath path;
        ASSERT_TRUE(LocateGameStateByte(byte, &path)) << "byte " << byte << " belongs to no field";
        EXPECT_LE(path.offset, byte);
        EXPECT_LT(byte, path.offset + path.width) << "byte " << byte << " lies outside the scalar it was located to";
        EXPECT_GE(path.width, 1u);
        EXPECT_LE(path.width, 4u);
        ++tops[path.top->name];
    }
    EXPECT_EQ(tops.size(), sizeof(kGameStateFields) / sizeof(kGameStateFields[0])) << "not every top-level field owns a byte";

    // A byte inside the second fighter's posX resolves to p[1].posX.
    GameState s{};
    const std::uint32_t byte = static_cast<std::uint32_t>(
        reinterpret_cast<const unsigned char*>(&s.p[1].posX) - reinterpret_cast<const unsigned char*>(&s)) + 2;
    FieldPath path;
    ASSERT_TRUE(LocateGameStateByte(byte, &path));
    EXPECT_STREQ(path.top->name, "p");
    EXPECT_EQ(path.topIndex, 1u);
    ASSERT_NE(path.member, nullptr);
    EXPECT_STREQ(path.member->name, "posX");
    EXPECT_EQ(path.width, 4u);
    EXPECT_EQ(path.type, FieldType::I32);
}

TEST(Desync, TheFirstDivergentFieldIsNamed) {
    GameState local{}, remote{};
    ResetMatch(local, 0xC0FFEEu);
    remote = local;
    Divergence d;
    EXPECT_FALSE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_FALSE(d.found);

    remote.p[1].health -= 7;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "p[1].health");
    EXPECT_EQ(d.tick, local.tick);
    EXPECT_EQ(d.local - d.remote, 7);
    EXPECT_EQ(d.width, 4u);

    remote = local; remote.rng ^= 0x80000000u;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "rng");

    remote = local; remote.ev[3].a = -5;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "ev[3].a");
    EXPECT_EQ(d.remote, -5);

    remote = local; remote.p[0].res[2] += 1;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "p[0].res[2]");

    remote = local; remote.roundsWon[1] = 2;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "roundsWon[1]");

    // The FIRST in table order when two differ: rng precedes every fighter.
    remote = local; remote.p[1].health -= 1; remote.rng += 1;
    ASSERT_TRUE(cse::game::FirstDivergence(local, remote, &d));
    EXPECT_EQ(d.field, "rng");

    const std::string json = cse::game::DesyncArtifactJson(42, 0x11111111u, 0x22222222u, 1, &d);
    EXPECT_NE(json.find("\"reportedFrame\": 42"), std::string::npos) << json;
    EXPECT_NE(json.find("\"field\": \"rng\""), std::string::npos) << json;
    EXPECT_NE(json.find("0x11111111"), std::string::npos) << json;
}

namespace {

struct WireInput { std::uint16_t bits; };

InputPair scriptedTick(int t) {
    InputPair in{};
    if (t % 7 == 0)  in.p[0].bits |= kInputRight;
    if (t % 11 == 0) in.p[0].bits |= kInputUp;
    if (t % 5 == 0)  in.p[1].bits |= kInputLeft;
    return in;
}

struct Peer {
    int          slot = 0;
    ISession*    session = nullptr;
    GameState    live{};
    StateHistory history;
    int          cheatFromIteration = -1;
    DesyncReport report{};
    bool         desynced = false;
};

SessionConfig onlineConfig(int localSlot, const char* remoteName) {
    SessionConfig cfg{};
    cfg.playerCount = 2; cfg.inputBytesPerPlayer = sizeof(WireInput); cfg.stateBytes = sizeof(GameState);
    cfg.predictionWindow = 8; cfg.desyncDetection = true; cfg.desyncCheckInterval = 8; cfg.localDelay = 2;
    cfg.peerAddresses = { localSlot == 0 ? std::string() : std::string(remoteName),
                          localSlot == 1 ? std::string() : std::string(remoteName) };
    return cfg;
}

void pump(Peer& p, int iteration) {
    WireInput w{ scriptedTick(iteration).p[p.slot].bits };
    p.session->AddLocalInput(p.slot, &w);
    int n = 0;
    const SessionEvent* ev = p.session->Update(&n);
    for (int i = 0; i < n; ++i) {
        switch (ev[i].type) {
        case SessionEventType::Save:
            *ev[i].saveLength = sizeof(GameState); *ev[i].saveChecksum = Checksum(p.live);
            std::memcpy(ev[i].saveBuffer, &p.live, sizeof(GameState));
            break;
        case SessionEventType::Load:
            std::memcpy(&p.live, ev[i].loadBuffer, sizeof(GameState));
            break;
        case SessionEventType::Advance: {
            const auto* wire = reinterpret_cast<const WireInput*>(ev[i].inputs);
            InputPair pair{};
            pair.p[0].bits = wire[0].bits; pair.p[1].bits = wire[1].bits;
            Simulate(p.live, pair);
            // The drift under test: a kernel that loses a point of health every
            // frame on one side only. Health feeds nothing back into the other
            // fighter for a long while, so the first divergent field is this one.
            if (p.cheatFromIteration >= 0 && iteration >= p.cheatFromIteration) p.live.p[p.slot].health -= 1;
            p.history.Push(p.live);
            break;
        }
        }
    }
    if (!p.desynced && p.session->PollDesync(&p.report)) p.desynced = true;
}

} // namespace

TEST(Desync, TwoPeersAbortAndTheArtifactNamesTheTickAndField) {
    LoopbackNetwork net;
    Peer a, b; a.slot = 0; b.slot = 1;
    ResetMatch(a.live, 0xC0FFEEu); ResetMatch(b.live, 0xC0FFEEu);
    a.session = CreateGekkoOnlineSession(onlineConfig(0, "B"), &net.Endpoint("A"));
    b.session = CreateGekkoOnlineSession(onlineConfig(1, "A"), &net.Endpoint("B"));
    ASSERT_NE(a.session, nullptr); ASSERT_NE(b.session, nullptr);
    b.cheatFromIteration = 120;
    int iterations = 0;
    for (; iterations < 400 && !(a.desynced || b.desynced); ++iterations) {
        pump(a, iterations); pump(b, iterations); net.Tick();
    }
    ASSERT_TRUE(a.desynced || b.desynced) << "no desync reported in 400 frames";
    // THE GRACE. The peer that detected first keeps its session pumping a
    // little longer, so its checksums keep flowing and the other side detects
    // too; a peer that tore its session down at once would leave the other
    // finishing the match alone after GekkoNet's disconnect timeout (seen over
    // UDP). The match is over from the first report; what continues is the
    // session's bookkeeping, not the game.
    for (int g = 0; g < 30; ++g, ++iterations) { pump(a, iterations); pump(b, iterations); net.Tick(); }
    EXPECT_TRUE(a.desynced) << "peer A never detected within the grace";
    EXPECT_TRUE(b.desynced) << "peer B never detected within the grace";
    const DesyncReport& report = a.desynced ? a.report : b.report;
    const std::uint32_t frame = static_cast<std::uint32_t>(report.frame);
    EXPECT_GE(frame, 100u) << "the desync is dated before the drift began";
    // GekkoNet's frame is the one whose ADVANCE produced the differing state:
    // the state to compare is the one after it, tick frame + 1.
    const std::uint32_t tick = frame + 1;

    // THE ABORT: the sessions go, and the transport that carried them now
    // carries the two states at that tick.
    DestroySession(a.session); DestroySession(b.session);
    a.session = b.session = nullptr;
    const GameState* mineA = a.history.Find(tick);
    const GameState* mineB = b.history.Find(tick);
    ASSERT_NE(mineA, nullptr) << "peer A no longer held tick " << tick;
    ASSERT_NE(mineB, nullptr) << "peer B no longer held tick " << tick;
    BlobExchange xa(net.Endpoint("A"), "B", tick, reinterpret_cast<const std::uint8_t*>(mineA), sizeof(GameState));
    BlobExchange xb(net.Endpoint("B"), "A", tick, reinterpret_cast<const std::uint8_t*>(mineB), sizeof(GameState));
    for (int i = 0; i < 64 && !(xa.Complete() && xb.Complete()); ++i) { xa.Pump(); xb.Pump(); net.Tick(); }
    ASSERT_TRUE(xa.Complete()) << "peer A never received B's state";
    ASSERT_TRUE(xb.Complete()) << "peer B never received A's state";
    ASSERT_EQ(xa.Theirs().size(), sizeof(GameState));
    EXPECT_EQ(xa.TheirTag(), tick);

    GameState theirsA{}, theirsB{};
    std::memcpy(&theirsA, xa.Theirs().data(), sizeof(GameState));
    std::memcpy(&theirsB, xb.Theirs().data(), sizeof(GameState));
    Divergence da, db;
    ASSERT_TRUE(cse::game::FirstDivergence(*mineA, theirsA, &da)) << "A sees no divergence at the reported frame";
    ASSERT_TRUE(cse::game::FirstDivergence(*mineB, theirsB, &db)) << "B sees no divergence at the reported frame";
    EXPECT_EQ(da.field, "p[1].health");
    EXPECT_EQ(db.field, "p[1].health");
    EXPECT_EQ(da.tick, tick);
    EXPECT_EQ(da.local, db.remote);   // the same two values, seen from each side
    EXPECT_EQ(da.remote, db.local);
    EXPECT_GT(da.local, da.remote) << "the peer that lost health is the remote, from A's side";

    const std::string json = cse::game::DesyncArtifactJson(frame, report.localChecksum, report.remoteChecksum, report.remotePlayer, &da);
    EXPECT_NE(json.find("\"field\": \"p[1].health\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"reportedFrame\": " + std::to_string(frame)), std::string::npos) << json;
    const std::string path = "desync_test_artifact/desync.json";
    std::string error;
    ASSERT_TRUE(cse::game::WriteDesyncArtifact(path, json, &error)) << error;
    std::ifstream in(path, std::ios::binary);
    const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(back, json);
    std::error_code ec;
    std::filesystem::remove_all("desync_test_artifact", ec);
}
