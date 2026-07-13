#pragma once
#include "Core.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "Scene.h"
#include "Shader.h"
#include "Model.h"
#include "../render/RenderPipeline.h"
#include "../render/passes/ShadowCSMPass.h"
#include "../render/passes/TonemapPass.h"
#include "../render/passes/ForwardOpaquePass.h"

namespace MyCoreEngine {

    // Render-only: owns the render-pass pipeline, HDR targets, and render
    // settings (sun, shadows, IBL, exposure). The window, input, camera,
    // timing, and main loop live in Application.
    class ENGINE_API Renderer {
    public:
        Renderer() = default;
        ~Renderer(); // frees HDR + fullscreen-quad GL resources

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) noexcept = delete;
        Renderer& operator=(Renderer&&) noexcept = delete;

        // Creates GL state, HDR targets, and the CSM pass. Requires a current
        // GL context with GLAD already loaded (Application::InitGL does both).
        void Setup(int fbWidth, int fbHeight);

        // Renders one frame of the scene (CSM -> forward opaque -> tonemap)
        // into targetFBO (0 = window backbuffer; the editor passes a
        // RenderTarget's FBO to show the scene inside its Viewport panel).
        // The HDR pipeline resizes automatically when fbWidth/fbHeight change.
        void RenderFrame(Scene& scene, Shader& shader, Camera& camera,
                         int fbWidth, int fbHeight, float deltaTime,
                         unsigned targetFBO = 0);

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

        // Receiver-side (shader) shadow filtering: texel-scaled bias + PCF kernel radius
        float getShadowBiasConst() const { return shadowBiasConst_; }
        void  setShadowBiasConst(float v) { shadowBiasConst_ = std::max(0.f, v); }
        float getShadowBiasSlope() const { return shadowBiasSlope_; }
        void  setShadowBiasSlope(float v) { shadowBiasSlope_ = std::max(0.f, v); }
        int   getCascadeKernel(int cascade) const {
            return (cascade >= 0 && cascade < 4) ? cascadeKernel_[cascade] : 0;
        }
        void  setCascadeKernel(int cascade, int radius) {
            if (cascade >= 0 && cascade < 4) cascadeKernel_[cascade] = glm::clamp(radius, 0, 4);
        }

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
        int   getCSMDynamicIntervalCap() const;
        void  setCSMDynamicIntervalCap(int frames);
        void  getCSMEpsilons(float& posMeters, float& angDegrees) const;
        void  setCSMEpsilons(float posMeters, float angDegrees);
        void  forceCSMUpdate(); // manual refresh

        const CSMSnapshot& getCSMSnapshot() const; // for debug UI

    private:
        PassContext passCtx_{};
        RenderPipeline pipeline_;
        ShadowCSMPass* csmPass_ = nullptr; // optional raw ptr for quick access
        ForwardOpaquePass* forwardPass_ = nullptr;
        TonemapPass* tonemapPass_ = nullptr;

        CSMSnapshot nullSnap_{};             // fallback for getCSMSnapshot()

        uint64_t frameIndex_ = 0;
        int lastFbW_ = 0, lastFbH_ = 0; // HDR pipeline size tracking

        unsigned int iblIrradiance_ = 0; // GL texture ids (0 = not set)
        unsigned int iblPrefiltered_ = 0;
        unsigned int iblBRDFLUT_ = 0;
        float iblPrefilterMipCount_ = 0.0f;

        // === Shadows / CSM ===
        // Cross-fade band at cascade splits, in view-space meters. Keep small
        // relative to the cascade slice sizes: 20 m (the old value, tuned for
        // a 1000 m shadow distance) spanned entire mid cascades.
        float splitBlend_ = 4.0f;

        // Receiver-side shadow filtering (uploaded by the forward pass each frame)
        float shadowBiasConst_ = 1.5f;                  // texels
        float shadowBiasSlope_ = 2.0f;                  // texels, scaled by (1 - N.L)
        std::array<int, 4> cascadeKernel_{ 1, 1, 1, 1 }; // PCF radius per cascade

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

        // helpers
        void recreateHDR_(int w, int h);

        int csmDebugMode_ = 0; // 0=off

        // Directional light rotation control (optional)
        bool  useSunYawPitch_ = true;
        float sunYawDeg_ = -30.0f, sunPitchDeg_ = 50.0f; // Unity-like defaults
    };
} // namespace MyCoreEngine
