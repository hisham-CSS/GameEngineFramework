// THE CONNECT HANDSHAKE (ROADMAP M2.2; DETERMINISM.md A4, A5).
//
// A4: the hash is over the LOADED POD ARRAYS -- two texts of the same
// character that differ in whitespace, key order or number spelling load to
// the same MatchData and hash alike; a text that changes one frame count does
// not. A5: a mismatch is a lobby error naming the field and both values,
// before any session exists to desync. And the whole flow: two peers offer,
// agree, and the same transport then carries their session.
//
// Links CseGame for HashMatchData (the one function that answers "the same
// data?" for replays and peers), CseData to load, CseKernel to build, CseNet
// for the handshake and the session. No Engine, no GL.
#include <gtest/gtest.h>

#include <cse/data/CharacterData.h>
#include <cse/data/MatchBuilder.h>
#include <cse/game/Replay.h>
#include <cse/kernel/Simulate.h>
#include <cse/net/Handshake.h>
#include <cse/net/ISession.h>
#include <cse/net/LoopbackTransport.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cse::net;
using cse::data::CharacterData;
using cse::data::LoadOptions;
using cse::data::LoadReport;
using json = nlohmann::json;

namespace {

std::string charactersDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / "fighter_a.json")) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
}

std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The character as the kernel will run it: load from TEXT (so the test can
// re-spell it), build the mirror match, hash the loaded arrays.
bool hashOfText(const std::string& text, std::uint32_t* hash, std::string* error) {
    LoadOptions o;
    o.expectedResources = { "meter", "juggle" };
    o.contentRoot = charactersDir();
    CharacterData c; LoadReport r;
    if (!cse::data::LoadCharacterJson("fighter_a.json", text, o, c, r)) { *error = r.error; return false; }
    cse::data::BuildOptions options{};
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    for (const cse::data::Move& mv : c.moves) {
        cse::data::MoveBinding b{};
        b.moveId = mv.id;
        b.button = static_cast<std::uint16_t>(options.bindings.size() + 1);
        options.bindings.push_back(b);
    }
    cse::data::MatchBuild build{};
    if (!cse::data::BuildMatchData(c, options, c, options, build)) { *error = "the match did not build"; return false; }
    *hash = cse::game::HashMatchData(build.data);
    return true;
}

HandshakeOffer offerFor(std::uint32_t contentHash, std::uint8_t slot) {
    HandshakeOffer o;
    o.contentHash = contentHash;
    o.stateBytes  = sizeof(cse::kernel::GameState);
    o.inputBytes  = 2;
    o.seed        = 0xC0FFEEu;
    o.playerCount = 2;
    o.slot        = slot;
    return o;
}

// Pump both handshakes over the network until neither is waiting, or `limit`.
int settle(Handshake& a, Handshake& b, LoopbackNetwork& net, int limit) {
    int pumps = 0;
    for (; pumps < limit; ++pumps) {
        a.Pump(); b.Pump(); net.Tick();
        if (a.Result().state != HandshakeState::Waiting && b.Result().state != HandshakeState::Waiting) break;
    }
    return pumps;
}

} // namespace

// A4. The shipped file re-spelled -- nlohmann re-serialises with sorted keys,
// one-space indentation and its own number formatting -- loads to the same
// arrays and hashes the same; one frame added to stand_lp's recovery does not.
TEST(Handshake, HashesTheLoadedPodArraysNotTheText) {
    const std::string original = readText(charactersDir() + "/fighter_a.json");
    ASSERT_FALSE(original.empty()) << "fighter_a.json not found under " << charactersDir();
    json doc = json::parse(original, nullptr, false);
    ASSERT_FALSE(doc.is_discarded());
    const std::string respelled = doc.dump(1);
    ASSERT_NE(original, respelled) << "the re-spelling changed nothing, so this test proves nothing";

    std::uint32_t h1 = 0, h2 = 0, h3 = 0;
    std::string err;
    ASSERT_TRUE(hashOfText(original, &h1, &err)) << err;
    ASSERT_TRUE(hashOfText(respelled, &h2, &err)) << err;
    EXPECT_EQ(h1, h2) << "the same loaded arrays hashed differently after a re-spelling of the text";

    for (json& mv : doc["moves"])
        if (mv["id"] == "stand_lp") mv["recovery"] = mv["recovery"].get<int>() + 1;
    // A21 wants the clip to match: hash the changed data without the model.
    doc["engine"].erase("anim3d");
    ASSERT_TRUE(hashOfText(doc.dump(1), &h3, &err)) << err;
    EXPECT_NE(h1, h3) << "one frame more of recovery did not change the hash";
}

