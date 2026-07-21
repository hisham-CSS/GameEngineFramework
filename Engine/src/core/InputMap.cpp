#include "InputMap.h"

#include <algorithm>
#include <cmath>

namespace MyCoreEngine {

    // --- bindings ---

    void InputMap::bindKey(const std::string& action, int glfwKey) {
        actions_[action].keys.push_back(glfwKey);
    }
    void InputMap::bindMouseButton(const std::string& action, int glfwMouseButton) {
        actions_[action].mouseButtons.push_back(glfwMouseButton);
    }
    void InputMap::bindGamepadButton(const std::string& action, int glfwGamepadButton) {
        actions_[action].padButtons.push_back(glfwGamepadButton);
    }
    void InputMap::clearAction(const std::string& action) {
        actions_.erase(action);
    }

    void InputMap::bindAxisKeys(const std::string& axis, int positiveKey, int negativeKey) {
        axes_[axis].keyPairs.emplace_back(positiveKey, negativeKey);
    }
    void InputMap::bindGamepadAxis(const std::string& axis, int glfwGamepadAxis, bool inverted) {
        axes_[axis].padAxes.emplace_back(glfwGamepadAxis, inverted);
    }
    void InputMap::clearAxis(const std::string& axis) {
        axes_.erase(axis);
    }

    void InputMap::setGamepadDeadzone(float dz) {
        deadzone_ = std::max(0.f, std::min(0.95f, dz));
    }

    // --- default poll implementations (null window == released) ---

    bool InputMap::pollKey(GLFWwindow* window, int key) const {
        return window && glfwGetKey(window, key) == GLFW_PRESS;
    }
    bool InputMap::pollMouseButton(GLFWwindow* window, int button) const {
        return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }
    bool InputMap::pollGamepad(GLFWgamepadstate& out) const {
        // Safe before glfwInit: GLFW reports NOT_INITIALIZED and returns false.
        return glfwGetGamepadState(GLFW_JOYSTICK_1, &out) == GLFW_TRUE;
    }

    // --- per-frame update ---

    void InputMap::update(GLFWwindow* window) {
        GLFWgamepadstate pad{};
        padConnected_ = pollGamepad(pad);

        for (auto& [name, a] : actions_) {
            a.prev = a.down;
            bool down = false;
            for (int k : a.keys) {
                if (pollKey(window, k)) { down = true; break; }
            }
            if (!down) {
                for (int b : a.mouseButtons) {
                    if (pollMouseButton(window, b)) { down = true; break; }
                }
            }
            if (!down && padConnected_) {
                for (int b : a.padButtons) {
                    if (b >= 0 && b <= GLFW_GAMEPAD_BUTTON_LAST && pad.buttons[b] == GLFW_PRESS) {
                        down = true;
                        break;
                    }
                }
            }
            a.down = down;
            // Latch the down-edge so a fixed tick that does not run on this
            // frame can still see it later. Retired by the Application once a
            // tick has had its chance, or when a later phase asks for it.
            if (a.down && !a.prev) {
                a.latched = true;
                a.servedPhase = 0; // a fresh press is unclaimed
            }
        }

        for (auto& [name, ax] : axes_) {
            float v = 0.f;
            for (const auto& [pos, neg] : ax.keyPairs) {
                if (pollKey(window, pos)) v += 1.f;
                if (pollKey(window, neg)) v -= 1.f;
            }
            if (padConnected_) {
                for (const auto& [idx, inverted] : ax.padAxes) {
                    if (idx >= 0 && idx <= GLFW_GAMEPAD_AXIS_LAST) {
                        const float a = applyDeadzone_(pad.axes[idx]);
                        v += inverted ? -a : a;
                    }
                }
            }
            ax.value = std::max(-1.f, std::min(1.f, v));
        }
    }

    // --- queries ---

