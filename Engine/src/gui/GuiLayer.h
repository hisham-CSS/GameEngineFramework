#pragma once
#include "../src/core/Core.h"
#include "GLFW/glfw3.h"

// Forward declare ImGui structures we need
struct ImGuiContext;

namespace MyCoreEngine {
    class ENGINE_API GuiLayer {
    public:
        static bool Init(GLFWwindow* window);
        static void BeginFrame();
        static void EndFrame();
        static void Shutdown();

        // Utility functions to start/end windows
        static void BeginDockspace();
        static void EndDockspace();

    private:
        static ImGuiContext* s_Context;
        static bool s_IsInitialized;
    };
}