// Both peers loaded the same data: they agree, and the same two endpoints then
// carry an online session whose first frames advance on both sides.
TEST(Handshake, PeersWithTheSameLoadedDataAgreeAndTheSessionFollows) {
    LoopbackNetwork net;
    ITransport& ta = net.Endpoint("A");
    ITransport& tb = net.Endpoint("B");
    Handshake a(ta, "B", offerFor(0x12345678u, 0));
    Handshake b(tb, "A", offerFor(0x12345678u, 1));
    const int pumps = settle(a, b, net, 60);
    EXPECT_LT(pumps, 5) << "two peers on a perfect network took " << pumps << " frames to agree";
    ASSERT_EQ(a.Result().state, HandshakeState::Agreed) << a.Result().reason;
    ASSERT_EQ(b.Result().state, HandshakeState::Agreed) << b.Result().reason;
    EXPECT_EQ(a.Result().theirs.slot, 1);
    EXPECT_EQ(b.Result().theirs.slot, 0);
    EXPECT_TRUE(a.Result().reason.empty());

    // The session is created ON the handshake, which keeps pumping its grace
    // resends alongside it, peels the peer's late offers off, and hands the
    // session every other packet.
    SessionConfig cfg{};
    cfg.playerCount = 2; cfg.inputBytesPerPlayer = 2; cfg.stateBytes = sizeof(cse::kernel::GameState);
    cfg.predictionWindow = 8; cfg.desyncDetection = true; cfg.desyncCheckInterval = 8; cfg.localDelay = 2;
    cfg.peerAddresses = { "", "B" };
    ISession* sa = CreateGekkoOnlineSession(cfg, &a);
    cfg.peerAddresses = { "A", "" };
    ISession* sb = CreateGekkoOnlineSession(cfg, &b);
    ASSERT_NE(sa, nullptr); ASSERT_NE(sb, nullptr);
    int advancesA = 0, advancesB = 0;
    for (int t = 0; t < 120; ++t) {
        a.Pump(); b.Pump();
        std::uint16_t in = 0;
        sa->AddLocalInput(0, &in); sb->AddLocalInput(1, &in);
        int n = 0;
        const SessionEvent* ev = sa->Update(&n);
        for (int i = 0; i < n; ++i) if (ev[i].type == SessionEventType::Advance) ++advancesA;
        ev = sb->Update(&n);
        for (int i = 0; i < n; ++i) if (ev[i].type == SessionEventType::Advance) ++advancesB;
        net.Tick();
    }
    EXPECT_GT(advancesA, 50) << "peer A's session did not start after the handshake";
    EXPECT_GT(advancesB, 50) << "peer B's session did not start after the handshake";
    EXPECT_GE(sa->ConnectedPeers(), 1);
    EXPECT_GT(a.PacketsQueued(), 0u) << "no session packet ever passed through the handshake";
    (void)ta; (void)tb;
    DestroySession(sa); DestroySession(sb);
}

