#pragma once
#include "../core/Core.h"
#include <GLFW/glfw3.h>

namespace MyCoreEngine {
    class ENGINE_API Window 
    {
    public:
        static int Init();
        static void Terminate();

        // Window management
        static GLFWwindow* CreateWindow(int width, int height, const char* title);
        static void DestroyWindow(GLFWwindow* window);
        static void MakeContextCurrent(GLFWwindow* window);
        static void SwapBuffers(GLFWwindow* window);
        static void PollEvents();
        static bool WindowShouldClose(GLFWwindow* window);
    };
}