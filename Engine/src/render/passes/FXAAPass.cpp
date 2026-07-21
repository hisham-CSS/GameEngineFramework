#include "FXAAPass.h"

#include "../../core/Shader.h"

#include <glad/glad.h>

FXAAPass::FXAAPass() = default;
FXAAPass::~FXAAPass() = default;

void FXAAPass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/fxaa_frag.glsl");
}

bool FXAAPass::execute(PassContext& ctx, MyCoreEngine::Scene&, Camera&,
                       const FrameParams& fp) {
    // ctx.postAAEnabled is what routes TonemapPass into the LDR target. If it
    // is off, tonemap already wrote the final output and there is nothing here
    // to resolve -- running anyway would read an LDR texture nobody filled.
    if (!ctx.postAAEnabled || !ctx.ldrColorTex || !shader_ || !shader_->isValid()) {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFBO);
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
    glBindTexture(GL_TEXTURE_2D, ctx.ldrColorTex);

    glBindVertexArray(ctx.fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    return true;
}
