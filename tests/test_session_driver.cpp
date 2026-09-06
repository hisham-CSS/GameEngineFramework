// The session owns the tick count (ROADMAP M2.4; DETERMINISM.md T1, T2).
//
// A rollback session (cse::net::ISession) is a stream of Save, Load and
// Advance events; the FightSession is a kernel with Snapshot, Restore and
// Tick(inputs). SessionDriver (Games/UntitledFighter/Modes/src) is the glue,
// and the property it exists for is the one this file measures: the number
// of ticks the kernel runs is EXACTLY the number of Advance events, whatever
// the host's frame count -- more than the frames when the session rolls back
// (T2: the fixed step never decided the tick count), zero on a frame the
// session does not advance (T1: legal), and never one fewer (T1: dropping is
// not).
//
// The mode's half -- what a live session makes inert -- is
// tests/test_fight_mode.cpp; this file needs no mode, only the driver, a
// FightSession built from the shipped character and the session factories.
#include <gtest/gtest.h>

#include "SessionDriver.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/game/FightSession.h"
#include "cse/kernel/GameState.h"
#include "cse/net/ISession.h"
#include "cse/net/LoopbackTransport.h"

#include <cstring>
#include <filesystem>
#include <string>

using cse::kernel::GameState;
using cse::kernel::Input;
using cse::kernel::InputPair;
using cse::net::CreateGekkoLocalSession;
using cse::net::CreateGekkoOnlineSession;
using cse::net::CreateGekkoStressSession;
using cse::net::DestroySession;
using cse::net::ISession;
using cse::net::LoopbackNetwork;
using cse::net::SessionConfig;
using untitledfighter::SessionDriver;

namespace {

// Same walk-up as test_press_delivery.cpp: the staged copy first, the source
// tree for a bare IDE run.
std::string charactersDir() {
    namespace fs = std::filesystem;
    const char* const marker = "fighter_a.json";
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / marker)) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Games" / "UntitledFighter" / "Assets" / "Characters";
        if (fs::exists(source / marker)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
}

// The shipped character, built and begun: the kernel the driver drives. The
// MatchBuild is a member because FightSetup::data is borrowed for every tick
// including the re-simulated ones (FightSession.h).
struct Fight {
    cse::data::MatchBuild   build{};
    cse::game::FightSession session{};

    bool BringUp(std::string& error) {
        cse::data::CharacterData character{};
        cse::data::LoadReport    report{};
        cse::data::LoadOptions   options;
        options.expectedResources = { "meter", "juggle" };
        if (!cse::data::LoadCharacterFile(charactersDir(), "fighter_a.json", options, character, report)) {
            error = report.error;
            return false;
        }
        cse::data::BuildOptions bo{};
        bo.bindings.push_back({ "stand_lp", cse::kernel::kInputLP });
        if (!cse::data::BuildMatchData(character, bo, character, bo, build)) {
            error = build.report[0].error;
            return false;
        }
        cse::game::FightSetup setup{};
        setup.data = &build.data;
        return session.Begin(setup, error);
    }
};

// test_session.cpp's scripted match: enough movement to make prediction
// wrong under the stress session's fake rollbacks.
InputPair scriptedTick(int t) {
    InputPair in{};
    if (t % 7 == 0)  in.p[0].bits |= cse::kernel::kInputRight;
    if (t % 11 == 0) in.p[0].bits |= cse::kernel::kInputUp;
    if (t % 5 == 0)  in.p[1].bits |= cse::kernel::kInputLeft;
    return in;
}

} // namespace

TEST(SessionDriver, RunsExactlyTheTicksTheSessionAdvances) {
    Fight driven, straight;
    std::string error;
    ASSERT_TRUE(driven.BringUp(error)) << error;
    ASSERT_TRUE(straight.BringUp(error)) << error;

    // The stress session: a local session that saves, loads and re-advances
    // at random, so the number of Advance events is NOT the number of frames.
    ISession* s = CreateGekkoStressSession(SessionDriver::WireConfig(2));
    ASSERT_NE(nullptr, s);

    SessionDriver driver;
    driver.Bind(s, &driven.session, /*localSlots*/ 0b11, /*padSlot*/ 0);
    ASSERT_TRUE(driver.Bound());

    const int frames = 300;
    int ticksReported = 0;
    for (int f = 0; f < frames; ++f) {
        // Both slots are local here, so the test offers both; a local session
        // never stalls, so every frame accepts an input.
        ASSERT_TRUE(driver.AcceptsInput()) << "frame " << f;
        const InputPair in = scriptedTick(f);
        driver.Offer(0, in.p[0]);
        driver.Offer(1, in.p[1]);
        ticksReported += driver.Pump();
    }
    ASSERT_TRUE(driver.Fatal().empty()) << driver.Fatal();

    const untitledfighter::DriverCounts& c = driver.Counts();
    EXPECT_EQ(static_cast<int>(c.ticksRun), ticksReported) << "Pump's return is the count";
    EXPECT_EQ(static_cast<unsigned>(frames), c.framesPumped);
    // T2, measured: the session rolled back, so the kernel ran MORE ticks than
    // the host pumped frames. A driver that ran one tick per frame -- the
    // shipped FixedTick rule -- could not produce this.
    EXPECT_GT(c.rollbackTicks, 0u) << "the stress session did not roll back; the test measures nothing";
    EXPECT_GT(c.ticksRun, static_cast<unsigned>(frames));
    EXPECT_GT(c.loads, 0u);

    // T1 + T5 together: the confirmed tick index is the frame count, and the
    // state at it is byte-identical to the same inputs run straight, one tick
    // each, with no session in the way -- so every Advance ran, none was
    // dropped, and the rollbacks left nothing behind.
    EXPECT_EQ(static_cast<std::uint32_t>(frames), driven.session.CurrentTick());
    for (std::uint32_t t = 0; t < driven.session.CurrentTick(); ++t)
        straight.session.Tick(scriptedTick(static_cast<int>(t)));
    EXPECT_EQ(0, std::memcmp(&straight.session.State(), &driven.session.State(), sizeof(GameState)))
        << "the driven kernel disagrees with the straight run";

    driver.Unbind();
    DestroySession(s);
}

