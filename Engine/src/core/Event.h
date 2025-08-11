// Event.h
#pragma once
#include <cstdint>

namespace MyCoreEngine {

    // Keep it super simple and POD-like for now.
    struct WindowResizeEvent {
        int width = 0;
        int height = 0;
    };

    struct MouseMoveEvent {
        float x = 0.f;
        float y = 0.f;
    };

    struct MouseScrollEvent {
        float yoffset = 0.f;
    };

    struct KeyEvent {
        int key = 0;     // GLFW_KEY_*
        int action = 0;  // GLFW_PRESS/RELEASE/REPEAT
        int mods = 0;    // GLFW_MOD_*
    };

} // namespace MyCoreEngine
