// The mode with a live session (ROADMAP M2.4; DETERMINISM.md T1, T3).
//
// UntitledFighterMode decides in FixedTick whether a tick runs: pause, frame
// step and slow motion are that decision, and the character swap, reset, the
// stage position and Demonstrate all restart or re-source the match. Every
// one of them is a training feature, and every one is a desync the moment a
// peer is running the same match -- so while a session is ATTACHED the
// session decides the tick count and the controls are inert. This file holds
// the mode itself to that, headlessly, through the seam M2.4 added for it:
// the mode reads and binds its keys on an InputMap the test supplies, so the
// Application and its window are not needed.
//
// The Application's own half -- the gameplay dt and the pad suppression a
// live session overrides -- is tests/test_frame_gate.cpp.
#include <gtest/gtest.h>

#include "Engine.h"

#include "SessionDriver.h"
#include "UntitledFighterMode.h"

#include "cse/net/ISession.h"
#include "cse/net/LoopbackTransport.h"

#include <filesystem>
#include <string>
#include <unordered_map>

using cse::net::CreateGekkoLocalSession;
using cse::net::CreateGekkoOnlineSession;
using cse::net::DestroySession;
using cse::net::ISession;
using cse::net::LoopbackNetwork;
using cse::net::SessionConfig;
using untitledfighter::SessionDriver;
using untitledfighter::UntitledFighterMode;

namespace {

// The content root the mode resolves "Characters/fighter_a.json" against: the
// staged Exported/ beside the tests, or the source assets for a bare IDE run.
std::string contentRoot() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported";
        if (fs::exists(staged / "Characters" / "fighter_a.json")) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Games" / "UntitledFighter" / "Assets";
        if (fs::exists(source / "Characters" / "fighter_a.json")) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported";
}

// The scripted keyboard from test_input_map.cpp: the poll seams are virtual
// exactly so a test can be the keyboard.
class FakeInput : public MyCoreEngine::InputMap {
public:
    std::unordered_map<int, bool> keys;

protected:
    bool pollKey(GLFWwindow*, int key) const override {
        auto it = keys.find(key);
        return it != keys.end() && it->second;
    }
    bool pollMouseButton(GLFWwindow*, int) const override { return false; }
    bool pollGamepad(GLFWgamepadstate& out) const override {
        (void)out;
        return false;
    }
};

// A mode with a keyboard and no window.
struct ModeHost {
    FakeInput           input;
    UntitledFighterMode mode;

    void BringUp() {
        mode.SetInputMap(&input);
        MyCoreEngine::GameModeContext ctx{};
        ctx.contentRoot = contentRoot();
        std::string error;
        ASSERT_TRUE(mode.Enter(ctx, error)) << error;
        ASSERT_TRUE(mode.MatchReady()) << mode.SetupError();
    }

    // One rendered frame running `steps` fixed steps: the Application's loop in
    // miniature (poll, phases, latch retirement), as test_press_delivery.cpp
    // mirrors it.
    void Frame(int steps = 1) {
        input.update(nullptr);
        for (int i = 0; i < steps; ++i) {
            input.beginInputPhase();
            mode.FixedTick(1.f / 60.f);
        }
        if (steps > 0) input.clearPressLatches();
    }

    // Press for one frame, release for one.
    void Tap(int key) {
        input.keys[key] = true;
        Frame(1);
        input.keys[key] = false;
        Frame(1);
    }
};

} // namespace

