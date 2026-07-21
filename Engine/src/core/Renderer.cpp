#include <glad/glad.h>

#include "Renderer.h"
#include "../render/passes/ForwardOpaquePass.h"
#include "../render/passes/TonemapPass.h"


#include <stdexcept>


#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp> // normalize, dot

namespace MyCoreEngine {

    static inline float degBetween(const glm::vec3& a, const glm::vec3& b) {
        float d = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
        return glm::degrees(acosf(d));
    }

    static inline glm::vec3 dirFromYawPitch(float yawDeg, float pitchDeg) {
        const float yaw = glm::radians(yawDeg);
        const float pitch = glm::radians(pitchDeg);
        // Right-handed: +Y up, -Z forward (typical view)
        const float cy = cosf(yaw), sy = sinf(yaw);
        const float cp = cosf(pitch), sp = sinf(pitch);
        // Point roughly towards (-Z) when yaw=pitch=0
        glm::vec3 d;
        d.x = sy * cp;
        d.y = -sp;
        d.z = -cy * cp;
        return glm::normalize(d);
    }

    Renderer::~Renderer() {
        // Application declares its Window before this Renderer, so the GL
        // context is destroyed after us — the deletes below run with a live
        // context.
        if (fsQuadVBO_) glDeleteBuffers(1, &fsQuadVBO_);
        if (fsQuadVAO_) glDeleteVertexArrays(1, &fsQuadVAO_);
        if (hdrDepthRBO_) glDeleteRenderbuffers(1, &hdrDepthRBO_);
        if (hdrColorTex_) glDeleteTextures(1, &hdrColorTex_);
        if (hdrFBO_) glDeleteFramebuffers(1, &hdrFBO_);
    }

    void Renderer::Setup(int fbWidth, int fbHeight) {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

		// HDR textures (IBL)
        // --- HDR FBO ---
        const int fbw = std::max(1, fbWidth);
        const int fbh = std::max(1, fbHeight);

        glGenFramebuffers(1, &hdrFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);

        // 16-bit float color
        glGenTextures(1, &hdrColorTex_);
        glBindTexture(GL_TEXTURE_2D, hdrColorTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fbw, fbh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // depth renderbuffer
        glGenRenderbuffers(1, &hdrDepthRBO_);
        glBindRenderbuffer(GL_RENDERBUFFER, hdrDepthRBO_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbw, fbh);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColorTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrDepthRBO_);

        if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR::HDR FBO incomplete, status 0x" << std::hex << status << std::dec << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // --- Fullscreen quad (NDC) ---
        float quad[6 * 4] = {
            //  pos        uv
               -1.f, -1.f, 0.f, 0.f,
                1.f, -1.f, 1.f, 0.f,
                1.f,  1.f, 1.f, 1.f,
               -1.f, -1.f, 0.f, 0.f,
                1.f,  1.f, 1.f, 1.f,
               -1.f,  1.f, 0.f, 1.f
        };
        glGenVertexArrays(1, &fsQuadVAO_);
        glGenBuffers(1, &fsQuadVBO_);
        glBindVertexArray(fsQuadVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, fsQuadVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);

        // Tonemap shader (lazy create is also fine; doing it here keeps RenderFrame clean)
        tonemapShader_ = std::make_unique<Shader>("Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/tonemap_frag.glsl");

        glViewport(0, 0, fbw, fbh);

        // Seed the size tracking with what we just allocated: RenderFrame
        // must recreate the HDR pipeline the moment its output size differs.
        // (The old `lastFbW_ != 0` guard skipped the FIRST differing frame,
        // permanently locking renderers whose first render size never
        // changes again — the editor's Game view — to the Setup size.)
        lastFbW_ = fbw;
        lastFbH_ = fbh;

        passCtx_.defaultFBO = 0;
        passCtx_.hdrFBO = hdrFBO_;
        passCtx_.hdrColorTex = hdrColorTex_;
        passCtx_.hdrDepthRBO = hdrDepthRBO_;
        passCtx_.fsQuadVAO = fsQuadVAO_;
        passCtx_.tonemapShader = tonemapShader_.get();
        passCtx_.exposure = exposure_;

        if (!csmPass_) {
            csmPass_ = &pipeline_.add<ShadowCSMPass>(/*cascades*/4, /*baseRes*/2048);
            pipeline_.setup(passCtx_);
            csmPass_->setUpdatePolicy(ShadowCSMPass::UpdatePolicy::CameraOrSunMoved);
            // Update ALL cascades whenever an update triggers. Round-robin
            // amortization (budget >= 1) leaves stale cascades whose light
            // matrices no longer cover the current slice -> shadows pop while
            // the camera moves. Opt back in via setCSMCascadeBudget if needed.
            csmPass_->setCascadeUpdateBudget(0);
            // seed defaults so Editor sliders reflect reality
            csmPass_->setNumCascades(4);
            csmPass_->setLambda(0.7f);
            csmPass_->setEpsilons(0.05f /*meters*/, 0.5f /*degrees*/);
            csmPass_->setEnabled(true);
            // base resolution already set from ctor via csmRes_
        }

    }

