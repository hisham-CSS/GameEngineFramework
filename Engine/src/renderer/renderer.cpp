#include "Renderer.h"
#include <iostream>

namespace MyCoreEngine {
    bool Renderer::s_IsInitialized = false;

    bool Renderer::Init(GLFWwindow* window) {
        if (s_IsInitialized)
            return true;

        // Initialize GLAD
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }

        // Print OpenGL info
        std::cout << "OpenGL Info:" << std::endl;
        std::cout << "  Vendor: " << glGetString(GL_VENDOR) << std::endl;
        std::cout << "  Renderer: " << glGetString(GL_RENDERER) << std::endl;
        std::cout << "  Version: " << glGetString(GL_VERSION) << std::endl;

        // Enable depth testing by default
        glEnable(GL_DEPTH_TEST);

        // Enable backface culling
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // Set default blend function
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Check for any errors
        if (GLenum error = glGetError(); error != GL_NO_ERROR) {
            std::cerr << "OpenGL Error during initialization: " << GetErrorString(error) << std::endl;
            return false;
        }

        s_IsInitialized = true;
        return true;
    }

    void Renderer::Shutdown() {
        s_IsInitialized = false;
    }

    void Renderer::Clear() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::SetClearColor(const glm::vec4& color) {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void Renderer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        glViewport(x, y, width, height);
    }

    void Renderer::Enable(GLenum cap) {
        glEnable(cap);
    }

    void Renderer::Disable(GLenum cap) {
        glDisable(cap);
    }

    void Renderer::EnableDepthTest() {
        glEnable(GL_DEPTH_TEST);
    }

    void Renderer::DisableDepthTest() {
        glDisable(GL_DEPTH_TEST);
    }

    bool Renderer::CheckError() {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cerr << "OpenGL Error: " << GetErrorString(error) << std::endl;
            return false;
        }
        return true;
    }

    const char* Renderer::GetErrorString(GLenum error) {
        switch (error) {
        case GL_NO_ERROR:          return "No error";
        case GL_INVALID_ENUM:      return "Invalid enum";
        case GL_INVALID_VALUE:     return "Invalid value";
        case GL_INVALID_OPERATION: return "Invalid operation";
        case GL_STACK_OVERFLOW:    return "Stack overflow";
        case GL_STACK_UNDERFLOW:   return "Stack underflow";
        case GL_OUT_OF_MEMORY:     return "Out of memory";
        default:                   return "Unknown error";
        }
    }
}