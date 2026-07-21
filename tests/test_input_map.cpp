// InputMap tests: edge detection, axis composition, deadzone — using the
// virtual poll seams to inject scripted input (no real keyboard/gamepad).
#include <gtest/gtest.h>
#include "Engine.h"

#include <unordered_map>

using MyCoreEngine::InputMap;

namespace {

    class FakeInput : public InputMap {
    public:
        std::unordered_map<int, bool> keys;
        bool padPresent = false;
        GLFWgamepadstate pad{};

    protected:
        bool pollKey(GLFWwindow*, int key) const override {
            auto it = keys.find(key);
            return it != keys.end() && it->second;
        }
        bool pollMouseButton(GLFWwindow*, int) const override { return false; }
        bool pollGamepad(GLFWgamepadstate& out) const override {
            if (padPresent) out = pad;
            return padPresent;
        }
    };

} // namespace

TEST(InputMap, NullWindowAndUnboundQueriesAreSafe) {
    InputMap input; // real GLFW polls, no window, GLFW possibly uninitialized
    input.bindKey("Quit", GLFW_KEY_ESCAPE);
    input.bindAxisKeys("MoveForward", GLFW_KEY_W, GLFW_KEY_S);
    input.update(nullptr);
    EXPECT_FALSE(input.isDown("Quit"));
    EXPECT_FALSE(input.wasPressed("Quit"));
    EXPECT_FALSE(input.wasReleased("Quit"));
    EXPECT_FLOAT_EQ(input.axis("MoveForward"), 0.f);
    // never-bound names
    EXPECT_FALSE(input.isDown("DoesNotExist"));
    EXPECT_FLOAT_EQ(input.axis("DoesNotExist"), 0.f);
}

TEST(InputMap, PressAndReleaseEdgesFireOnce) {
    FakeInput input;
    input.bindKey("Jump", GLFW_KEY_SPACE);

    input.keys[GLFW_KEY_SPACE] = true;
    input.update(nullptr);
    EXPECT_TRUE(input.isDown("Jump"));
    EXPECT_TRUE(input.wasPressed("Jump"));   // edge on first frame down
    EXPECT_FALSE(input.wasReleased("Jump"));

    input.update(nullptr);
    EXPECT_TRUE(input.isDown("Jump"));
    EXPECT_FALSE(input.wasPressed("Jump"));  // held, no new edge

    input.keys[GLFW_KEY_SPACE] = false;
    input.update(nullptr);
    EXPECT_FALSE(input.isDown("Jump"));
    EXPECT_TRUE(input.wasReleased("Jump"));  // edge on first frame up

    input.update(nullptr);
    EXPECT_FALSE(input.wasReleased("Jump")); // only once
}

TEST(InputMap, KeyPairAxisComposesAndCancels) {
    FakeInput input;
    input.bindAxisKeys("MoveForward", GLFW_KEY_W, GLFW_KEY_S);

    input.keys[GLFW_KEY_W] = true;
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveForward"), 1.f);

    input.keys[GLFW_KEY_W] = false;
    input.keys[GLFW_KEY_S] = true;
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveForward"), -1.f);

    input.keys[GLFW_KEY_W] = true; // both held -> cancel out
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveForward"), 0.f);
}

TEST(InputMap, MultipleBindingsAreOrEd) {
    FakeInput input;
    input.bindKey("Interact", GLFW_KEY_E);
    input.bindKey("Interact", GLFW_KEY_ENTER); // alt binding

    input.keys[GLFW_KEY_ENTER] = true;
    input.update(nullptr);
    EXPECT_TRUE(input.isDown("Interact"));

    input.keys[GLFW_KEY_ENTER] = false;
    input.keys[GLFW_KEY_E] = true;
    input.update(nullptr);
    EXPECT_TRUE(input.isDown("Interact"));
}

