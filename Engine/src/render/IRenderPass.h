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

// Things the renderer already owns & sets up each frame.
struct PassContext {
    // GL targets
    unsigned defaultFBO{ 0 };
    unsigned hdrFBO{ 0 };
    unsigned hdrColorTex{ 0 };
    unsigned hdrDepthRBO{ 0 };

    // Fullscreen quad for post
    unsigned fsQuadVAO{ 0 };

    // Global shaders/resources the renderer already creates
    Shader* tonemapShader{ nullptr };

    // Global lighting-ish knobs the renderer exposes
    glm::vec3 sunDir{ 0.f, -1.f, 0.f };
    float exposure{ 1.0f };

    // Shadow data (produced by ShadowCSMPass; consumed by Forward pass)
    CSMSnapshot csm{};
};

struct IRenderPass {
    virtual ~IRenderPass() = default;
    virtual const char* name() const = 0;

    // Called once after GL context is ready (you already have InitGL())
    virtual void setup(PassContext&) {}

    // Called when the window framebuffer changes (Renderer already handles this)
    virtual void resize(PassContext&, int /*w*/, int /*h*/) {}

    // Do the pass’ work; return true if you drew something
    virtual bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) = 0;
};
