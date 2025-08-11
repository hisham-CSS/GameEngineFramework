// InputSystem.h
#pragma once
#include <GLFW/glfw3.h>
#include "Camera.h"

namespace MyCoreEngine {

    class InputSystem {
    public:
        InputSystem() : window_(nullptr) {}
        explicit InputSystem(GLFWwindow* window) : window_(window) {}

        void setWindow(GLFWwindow* window) { window_ = window; }

        void update(Camera& camera, float deltaTime) {
            if (!window_) return; // <- safety: nothing to do yet

            if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window_, true);
                return; // early out to avoid mixing movement with close in same frame
            }
            if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
            if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
            if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
            if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        }

    private:
        GLFWwindow* window_;
    };

} // namespace MyCoreEngine
