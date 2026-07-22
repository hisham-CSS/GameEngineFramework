// Engine/src/render/passes/ColorGradePass.cpp
#include "ColorGradePass.h"

#include "../../core/Shader.h"
#include "../../core/Scene.h"

#include <glad/glad.h>

ColorGradePass::ColorGradePass() = default;
ColorGradePass::~ColorGradePass() = default;

void ColorGradePass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/colorgrade_frag.glsl");
}

bool ColorGradePass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera&,
                             const FrameParams& fp) {
    const auto& g = scene.PostFX().colorGrade;
    if (!g.enabled || !ctx.ldrTex_A || !shader_ || !shader_->isValid()) return false;
    const PassContext::PostTarget t = ctx.nextPostTarget();

    glBindFramebuffer(GL_FRAMEBUFFER, t.dstFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setInt("uScene", 0);
    shader_->setFloat("uContrast", g.contrast);
    shader_->setFloat("uSaturation", g.saturation);
    shader_->setFloat("uTemperature", g.temperature);
    shader_->setFloat("uTint", g.tint);
    shader_->setFloat("uLift", g.lift);
    shader_->setFloat("uGain", g.gain);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.srcTex);

    glBindVertexArray(ctx.fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    return true;
}
