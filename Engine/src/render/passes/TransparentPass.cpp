#include "TransparentPass.h"
#include "ForwardShading.h"

#include <glad/glad.h>

bool TransparentPass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& cam,
                              const FrameParams& fp) {
    // Skip the whole pass when the frame collected no blend geometry -- the
    // common case, so transparency costs nothing until a scene uses it.
    if (!scene.HasTransparent()) return false;

    // Same HDR target the forward + skybox passes filled. Do NOT clear it.
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);

    shader_->use();
    // Re-bind shadows + IBL + camera so translucent geometry shades identically
    // to the opaque pass (the skybox pass ran in between on its own program).
    ApplyForwardShadingState(*shader_, ctx, fp);

    // Scene owns the sort + blend state + draw + state restore.
    scene.RenderTransparent(*shader_, cam);
    return true;
}
