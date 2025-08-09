// InputSystem.h
// A minimal input system that polls keyboard and mouse state from GLFW
// and updates a camera accordingly. This encapsulates the raw input logic
// that was previously hard‑coded in the Renderer class.

#pragma once

#include <GLFW/glfw3.h>
#include "Camera.h"

namespace MyCoreEngine {

    class InputSystem {
    public:
        // Default constructor initialises with a null window. Call setWindow()
        // or assign a valid InputSystem later before calling update().
        InputSystem() : window_(nullptr) {}
        // Construct the input system with a pointer to the GLFW window.
        // The window must outlive the InputSystem.
        InputSystem(GLFWwindow* window) : window_(window) {}

        // Update the camera based on the current keyboard state and elapsed time.
        void update(Camera& camera, float deltaTime) {
            // Close the application if Escape is pressed
            if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window_, true);
            }
            // Basic WASD controls for forward/backward/strafe movement
            if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) {
                camera.ProcessKeyboard(FORWARD, deltaTime);
            }
            if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) {
                camera.ProcessKeyboard(BACKWARD, deltaTime);
            }
            if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) {
                camera.ProcessKeyboard(LEFT, deltaTime);
            }
            if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) {
                camera.ProcessKeyboard(RIGHT, deltaTime);
            }
        }

    private:
        GLFWwindow* window_;
    };

} // namespace MyCoreEngine
