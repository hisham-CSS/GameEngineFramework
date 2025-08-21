// Engine/src/render/passes/ShadowCSMPass.cpp
#include "ShadowCSMPass.h"
#include <glad/glad.h>
#include <cfloat>
#include <cmath>

ShadowCSMPass::ShadowCSMPass(int cascades, int baseRes)
    : cascades_(cascades), baseRes_(baseRes) {
}

void ShadowCSMPass::setup(PassContext& ctx) {
    if (!depthProg_) {
        depthProg_ = std::make_unique<Shader>(
            "Exported/Shaders/shadow_depth_vert.glsl",
            "Exported/Shaders/shadow_depth_frag.glsl");
    }
    ensureTargets_();
}

void ShadowCSMPass::ensureTargets_() {
    if (shadowFBO_ == 0) glGenFramebuffers(1, &shadowFBO_);

    // desired per-cascade sizes = 1x, 1/2x, 1/4x, (keep 4th coherent)
    const int desired[4] = {
        baseRes_, std::max(512, baseRes_ / 2),
        std::max(512, baseRes_ / 4), std::max(512, baseRes_ / 4)
    };

    for (int i = 0; i < cascades_; ++i) {
        const int want = desired[i];
        if (depth_[i] == 0 || resPer_[i] != want) {
            if (resPer_[i] != want) {
                resPer_[i] = want;
                if (depth_[i] == 0) glGenTextures(1, &depth_[i]);
                glBindTexture(GL_TEXTURE_2D, depth_[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                    resPer_[i], resPer_[i], 0,
                    GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                const float border[4] = { 1.f,1.f,1.f,1.f };
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool ShadowCSMPass::rebuild_(const Camera& cam, float aspect) {
    // this is your Renderer::rebuildCSM_ adapted to use lambda_ and aspect
    // near/far must match your camera proj in forward
    constexpr float camNear = 0.1f;
    const float camFar = maxShadowDistance_;

    // splits
    splitZ_[0] = camNear;
    splitZ_[cascades_] = camFar;
    for (int i = 1; i < cascades_; ++i) {
        float u = float(i) / float(cascades_);
        float log = camNear * powf(camFar / camNear, u);
        float uni = camNear + (camFar - camNear) * u;
        splitZ_[i] = glm::mix(uni, log, lambda_);
    }
    for (int i = 0; i < cascades_; ++i) splitFar_[i] = splitZ_[i + 1];

    // lightVP_ computed in execute (needs sunDir); splits are enough here
    return true;
}


bool ShadowCSMPass::execute(PassContext& ctx, Scene& scene, Camera& cam, const FrameParams& fp) {
    if (!enabled_) {
        ctx.csm = {}; // publish "off"
        return false;
    }
    ensureTargets_();
    ++frameIndex_;

    // --- movement & config change detection ---
    const glm::vec3 pos = cam.Position;
    const glm::vec3 fwd = cam.Front;
    const glm::vec3 sun = glm::normalize(ctx.sunDir);
    const float     aspect = (fp.viewportH > 0) ? float(fp.viewportW) / float(fp.viewportH) : 1.777f;
    const float     fovDeg = cam.Zoom;
    
    const glm::vec3 dp = pos - lastCamPos_;
    const float posMoved2 = glm::dot(dp, dp);
    const float fwdDot = glm::clamp(glm::dot(glm::normalize(fwd), glm::normalize(lastCamFwd_)), -1.f, 1.f);
    const float fwdDeg = glm::degrees(std::acos(fwdDot));
    const float sunDot = glm::clamp(glm::dot(glm::normalize(sun), glm::normalize(lastSunDir_)), -1.f, 1.f);
    const float sunDeg = glm::degrees(std::acos(sunDot));
    const bool  aspectChanged = (std::abs(aspect - lastAspect_) > 1e-4f);
    const bool  fovChanged = (std::abs(fovDeg - lastFovDeg_) > 1e-3f);
    
    bool moved = false;
    if (policy_ == UpdatePolicy::Always) {
        moved = true;
    }
    else if (policy_ == UpdatePolicy::CameraOrSunMoved) {
        moved = (posMoved2 > posEps_ * posEps_) || (fwdDeg > angEps_) || (sunDeg > angEps_) || aspectChanged || fovChanged;
    }
    else { // Manual
        moved = shadowParamsDirty_;
    }
    // If nothing changed, publish last snapshot and skip GPU work
    if (!moved) {
        ctx.csm.enabled = true;
        ctx.csm.cascades = cascades_;
        for (int i = 0; i < kMaxCascades; ++i) {
            ctx.csm.lightVP[i] = lightVP_[i];
            ctx.csm.splitFar[i] = splitFar_[i];
            ctx.csm.depthTex[i] = depth_[i];
            ctx.csm.resPer[i] = resPer_[i];
        }
        return false;
    }
    
    // Update caches
    lastCamPos_ = pos; lastCamFwd_ = fwd; lastSunDir_ = sun; lastAspect_ = aspect; lastFovDeg_ = fovDeg;
    shadowParamsDirty_ = false;

    // recompute splits whenever FOV/aspect/near/far changed
    rebuild_(cam, aspect);

    // Build per-cascade light VP using your stabilized ortho code
    const glm::vec3 sunDir = sun;
    const glm::mat4 V = cam.GetViewMatrix();

    // Decide how many cascades to refresh (round-robin)
    const int toUpdate = (budgetPerFrame_ <= 0) ? cascades_ : std::min(budgetPerFrame_, cascades_);
    int updated = 0;
    for (int k = 0; k < cascades_ && updated < toUpdate; ++k) {
        const int i = (nextCascade_ + k) % cascades_;        // slice frustum corners (world)
        glm::mat4 sliceProj = glm::perspective(glm::radians(cam.Zoom), aspect, splitZ_[i], splitZ_[i + 1]);
        glm::mat4 invSliceVP = glm::inverse(sliceProj * V);

        const glm::vec3 ndc[8] = {
            {-1,-1,-1},{+1,-1,-1},{+1,+1,-1},{-1,+1,-1},
            {-1,-1,+1},{+1,-1,+1},{+1,+1,+1},{-1,+1,+1}
        };
        std::array<glm::vec3, 8> corners{};
        for (int k = 0; k < 8; ++k) {
            glm::vec4 w = invSliceVP * glm::vec4(ndc[k], 1.0f);
            corners[k] = glm::vec3(w) / w.w;
        }

        glm::vec3 center(0);
        for (auto& c : corners) center += c;
        center *= 1.0f / 8.0f;

        glm::mat4 lightView = glm::lookAt(center - sunDir * 100.0f, center, glm::vec3(0, 1, 0));

        float minX = +FLT_MAX, maxX = -FLT_MAX, minY = +FLT_MAX, maxY = -FLT_MAX, minZ = +FLT_MAX, maxZ = -FLT_MAX;
        for (auto& c : corners) {
            glm::vec3 lp = glm::vec3(lightView * glm::vec4(c, 1.0));
            minX = std::min(minX, lp.x); maxX = std::max(maxX, lp.x);
            minY = std::min(minY, lp.y); maxY = std::max(maxY, lp.y);
            minZ = std::min(minZ, lp.z); maxZ = std::max(maxZ, lp.z);
        }
        //float zNear = std::max(0.001f, -maxZ - 5.0f);
        //float zFar = (-minZ) + 5.0f;

        // replace the hardcoded 5.0f margins with editor-configurable values
        float zNear = std::max(0.001f, -maxZ - depthMarginMeters_);
        float zFar = (-minZ) + depthMarginMeters_;
        
        // XY padding to tighten/loosen each cascade box in world units
        minX -= cascadePaddingMeters_; maxX += cascadePaddingMeters_;
        minY -= cascadePaddingMeters_; maxY += cascadePaddingMeters_;

        // texel snapping
        float wptx = (maxX - minX) / float(resPer_[i] ? resPer_[i] : baseRes_);
        float wpty = (maxY - minY) / float(resPer_[i] ? resPer_[i] : baseRes_);
        minX = std::floor(minX / wptx) * wptx; maxX = std::floor(maxX / wptx) * wptx;
        minY = std::floor(minY / wpty) * wpty; maxY = std::floor(maxY / wpty) * wpty;

        glm::mat4 lightProj = glm::orthoRH_NO(minX, maxX, minY, maxY, zNear, zFar);
        lightVP_[i] = lightProj * lightView;
		++updated;
    }
    nextCascade_ = (nextCascade_ + updated) % cascades_;

    // render depth per cascade (your renderCSM_ body)
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO_);
    glEnable(GL_DEPTH_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    depthProg_->use();
    depthProg_->setInt("uUseInstancing", 0);

    for (int k = 0; k < updated; ++k) {
        const int i = (nextCascade_ - updated + k + cascades_) % cascades_;

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_[i], 0);
        glViewport(0, 0, resPer_[i], resPer_[i]);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

#ifndef NDEBUG
        GLenum fb = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fb != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "Shadow FBO incomplete for cascade %d (0x%04x)\n", i, fb);
        }
#endif
        glClear(GL_DEPTH_BUFFER_BIT);
        scene.RenderDepthCascade(*depthProg_, lightVP_[i], splitZ_[i], splitZ_[i + 1], cam.GetViewMatrix());
        // you already have this overload: Scene::RenderDepthCascade(...). :contentReference[oaicite:0]{index=0}
    }

    // restore state
    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // publish snapshot for forward
    ctx.csm.enabled = true;
    ctx.csm.cascades = cascades_;
    for (int i = 0; i < kMaxCascades; ++i) {
        ctx.csm.lightVP[i] = lightVP_[i];
        ctx.csm.splitFar[i] = splitFar_[i];
        ctx.csm.depthTex[i] = depth_[i];
        ctx.csm.resPer[i] = resPer_[i];
    }
    return true;
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