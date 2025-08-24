#include <glad/glad.h>

#include "Renderer.h"
#include "Window.h"
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

    Renderer::Renderer(int width, int height, const char* title)
        : window_(width, height, title),
        input_(window_.getGLFWwindow())
    {
        // GLFW window is created here and context is current on this thread,
        // but GLAD is not loaded yet. Do not create GL resources here.
    }

    void Renderer::updateDeltaTime_() {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime_ = currentFrame - lastFrame_;
        lastFrame_ = currentFrame;
    }

    void Renderer::setupGL_() {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            throw std::runtime_error("Failed to initialize GLAD");
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

		// HDR textures (IBL)
        // --- HDR FBO ---
        int fbw, fbh;
        window_.getFramebufferSize(fbw, fbh);

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

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            // log: HDR FBO incomplete
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

        // Tonemap shader (lazy create is also fine; doing it here keeps run() clean)
        tonemapShader_ = std::make_unique<Shader>("Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/tonemap_frag.glsl");

        int w, h;
        window_.getFramebufferSize(w, h);
        glViewport(0, 0, w, h);

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
            csmPass_->setCascadeUpdateBudget(1);
            // seed defaults so Editor sliders reflect reality
            csmPass_->setNumCascades(4);
            csmPass_->setLambda(0.7f);
            csmPass_->setEpsilons(0.05f /*meters*/, 0.5f /*degrees*/);
            csmPass_->setEnabled(true);
            // base resolution already set from ctor via csmRes_
        }

        if (!readyFired_ && onReady_) {
            onReady_();          // Editor initializes ImGui and creates GL objects here
            readyFired_ = true;
        }
    }

    void Renderer::InitGL() {
        setupGL_();
        glfwSetWindowUserPointer(window_.getGLFWwindow(), this);
        glfwSetScrollCallback(window_.getGLFWwindow(), &Renderer::ScrollThunk_);
        glfwSetFramebufferSizeCallback(window_.getGLFWwindow(), &Renderer::FramebufferSizeThunk_);
    }

    void Renderer::run(Scene& scene, Shader& shader) {
        // GL is expected to be initialized already via InitGL()

        while (!window_.shouldClose()) {
            updateDeltaTime_();

            bool capK = false, capM = false;
            if (captureFn_) {
                auto caps = captureFn_();
                capK = caps.first; capM = caps.second;
            }
            if (!capK) {
                input_.update(camera_, deltaTime_);
            }
            if (!capM) handleMouseLook_(capM);

            // Update scene data (transforms etc.)
            scene.UpdateTransforms();

            // Clear
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Basic camera matrices
            glm::mat4 projection = glm::perspective(glm::radians(camera_.Zoom),
                window_.getAspectRatio(),
                0.1f, 1000.0f);
            glm::mat4 view = camera_.GetViewMatrix();

            FrameParams fp;
            fp.view = view;
            fp.proj = projection;
            fp.deltaTime = deltaTime_;
            fp.frameIndex = ++frameIndex_;
            int w, h; window_.getFramebufferSize(w, h);
            fp.viewportW = w; fp.viewportH = h;

            if (useSunYawPitch_) {
                sunDir_ = dirFromYawPitch(sunYawDeg_, sunPitchDeg_);
            }

            // keep passCtx_ in sync with per-frame globals you already tweak
            passCtx_.sunDir = sunDir_;
            passCtx_.exposure = exposure_;

            pipeline_.executeAll(passCtx_, scene, camera_, fp);


            //window_.getFramebufferSize(w, h);
            glViewport(0, 0, w, h);   // <-- restore main viewport
            // 1) Render scene into HDR FBO
            glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Restore main viewport
            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);
			shader.setInt("uShadowsOn", passCtx_.csm.enabled ? 1 : 0);

            shader.setFloat("uSplitBlend", splitBlend_);

            shader.setInt("uCSMDebug", csmDebugMode_);
            
            // ---- CSM uniforms & bindings (dynamic count) ----
            const int csmCount = passCtx_.csm.cascades;      // 1..4
            const int baseUnit = 8;                          // reserve 8..11
            // (Optional if your shader exposes it; safe to set even if unused)
            shader.setInt("uCascadeCount", csmCount);
            shader.setFloat("uCamNear", 0.1f);
            shader.setFloat("uCamFar", getCSMMaxShadowDistance()); // or cached value
            
            // matrices, split distances, texel size per cascade
            for (int i = 0; i < csmCount; ++i) {
            // uLightVP[i]
                {
                    char name[32];
                    snprintf(name, sizeof(name), "uLightVP[%d]", i);
                    shader.setMat4(name, passCtx_.csm.lightVP[i]);
                }
                // uCSMSplits[i]
                {
                    char name[32];
                    snprintf(name, sizeof(name), "uCSMSplits[%d]", i);
                    shader.setFloat(name, passCtx_.csm.splitFar[i]);
                }
                // uCascadeTexel[i] = 1/res
                {
                    char name[32];
                    snprintf(name, sizeof(name), "uCascadeTexel[%d]", i);
                    shader.setFloat(name, 1.0f / float(passCtx_.csm.resPer[i] > 0 ? passCtx_.csm.resPer[i] : 1));
                }   
            }
            
            // bind N shadow maps to texture units baseUnit..baseUnit+N-1
            for (int i = 0; i < csmCount; ++i) {
                const int unit = baseUnit + i;
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, passCtx_.csm.depthTex[i]);
                char name[32];
                snprintf(name, sizeof(name), "uShadowCascade[%d]", i);
                shader.setInt(name, unit);
            }
            
            //// cascades: matrices + distances
            //shader.setInt("uCSMDebug", csmDebugMode_);

            //shader.setMat4("uLightVP[0]", passCtx_.csm.lightVP[0]);
            //shader.setMat4("uLightVP[1]", passCtx_.csm.lightVP[1]);
            //shader.setMat4("uLightVP[2]", passCtx_.csm.lightVP[2]);
            //shader.setMat4("uLightVP[3]", passCtx_.csm.lightVP[3]);

            //shader.setFloat("uCSMSplits[0]", passCtx_.csm.splitFar[0]);
            //shader.setFloat("uCSMSplits[1]", passCtx_.csm.splitFar[1]);
            //shader.setFloat("uCSMSplits[2]", passCtx_.csm.splitFar[2]);
            //shader.setFloat("uCSMSplits[3]", passCtx_.csm.splitFar[3]);

            //shader.setFloat("uCascadeTexel[0]", 1.0f / float(passCtx_.csm.resPer[0]));
            //shader.setFloat("uCascadeTexel[1]", 1.0f / float(passCtx_.csm.resPer[1]));
            //shader.setFloat("uCascadeTexel[2]", 1.0f / float(passCtx_.csm.resPer[2]));
            //shader.setFloat("uCascadeTexel[3]", 1.0f / float(passCtx_.csm.resPer[3]));


            //// bind cascade depth maps to 8/9/10
            //glActiveTexture(GL_TEXTURE8);  glBindTexture(GL_TEXTURE_2D, passCtx_.csm.depthTex[0]); shader.setInt("uShadowCascade[0]", 8);
            //glActiveTexture(GL_TEXTURE9);  glBindTexture(GL_TEXTURE_2D, passCtx_.csm.depthTex[1]); shader.setInt("uShadowCascade[1]", 9);
            //glActiveTexture(GL_TEXTURE10); glBindTexture(GL_TEXTURE_2D, passCtx_.csm.depthTex[2]); shader.setInt("uShadowCascade[2]", 10);

            // Bind IBL only if provided; these are global, not per-mesh
            if (iblIrradiance_ && iblPrefiltered_ && iblBRDFLUT_) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_CUBE_MAP, iblIrradiance_);
                shader.setInt("irradianceMap", 5);

                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_CUBE_MAP, iblPrefiltered_);
                shader.setInt("prefilteredMap", 6);

                glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D, iblBRDFLUT_);
                shader.setInt("brdfLUT", 7);

                shader.setFloat("uPrefilterMipCount", iblPrefilterMipCount_);
            }
            else {
                // Safe defaults: shader will ignore IBL if uUseIBL == 0
                shader.setFloat("uPrefilterMipCount", 0.0f);
            }


            // Render your ECS/scene content
            const Frustum camFrustum = createFrustumFromCamera(camera_,
                (float)window_.getWidth() / window_.getHeight(),
                glm::radians(camera_.Zoom), 0.1f, 1000.0f);

            scene.RenderScene(camFrustum, shader, camera_);

            // Tonemap to default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, w, h);
            glDisable(GL_DEPTH_TEST); // fullscreen pass

            tonemapShader_->use();
            tonemapShader_->setFloat("uExposure", exposure_);
            // If you want to toggle between ACES/Reinhard/None, add a uniform switch too.

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hdrColorTex_);
            tonemapShader_->setInt("uHDRColor", 0);

            glBindVertexArray(fsQuadVAO_);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            glEnable(GL_DEPTH_TEST);

            // Editor UI (after 3D draw)
            if (uiDraw_) uiDraw_(deltaTime_);

            window_.swapBuffers();
            window_.pollEvents();
        }
    }

    void Renderer::handleMouseLook_(bool uiWantsMouse) {
        GLFWwindow* win = window_.getGLFWwindow();
        if (!win) return;

        const int rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);

        if (!uiWantsMouse && rmb == GLFW_PRESS) {
            if (!rotating_) {
                rotating_ = true;
                firstMouse_ = true;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }

            double xpos, ypos;
            glfwGetCursorPos(win, &xpos, &ypos);

            if (firstMouse_) {
                lastX_ = xpos; lastY_ = ypos;
                firstMouse_ = false;
                return;
            }

            // Note: yaw increases with +x, pitch increases with -y
            float xoffset = static_cast<float>(xpos - lastX_);
            float yoffset = static_cast<float>(lastY_ - ypos);

            lastX_ = xpos; lastY_ = ypos;

            camera_.ProcessMouseMovement(xoffset, yoffset, true);
        }
        else {
            if (rotating_) {
                rotating_ = false;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            firstMouse_ = true; // reset when not rotating
        }
    }

    void Renderer::ScrollThunk_(GLFWwindow* w, double /*xoff*/, double yoff) {
        if (auto* self = static_cast<Renderer*>(glfwGetWindowUserPointer(w))) {
            self->onScroll_(yoff);
        }
    }

    void Renderer::onScroll_(double yoff) {
        // If you want to respect UI capture, you can check captureFn_ here
        camera_.ProcessMouseScroll(static_cast<float>(yoff));
    }

    void Renderer::FramebufferSizeThunk_(GLFWwindow * w, int width, int height) {
       if (auto* self = static_cast<Renderer*>(glfwGetWindowUserPointer(w))) {
           self->onFramebufferSize_(width, height);
        }
    }
    void Renderer::onFramebufferSize_(int width, int height) {
        // Prevent GL errors on minimization
        if (width <= 0 || height <= 0) return;
            
        glViewport(0, 0, width, height);
        if (hdrFBO_) recreateHDR_(width, height);
    }

    void Renderer::SetIBLTextures(unsigned int irr, unsigned int pre, unsigned int lut, float mipCount) {
        iblIrradiance_ = irr;
        iblPrefiltered_ = pre;
        iblBRDFLUT_ = lut;
        iblPrefilterMipCount_ = mipCount;
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
        // leave bound check optional (we assume previous setup succeeded)
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

    glm::mat4 Renderer::getCameraPerspectiveMatrix(const Camera& cam)
    {
        return glm::perspective(glm::radians(cam.Zoom),
            window_.getAspectRatio(),
            0.1f, 1000.0f);
    }
    
    // -------- Editor wrapper implementations --------
    bool Renderer::getCSMEnabled() const {
        return csmPass_ ? csmPass_->enabled() : false;
    }
    void Renderer::setCSMEnabled(bool e) {
        if (csmPass_) csmPass_->setEnabled(e);
    }
    float Renderer::getCSMMaxShadowDistance() const {
        return csmPass_ ? csmPass_->maxShadowDistance() : 1000.f;
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
