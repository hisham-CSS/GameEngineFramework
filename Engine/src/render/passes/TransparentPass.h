#pragma once
// Draws Blend-mode geometry, alpha-composited over the opaque scene and the
// sky.
//
// ORDERING: after ForwardOpaquePass AND SkyboxPass, before TonemapPass.
// - After opaque + sky, because a transparent surface blends over whatever is
//   already in the HDR target behind it.
// - Before tonemap, because the blend must happen in linear HDR (a 50%-opacity
//   surface is a true radiance mix); tonemapping the composited result once is
//   correct, compositing after tonemap double-applies the curve.
//
// The heavy lifting (back-to-front sort, blend state, the draw) is in
// Scene::RenderTransparent; this pass only rebinds the forward shader's
// shadow/IBL state so translucent geometry is lit exactly like opaque geometry.

#include "../IRenderPass.h"

#include <memory>

class ENGINE_API TransparentPass final : public IRenderPass {
public:
    explicit TransparentPass(Shader& shader) : shader_(&shader) {}
    const char* name() const override { return "Transparent"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    Shader* shader_; // not owned; the same forward shader ForwardOpaquePass uses
    // The skinned forward variant for translucent skinned geometry (M3.2e):
    // the forward pair with "#define SKINNED 1", lit by the same state.
    std::unique_ptr<Shader> skinnedShader_;
};
