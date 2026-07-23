// Engine/src/render/passes/BloomPass.cpp
#include "BloomPass.h"

#include "../../core/Shader.h"
#include "../../core/Scene.h"

#include <glad/glad.h>
#include <algorithm>

BloomPass::BloomPass() = default;
BloomPass::~BloomPass() { release_(); }

void BloomPass::setup(PassContext&) {
    brightShader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/bloom_bright_frag.glsl");
    blurShader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/bloom_blur_frag.glsl");
    compositeShader_ = std::make_unique<MyCoreEngine::Shader>(
        "Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/bloom_composite_frag.glsl");
}

void BloomPass::release_() {
    if (texA_) glDeleteTextures(1, &texA_);
    if (fboA_) glDeleteFramebuffers(1, &fboA_);
    if (texB_) glDeleteTextures(1, &texB_);
    if (fboB_) glDeleteFramebuffers(1, &fboB_);
    texA_ = fboA_ = texB_ = fboB_ = 0;
    halfW_ = halfH_ = 0;
}

void BloomPass::ensureTargets_(int fullW, int fullH) {
    const int hw = std::max(1, fullW / 2);
    const int hh = std::max(1, fullH / 2);
    if (hw == halfW_ && hh == halfH_ && fboA_ && fboB_) return; // already sized
    release_();
    halfW_ = hw; halfH_ = hh;

    auto make = [&](unsigned& fbo, unsigned& tex) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // RGBA16F: bloom carries HDR energy through the blur/composite.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, hw, hh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    make(fboA_, texA_);
    make(fboB_, texB_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool BloomPass::execute(PassContext& ctx, MyCoreEngine::Scene& scene, Camera&,
                        const FrameParams& fp) {
    const auto& b = scene.PostFX().bloom;
    if (!b.enabled || !ctx.hdrColorTex || !ctx.hdrFBO ||
        !brightShader_ || !brightShader_->isValid()) {
        return false;
    }
    ensureTargets_(fp.viewportW, fp.viewportH);
    if (!fboA_ || !fboB_) return false;

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(ctx.fsQuadVAO);

    // 1) Bright-pass: scene HDR -> half-res buffer A.
    glBindFramebuffer(GL_FRAMEBUFFER, fboA_);
    glViewport(0, 0, halfW_, halfH_);
    brightShader_->use();
    brightShader_->setInt("uScene", 0);
    brightShader_->setFloat("uThreshold", b.threshold);
    brightShader_->setFloat("uKnee", 0.5f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.hdrColorTex);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 2) Separable Gaussian, ping-pong A<->B. Each iteration = H then V pass;
    // more iterations = wider, softer glow. Result ends in A.
    blurShader_->use();
    blurShader_->setInt("uImage", 0);
    const float du = 1.0f / float(halfW_);
    const float dv = 1.0f / float(halfH_);
    const int kIterations = 5;
    for (int i = 0; i < kIterations; ++i) {
        // horizontal: A -> B
        glBindFramebuffer(GL_FRAMEBUFFER, fboB_);
        blurShader_->setVec2("uDirection", du, 0.0f);
        glBindTexture(GL_TEXTURE_2D, texA_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // vertical: B -> A
        glBindFramebuffer(GL_FRAMEBUFFER, fboA_);
        blurShader_->setVec2("uDirection", 0.0f, dv);
        glBindTexture(GL_TEXTURE_2D, texB_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // 3) Composite: ADD the blurred glow back into the HDR buffer (bilinearly
    // upscaled) so tonemap picks it up. Additive blend, full-res viewport.
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    compositeShader_->use();
    compositeShader_->setInt("uBloom", 0);
    compositeShader_->setFloat("uIntensity", b.intensity);
    glBindTexture(GL_TEXTURE_2D, texA_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    return true;
}