TEST(FightMode, PauseStepAndSlowMotionAreInertWhileASessionIsLive) {
    ModeHost h;
    h.BringUp();

    // Without a session the controls do what they say, through the same seam
    // -- so the inertness below is the session's doing and not a dead key.
    const std::uint32_t t0 = h.mode.CurrentTick();
    h.Frame(5);
    EXPECT_EQ(t0 + 5, h.mode.CurrentTick());
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_TRUE(h.mode.Paused());
    const std::uint32_t tPaused = h.mode.CurrentTick();
    h.Frame(5);
    EXPECT_EQ(tPaused, h.mode.CurrentTick()) << "the training pause did not pause";
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_FALSE(h.mode.Paused());

    // A live LOCAL session -- both slots local, the dummy neutral -- is enough:
    // T3 is about who decides, not about the wire.
    ISession* s = CreateGekkoLocalSession(SessionDriver::WireConfig(2));
    ASSERT_NE(nullptr, s);
    std::string error;
    ASSERT_TRUE(h.mode.AttachSession(s, /*padSlot*/ 0, /*localSlots*/ 0b11, error)) << error;
    ASSERT_TRUE(h.mode.SessionLive());

    const std::uint32_t t1 = h.mode.CurrentTick();
    h.Tap(GLFW_KEY_SPACE);                       // pause
    EXPECT_FALSE(h.mode.Paused());
    h.Tap(GLFW_KEY_PERIOD);                      // frame step
    EXPECT_EQ(0u, h.mode.PendingSteps());
    h.Tap(GLFW_KEY_COMMA);                       // slow motion
    EXPECT_EQ(1, h.mode.SlowDivisor());
    h.Frame(10);
    const std::uint32_t ran = h.mode.CurrentTick() - t1;
    EXPECT_EQ(h.mode.Driver().Counts().ticksRun, ran)
        << "a tick ran that the session did not advance";
    EXPECT_GE(ran, 10u) << "the taps above stopped or slowed the match";

    // Reset, the character swap and the stage position restart the match;
    // live, they do nothing -- the tick index never goes back.
    const std::uint32_t t2 = h.mode.CurrentTick();
    h.Tap(GLFW_KEY_R);
    h.Tap(GLFW_KEY_C);
    h.Tap(GLFW_KEY_V);
    EXPECT_GT(h.mode.CurrentTick(), t2);
    EXPECT_TRUE(h.mode.Fatal().empty()) << h.mode.Fatal();

    // Detached, the controls are the host's again.
    h.mode.DetachSession();
    EXPECT_FALSE(h.mode.SessionLive());
    DestroySession(s);
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_TRUE(h.mode.Paused());

    h.mode.Exit();
}

TEST(FightMode, AFrameTheSessionDoesNotAdvanceRunsNoTickAndIsNotAnError) {
    // Two modes, two peers, one loopback with latency: the first fixed steps
    // run no tick at all, because nothing has connected -- and that is a
    // legal frame, not a fatal one (T1).
    ModeHost a, b;
    a.BringUp();
    b.BringUp();

    LoopbackNetwork net;
    net.SetLatencyFrames(2);
    SessionConfig ca = SessionDriver::WireConfig(2);
    ca.localDelay    = 2;
    ca.peerAddresses = { std::string(), "B" };
    SessionConfig cb = ca;
    cb.peerAddresses = { "A", std::string() };
    ISession* sa = CreateGekkoOnlineSession(ca, &net.Endpoint("A"));
    ISession* sb = CreateGekkoOnlineSession(cb, &net.Endpoint("B"));
    ASSERT_NE(nullptr, sa);
    ASSERT_NE(nullptr, sb);

    std::string error;
    ASSERT_TRUE(a.mode.AttachSession(sa, 0, 1u << 0, error)) << error;
    ASSERT_TRUE(b.mode.AttachSession(sb, 1, 1u << 1, error)) << error;

    const std::uint32_t ta = a.mode.CurrentTick();
    a.Frame(1);
    b.Frame(1);
    net.Tick();
    EXPECT_EQ(ta, a.mode.CurrentTick()) << "a tick ran before the peers connected";
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();
    EXPECT_TRUE(a.mode.MatchReady());

    // Held Right on A for a stretch: the match gets going and both kernels
    // follow the session, never the fixed step.
    for (int f = 0; f < 240; ++f) {
        a.input.keys[GLFW_KEY_D] = (f >= 40 && f < 80);
        a.Frame(1);
        b.Frame(1);
        net.Tick();
    }
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();
    EXPECT_TRUE(b.mode.Fatal().empty()) << b.mode.Fatal();
    EXPECT_GT(a.mode.CurrentTick() - ta, 150u) << "the peers never got going";
    EXPECT_EQ(a.mode.Driver().Counts().ticksRun, a.mode.Driver().Counts().advances);
    EXPECT_EQ(b.mode.Driver().Counts().ticksRun, b.mode.Driver().Counts().advances);
    EXPECT_LE(a.mode.CurrentTick() > b.mode.CurrentTick() ? a.mode.CurrentTick() - b.mode.CurrentTick()
                                                          : b.mode.CurrentTick() - a.mode.CurrentTick(),
              8u) << "the peers drifted apart by more than the prediction window";
    // The held walk reached BOTH kernels: A's fighter is where B says it is.
    EXPECT_EQ(a.mode.State().p[0].posX, b.mode.State().p[0].posX);
    EXPECT_NE(a.mode.State().p[0].posX, a.mode.State().p[1].posX);

    a.mode.DetachSession();
    b.mode.DetachSession();
    DestroySession(sa);
    DestroySession(sb);
    a.mode.Exit();
    b.mode.Exit();
}
