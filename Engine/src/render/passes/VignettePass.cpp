// Engine/src/render/passes/VignettePass.cpp
#include "VignettePass.h"

#include "../../core/Shader.h"
#include "../../core/Scene.h"

#include <glad/glad.h>

VignettePass::VignettePass() = default;
VignettePass::~VignettePass() = default;

void VignettePass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/vignette_frag.glsl");
}

bool VignettePass::wantsLdrSlot(const PassContext&, const MyCoreEngine::Scene& scene) const {
    return scene.PostFX().vignette.enabled && shader_ && shader_->isValid();
}

bool VignettePass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera&,
                           const FrameParams& fp) {
    const auto& v = scene.PostFX().vignette;
    // ldrTex_A guards the rare FBO-alloc-failed case (then no chain runs).
    if (!ctx.ldrTex_A || !wantsLdrSlot(ctx, scene)) return false;
    const PassContext::PostTarget t = ctx.nextPostTarget();

    glBindFramebuffer(GL_FRAMEBUFFER, t.dstFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setInt("uScene", 0);
    shader_->setFloat("uIntensity", v.intensity);
    shader_->setFloat("uRoundness", v.roundness);
    shader_->setFloat("uSmoothness", v.smoothness);
    shader_->setFloat("uAspect", fp.viewportH > 0
                                     ? float(fp.viewportW) / float(fp.viewportH)
                                     : 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.srcTex);

    glBindVertexArray(ctx.fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    return true;
}