    void Renderer::RenderFrame(Scene& scene, Shader& shader, Camera& camera,
                               int fbWidth, int fbHeight, float deltaTime,
                               unsigned targetFBO) {
        // resize the HDR pipeline whenever the output size changes (window
        // resize in the player, viewport-panel resize in the editor)
        if (fbWidth != lastFbW_ || fbHeight != lastFbH_) {
            if (hdrFBO_) recreateHDR_(fbWidth, fbHeight);
            lastFbW_ = fbWidth; lastFbH_ = fbHeight;
        }
        passCtx_.defaultFBO = targetFBO;
        passCtx_.hdrFBO = hdrFBO_;
        passCtx_.hdrColorTex = hdrColorTex_;
        passCtx_.hdrDepthRBO = hdrDepthRBO_;

        // ensure forward & tonemap passes exist (CSM was added in Setup)
        if (!forwardPass_) {
            forwardPass_ = &pipeline_.add<ForwardOpaquePass>(shader);
            pipeline_.setup(passCtx_); // idempotent
        }
        // ORDER IS LOAD-BEARING and is encoded ONLY by the sequence of these
        // blocks, because RenderPipeline::add appends. Skybox must sit between
        // forward and tonemap: it needs the depth buffer forward produced, and
        // it writes linear HDR that tonemap must still process. Moving either
        // block past the other silently changes the frame.
        if (!skyboxPass_) {
            skyboxPass_ = &pipeline_.add<SkyboxPass>();
            pipeline_.setup(passCtx_);
        }
        if (!tonemapPass_) {
            tonemapPass_ = &pipeline_.add<TonemapPass>();
            pipeline_.setup(passCtx_);
        }

        // Bake the environment if it changed. Driven from here so BOTH hosts
        // get it without either having to remember, which is what keeps Play
        // and the shipped build lit identically.
        SyncEnvironment(scene.Environment(), sunDir_);

        // Clear screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // per-frame matrices (clip planes come from the camera — synced from
        // the CameraComponent when rendering through a scene camera)
        const float aspect = (fbHeight > 0) ? float(fbWidth) / float(fbHeight) : 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect,
                                                camera.NearClip, camera.FarClip);
        glm::mat4 view = camera.GetViewMatrix();

        // per-frame params
        FrameParams fp;
        fp.view = view;
        fp.proj = projection;
        fp.deltaTime = deltaTime;
        fp.frameIndex = ++frameIndex_;
        fp.viewportW = fbWidth;
        fp.viewportH = fbHeight;

        // Update CSM if needed
        if (useSunYawPitch_) {
            sunDir_ = dirFromYawPitch(sunYawDeg_, sunPitchDeg_);
        }

        // keep passCtx_ in sync with per-frame globals you already tweak
        passCtx_.sunDir = sunDir_;
        passCtx_.exposure = exposure_;

        // per-frame knobs for passes
        passCtx_.splitBlend = splitBlend_;
        passCtx_.csmDebug = csmDebugMode_;
        passCtx_.shadowBiasConst = shadowBiasConst_;
        passCtx_.shadowBiasSlope = shadowBiasSlope_;
        passCtx_.cascadeKernel = cascadeKernel_;
        passCtx_.ibl.environment = iblEnvironment_;
        passCtx_.ibl.irradiance = iblIrradiance_;
        passCtx_.ibl.prefiltered = iblPrefiltered_;
        passCtx_.ibl.brdfLUT = iblBRDFLUT_;
        passCtx_.ibl.mipCount = iblPrefilterMipCount_;