TEST(InputMap, GamepadDeadzoneRescalesAndInverts) {
    FakeInput input;
    input.padPresent = true;
    input.setGamepadDeadzone(0.15f);
    input.bindGamepadAxis("MoveRight", GLFW_GAMEPAD_AXIS_LEFT_X);
    input.bindGamepadAxis("MoveForward", GLFW_GAMEPAD_AXIS_LEFT_Y, /*inverted=*/true);
    input.bindGamepadButton("Quit", GLFW_GAMEPAD_BUTTON_BACK);

    // inside deadzone -> 0
    input.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.10f;
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveRight"), 0.f);

    // full deflection -> 1
    input.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 1.0f;
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveRight"), 1.f);

    // midrange rescaled: (0.575 - 0.15) / (1 - 0.15) = 0.5
    input.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.575f;
    input.update(nullptr);
    EXPECT_NEAR(input.axis("MoveRight"), 0.5f, 1e-4f);

    // stick up is negative in GLFW; inverted binding maps it to +forward
    input.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = -1.0f;
    input.update(nullptr);
    EXPECT_FLOAT_EQ(input.axis("MoveForward"), 1.f);

    // gamepad button feeds actions
    input.pad.buttons[GLFW_GAMEPAD_BUTTON_BACK] = GLFW_PRESS;
    input.update(nullptr);
    EXPECT_TRUE(input.wasPressed("Quit"));
}

TEST(InputMap, DisconnectedGamepadContributesNothing) {
    FakeInput input;
    input.padPresent = false;
    input.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 1.0f; // would be 1 if connected
    input.bindGamepadAxis("MoveRight", GLFW_GAMEPAD_AXIS_LEFT_X);
    input.update(nullptr);
    EXPECT_FALSE(input.gamepadConnected());
    EXPECT_FLOAT_EQ(input.axis("MoveRight"), 0.f);
}

// --- press latch: the fixed-tick edge problem -------------------------------
//
// wasPressed() is scoped to a rendered FRAME, but the fixed tick does not run
// once per frame. These pin the latch that bridges the two, because getting it
// wrong is invisible: the jump just feels unreliable.

TEST(InputMap, PressLatchSurvivesAFrameThatRanNoFixedTick) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);

    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);                  // the down-edge frame
    EXPECT_TRUE(in.wasPressed("Jump"));  // frame-scoped view sees it

    // Next frame the key is still held, so the frame-scoped edge is GONE.
    // A fixed tick that only runs now would miss the press entirely without
    // the latch -- this is the common case above the fixed rate.
    in.update(nullptr);
    EXPECT_FALSE(in.wasPressed("Jump")) << "edge should be frame-scoped";
    EXPECT_TRUE(in.consumePressed("Jump")) << "latch did not survive the frame";
}

TEST(InputMap, HoldingAKeyDoesNotRelatch) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);

    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);
    in.beginInputPhase();
    ASSERT_TRUE(in.consumePressed("Jump"));

    for (int i = 0; i < 10; ++i) in.update(nullptr); // still held
    in.beginInputPhase();
    EXPECT_FALSE(in.consumePressed("Jump")) << "hold produced a second press";

    // Release and press again -> a genuinely new press.
    in.keys[GLFW_KEY_SPACE] = false;
    in.update(nullptr);
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);
    in.beginInputPhase();
    EXPECT_TRUE(in.consumePressed("Jump"));
}

TEST(InputMap, ClearPressLatchesDropsUnconsumedPresses) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);

    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);
    // Edit mode runs no ticks at all; without this the first tick after Play
    // would replay a press the user made while still editing.
    in.clearPressLatches();
    EXPECT_FALSE(in.consumePressed("Jump"));
}

TEST(InputMap, ConsumePressedOnUnboundActionIsSafe) {
    FakeInput in;
    EXPECT_FALSE(in.consumePressed("NoSuchAction"));
    in.clearPressLatches(); // must not throw on an empty map
}

// --- default bindings -------------------------------------------------------

TEST(InputMap, DefaultBindingsIncludeJump) {
    InputMap map;
    MyCoreEngine::BindDefaultActions(map);

    // Regression: the shipped bouncer.lua example queried "Jump" before
    // anything bound it. An unbound action reads false forever with no
    // diagnostic, so the script silently did nothing and there was nothing
    // in any log to explain why.
    EXPECT_TRUE(map.hasAction("Jump")) << "bouncer.lua depends on this";
    EXPECT_TRUE(map.hasAction("Quit"));
    EXPECT_TRUE(map.hasAxis("MoveForward"));
    EXPECT_TRUE(map.hasAxis("MoveRight"));
    EXPECT_FALSE(map.hasAction("MoveForward")) << "axes are not actions";
    EXPECT_FALSE(map.hasAction("TotallyMadeUp"));
}

