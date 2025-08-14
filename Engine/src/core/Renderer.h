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


        // prevent copying (vector<unique_ptr<...>> cannot be copied)
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // simplest: also prevent moves for now (unless you really need them)
        Renderer(Renderer&&) noexcept = delete;
        Renderer& operator=(Renderer&&) noexcept = delete;

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

        // public:
        void InitGL(); // new: loads GLAD and fires OnContextReady once

        // public API:
        void SetIBLTextures(unsigned int irradianceCube,
            unsigned int prefilteredCube,
            unsigned int brdfLUT2D,
            float prefilteredMipCount);

    private:
        // Window / timing
        Window window_;
        float  deltaTime_ = 0.0f;
        float  lastFrame_ = 0.0f;

        unsigned int iblIrradiance_ = 0; // GL texture ids (0 = not set)
        unsigned int iblPrefiltered_ = 0;
        unsigned int iblBRDFLUT_ = 0;
        float        iblPrefilterMipCount_ = 0.0f;

        // Camera & input
        Camera      camera_{ glm::vec3(0.0f, 0.0f, 3.0f) };
        InputSystem input_;

        // UI hooks
        UIDrawFn       uiDraw_{};
        UICaptureFn    captureFn_{};
        OnContextReadyFn onReady_{};
        bool           readyFired_ = false;

        // mouse-look state
        bool rotating_ = false;
        bool firstMouse_ = true;
        double lastX_ = 0.0;
        double lastY_ = 0.0;

        void handleMouseLook_(bool uiWantsMouse);   // new
        // (optional) wheel zoom callback hook
        static void ScrollThunk_(GLFWwindow* w, double /*xoff*/, double yoff);
        void onScroll_(double yoff);

        // framebuffer resize -> update viewport
        static void FramebufferSizeThunk_(GLFWwindow* w, int width, int height);
        void onFramebufferSize_(int width, int height);

        // helpers
        void updateDeltaTime_();
        void setupGL_();           // creates GL state + fires OnContextReady once
    };

} // namespace MyCoreEngine

