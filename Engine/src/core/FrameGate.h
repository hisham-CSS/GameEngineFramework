#pragma once
// What one rendered frame owes the gameplay hooks (ROADMAP M2.4;
// DETERMINISM.md T3, N5).
//
// Application::RunLoop makes two decisions per frame before it calls anything
// of the game's: how much gameplay time passes (pause and time scale), and
// whether the InputMap answers the gameplay hooks at all (the Editor turns
// gameplay input off when its Game view loses focus, so a key typed into the
// Inspector does not also punch). Both are conveniences for a scene nobody
// else is simulating.
//
// A LIVE SESSION is a match another peer is also running. Its tick count is the
// session's -- ISession's Advance events, which the mode turns into kernel
// ticks -- and the host's only job is to pump it once per fixed step at the
// real rate with the real pad. So while a session is live, the gameplay dt is
// the raw frame dt (pausing this host would stall the peer, and slowing it
// would make the peer's session wait on ours), and the pad is never suppressed
// on the host's behalf: a fighter that stops answering because a panel took
// focus is a fighter the peer beats for a reason nothing on either screen
// explains.
//
// A pure function, so tests/test_frame_gate.cpp can hold the rule without a
// window; RunLoop calls it and does what it says.
namespace MyCoreEngine {

    struct FrameGate {
        float gameDt                = 0.f;    // what the fixed accumulator and the variable update receive
        bool  suppressGameplayInput = false;  // InputMap::setSuppressed around the gameplay hooks
    };

    inline FrameGate GateFrame(float dt, bool paused, float timeScale, bool gameplayInput, bool sessionLive) {
        FrameGate gate{};
        if (sessionLive) {
            // T3, N5: the session owns time and the pad. The real dt, so the
            // pump runs at the real rate whatever the host's pause and time
            // scale say; never suppressed, so the peer always receives this
            // player's actual input.
            gate.gameDt                = dt;
            gate.suppressGameplayInput = false;
            return gate;
        }
        gate.gameDt                = paused ? 0.f : dt * timeScale;
        gate.suppressGameplayInput = !gameplayInput;
        return gate;
    }

} // namespace MyCoreEngine