// A5. Different loaded data is refused by name, with both hashes, before any
// session exists -- a lobby error, not a desync at tick 3.
TEST(Handshake, AContentMismatchIsALobbyErrorNamingTheHash) {
    LoopbackNetwork net;
    Handshake a(net.Endpoint("A"), "B", offerFor(0xAAAA0001u, 0));
    Handshake b(net.Endpoint("B"), "A", offerFor(0xAAAA0002u, 1));
    settle(a, b, net, 60);
    ASSERT_EQ(a.Result().state, HandshakeState::Refused);
    ASSERT_EQ(b.Result().state, HandshakeState::Refused);
    EXPECT_NE(a.Result().reason.find("content hash"), std::string::npos) << a.Result().reason;
    EXPECT_NE(a.Result().reason.find("0xaaaa0001"), std::string::npos) << a.Result().reason;
    EXPECT_NE(a.Result().reason.find("0xaaaa0002"), std::string::npos) << a.Result().reason;
    EXPECT_NE(b.Result().reason.find("0xaaaa0002"), std::string::npos) << b.Result().reason;
}

// The order of the verdict: a build whose GameState differs is named as a
// build before its content is compared; two peers claiming one slot are named
// as a seating problem after everything else agreed.
TEST(Handshake, TheFirstDisagreementIsTheReason) {
    HandshakeOffer mine = offerFor(1u, 0), theirs = offerFor(2u, 0);
    theirs.stateBytes += 8;
    HandshakeResult r = Handshake::Compare(mine, theirs);
    EXPECT_EQ(r.state, HandshakeState::Refused);
    EXPECT_NE(r.reason.find("state bytes"), std::string::npos) << r.reason;
    EXPECT_EQ(r.reason.find("content hash"), std::string::npos) << "the hash was named before the size: " << r.reason;

    theirs = offerFor(1u, 0);
    r = Handshake::Compare(mine, theirs);
    EXPECT_EQ(r.state, HandshakeState::Refused);
    EXPECT_NE(r.reason.find("both peers claim slot 0"), std::string::npos) << r.reason;

    theirs.protocol = kHandshakeProtocol + 1;
    r = Handshake::Compare(mine, theirs);
    EXPECT_NE(r.reason.find("protocol"), std::string::npos) << r.reason;
}

// The offer is sent every pump, so a network that eats one packet in three
// still lets the peers agree; a wire that is not an offer is queued for the
// session, never misread as one.
TEST(Handshake, SurvivesLossAndQueuesWhatIsNotAnOffer) {
    LoopbackNetwork net;
    net.SetDropEvery(3);
    ITransport& ta = net.Endpoint("A");
    Handshake a(ta, "B", offerFor(7u, 0));
    Handshake b(net.Endpoint("B"), "A", offerFor(7u, 1));
    const std::uint8_t junk[5] = { 1, 2, 3, 4, 5 };
    net.Endpoint("B").Send("A", junk, 5);
    settle(a, b, net, 60);
    EXPECT_EQ(a.Result().state, HandshakeState::Agreed) << a.Result().reason;
    EXPECT_EQ(b.Result().state, HandshakeState::Agreed) << b.Result().reason;
    EXPECT_GT(net.PacketsDropped(), 0u);
    EXPECT_GE(a.PacketsQueued(), 1u) << "the junk packet was not queued for the session";
    const std::vector<TransportPacket> handed = a.Receive();
    ASSERT_EQ(handed.size(), 1u) << "the queued junk was not handed through";
    EXPECT_EQ(handed[0].bytes.size(), 5u);
    HandshakeOffer decoded{};
    EXPECT_FALSE(Handshake::Decode(junk, 5, &decoded));
    std::uint8_t wire[Handshake::kWireBytes];
    Handshake::Encode(offerFor(0xDEADBEEFu, 1), wire);
    ASSERT_TRUE(Handshake::Decode(wire, Handshake::kWireBytes, &decoded));
    EXPECT_EQ(decoded.contentHash, 0xDEADBEEFu);
    EXPECT_EQ(decoded.slot, 1);
    EXPECT_EQ(decoded.stateBytes, sizeof(cse::kernel::GameState));
}