    bool InputMap::isDown(const std::string& action) const {
        if (suppressed_) return false;
        auto it = actions_.find(action);
        return it != actions_.end() && it->second.down;
    }
    bool InputMap::wasPressed(const std::string& action) const {
        if (suppressed_) return false;
        auto it = actions_.find(action);
        return it != actions_.end() && it->second.down && !it->second.prev;
    }
    bool InputMap::wasReleased(const std::string& action) const {
        if (suppressed_) return false;
        auto it = actions_.find(action);
        return it != actions_.end() && !it->second.down && it->second.prev;
    }
    float InputMap::axis(const std::string& axis) const {
        if (suppressed_) return 0.f;
        auto it = axes_.find(axis);
        return it != axes_.end() ? it->second.value : 0.f;
    }

    bool InputMap::consumePressed(const std::string& action) {
        // Not consumed either: a suppressed reader must not silently eat a
        // press that a legitimate consumer would otherwise get.
        if (suppressed_) return false;
        auto it = actions_.find(action);
        if (it == actions_.end()) return false;
        Action& a = it->second;
        if (!a.latched) return false;

        if (a.servedPhase == 0) {
            a.servedPhase = phase_; // first reader in this phase claims it...
            return true;
        }
        if (a.servedPhase == phase_) {
            return true;            // ...and every later reader in it agrees
        }
        // A LATER phase is asking, so the press has already been delivered.
        // Retire it here rather than waiting for clearPressLatches, which
        // does not run on the zero-tick frames the latch exists to bridge.
        a.latched = false;
        a.servedPhase = 0;
        return false;
    }

    void InputMap::beginInputPhase() {
        ++phase_;
    }

    void InputMap::clearPressLatches() {
        for (auto& [name, a] : actions_) {
            a.latched = false;
            a.servedPhase = 0;
        }
    }

    bool InputMap::hasAction(const std::string& action) const {
        return actions_.find(action) != actions_.end();
    }
    bool InputMap::hasAxis(const std::string& axis) const {
        return axes_.find(axis) != axes_.end();
    }

    void BindDefaultActions(InputMap& map) {
        // Fly-camera movement (keyboard + left stick).
        map.bindAxisKeys("MoveForward", GLFW_KEY_W, GLFW_KEY_S);
        map.bindAxisKeys("MoveForward", GLFW_KEY_UP, GLFW_KEY_DOWN);
        map.bindAxisKeys("MoveRight", GLFW_KEY_D, GLFW_KEY_A);
        map.bindAxisKeys("MoveRight", GLFW_KEY_RIGHT, GLFW_KEY_LEFT);
        map.bindGamepadAxis("MoveForward", GLFW_GAMEPAD_AXIS_LEFT_Y, /*inverted=*/true);
        map.bindGamepadAxis("MoveRight", GLFW_GAMEPAD_AXIS_LEFT_X);
        // Look is gamepad-only; mouse look is handled separately by the
        // Application, so these read 0 with no controller attached.
        map.bindGamepadAxis("LookX", GLFW_GAMEPAD_AXIS_RIGHT_X);
        map.bindGamepadAxis("LookY", GLFW_GAMEPAD_AXIS_RIGHT_Y, /*inverted=*/true);

        map.bindKey("Quit", GLFW_KEY_ESCAPE);
        map.bindGamepadButton("Quit", GLFW_GAMEPAD_BUTTON_BACK);

        // "Jump" has no engine-side consumer -- it exists so gameplay and
        // scripts have one conventional action bound out of the box. Without
        // it the shipped bouncer.lua example queried a name nothing had
        // bound and silently did nothing forever.
        map.bindKey("Jump", GLFW_KEY_SPACE);
        map.bindGamepadButton("Jump", GLFW_GAMEPAD_BUTTON_A);
    }

    float InputMap::applyDeadzone_(float v) const {
        const float mag = std::fabs(v);
        if (mag < deadzone_) return 0.f;
        const float scaled = (mag - deadzone_) / (1.f - deadzone_);
        return v < 0.f ? -scaled : scaled;
    }

} // namespace MyCoreEngine