TEST(InputMap, DefaultJumpRespondsToSpace) {
    FakeInput in;
    MyCoreEngine::BindDefaultActions(in);

    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);
    EXPECT_TRUE(in.isDown("Jump"));
    EXPECT_TRUE(in.consumePressed("Jump"));
}

// --- phase scoping ----------------------------------------------------------
//
// A press is claimed by a PHASE, not by whoever reads it first. Getting this
// wrong meant two entities running the same jump script only jumped once,
// with nothing logged to explain the dead one.

TEST(InputMap, EveryReaderInOnePhaseSeesTheSamePress) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);

    in.beginInputPhase();
    EXPECT_TRUE(in.consumePressed("Jump")) << "entity A";
    EXPECT_TRUE(in.consumePressed("Jump")) << "entity B missed the press";
    EXPECT_TRUE(in.consumePressed("Jump")) << "entity C missed the press";
}

TEST(InputMap, ALaterPhaseDoesNotSeeAnAlreadyServedPress) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);

    in.beginInputPhase();
    ASSERT_TRUE(in.consumePressed("Jump"));

    // A stalled frame runs several ticks back to back; only the first is the
    // press's phase, or one tap becomes N impulses.
    in.beginInputPhase();
    EXPECT_FALSE(in.consumePressed("Jump"));
    in.beginInputPhase();
    EXPECT_FALSE(in.consumePressed("Jump"));
}

TEST(InputMap, AnUnservedLatchStillCrossesPhases) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);   // down-edge on a frame that runs no tick
    in.update(nullptr);   // held; frame-scoped edge is gone

    // The tick finally arrives a frame late and must still see it.
    in.beginInputPhase();
    EXPECT_TRUE(in.consumePressed("Jump"));
}

// --- suppression: gameplay vs editor -----------------------------------------
//
// The editor drives its fly camera from the SAME named axes the game reads, so
// suppression must be scoped to gameplay rather than switching the map off.

TEST(InputMap, SuppressedQueriesReadNeutral) {
    FakeInput in;
    BindDefaultActions(in);
    in.keys[GLFW_KEY_SPACE] = true;
    in.keys[GLFW_KEY_W] = true;
    in.update(nullptr);

    ASSERT_TRUE(in.isDown("Jump"));
    ASSERT_FLOAT_EQ(in.axis("MoveForward"), 1.0f);

    in.setSuppressed(true);
    EXPECT_FALSE(in.isDown("Jump"));
    EXPECT_FALSE(in.wasPressed("Jump"));
    EXPECT_FLOAT_EQ(in.axis("MoveForward"), 0.0f);

    // Unsuppressing restores the real state: the underlying poll never stopped.
    in.setSuppressed(false);
    EXPECT_TRUE(in.isDown("Jump"));
    EXPECT_FLOAT_EQ(in.axis("MoveForward"), 1.0f);
}

TEST(InputMap, SuppressedReaderDoesNotEatThePress) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);

    // Game view unfocused: the game reads nothing AND must not consume the
    // latch, or refocusing would find the press already gone.
    in.setSuppressed(true);
    in.beginInputPhase();
    EXPECT_FALSE(in.consumePressed("Jump"));

    in.setSuppressed(false);
    in.beginInputPhase();
    EXPECT_TRUE(in.consumePressed("Jump")) << "suppressed reader swallowed the press";
}

TEST(InputMap, SuppressingDoesNotManufactureAnEdgeOnRelease) {
    FakeInput in;
    in.bindKey("Jump", GLFW_KEY_SPACE);

    // Key held down across the whole suppressed window.
    in.keys[GLFW_KEY_SPACE] = true;
    in.update(nullptr);
    in.setSuppressed(true);
    for (int i = 0; i < 5; ++i) in.update(nullptr);
    in.setSuppressed(false);
    in.clearPressLatches(); // as the Application does while input is off

    // Regaining focus with the key still held must NOT look like a new press.
    in.update(nullptr);
    in.beginInputPhase();
    EXPECT_FALSE(in.consumePressed("Jump")) << "focus change forged a press";
}
