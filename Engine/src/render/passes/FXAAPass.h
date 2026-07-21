#pragma once
// Fast approXimate Anti-Aliasing, as a resolve step from the LDR target to
// the final output.
//
// ORDERING: runs LAST, after TonemapPass.
//
// FXAA is a POST-TONEMAP filter. Its thresholds are tuned against perceptual
// luma, so it must see gamma-space LDR; run on linear HDR the same constants
// mean something entirely different and it smears highlights while ignoring
// dark detail. That is why TonemapPass renders into an intermediate LDR
// target (ctx.ldrFBO) whenever this pass is active, instead of writing
// straight to the output as it does otherwise.
//
// Chosen over MSAA deliberately: MSAA costs rasterization, and this renderer
// is rasterization-bound at ~40k instances, while measurement showed fill
// rate is the resource with headroom (a 16x pixel-count reduction changed
// frame time almost not at all). FXAA spends exactly the cheap resource.

#include "../IRenderPass.h"

#include <memory>

namespace MyCoreEngine { class Shader; }

class ENGINE_API FXAAPass final : public IRenderPass {
public:
    FXAAPass();
    ~FXAAPass() override;

    const char* name() const override { return "FXAA"; }
    void setup(PassContext&) override;
    bool execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& camera,
                 const FrameParams& fp) override;

    // Relative luma contrast needed before a pixel is treated as an edge.
    // 1/8 is the standard "quality" setting; lower catches more edges and
    // softens more of the image.
    void  setEdgeThreshold(float t) { edgeThreshold_ = t; }
    float edgeThreshold() const { return edgeThreshold_; }
    // Absolute floor, so near-black gradients are not "edges".
    void  setEdgeThresholdMin(float t) { edgeThresholdMin_ = t; }
    float edgeThresholdMin() const { return edgeThresholdMin_; }
    // Single-pixel-feature softening. 1.0 is soft, 0 disables.
    void  setSubpixel(float s) { subpixel_ = s; }
    float subpixel() const { return subpixel_; }

private:
    std::unique_ptr<MyCoreEngine::Shader> shader_;
    float edgeThreshold_ = 0.125f;
    float edgeThresholdMin_ = 0.0312f;
    float subpixel_ = 0.75f;
};
