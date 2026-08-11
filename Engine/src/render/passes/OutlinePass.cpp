// Engine/src/render/passes/OutlinePass.cpp
#include "OutlinePass.h"

#include "../../core/Shader.h"
#include "../../core/Scene.h"
#include "../../core/Camera.h"

#include <glad/glad.h>

OutlinePass::OutlinePass() = default;
OutlinePass::~OutlinePass() = default;

void OutlinePass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/outline_frag.glsl");
}

// Needs the scene depth texture as well as the effect being on. hdrDepthTex is
// published into the context before the Renderer counts, so it is safe to ask
// here -- and it has to be asked, or a depth-less frame would be miscounted.
bool OutlinePass::wantsLdrSlot(const PassContext& ctx, const MyCoreEngine::Scene& scene) const {
    return scene.PostFX().outline.enabled && ctx.hdrDepthTex &&
           shader_ && shader_->isValid();
}

bool OutlinePass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& cam,
                          const FrameParams& fp) {
    const auto& o = scene.PostFX().outline;
    if (!ctx.ldrTex_A || !wantsLdrSlot(ctx, scene)) return false;
    const PassContext::PostTarget t = ctx.nextPostTarget();

    glBindFramebuffer(GL_FRAMEBUFFER, t.dstFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setInt("uScene", 0);
    shader_->setInt("uDepth", 1);
    shader_->setVec2("uTexel",
                     1.0f / float(fp.viewportW > 0 ? fp.viewportW : 1),
                     1.0f / float(fp.viewportH > 0 ? fp.viewportH : 1));
    shader_->setFloat("uThickness", o.thickness);
    shader_->setFloat("uThreshold", o.threshold);
    shader_->setFloat("uStrength", o.strength);
    shader_->setVec3("uColor", o.color);
    shader_->setFloat("uNear", cam.NearClip);
    shader_->setFloat("uFar", cam.FarClip);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.srcTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.hdrDepthTex);

    glBindVertexArray(ctx.fsQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0); // leave unit 0 active for the next pass
    glEnable(GL_DEPTH_TEST);
    return true;
}
