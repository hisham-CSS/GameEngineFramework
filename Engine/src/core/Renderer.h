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

        // Light exposure
        float exposure() const { return exposure_; }
        void setExposure(float e) { exposure_ = std::max(0.01f, e); }
        
        // Sun / shadows
        glm::vec3 sunDir() const { return sunDir_; }
        float sunOrthoHalf() const { return sunOrthoHalf_; }
        float sunNear() const { return sunNear_; }
        float sunFar()  const { return sunFar_; }

        

        // Setters used by the Editor UI
        void setSunDir(const glm::vec3& d) {
            glm::vec3 n = (glm::length(d) > 1e-6f) ? glm::normalize(d) : glm::vec3(0, -1, 0);
            if (glm::any(glm::epsilonNotEqual(n, sunDir_, 1e-6f))) {
                sunDir_ = n;
                shadowParamsDirty_ = true;
            }
        }
        void setSunOrthoHalf(float v) { if (v != sunOrthoHalf_) { sunOrthoHalf_ = v; shadowParamsDirty_ = true; } }
        void setSunNear(float v) { if (v != sunNear_) { sunNear_ = v;     shadowParamsDirty_ = true; } }
        void setSunFar(float v) { if (v != sunFar_) { sunFar_ = v;     shadowParamsDirty_ = true; } }

        float cascadeLambda() const { return csmLambda_; }
        void  setCascadeLambda(float v) { csmLambda_ = glm::clamp(v, 0.0f, 1.0f); }
        int   cascadeResolution() const { return csmRes_; }
        void  setCascadeResolution(int r) { csmRes_ = std::max(512, r); }
        int  getCascadeCount() const { return kCascades; }
        int  getCascadeResolution() const { return csmRes_; }
        float debugCascadeSplit(int i) const { return splitZ_[i + 1]; }

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
        GLuint shadowDepthTex_ = 0;

        // === Shadows / CSM ===
        static constexpr int kCascades = 4;

        int   shadowSize_ = 2048;           // ImGui slider if you like
        float splitLambda_ = 0.6f;           // 0=uniform, 1=logarithmic
        bool  csmEnabled_ = true;

        // Light (editor already exposes dir/intensity; add projection span)
        glm::vec3 sunDir_ = glm::vec3(-0.282f, -0.941f, 0.188f);
        float     sunOrthoHalf_ = 100.0f;
        float     sunNear_ = 1.0f;
        float     sunFar_ = 300.0f;

        // A simple dirty bit that forces shadow pass to re-render
        bool shadowParamsDirty_ = true;
        bool csmDataDirty_ = true;           // set when camera moves/rotates beyond a threshold

        float     splitZ_[kCascades + 1];           // view-space distances [near, …, far]

        // GL objects
        GLuint shadowArrayTex_ = 0;                 // GL_TEXTURE_2D_ARRAY, depth-only
        GLuint shadowFBO_ = 0;

        // Optional threshold to avoid rebuilding on tiny camera jitters
        float csmRebuildPosEps_ = 0.05f;            // meters
        float csmRebuildAngEps_ = 0.5f;             // degrees
        glm::vec3 lastCamPos_ = {};
        glm::vec3 lastCamFwd_ = {};
        // === Shadows / CSM ===

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
        GLuint csmFBO_[kCascades] = { 0,0,0 };
        GLuint csmDepth_[kCascades] = { 0,0,0 };
        int    csmRes_ = 2048;
        float  csmLambda_ = 0.7f;            // 0=uniform, 1=log
        float  csmSplits_[kCascades] = { 0,0,0 }; // view-space distances (end of each split)
        glm::mat4 csmLightVP_[kCascades];       // matrices for each split
        int  shadowUpdateRate_ = 1;  // 1 = every frame, 2 = every other frame, etc.
        int  frameIndex_ = 0;
        void setShadowUpdateRate(int n) { shadowUpdateRate_ = std::max(1, n); }

        void computeCSMSplits_(float camNear, float camFar);
        void computeCSMMatrix_(int idx, const glm::mat4 & camView, const glm::mat4 & camProj, float splitNear, float splitFar, const glm::vec3 & sunDir, glm::mat4 & outLightVP);
        void renderCSMShadowPass_(const glm::mat4 & camView, const glm::mat4 & camProj, Scene &scene);
		void updateCSMDirty_(const Camera& cam);
		bool rebuildCSM_(const Camera& cam);
        glm::mat4 Renderer::getCameraPerspectiveMatrix(const Camera& cam);
        void renderCSM_(Scene& scene, const Camera& cam);
        void ensureCSM_();
    };

} // namespace MyCoreEngine