TEST(SessionDriver, AFrameWithNoAdvanceRunsZeroTicksAndDropsNone) {
    Fight a, b, straight;
    std::string error;
    ASSERT_TRUE(a.BringUp(error)) << error;
    ASSERT_TRUE(b.BringUp(error)) << error;
    ASSERT_TRUE(straight.BringUp(error)) << error;

    // Two peers over the deterministic loopback with two frames of latency:
    // nothing advances until the handshake crosses, and the peers stall on
    // each other after that.
    LoopbackNetwork net;
    net.SetLatencyFrames(2);
    cse::net::ITransport& ta = net.Endpoint("A");
    cse::net::ITransport& tb = net.Endpoint("B");

    SessionConfig ca = SessionDriver::WireConfig(2);
    ca.localDelay    = 2;
    ca.peerAddresses = { std::string(), "B" };
    SessionConfig cb = ca;
    cb.peerAddresses = { "A", std::string() };
    ISession* sa = CreateGekkoOnlineSession(ca, &ta);
    ISession* sb = CreateGekkoOnlineSession(cb, &tb);
    ASSERT_NE(nullptr, sa);
    ASSERT_NE(nullptr, sb);

    SessionDriver da, db;
    da.Bind(sa, &a.session, /*localSlots*/ 1u << 0, /*padSlot*/ 0);
    db.Bind(sb, &b.session, /*localSlots*/ 1u << 1, /*padSlot*/ 1);

    // Peer A holds Right for exactly twenty ACCEPTED offers. The session takes
    // one input per session frame and ignores a re-offer while the frame
    // stalls (ISession.h, rule 5), so the count is kept against
    // AcceptsInput(), which is the driver's own gate.
    const int frames = 400;
    int accepted = 0, zeroTickFrames = 0;
    for (int f = 0; f < frames; ++f) {
        Input padA{};
        if (da.AcceptsInput()) {
            ++accepted;
            if (accepted > 20 && accepted <= 40) padA.bits = cse::kernel::kInputRight;
        }
        const int ranA = da.Frame(padA);
        const int ranB = db.Frame(Input{});
        net.Tick();
        if (f == 0) {
            // T1: nothing is connected yet -- zero ticks, and not an error.
            EXPECT_EQ(0, ranA);
            EXPECT_EQ(0, ranB);
            EXPECT_TRUE(da.Fatal().empty()) << da.Fatal();
        }
        if (ranA == 0) ++zeroTickFrames;
    }
    ASSERT_TRUE(da.Fatal().empty()) << da.Fatal();
    ASSERT_TRUE(db.Fatal().empty()) << db.Fatal();
    EXPECT_GT(zeroTickFrames, 0);
    EXPECT_GT(da.Counts().ticksRun, 300u) << "the peers never got going";
    EXPECT_EQ(da.Counts().ticksRun, da.Counts().advances);
    EXPECT_EQ(db.Counts().ticksRun, db.Counts().advances);

    // Dropping none, measured by count: twenty held ticks of Right move the
    // fighter as far as twenty held ticks run straight, whatever frames they
    // landed on. One dropped or one duplicated offer is one walk step off,
    // and the window closed hundreds of ticks ago on both peers, so both
    // kernels are at rest on the same answer.
    const std::int32_t startX = straight.session.State().p[0].posX;
    for (std::uint32_t t = 0; t < a.session.CurrentTick(); ++t) {
        InputPair in{};
        if (t >= 30 && t < 50) in.p[0].bits = cse::kernel::kInputRight;
        straight.session.Tick(in);
    }
    const std::int32_t expected = straight.session.State().p[0].posX;
    EXPECT_NE(startX, expected) << "sanity: the walk moved the fighter";
    EXPECT_EQ(expected, a.session.State().p[0].posX) << "peer A delivered a different count of Right";
    EXPECT_EQ(expected, b.session.State().p[0].posX) << "peer B received a different count of Right";

    da.Unbind();
    db.Unbind();
    DestroySession(sa);
    DestroySession(sb);
}
