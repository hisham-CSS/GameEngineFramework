#include "SkyboxPass.h"

#include "../../core/Shader.h"

#include <glad/glad.h>

SkyboxPass::SkyboxPass() = default;

SkyboxPass::~SkyboxPass() {
    if (cubeVBO_) glDeleteBuffers(1, &cubeVBO_);
    if (cubeVAO_) glDeleteVertexArrays(1, &cubeVAO_);
}

void SkyboxPass::setup(PassContext&) {
    shader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/skybox_vert.glsl", "Exported/Shaders/skybox_frag.glsl");

    // Outward-facing unit cube; we look at it from the centre with culling off,
    // so winding does not matter, but keeping it conventional avoids surprises
    // if a caller leaves GL_CULL_FACE on.
    const float verts[] = {
        -1,-1,-1,  -1, 1, 1,  -1, 1,-1,  -1,-1,-1,  -1,-1, 1,  -1, 1, 1,
         1,-1,-1,   1, 1,-1,   1, 1, 1,   1,-1,-1,   1, 1, 1,   1,-1, 1,
        -1,-1,-1,   1,-1,-1,   1,-1, 1,  -1,-1,-1,   1,-1, 1,  -1,-1, 1,
        -1, 1,-1,  -1, 1, 1,   1, 1, 1,  -1, 1,-1,   1, 1, 1,   1, 1,-1,
        -1,-1,-1,  -1, 1,-1,   1, 1,-1,  -1,-1,-1,   1, 1,-1,   1,-1,-1,
        -1,-1, 1,   1,-1, 1,   1, 1, 1,  -1,-1, 1,   1, 1, 1,  -1, 1, 1,
    };
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

bool SkyboxPass::execute(PassContext& ctx, MyCoreEngine::Scene&, Camera&,
                         const FrameParams& fp) {
    if (!ctx.ibl.environment || !shader_ || !shader_->isValid() || !cubeVAO_) {
        return false; // no environment baked: leave the cleared background
    }

    // Same HDR target the forward pass just filled — do NOT clear it.
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);

    // LEQUAL because skybox_vert forces gl_Position.z == w, i.e. exactly the
    // far plane; the default GL_LESS would reject every one of those fragments
    // against a depth buffer cleared to 1.0 and draw nothing at all.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);   // the sky is infinitely far; it occludes nothing
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    shader_->use();
    // Strip translation: the sky must not parallax as the camera moves.
    glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(fp.view));
    shader_->setMat4("uView", viewNoTranslation);
    shader_->setMat4("uProjection", fp.proj);
    shader_->setInt("uEnvironment", 0);
    shader_->setFloat("uIntensity", intensity_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.ibl.environment);

    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Restore what the rest of the pipeline assumes.
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    return true;
}
