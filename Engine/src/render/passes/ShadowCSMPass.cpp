// Engine/src/render/passes/ShadowCSMPass.cpp
#include "ShadowCSMPass.h"
#include <glad/glad.h>

ShadowCSMPass::ShadowCSMPass(int cascades, int baseRes)
    : cascades_(cascades), baseRes_(baseRes) {
}

void ShadowCSMPass::setup(PassContext& ctx) {
    // no-op for now; we’ll migrate your CSM FBO/tex creation here next step
    (void)ctx;
}

bool ShadowCSMPass::execute(PassContext& ctx, MyCoreEngine::Scene&, Camera&, const FrameParams&) {
    // Publish disabled snapshot so forward pass has predictable inputs
    ctx.csm.enabled = enabled_;
    ctx.csm.cascades = cascades_;
    for (int i = 0; i < kMaxCascades; ++i) {
        ctx.csm.depthTex[i] = 0;
        ctx.csm.resPer[i] = 0;
        ctx.csm.lightVP[i] = {};
        ctx.csm.splitFar[i] = 0.0f;
    }
    snap_ = ctx.csm;
    return false; // drew nothing yet
}
void ShadowCSMPass::setLambda(float v) {
    lambda_ = glm::clamp(v, 0.0f, 1.0f);
}
void ShadowCSMPass::setBaseResolution(int r) {
    baseRes_ = std::max(1, r);
    for (int i = 0; i < kMaxCascades; ++i) {
        resPer_[i] = baseRes_;
    }
}