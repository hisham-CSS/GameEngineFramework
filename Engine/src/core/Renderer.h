#pragma once
#include "Core.h"

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

#include "Window.h"
#include "Camera.h"
#include "Scene.h"
#include "Shader.h"
#include "InputSystem.h"
#include "Model.h"
#include "../render/RenderPipeline.h"
#include "../render/passes/ShadowCSMPass.h"
#include "../render/passes/TonemapPass.h"
#include "../render/passes/ForwardOpaquePass.h"

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
        void SetIBLTextures(unsigned int irradianceCube, unsigned int prefilteredCube, unsigned int brdfLUT2D, float prefilteredMipCount);

        // Light exposure
        float exposure() const { return exposure_; }
        void setExposure(float e) { exposure_ = std::max(0.01f, e); }

        // Sun / shadows
        glm::vec3 sunDir() const { return sunDir_; }

        // Setters used by the Editor UI
        void setSunDir(const glm::vec3& d) {
            glm::vec3 n = (glm::length(d) > 1e-6f) ? glm::normalize(d) : glm::vec3(0, -1, 0);
            if (glm::any(glm::epsilonNotEqual(n, sunDir_, 1e-6f))) {
                sunDir_ = n;
                shadowParamsDirty_ = true;
            }
        }

        //int cascadeResolution() const { return csmRes_; }
        //int getCascadeCount() const { return kCascades; }
        //int getCascadeResolution() const { return csmRes_; }
        //float debugCascadeSplit(int i) const { return splitZ_[i + 1]; }
        int csmDebugMode() const { return csmDebugMode_; }
        void setCSMDebugMode(int m) { csmDebugMode_ = glm::clamp(m, 0, 5); }


        // --- Editor: directional light rotation (Unity-style) ---
        // If enabled, renderer computes sunDir_ from yaw/pitch every frame.
        void  setUseSunYawPitch(bool e);
        bool  getUseSunYawPitch() const;
        void  setSunYawPitchDegrees(float yawDeg, float pitchDeg); // yaw=around +Y, pitch=look up/down
        void  getSunYawPitchDegrees(float& yawDeg, float& pitchDeg) const;

        // -------- Editor wrappers for CSM controls --------
        // (These let your existing editor panels keep calling into Renderer.)
        bool  getCSMEnabled() const;
        void  setCSMEnabled(bool e);
        
        float getCSMMaxShadowDistance() const;
        void  setCSMMaxShadowDistance(float d);
        float getCSMCascadePadding() const;
        void  setCSMCascadePadding(float m);
        float getCSMDepthMargin() const;
        void  setCSMDepthMargin(float m);

        // Bias/cull
        float getCSMSlopeDepthBias() const;
        void  setCSMSlopeDepthBias(float v);
        float getCSMConstantDepthBias() const;
        void  setCSMConstantDepthBias(float v);
        void  setCSMCullFrontFaces(bool on);
        bool  getCSMCullFrontFaces() const;


        float getCSMLambda() const;
        void  setCSMLambda(float v);
        int   getCSMBaseResolution() const;
        void  setCSMBaseResolution(int r);
        int   getCSMNumCascades() const;
        void  setCSMNumCascades(int n);
        ShadowCSMPass::UpdatePolicy getCSMUpdatePolicy() const;
        void  setCSMUpdatePolicy(ShadowCSMPass::UpdatePolicy p);
        int   getCSMCascadeBudget() const;
        void  setCSMCascadeBudget(int n);
        void  getCSMEpsilons(float& posMeters, float& angDegrees) const;
        void  setCSMEpsilons(float posMeters, float angDegrees);
        void  forceCSMUpdate(); // manual refresh

        const CSMSnapshot & getCSMSnapshot() const; // for debug UI

    private:
        // Window / timing
        Window window_;
        float deltaTime_ = 0.0f;
        float lastFrame_ = 0.0f;
        uint64_t frameIndex_ = 0;

        PassContext passCtx_{};
        RenderPipeline pipeline_;
        ShadowCSMPass* csmPass_ = nullptr; // optional raw ptr for quick access
        ForwardOpaquePass* forwardPass_ = nullptr;
        TonemapPass* tonemapPass_ = nullptr;
        
        CSMSnapshot nullSnap_{};             // fallback for getCSMSnapshot()
        

        unsigned int iblIrradiance_ = 0; // GL texture ids (0 = not set)
        unsigned int iblPrefiltered_ = 0;
        unsigned int iblBRDFLUT_ = 0;
        float iblPrefilterMipCount_ = 0.0f;

        // Shadow resources
        GLuint shadowDepthTex_ = 0;

        // === Shadows / CSM ===
        float splitBlend_ = 20.0f; // meters; expose in your UI if you like


        // Light (editor already exposes dir/intensity; add projection span)
        glm::vec3 sunDir_ = glm::vec3(-0.282f, -0.941f, 0.188f);

        // A simple dirty bit that forces shadow pass to re-render
        bool shadowParamsDirty_ = true;

        // HDR resources
        GLuint hdrFBO_ = 0;
        GLuint hdrColorTex_ = 0;
        GLuint hdrDepthRBO_ = 0;

        // Fullscreen quad
        GLuint fsQuadVAO_ = 0;
        GLuint fsQuadVBO_ = 0;

        // Tonemap shader
        std::unique_ptr<Shader> tonemapShader_;

        // Exposure control (you can surface this in the editor)
        float exposure_ = 1.0f;

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
        void recreateHDR_(int w, int h);

        // --- CSM (4 cascades) ---
        //int csmRes_ = 2048;
        int csmDebugMode_ = 0; // 0=off

        glm::mat4 Renderer::getCameraPerspectiveMatrix(const Camera& cam);

        // Directional light rotation control (optional)
        bool  useSunYawPitch_ = true;
        float sunYawDeg_ = -30.0f, sunPitchDeg_ = 50.0f; // Unity-like defaults
    };
} // namespace MyCoreEngine