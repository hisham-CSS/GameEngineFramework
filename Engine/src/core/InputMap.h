#pragma once
#include "Core.h"
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MyCoreEngine {

    // Named, rebindable input mapping.
    //
    // Digital ACTIONS are bound to keys, mouse buttons, and/or gamepad buttons
    // (multiple bindings are OR'd). Analog AXES are bound to key pairs
    // (positive/negative, e.g. W/S) and/or gamepad axes (contributions are
    // summed, clamped to [-1, 1], with a radial deadzone on stick input).
    //
    // Call update(window) once per frame, then query by name:
    //   input.bindKey("Jump", GLFW_KEY_SPACE);
    //   input.bindAxisKeys("MoveForward", GLFW_KEY_W, GLFW_KEY_S);
    //   if (input.wasPressed("Jump")) ...
    //   pos += forward * input.axis("MoveForward") * speed * dt;
    //
    // Querying an unbound name returns false / 0. A null window is safe
    // (keyboard/mouse read as released; the gamepad still polls).
    class ENGINE_API InputMap {
    public:
        virtual ~InputMap() = default;

        // --- bindings (rebindable at runtime) ---
        void bindKey(const std::string& action, int glfwKey);
        void bindMouseButton(const std::string& action, int glfwMouseButton);
        void bindGamepadButton(const std::string& action, int glfwGamepadButton);
        void clearAction(const std::string& action);

        void bindAxisKeys(const std::string& axis, int positiveKey, int negativeKey);
        void bindGamepadAxis(const std::string& axis, int glfwGamepadAxis, bool inverted = false);
        void clearAxis(const std::string& axis);

        void  setGamepadDeadzone(float dz);
        float gamepadDeadzone() const { return deadzone_; }

        // Poll all bound inputs; call once per frame before querying.
        void update(GLFWwindow* window);

        // --- queries ---
        bool  isDown(const std::string& action) const;
        bool  wasPressed(const std::string& action) const;  // went down this frame
        bool  wasReleased(const std::string& action) const; // went up this frame
        float axis(const std::string& axis) const;          // clamped to [-1, 1]

        bool gamepadConnected() const { return padConnected_; }

    protected:
        // Poll seams; virtual so tests can inject scripted input states.
        virtual bool pollKey(GLFWwindow* window, int key) const;
        virtual bool pollMouseButton(GLFWwindow* window, int button) const;
        virtual bool pollGamepad(GLFWgamepadstate& out) const;

    private:
        struct Action {
            std::vector<int> keys;
            std::vector<int> mouseButtons;
            std::vector<int> padButtons;
            bool down = false;
            bool prev = false;
        };
        struct Axis {
            std::vector<std::pair<int, int>> keyPairs; // {positive, negative}
            std::vector<std::pair<int, bool>> padAxes; // {axis index, inverted}
            float value = 0.f;
        };

        float applyDeadzone_(float v) const;

        std::unordered_map<std::string, Action> actions_;
        std::unordered_map<std::string, Axis> axes_;
        float deadzone_ = 0.15f;
        bool padConnected_ = false;
    };

} // namespace MyCoreEngine
