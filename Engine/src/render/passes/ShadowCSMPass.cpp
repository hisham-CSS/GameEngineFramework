// Engine/src/render/passes/ShadowCSMPass.cpp
#include "ShadowCSMPass.h"
#include <glad/glad.h>
#include <cfloat>
#include <cmath>
#include <cstring>

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
    // Reallocate if baseRes_ or cascade count changed.
    if (!shadowFBO_) glGenFramebuffers(1, &shadowFBO_);

    const bool needRealloc = (allocBaseRes_ != baseRes_) || (allocCascades_ != cascades_);
    // We keep all cascades at the same resolution == baseRes_ (simple, matches your UI).
    for (int i = 0; i < kMaxCascades; ++i) {
        const int desired = (i < cascades_) ? baseRes_ : 0;
        if (desired <= 0) {
            // disable unused cascade slot
            if (depth_[i]) {
                glDeleteTextures(1, &depth_[i]);
                depth_[i] = 0;
            }
            resPer_[i] = 0;
            continue;
        }
        if (!depth_[i]) {
            glGenTextures(1, &depth_[i]);
        }
        // (Re)define storage when first time or when resolution changed
        if (needRealloc || resPer_[i] != desired) {
            glBindTexture(GL_TEXTURE_2D, depth_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, desired, desired, 0,
            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            // Keep your previous params (PCF works with LINEAR + compare mode)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            resPer_[i] = desired;
            // After resize, force a clean rebuild + full redraw starting from cascade 0
            shadowParamsDirty_ = true;
            forceFullUpdateOnce_ = true;
            nextCascade_ = 0;
        }
    }
    allocBaseRes_ = baseRes_;
    allocCascades_ = cascades_;
}

bool ShadowCSMPass::rebuild_(const Camera& cam, float aspect) {

    const float n = 0.1f; // camera near; keep in sync with forward
    const float f = std::max(n + 1e-3f, maxShadowDistance_);
    const float eps = 1e-3f; // strictly increasing guard
    
    splitZ_[0] = n;
    if (splitMode_ == SplitMode::Fixed) {
        // simple, reliable: fixed ratios (feel free to tweak)
        // for 4 cascades these are ~{5%, 15%, 40%, 100%} of max distance
        static const float kDefaultRatios[4] = { 0.05f, 0.15f, 0.40f, 1.0f };
        for (int i = 1; i < cascades_; ++i) {
            float r = kDefaultRatios[std::min(i, 3)];
            float d = glm::clamp(n + r * (f - n), n + eps, f - eps);
            if (d <= splitZ_[i - 1] + eps) d = splitZ_[i - 1] + eps;
            splitZ_[i] = d;
        }
        splitZ_[cascades_] = f;
    }
    else { // Lambda (PSSM) with safeguards; works for λ∈[0,1]
        const float lam = glm::clamp(lambda_, 0.0f, 1.0f);
        for (int i = 1; i < cascades_; ++i) {
            const float si = float(i) / float(cascades_);
            const float logd = n * std::pow(f / n, si);
            const float lind = n + (f - n) * si;
            float d = glm::mix(lind, logd, lam);
            d = glm::clamp(d, n + eps, f - eps);
            if (d <= splitZ_[i - 1] + eps) d = splitZ_[i - 1] + eps;
            splitZ_[i] = d;
        }
        splitZ_[cascades_] = f;
    }
    for (int i = 0; i < cascades_; ++i) {
        splitFar_[i] = splitZ_[i + 1];   // publish to forward as FAR distances
    }
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

    // Any dirty setting must refresh even if the camera didn’t move:
    moved = moved || shadowParamsDirty_;

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
    int toUpdate = (budgetPerFrame_ <= 0) ? cascades_ : std::min(budgetPerFrame_, cascades_);
    if (forceFullUpdateOnce_) toUpdate = cascades_; // settings changed -> redraw all

    int updated = 0;
    for (int k = 0; k < cascades_ && updated < toUpdate; ++k) {
        const int i = (nextCascade_ + k) % cascades_;        
        
        // slice frustum corners (world)
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

        //glm::mat4 lightView = glm::lookAt(center - sunDir * 100.0f, center, glm::vec3(0, 1, 0));
        // Robust "up" to avoid flips when sunDir ~ world up
        glm::vec3 upCand = (std::abs(sunDir.y) > 0.95f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::mat4 lightView = glm::lookAt(center - sunDir * 100.0f, center, upCand);

        float minX = +FLT_MAX, maxX = -FLT_MAX, minY = +FLT_MAX, maxY = -FLT_MAX, minZ = +FLT_MAX, maxZ = -FLT_MAX;
        for (auto& c : corners) {
            glm::vec3 lp = glm::vec3(lightView * glm::vec4(c, 1.0));
            minX = std::min(minX, lp.x); maxX = std::max(maxX, lp.x);
            minY = std::min(minY, lp.y); maxY = std::max(maxY, lp.y);
            minZ = std::min(minZ, lp.z); maxZ = std::max(maxZ, lp.z);
        }
        // z range + configurable margins
        float zNear = std::max(0.001f, -maxZ - depthMarginMeters_);
        float zFar = (-minZ) + depthMarginMeters_;
        
        // XY padding (before snapping)
        minX -= cascadePaddingMeters_; maxX += cascadePaddingMeters_;
        minY -= cascadePaddingMeters_; maxY += cascadePaddingMeters_;
        
        // ----- stable texel snapping (CENTER snap) -----
        // keep width/height constant, snap only the center to the texel grid
        const int res = (resPer_[i] > 0) ? resPer_[i] : baseRes_;
        const float width = (maxX - minX);
        const float height = (maxY - minY);
        const float texX = width / float(res);
        const float texY = height / float(res);
        
        // current center in light space
        float cx = 0.5f * (minX + maxX);
        float cy = 0.5f * (minY + maxY);
        
        // snap center to texel-sized steps
        cx = std::floor(cx / texX) * texX;
        cy = std::floor(cy / texY) * texY;
        
        // rebuild bounds around snapped center with SAME extent
        minX = cx - width * 0.5f;
        maxX = cx + width * 0.5f;
        minY = cy - height * 0.5f;
        maxY = cy + height * 0.5f;

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
    glCullFace(cullFrontFaces_ ? GL_FRONT : GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(slopeBias_, constBias_);

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
    
    shadowParamsDirty_ = false;
    forceFullUpdateOnce_ = false;
    return (updated > 0);
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