// Engine/src/render/IRenderPass.h
#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include "../src/core/Shader.h"
#include "../src/core/Camera.h"  // for ENGINE_API
#include "../src/core/Scene.h"

// Forward decls to avoid heavy includes

struct FrameParams {
    // immutable per-frame view
    glm::mat4 view;         // from Renderer::camera_
    glm::mat4 proj;         // matches Renderer perspective
    float     deltaTime{ 0 };
    uint64_t  frameIndex{ 0 };
    int       viewportW{ 0 }, viewportH{ 0 };
};

struct CSMSnapshot {
    // Published by ShadowCSMPass; read by forward lighting
    int cascades{ 0 };                                  // you use 4
    std::array<glm::mat4, 4> lightVP{};              // uLightVP[n]
    std::array<float, 4> splitFar{};              // uCSMSplits[n] (far per slice)
    std::array<unsigned, 4> depthTex{};              // GL texture ids
    std::array<int, 4> resPer{};                // per-cascade size
    bool enabled{ false };
};

// IBL bindings (optional)
struct IBLSnapshot {
    unsigned int environment = 0;    // GL texture (cube) — drawn by SkyboxPass
    unsigned int irradiance = 0;     // GL texture (cube)
    unsigned int prefiltered = 0;    // GL texture (cube)
    unsigned int brdfLUT = 0;        // GL texture (2D)
    // MAX MIP INDEX of `prefiltered` (frag.glsl multiplies roughness by it),
    // not the number of mips. See IBLTextures::maxMip.
    float mipCount = 0.0f;
};

// Things the renderer already owns & sets up each frame.
struct PassContext {
    // GL targets
    unsigned defaultFBO{ 0 };
    unsigned hdrFBO{ 0 };
    unsigned hdrColorTex{ 0 };
    unsigned hdrDepthRBO{ 0 };

    // --- LDR post-process chain (ping-pong) ---
    // After tonemapping, an arbitrary number of full-screen LDR (gamma-space)
    // effects can run -- vignette, colour grade, depth outline, FXAA, ... They
    // bounce colour between two buffers A and B via nextPostTarget(); the LAST
    // enabled one resolves to defaultFBO. When none are enabled, tonemap writes
    // straight to defaultFBO and neither buffer is touched (nor allocated), so
    // a project using no post pays nothing. The Renderer sets postPassesLeft to
    // the count of enabled LDR passes each frame and seeds postSrcTex = ldrTex_A
    // (tonemap's first target); each pass decrements as it consumes a slot.
    unsigned ldrFBO_A{ 0 }, ldrTex_A{ 0 };
    unsigned ldrFBO_B{ 0 }, ldrTex_B{ 0 };
    int      postPassesLeft{ 0 };
    unsigned postSrcTex{ 0 };

    // Where TonemapPass writes: buffer A if any LDR post pass follows this
    // frame, else the final output.
    unsigned tonemapTarget() const { return postPassesLeft > 0 ? ldrFBO_A : defaultFBO; }

    struct PostTarget { unsigned srcTex{ 0 }; unsigned dstFBO{ 0 }; bool isFinal{ false }; };
    // Called by each enabled LDR post pass: returns the texture to sample and
    // the FBO to draw into, advancing the ping-pong. The final pass (nothing
    // left after it) resolves to defaultFBO.
    PostTarget nextPostTarget() {
        PostTarget t;
        t.srcTex = postSrcTex;
        --postPassesLeft;
        t.isFinal = (postPassesLeft <= 0);
        if (t.isFinal) {
            t.dstFBO = defaultFBO;
        } else {
            const bool srcIsA = (postSrcTex == ldrTex_A);
            t.dstFBO   = srcIsA ? ldrFBO_B : ldrFBO_A;
            postSrcTex = srcIsA ? ldrTex_B : ldrTex_A;
        }
        return t;
    }

    // Fullscreen quad for post
    unsigned fsQuadVAO{ 0 };

    // Global shaders/resources the renderer already creates
    Shader* tonemapShader{ nullptr };

    // Global lighting-ish knobs the renderer exposes
    glm::vec3 sunDir{ 0.f, -1.f, 0.f };
    float exposure{ 1.0f };

    // forward pass helpers (set per frame by Renderer)
    float splitBlend = 0.0f;     // meters; cascade cross-fade band
    int   csmDebug = 0;        // 0=off, >0 = modes in your shader

    // Receiver-side shadow filtering (consumed by Forward pass -> frag shader).
    // Biases are in texels (the shader scales by uCascadeTexel).
    float shadowBiasConst = 1.5f;
    float shadowBiasSlope = 2.0f;
    std::array<int, 4> cascadeKernel{ 1, 1, 1, 1 }; // PCF radius per cascade; 0 = single tap

    // Shadow data (produced by ShadowCSMPass; consumed by Forward pass)
    CSMSnapshot csm{};
	IBLSnapshot ibl{};
};

struct IRenderPass {
    virtual ~IRenderPass() = default;
    virtual const char* name() const = 0;

    // Called once after GL context is ready (you already have InitGL())
    virtual void setup(PassContext&) {}

    // Called when the window framebuffer changes (Renderer already handles this)
    virtual void resize(PassContext&, int /*w*/, int /*h*/) {}

    // Do the pass� work; return true if you drew something
    virtual bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) = 0;
};
