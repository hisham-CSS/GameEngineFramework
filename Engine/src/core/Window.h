#pragma once
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

            // STICKY KEYS (ROADMAP M1.3h). glfwGetKey answers from the state
            // cached at the last glfwPollEvents, so a key pressed AND released
            // between two polls read GLFW_RELEASE at both and the press never
            // existed -- at any frame rate, for every InputMap consumer.
            // Sticky mode makes that missed press read GLFW_PRESS exactly once
            // before resetting, which InputMap's per-frame edge detector turns
            // into a normal one-frame press-and-release.
            //
            // Two recorded limits, neither reachable by a headless test:
            // the reset happens on the FIRST glfwGetKey of that key, so when
            // one physical key feeds several polls in one InputMap::update
            // (two actions sharing a key, or an action and an axis pair) only
            // the first poll sees the missed tap -- real taps that span a poll
            // are unaffected; and two full taps inside one poll interval still
            // collapse into one press, the same collapse InputMap::latched
            // documents for zero-tick windows.
            glfwSetInputMode(window_, GLFW_STICKY_KEYS, GLFW_TRUE);
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
        // Query live framebuffer size: width_/height_ are creation-time values and
        // would go stale after a resize (stretched image, disagrees with culling).
        float getAspectRatio() const {
            int w = 0, h = 0;
            glfwGetFramebufferSize(window_, &w, &h);
            if (w <= 0 || h <= 0) return static_cast<float>(width_) / static_cast<float>(height_); // minimized
            return static_cast<float>(w) / static_cast<float>(h);
        }
		void getFramebufferSize(int& width, int& height) const { glfwGetFramebufferSize(window_, &width, &height); }

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
