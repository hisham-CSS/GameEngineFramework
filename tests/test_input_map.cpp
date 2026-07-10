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
