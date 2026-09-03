#include "TransparentPass.h"
#include "ForwardShading.h"
#include "../SkinPalette.h"

#include <glad/glad.h>

void TransparentPass::setup(PassContext&) {
    if (!skinnedShader_) {
        skinnedShader_ = std::make_unique<Shader>(
            "Exported/Shaders/vertex.glsl",
            "Exported/Shaders/frag.glsl",
            "#define SKINNED 1");
        if (skinnedShader_->isValid())
            skinnedShader_->bindUniformBlock(MyCoreEngine::SkinPaletteUBO::kBlockName,
                                             MyCoreEngine::SkinPaletteUBO::kBinding);
    }
}

bool TransparentPass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& cam,
                              const FrameParams& fp) {
    // Skip the whole pass when the frame collected no blend geometry -- the
    // common case, so transparency costs nothing until a scene uses it.
    if (!scene.HasTransparent()) return false;

    // Same HDR target the forward + skybox passes filled. Do NOT clear it.
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);

    // The skinned variant gets the same per-frame state, so a translucent
    // skinned item is lit and shadowed exactly like an opaque one (M3.2e).
    if (skinnedShader_ && skinnedShader_->isValid()) {
        skinnedShader_->use();
        ApplyForwardShadingState(*skinnedShader_, ctx, fp);
        scene.SetSkinnedTransparentShader(skinnedShader_.get());
    }
    else {
        scene.SetSkinnedTransparentShader(nullptr);
    }

    shader_->use();
    // Re-bind shadows + IBL + camera so translucent geometry shades identically
    // to the opaque pass (the skybox pass ran in between on its own program).
    ApplyForwardShadingState(*shader_, ctx, fp);

    // Scene owns the sort + blend state + draw + state restore.
    scene.RenderTransparent(*shader_, cam);
    return true;
}
