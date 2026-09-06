// The frame gate (ROADMAP M2.4; DETERMINISM.md T3, N5): what one rendered
// frame owes the gameplay hooks, and how a live session changes the answer.
//
// Application::RunLoop makes two decisions every frame that a rollback match
// cannot leave to it: the gameplay dt (pause and time scale) and whether the
// InputMap is suppressed for the gameplay hooks (the Editor turns gameplay
// input off when its Game view loses focus). Both are conveniences for a scene
// nobody else is simulating. A LIVE SESSION -- a match a peer is also running
// -- owns its own tick count (ISession's Advance events) and needs its pump
// called at the real rate with the real pad, or the peer plays against a
// fighter that froze because the Inspector took focus.
//
// The loop cannot be constructed without a window, so the decision is a pure
// function (core/FrameGate.h) the loop calls, and this file holds it to the
// rules. The mode-level half -- what a live session makes inert inside
// UntitledFighterMode -- is tests/test_fight_mode.cpp.
#include <gtest/gtest.h>

#include "Engine.h"

using MyCoreEngine::FrameGate;
using MyCoreEngine::GateFrame;

TEST(FrameGate, WithoutASessionPauseTimeScaleAndFocusAreTheHosts) {
    // The shipped behaviour, unchanged: what the loop did before M2.4.
    FrameGate g = GateFrame(0.016f, /*paused*/ false, /*timeScale*/ 1.0f,
                            /*gameplayInput*/ true, /*sessionLive*/ false);
    EXPECT_FLOAT_EQ(0.016f, g.gameDt);
    EXPECT_FALSE(g.suppressGameplayInput);

    g = GateFrame(0.016f, true, 1.0f, true, false);
    EXPECT_FLOAT_EQ(0.f, g.gameDt) << "paused: no gameplay time passes";

    g = GateFrame(0.016f, false, 0.5f, true, false);
    EXPECT_FLOAT_EQ(0.008f, g.gameDt) << "time scale scales the gameplay dt";

    g = GateFrame(0.016f, false, 1.0f, false, false);
    EXPECT_TRUE(g.suppressGameplayInput)
        << "gameplay input off (the Editor's Game view lost focus) suppresses the pad";
}

TEST(FrameGate, ALiveSessionOwnsTimeAndThePad) {
    // T3: paused, stopped, slowed or sped up, the session's pump receives the
    // real frame dt -- the host's clock has no say in how many ticks run.
    // N5: and the pad is never suppressed on the host's behalf.
    for (float scale : { 0.f, 0.25f, 1.f, 4.f }) {
        for (bool paused : { false, true }) {
            for (bool gameplayInput : { false, true }) {
                const FrameGate g = GateFrame(0.016f, paused, scale, gameplayInput,
                                              /*sessionLive*/ true);
                EXPECT_FLOAT_EQ(0.016f, g.gameDt)
                    << "paused=" << paused << " scale=" << scale;
                EXPECT_FALSE(g.suppressGameplayInput)
                    << "gameplayInput=" << gameplayInput;
            }
        }
    }
}
