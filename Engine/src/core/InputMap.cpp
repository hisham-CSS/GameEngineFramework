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
        auto it = actions_.find(action);
        return it != actions_.end() && it->second.down;
    }
    bool InputMap::wasPressed(const std::string& action) const {
        auto it = actions_.find(action);
        return it != actions_.end() && it->second.down && !it->second.prev;
    }
    bool InputMap::wasReleased(const std::string& action) const {
        auto it = actions_.find(action);
        return it != actions_.end() && !it->second.down && it->second.prev;
    }
    float InputMap::axis(const std::string& axis) const {
        auto it = axes_.find(axis);
        return it != axes_.end() ? it->second.value : 0.f;
    }

    float InputMap::applyDeadzone_(float v) const {
        const float mag = std::fabs(v);
        if (mag < deadzone_) return 0.f;
        const float scaled = (mag - deadzone_) / (1.f - deadzone_);
        return v < 0.f ? -scaled : scaled;
    }

} // namespace MyCoreEngine
