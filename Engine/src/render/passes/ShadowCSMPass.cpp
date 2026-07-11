// Engine/src/render/passes/ShadowCSMPass.cpp
#include "ShadowCSMPass.h"
#include <glad/glad.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include "../CSMSplits.h"

ShadowCSMPass::ShadowCSMPass(int cascades, int baseRes)
    : cascades_(cascades), baseRes_(baseRes) {
}

ShadowCSMPass::~ShadowCSMPass() {
    for (auto& tex : depth_) {
        if (tex) glDeleteTextures(1, &tex);
    }
    if (shadowFBO_) glDeleteFramebuffers(1, &shadowFBO_);
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
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // The forward shader samples these as plain sampler2D and compares
            // manually; compare mode must stay off (sampling a compare-mode depth
            // texture through a non-shadow sampler is undefined behavior).
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            resPer_[i] = desired;
            // After resize, force a clean rebuild + full redraw
            shadowParamsDirty_ = true;
            forceFullUpdateOnce_ = true;
            fboChecked_ = false;
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
    else { // Lambda (PSSM) using shared CPU helper; λ∈[0,1]
        const float lam = glm::clamp(lambda_, 0.0f, 1.0f);
        const auto Z = MyCoreEngine::ComputeCSMSplits(n, f, cascades_, lam);
        // copy results
        for (int i = 0; i <= cascades_; ++i) {
            splitZ_[i] = Z[i];    
        }
        // enforce strictly-increasing (guard float noise)
        for (int i = 1; i <= cascades_; ++i) {
            if (splitZ_[i] <= splitZ_[i - 1] + eps)
                splitZ_[i] = splitZ_[i - 1] + eps;
        }
    }

    //else { // Lambda (PSSM) with safeguards; works for λ∈[0,1]
    //    const float lam = glm::clamp(lambda_, 0.0f, 1.0f);
    //    for (int i = 1; i < cascades_; ++i) {
    //        const float si = float(i) / float(cascades_);
    //        const float logd = n * std::pow(f / n, si);
    //        const float lind = n + (f - n) * si;
    //        float d = glm::mix(lind, logd, lam);
    //        d = glm::clamp(d, n + eps, f - eps);
    //        if (d <= splitZ_[i - 1] + eps) d = splitZ_[i - 1] + eps;
    //        splitZ_[i] = d;
    //    }
    //    splitZ_[cascades_] = f;
    //}
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

    // --- global invalidation: sun/fov/aspect/settings affect every cascade ---
    const glm::vec3 pos = cam.Position;
    const glm::vec3 fwd = glm::normalize(cam.Front);
    const glm::vec3 sun = glm::normalize(ctx.sunDir);
    const float     aspect = (fp.viewportH > 0) ? float(fp.viewportW) / float(fp.viewportH) : 1.777f;
    const float     fovDeg = cam.Zoom;

    const float sunDot = glm::clamp(glm::dot(sun, glm::normalize(lastSunDir_)), -1.f, 1.f);
    const float sunDeg = glm::degrees(std::acos(sunDot));
    const bool  aspectChanged = (std::abs(aspect - lastAspect_) > 1e-4f);
    const bool  fovChanged = (std::abs(fovDeg - lastFovDeg_) > 1e-3f);

    if (shadowParamsDirty_ || forceFullUpdateOnce_ || (sunDeg > angEps_) || aspectChanged || fovChanged) {
        rebuild_(cam, aspect); // splits depend on fov/aspect/settings
        for (auto& v : cascadeValid_) v = false;
        lastSunDir_ = sun; lastAspect_ = aspect; lastFovDeg_ = fovDeg;
        shadowParamsDirty_ = false;
        forceFullUpdateOnce_ = false;
    }

    // --- per-cascade update decisions (movement-proportional cadence) ---
    // Each rendered cascade covers its slice PLUS a movement margin, so it
    // stays valid until the slice center drifts past that margin. Near
    // cascades update often but are cheap (small light frustum = few
    // casters); far cascades are expensive but update rarely. Deferred
    // cascades keep their published lightVP/depth-map pair consistent and
    // still cover the current slice, so nothing pops or shimmers.
    int needIdx[kMaxCascades] = {};
    int needCount = 0;
    for (int i = 0; i < cascades_; ++i) {
        const glm::vec3 sliceCenter = pos + fwd * (0.5f * (splitZ_[i] + splitZ_[i + 1]));
        bool needs = !cascadeValid_[i];
        if (!needs && policy_ == UpdatePolicy::Always) needs = true;
        if (!needs && policy_ == UpdatePolicy::CameraOrSunMoved) {
            // refresh slightly before the margin is exhausted so boundary
            // fragments never lose coverage to snap offsets / FP rounding
            needs = glm::length(sliceCenter - renderedCenter_[i]) > renderedMargin_[i] * 0.95f;
        }
        // Manual: only global invalidation (markDirty_) refreshes
        if (needs) needIdx[needCount++] = i;
    }

    // Optional cap (opt-in extra amortization; nearest cascades first).
    const int toUpdate = (budgetPerFrame_ > 0) ? std::min(budgetPerFrame_, needCount) : needCount;

    // Nothing stale: publish the still-valid snapshot and skip all GPU work.
    if (toUpdate == 0) {
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

    const glm::vec3 sunDir = sun;
    const glm::mat4 V = cam.GetViewMatrix();

    for (int k = 0; k < toUpdate; ++k) {
        const int i = needIdx[k];

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

        // ----- stable fit: bounding SPHERE of the slice -----
        // The radius depends only on FOV/aspect/split distances (the slice is
        // rigid geometry), NOT on camera position/orientation. A box fit to
        // the corners changes size as the camera turns, which resizes the
        // texel grid every update and makes snapping useless (shimmer).
        float radius = 0.f;
        for (auto& c : corners) radius = std::max(radius, glm::length(c - center));
        radius = std::ceil(radius * 16.f) / 16.f; // quantize away FP noise
        radius += cascadePaddingMeters_;

        // Movement margin: extra coverage so this cascade stays valid while
        // the camera moves, deferring the next (expensive) re-render.
        const float margin = std::max(1.0f, updateMarginFrac_ * radius);
        const float extent = radius + margin; // ortho half-size incl. margin

        // Robust "up" to avoid flips when sunDir ~ world up
        glm::vec3 upCand = (std::abs(sunDir.y) > 0.95f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const float backup = extent + depthMarginMeters_; // eye distance behind the slice
        glm::mat4 lightView = glm::lookAt(center - sunDir * backup, center, upCand);

        // Constant square extent; casters closer to the light than zNear are
        // handled by GL_DEPTH_CLAMP (pancaking) during the depth render.
        const float zNear = 0.01f;
        const float zFar = backup + extent + depthMarginMeters_;
        glm::mat4 lightProj = glm::orthoRH_NO(-extent, extent, -extent, extent, zNear, zFar);

        // ----- texel snap: nudge the projection so the world origin lands on
        // a texel corner. With a constant extent the texel size is constant,
        // so translation-only camera movement shifts the map by whole texels.
        const int res = (resPer_[i] > 0) ? resPer_[i] : baseRes_;
        glm::mat4 shadowMat = lightProj * lightView;
        glm::vec4 origin = shadowMat * glm::vec4(0.f, 0.f, 0.f, 1.f); // w == 1 for ortho
        const float halfRes = float(res) * 0.5f;
        glm::vec2 originTexels = glm::vec2(origin) * halfRes;
        glm::vec2 rounded = glm::round(originTexels);
        glm::vec2 snapOffset = (rounded - originTexels) / halfRes; // clip-space nudge
        glm::mat4 snap = glm::translate(glm::mat4(1.f), glm::vec3(snapOffset, 0.f));
        lightVP_[i] = snap * shadowMat;

        renderedCenter_[i] = center;
        renderedMargin_[i] = margin;
        cascadeValid_[i] = true;
    }

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
    // Pancake casters that sit between the light and the ortho near plane
    // onto the near plane instead of clipping them away (their shadows must
    // still land in the slice).
    glEnable(GL_DEPTH_CLAMP);

    depthProg_->use();
    depthProg_->setInt("uUseInstancing", 0);

    // Prepare cascade parameters
    // Note: We need to map from our "updated" list back to the actual cascades.
    // BUT RenderShadowsCombined processes ALL provided cascades.
    // If we only want to update SOME, we should passed only those.
    // HOWEVER, RenderShadowsCombined assumes sequential buckets 0..N.
    // If we only pass "Cascade 3", it will be bucket 0.
    // This is fine as long as our callback maps bucket index -> actual cascade index.
    
    struct BatchEntry {
        int actualIndex;
        Scene::CascadeParam param;
    };
    std::vector<BatchEntry> batch;
    batch.reserve(toUpdate);
    std::vector<Scene::CascadeParam> params;
    params.reserve(toUpdate);

    for (int k = 0; k < toUpdate; ++k) {
        const int i = needIdx[k];
        Scene::CascadeParam p;
        p.lightVP = lightVP_[i];
        p.splitNear = splitZ_[i];
        p.splitFar = splitZ_[i + 1]; // splitZ is [0..cascades]
        p.viewMatrix = V;

        BatchEntry be; be.actualIndex = i; be.param = p;
        batch.push_back(be);
        params.push_back(p);
    }

    // Callback to bind target
    auto preDraw = [&](int bucketIndex) {
        if (bucketIndex < 0 || bucketIndex >= (int)batch.size()) return;
        int i = batch[bucketIndex].actualIndex;
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_[i], 0);
        glViewport(0, 0, resPer_[i], resPer_[i]);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        // One-shot completeness check after each (re)allocation
        if (!fboChecked_) {
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "ERROR::CSM shadow FBO incomplete, status 0x"
                          << std::hex << status << std::dec << std::endl;
            }
            fboChecked_ = true;
        }
        // Clear depth
        glClear(GL_DEPTH_BUFFER_BIT);
    };

    // Execute combined pass
    scene.RenderShadowsCombined(*depthProg_, params, preDraw);

    // you already have this overload: Scene::RenderDepthCascade(...). :contentReference[oaicite:0]{index=0}
    // removed old loop


    // restore state
    glDisable(GL_DEPTH_CLAMP);
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
    
    #ifdef UNIT_TEST
        lastUpdatedCount_ = toUpdate;
    #endif

    return (toUpdate > 0);
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