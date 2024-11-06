// Engine/include/Renderer.h
#pragma once
#include "../src/core/Core.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace MyCoreEngine {
    class ENGINE_API Renderer {
    public:
        static bool Init(GLFWwindow* window);
        static void Shutdown();

        // Basic rendering functions
        static void Clear();
        static void SetClearColor(const glm::vec4& color);
        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

        // OpenGL state functions
        static void Enable(GLenum cap);
        static void Disable(GLenum cap);
        static void EnableDepthTest();
        static void DisableDepthTest();

        // Error handling
        static bool CheckError();
        static const char* GetErrorString(GLenum error);

    private:
        static bool s_IsInitialized;
    };
}