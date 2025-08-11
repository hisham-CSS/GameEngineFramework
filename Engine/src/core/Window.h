#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Core.h"

namespace MyCoreEngine
{
    class ENGINE_API Window {
    public:
        Window(int width, int height, const std::string& title)
            : width_(width), height_(height)
        {
            if (!glfwInit()) {
                throw std::runtime_error("Failed to initialize GLFW");
            }
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            window_ = glfwCreateWindow(width_, height_, title.c_str(), nullptr, nullptr);
            if (!window_) {
                glfwTerminate();
                throw std::runtime_error("Failed to create GLFW window");
            }
            glfwMakeContextCurrent(window_);
            // Set user pointer so callbacks can access this instance
            glfwSetWindowUserPointer(window_, this);
        }

        ~Window() {
            if (window_) {
                glfwDestroyWindow(window_);
            }
            glfwTerminate();
        }

        GLFWwindow* getGLFWwindow() const { return window_; }
        int getWidth() const { return width_; }
        int getHeight() const { return height_; }

        // Optional: Wrap the poll/swap functions
        void pollEvents() { glfwPollEvents(); }
        void swapBuffers() { glfwSwapBuffers(window_); }
        bool shouldClose() const { return glfwWindowShouldClose(window_); }

    private:
        GLFWwindow* window_;
        int width_;
        int height_;
    };
};