        // execute the whole pipeline (CSM → forward → tonemap)
        pipeline_.executeAll(passCtx_, scene, camera, fp);
    }


    void Renderer::SetIBLTextures(unsigned int irr, unsigned int pre, unsigned int lut, float mipCount) {
        iblIrradiance_ = irr;
        iblPrefiltered_ = pre;
        iblBRDFLUT_ = lut;
        iblPrefilterMipCount_ = mipCount;
    }

    void Renderer::SyncEnvironment(const EnvironmentSettings& env, const glm::vec3& sunDir) {
        bool needsBake = !envApplied_ || env != appliedEnv_;

        // The procedural sky bakes the sun INTO the environment, so moving the
        // sun must re-bake or the sky keeps yesterday's sun while the shadows
        // move. Thresholded because baking costs milliseconds and dragging the
        // sun slider would otherwise re-bake every frame of the drag.
        if (!needsBake && env.source == EnvironmentSettings::Source::ProceduralSky) {
            needsBake = degBetween(sunDir, appliedSunDir_) > 1.5f;
        }
        if (!needsBake) return;

        if (ApplyEnvironment(env, sunDir)) {
            appliedEnv_ = env;
            appliedSunDir_ = sunDir;
            envApplied_ = true;
        } else {
            // Record the attempt anyway. Without this a permanently failing
            // bake (a bad HDRi path) retries every single frame, which turns
            // one mistake into a stutter the user cannot connect to anything.
            appliedEnv_ = env;
            appliedSunDir_ = sunDir;
            envApplied_ = true;
        }
    }

    bool Renderer::ApplyEnvironment(const EnvironmentSettings& env, const glm::vec3& sunDir) {
        bool ok = false;
        if (env.source == EnvironmentSettings::Source::HDRi && !env.hdriPath.empty()) {
            ok = ibl_.BakeFromFile(env.hdriPath);
            if (!ok) {
                // Fall back rather than going black. A mistyped path in the
                // Inspector should not leave the author staring at an unlit
                // scene with no idea which setting broke it; the error string
                // is surfaced next to the field instead.
                std::printf("[ibl] %s - falling back to the procedural sky\n",
                            ibl_.lastError().c_str());
            }
        }
        if (!ok) {
            IBLBaker::SkyParams sky;
            sky.sunDir = sunDir;
            sky.zenith = env.zenith;
            sky.horizon = env.horizon;
            sky.ground = env.ground;
            sky.sunIntensity = env.sunIntensity;
            ok = ibl_.BakeProceduralSky(sky);
        }
        if (!ok) return false;

        const IBLTextures& t = ibl_.textures();
        SetIBLTextures(t.irradiance, t.prefiltered, t.brdfLUT, t.maxMip);
        SetEnvironmentCube(env.drawSkybox ? t.environment : 0u);
        if (skyboxPass_) skyboxPass_->setIntensity(env.skyIntensity);
        return true;
    }

    void Renderer::recreateHDR_(int w, int h) {
        // delete old
        if (hdrColorTex_) { glDeleteTextures(1, &hdrColorTex_); hdrColorTex_ = 0; }
        if (hdrDepthRBO_) { glDeleteRenderbuffers(1, &hdrDepthRBO_); hdrDepthRBO_ = 0; }
        // color
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
        glGenTextures(1, &hdrColorTex_);
        glBindTexture(GL_TEXTURE_2D, hdrColorTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColorTex_, 0);
        // depth-stencil
        glGenRenderbuffers(1, &hdrDepthRBO_);
        glBindRenderbuffer(GL_RENDERBUFFER, hdrDepthRBO_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrDepthRBO_);
        if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR::HDR FBO incomplete after resize, status 0x" << std::hex << status << std::dec << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void frustumCornersViewSpace(const glm::mat4& invProj, float nz, float fz, std::array<glm::vec3, 8>& out)
    {
        // NDC cube corners
        static const glm::vec3 ndc[8] = {
          {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
          {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };
        // scale z to slice
        // map ndc z -1..1 to the slice plane in clip, then to view using invProj
        int idx = 0;
        for (int i = 0; i < 8; ++i) {
            glm::vec4 p = glm::vec4(ndc[i], 1.0);
            // remap z: -1 => nz, +1 => fz in clip space isn’t linear; easiest is to build a proj with (nz,fz) and invert once.
            out[idx++] = glm::vec3(invProj * p); // homogeneous divide happens outside; we’ll do normalize later
        }
        // we’ll actually compute corners by directly inverting (proj for (nz,fz)) below in computeCSMMatrix_.
    }

    // -------- Editor wrapper implementations --------
    bool Renderer::getCSMEnabled() const {
        return csmPass_ ? csmPass_->enabled() : false;
    }
    void Renderer::setCSMEnabled(bool e) {
        if (csmPass_) csmPass_->setEnabled(e);
    }
    float Renderer::getCSMMaxShadowDistance() const {
        return csmPass_ ? csmPass_->maxShadowDistance() : 200.f;
    }
    void Renderer::setCSMMaxShadowDistance(float d) {
        if (csmPass_) csmPass_->setMaxShadowDistance(d);
    }
    float Renderer::getCSMCascadePadding() const {
        return csmPass_ ? csmPass_->cascadePaddingMeters() : 0.0f;
    }
    void Renderer::setCSMCascadePadding(float m) {
        if (csmPass_) csmPass_->setCascadePaddingMeters(m);
    }
    float Renderer::getCSMDepthMargin() const {
        return csmPass_ ? csmPass_->depthMarginMeters() : 5.0f;
    }
    void Renderer::setCSMDepthMargin(float m) {
        if (csmPass_) csmPass_->setDepthMarginMeters(m);
    }
    float Renderer::getCSMLambda() const {
        return csmPass_ ? csmPass_->lambda() : 0.7f;
    }
    void Renderer::setCSMLambda(float v) {
        if (csmPass_) csmPass_->setLambda(v);
    }
    int Renderer::getCSMBaseResolution() const {
        return csmPass_ ? csmPass_->baseResolution() : 2048;
    }
    void Renderer::setCSMBaseResolution(int r) {
        if (csmPass_) csmPass_->setBaseResolution(r);
    }
    int Renderer::getCSMNumCascades() const {
        return csmPass_ ? csmPass_->numCascades() : 4;
    }
    void Renderer::setCSMNumCascades(int n) {
        if (csmPass_) csmPass_->setNumCascades(n);
    }
    ShadowCSMPass::UpdatePolicy Renderer::getCSMUpdatePolicy() const {
        return csmPass_ ? csmPass_->updatePolicy() : ShadowCSMPass::UpdatePolicy::CameraOrSunMoved;
    }
    void Renderer::setCSMUpdatePolicy(ShadowCSMPass::UpdatePolicy p) {
        if (csmPass_) csmPass_->setUpdatePolicy(p);
    }
    int Renderer::getCSMCascadeBudget() const {
        return csmPass_ ? csmPass_->cascadeUpdateBudget() : 0;
    }
    void Renderer::setCSMCascadeBudget(int n) {
        if (csmPass_) csmPass_->setCascadeUpdateBudget(n);
    }
    int Renderer::getCSMDynamicIntervalCap() const {
        return csmPass_ ? csmPass_->dynamicIntervalCap() : 1;
    }
    void Renderer::setCSMDynamicIntervalCap(int frames) {
        if (csmPass_) csmPass_->setDynamicIntervalCap(frames);
    }
    void Renderer::getCSMEpsilons(float& posMeters, float& angDegrees) const {
        if (csmPass_) csmPass_->getEpsilons(posMeters, angDegrees);
        else { posMeters = 0.05f; angDegrees = 0.5f; }
    }
    void Renderer::setCSMEpsilons(float posMeters, float angDegrees) {
        if (csmPass_) csmPass_->setEpsilons(posMeters, angDegrees);
    }
    float Renderer::getCSMSlopeDepthBias() const {
        return csmPass_ ? csmPass_->slopeDepthBias() : 2.0f;
    }
    void Renderer::setCSMSlopeDepthBias(float v) {
        if (csmPass_) csmPass_->setSlopeDepthBias(v);
    }
    float Renderer::getCSMConstantDepthBias() const {
        return csmPass_ ? csmPass_->constantDepthBias() : 4.0f;
    }
    void Renderer::setCSMConstantDepthBias(float v) {
        if (csmPass_) csmPass_->setConstantDepthBias(v);
    }
    void Renderer::setCSMCullFrontFaces(bool on) {
        if (csmPass_) csmPass_->setCullFrontFaces(on);
    }
    bool Renderer::getCSMCullFrontFaces() const {
        return csmPass_ ? csmPass_->cullFrontFaces() : true;
    }
    void Renderer::forceCSMUpdate() {
        if (csmPass_) csmPass_->forceUpdate();
    }
    const CSMSnapshot & Renderer::getCSMSnapshot() const {
        return csmPass_ ? csmPass_->snapshot() : nullSnap_;
    }

    // --- Editor: directional light rotation control ---
    void Renderer::setUseSunYawPitch(bool e) { useSunYawPitch_ = e; }
    bool Renderer::getUseSunYawPitch() const { return useSunYawPitch_; }
    void Renderer::setSunYawPitchDegrees(float yawDeg, float pitchDeg) {
        sunYawDeg_ = yawDeg; sunPitchDeg_ = pitchDeg;
    }
    void Renderer::getSunYawPitchDegrees(float& yawDeg, float& pitchDeg) const {
        yawDeg = sunYawDeg_; pitchDeg = sunPitchDeg_;
    }
} // namespace MyCoreEngine
