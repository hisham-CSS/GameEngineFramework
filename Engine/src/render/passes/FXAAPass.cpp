#include "FXAAPass.h"

#include "../../core/Shader.h"

#include <glad/glad.h>

FXAAPass::FXAAPass() = default;
FXAAPass::~FXAAPass() = default;

void FXAAPass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/fxaa_frag.glsl");
}

// Runs last in the LDR chain when AA is on. A broken shader means no pass,
// which the count has to see too -- hence this, and not a copy in the Renderer.
bool FXAAPass::wantsLdrSlot(const PassContext&, const MyCoreEngine::Scene& scene) const {
    return scene.GetAAEnabled() && shader_ && shader_->isValid();
}

bool FXAAPass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera&,
                       const FrameParams& fp) {
    // ldrTex_A additionally guards the (rare) FBO-alloc-failed case, where the
    // Renderer zeroes postPassesLeft and the whole chain stands down together.
    if (!ctx.ldrTex_A || !wantsLdrSlot(ctx, scene)) return false;
    const PassContext::PostTarget t = ctx.nextPostTarget();

    glBindFramebuffer(GL_FRAMEBUFFER, t.dstFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setInt("uScene", 0);
    // Texel size must come from the ACTUAL target size, not a cached one: the
    // editor's viewport panel changes size constantly, and a stale texel step
    // makes FXAA sample the wrong neighbours and blur the whole image.
    shader_->setVec2("uTexel",
                     1.0f / static_cast<float>(fp.viewportW > 0 ? fp.viewportW : 1),
                     1.0f / static_cast<float>(fp.viewportH > 0 ? fp.viewportH : 1));
    shader_->setFloat("uEdgeThreshold", edgeThreshold_);
    shader_->setFloat("uEdgeThresholdMin", edgeThresholdMin_);
    shader_->setFloat("uSubpixel", subpixel_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.srcTex);

    glBindVertexArray(ctx.fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    return true;
}
