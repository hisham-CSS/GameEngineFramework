// InputSystem.h
#pragma once
#include <array>
#include <GLFW/glfw3.h>
#include "Camera.h"
#include "Event.h"
#include "EventBus.h"

namespace MyCoreEngine {

    class InputSystem {
    public:
        InputSystem() : window_(nullptr) {
            prev_.fill(GLFW_RELEASE);
        }
        explicit InputSystem(GLFWwindow* window) : window_(window) {
            prev_.fill(GLFW_RELEASE);
        }

        void setWindow(GLFWwindow* window) {
            window_ = window;
            prev_.fill(GLFW_RELEASE); // reset transitions on retarget
        }

        void update(Camera& camera, float deltaTime) {
            if (!window_) return;

            // Keys we care about for now
            const int keys[] = {
                GLFW_KEY_ESCAPE, GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D
            };

            // 1) transitions + publish
            for (int key : keys) {
                const int cur = glfwGetKey(window_, key);
                const int old = prev_[key];
                if (cur != old) {
                    // action is press or release (GLFW_REPEAT we pass through as-is)
                    EventBus::Get().publish(KeyEvent{ key, cur, /*mods*/0 });
                    prev_[key] = cur;
                }
            }

            // 2) behaviour (kept identical)
            if (prev_[GLFW_KEY_ESCAPE] == GLFW_PRESS) {
                glfwSetWindowShouldClose(window_, true);
                return; // early out to avoid mixing movement with close in same frame
            }
            if (prev_[GLFW_KEY_W] == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
            if (prev_[GLFW_KEY_S] == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
            if (prev_[GLFW_KEY_A] == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
            if (prev_[GLFW_KEY_D] == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        }

    private:
        GLFWwindow* window_;
        // store last observed action per key; size per GLFW key enum
        std::array<int, GLFW_KEY_LAST + 1> prev_;
    };

} // namespace MyCoreEngine
