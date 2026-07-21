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

        // Edge-triggered press for FIXED-TICK consumers. Use this instead of
        // wasPressed() from anything driven by the fixed timestep.
        //
        // wasPressed() is scoped to a RENDERED FRAME, and the fixed tick does
        // not run once per frame: above the fixed rate most frames run ZERO
        // ticks (at 144fps roughly 58% of them), and a stalled frame runs up
        // to maxSteps in a row. Reading wasPressed() from a fixed tick
        // therefore MISSES most presses and multiplies the rest.
        //
        // This latches the press when it happens and holds it until a phase
        // consumes it, so a press is seen EXACTLY ONCE per phase no matter
        // how the frame/tick boundaries fall.
        //
        // The claim is scoped to the PHASE, not to the caller: every reader
        // within one fixed tick sees the same answer. That distinction is the
        // whole point -- with a first-reader-wins latch, putting the same
        // jump script on two entities meant only the first one ever jumped,
        // with nothing logged to explain why the second looked dead.
        //
        // Reading the same action from both a per-frame and a fixed-tick hook
        // is still unsupported: those are different phases, so whichever runs
        // first serves the press and the other sees false.
        bool consumePressed(const std::string& action);

        // Opens a new consumption phase. The Application calls this once per
        // fixed tick and once per variable update, so all consumers inside a
        // phase agree and a press cannot be served to two phases.
        void beginInputPhase();

        // Drops any latch nothing consumed. The Application calls this each
        // frame EXCEPT when a tick was expected and did not run, which is the
        // one case a latch must survive.
        void clearPressLatches();

        // Binding introspection. Querying an UNBOUND name is deliberately
        // silent (false / 0) so unconfigured input cannot kill a frame -- but
        // that makes a typo or a missing binding invisible, so callers that
        // can report it (the script host) use these to say so out loud.
        bool hasAction(const std::string& action) const;
        bool hasAxis(const std::string& axis) const;

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
            // Set on the down-edge, cleared by the Application (or once a
            // LATER phase asks). Bridges frames that run no fixed tick.
            //
            // Deliberately a bool, not a counter: two full press-release
            // cycles inside a single zero-tick window (~14ms at 144fps)
            // collapse into one press. That is below deliberate double-tap
            // speed, and a counter would let a bouncing contact queue up
            // impulses that all fire on one tick.
            bool     latched = false;
            uint64_t servedPhase = 0; // 0 = not yet claimed by any phase
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
        // Monotonic phase counter; starts at 1 so 0 can mean "unclaimed".
        uint64_t phase_ = 1;
    };

    // The engine's starter bindings: the fly-camera axes, "Quit", and "Jump".
    //
    // Lives here rather than inside Application so it is TESTABLE without a
    // window. That matters: the shipped example script referenced "Jump"
    // before anything bound it, and because an unbound action reads as false
    // forever with no diagnostic, the script simply did nothing and there was
    // nothing to see. A test over this function is what catches that class of
    // mistake now.
    //
    // Projects are free to rebind or clearAction() any of these.
    ENGINE_API void BindDefaultActions(InputMap& map);

} // namespace MyCoreEngine
