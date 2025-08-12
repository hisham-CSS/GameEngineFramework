#pragma once
#include "Core.h"

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include "Window.h"
#include "Camera.h"
#include "Scene.h"
#include "Shader.h"
#include "InputSystem.h"
#include "Model.h"

namespace MyCoreEngine {

    class ENGINE_API Renderer {
    public:
        Renderer(int width, int height, const char* title);

        // Main loop
        void run(Scene& scene, Shader& shader);

        // Expose native window so the Editor can init ImGui on it
        GLFWwindow* GetNativeWindow() { return window_.getGLFWwindow(); }

        // Editor-provided UI draw (called each frame after 3D draw)
        using UIDrawFn = std::function<void(float /*deltaTime*/)>;
        void SetUIDraw(UIDrawFn fn) { uiDraw_ = std::move(fn); }

        // Editor-provided capture flags (so InputSystem can be skipped when UI is focused)
        using UICaptureFn = std::function<std::pair<bool, bool>()>;
        void SetUICaptureProvider(UICaptureFn fn) { captureFn_ = std::move(fn); }

        // Fires once, right after GLAD is initialized and the context is ready.
        using OnContextReadyFn = std::function<void()>;
        void SetOnContextReady(OnContextReadyFn fn) { onReady_ = std::move(fn); }

        // Optional: simple model enqueue (Renderer creates them after GL init)
        void EnqueueModel(std::string path) {
            pendingModels_.emplace_back(std::move(path));
        }

        // public:
        void InitGL(); // new: loads GLAD and fires OnContextReady once


    private:
        // Window / timing
        Window window_;
        float  deltaTime_ = 0.0f;
        float  lastFrame_ = 0.0f;

        // Camera & input
        Camera      camera_{ glm::vec3(0.0f, 0.0f, 3.0f) };
        InputSystem input_;

        // UI hooks
        UIDrawFn       uiDraw_{};
        UICaptureFn    captureFn_{};
        OnContextReadyFn onReady_{};
        bool           readyFired_ = false;

        // Simple model list (optional convenience)
        std::vector<std::string>               pendingModels_;
        std::vector<Model>    models_;

        // helpers
        void updateDeltaTime_();
        void setupGL_();           // creates GL state + fires OnContextReady once
        void loadPendingModels_(); // safe only after setupGL_ ran
    };

} // namespace MyCoreEngine

