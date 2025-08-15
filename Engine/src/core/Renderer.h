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

        // Sun / shadows
        glm::vec3 sunDir() const { return sunDir_; }
        void setSunDir(const glm::vec3 & d) { sunDir_ = glm::normalize(d); }
        float sunOrthoHalf() const { return sunOrthoHalf_; }
        void setSunOrthoHalf(float h) { sunOrthoHalf_ = std::max(0.1f, h); }
        float sunNear() const { return sunNear_; }
        float sunFar()  const { return sunFar_; }
        void setSunNearFar(float n, float f) { sunNear_ = n; sunFar_ = std::max(n + 1.f, f); }
        float exposure() const { return exposure_; }
        void setExposure(float e) { exposure_ = std::max(0.01f, e); }

        float cascadeLambda() const { return csmLambda_; }
        void  setCascadeLambda(float v) { csmLambda_ = glm::clamp(v, 0.0f, 1.0f); }
        int   cascadeResolution() const { return csmRes_; }
        void  setCascadeResolution(int r) { csmRes_ = std::max(512, r); }
        int  getCascadeCount() const { return kCascadeCount_; }
        int  getCascadeResolution() const { return csmRes_; }
        float debugCascadeSplit(int i) const {
            return (i >= 0 && i < kCascadeCount_) ? csmSplits_[i] : 0.0f;
        }

    private:
        // Window / timing
        Window window_;
        float  deltaTime_ = 0.0f;
        float  lastFrame_ = 0.0f;

        unsigned int iblIrradiance_ = 0; // GL texture ids (0 = not set)
        unsigned int iblPrefiltered_ = 0;
        unsigned int iblBRDFLUT_ = 0;
        float        iblPrefilterMipCount_ = 0.0f;

        // Shadow resources
        GLuint shadowFBO_ = 0;
        GLuint shadowDepthTex_ = 0;
        int    shadowSize_ = 2048;

        // Light (editor already exposes dir/intensity; add projection span)
        glm::vec3 sunDir_ = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
        float     sunOrthoHalf_ = 100.0f;   // view volume half-extent
        float     sunNear_ = 1.0f;
        float     sunFar_ = 300.0f;
        glm::mat4 lightViewProj_{ 1.0f };
        // Shadow pass shader (depth-only)
        std::unique_ptr<Shader> shadowDepthShader_;

        glm::mat4 lastCamViewProj_ = glm::mat4(1.0f);
        glm::vec3 lastSunDir_ = glm::vec3(0, -1, 0);
        float     updateThreshold_ = 0.002f;   // tweak

        bool needShadowUpdate_(const glm::mat4& camVP, const glm::vec3& sunDir) const {
            float dSun = glm::length(sunDir - lastSunDir_);
            float dCam = glm::length(glm::vec4(camVP[0][0] - lastCamViewProj_[0][0],
                camVP[1][1] - lastCamViewProj_[1][1],
                camVP[2][2] - lastCamViewProj_[2][2],
                camVP[3][3] - lastCamViewProj_[3][3]));
            return (dSun > 1e-5f) || (dCam > updateThreshold_);
        }

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


        // --- CSM (3 cascades) ---
        static constexpr int kCascadeCount_ = 3;
        GLuint csmFBO_[kCascadeCount_] = { 0,0,0 };
        GLuint csmDepth_[kCascadeCount_] = { 0,0,0 };
        int    csmRes_ = 2048;
        float  csmLambda_ = 0.7f;            // 0=uniform, 1=log
        float  csmSplits_[kCascadeCount_] = { 0,0,0 }; // view-space distances (end of each split)
        glm::mat4 csmLightVP_[kCascadeCount_];       // matrices for each split
        int  shadowUpdateRate_ = 1;  // 1 = every frame, 2 = every other frame, etc.
        int  frameIndex_ = 0;
        void setShadowUpdateRate(int n) { shadowUpdateRate_ = std::max(1, n); }

        void computeCSMSplits_(float camNear, float camFar);
        void computeCSMMatrix_(int idx,
            const glm::mat4 & camView,
            const glm::mat4 & camProj,
            float splitNear, float splitFar,
            const glm::vec3 & sunDir,
            glm::mat4 & outLightVP);
        void renderCSMShadowPass_(const glm::mat4 & camView, const glm::mat4 & camProj, Scene &scene);
    };

} // namespace MyCoreEngine

