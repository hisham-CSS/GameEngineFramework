#pragma once
// Draws the environment cubemap behind everything else.
//
// ORDERING: must run AFTER ForwardOpaquePass and BEFORE TonemapPass.
// - After forward, because it relies on the depth buffer to reject every
//   pixel geometry already claimed. Drawing it first would work too but
//   would shade a full screen of sky that opaque geometry then overwrites.
// - Before tonemap, because it writes LINEAR HDR into the same target. A sky
//   tonemapped separately from the geometry in front of it always looks
//   pasted on.

#include "../IRenderPass.h"

#include <memory>

namespace MyCoreEngine { class Shader; }

class ENGINE_API SkyboxPass final : public IRenderPass {
public:
    SkyboxPass();
    ~SkyboxPass() override;

    const char* name() const override { return "Skybox"; }
    void setup(PassContext&) override;
    bool execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& camera,
                 const FrameParams& fp) override;

    // Multiplies the sampled environment. Lets the sky be dimmed independently
    // of how strongly it lights the scene.
    void  setIntensity(float i) { intensity_ = i; }
    float intensity() const { return intensity_; }

private:
    std::unique_ptr<MyCoreEngine::Shader> shader_;
    unsigned cubeVAO_ = 0, cubeVBO_ = 0;
    float    intensity_ = 1.0f;
};
