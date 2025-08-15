#include <glad/glad.h>

#include "Renderer.h"
#include "Window.h"
#include <stdexcept>


#include <glm/gtc/matrix_transform.hpp>

namespace MyCoreEngine {

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
        //glEnable(GL_FRAMEBUFFER_SRGB); // linear -> sRGB on writes to default framebuffer

        // Shadow map setup
        glGenFramebuffers(1, &shadowFBO_);
        glGenTextures(1, &shadowDepthTex_);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0,
            GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTex_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        // --- CSM depth textures ---
        for (int i = 0; i < kCascadeCount_; ++i) {
            glGenFramebuffers(1, &csmFBO_[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, csmFBO_[i]);
            glGenTextures(1, &csmDepth_[i]);
            glBindTexture(GL_TEXTURE_2D, csmDepth_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, csmRes_, csmRes_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, csmDepth_[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

            // --- Step 2: build light view-projection (directional) ---
            // Center the light frustum near the camera (simple version)
            glm::vec3 center = camera_.Position;
            glm::vec3 lightPos = center - sunDir_ * 150.0f;
            glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0, 1, 0));
            glm::mat4 lightProj = glm::ortho(-sunOrthoHalf_, sunOrthoHalf_,
            -sunOrthoHalf_, sunOrthoHalf_,
            sunNear_, sunFar_);
            lightViewProj_ = lightProj * lightView;
            
            // --- CSM shadow pass (replaces single shadow pass) ---
            if (!shadowDepthShader_) {
                shadowDepthShader_ = std::make_unique<Shader>(
                    "Exported/Shaders/shadow_depth_vert.glsl",
                    "Exported/Shaders/shadow_depth_frag.glsl");
            }
            const glm::mat4 camProj = glm::perspective(glm::radians(camera_.Zoom),
                window_.getAspectRatio(),
                0.1f, 1000.0f);
            const glm::mat4 camView = camera_.GetViewMatrix();

            glm::mat4 camVP = camProj * camView;
            if (needShadowUpdate_(camVP, sunDir_) && (frameIndex_++ % shadowUpdateRate_) == 0) {
                renderCSMShadowPass_(camView, camProj, scene);
                lastCamViewProj_ = camVP;
                lastSunDir_ = sunDir_;
            }
            
            int w, h;
            window_.getFramebufferSize(w, h);
            glViewport(0, 0, w, h);   // <-- restore main viewport
            // 1) Render scene into HDR FBO
            glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Restore main viewport
            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);
            // cascades: matrices + distances
            shader.setMat4("uLightVP[0]", csmLightVP_[0]);
            shader.setMat4("uLightVP[1]", csmLightVP_[1]);
            shader.setMat4("uLightVP[2]", csmLightVP_[2]);
            shader.setFloat("uCSMSplits[0]", csmSplits_[0]);
            shader.setFloat("uCSMSplits[1]", csmSplits_[1]);
            shader.setFloat("uCSMSplits[2]", csmSplits_[2]);

            // bind cascade depth maps to 8/9/10
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, csmDepth_[0]);
            shader.setInt("uShadowCascade[0]", 8);

            glActiveTexture(GL_TEXTURE9);
            glBindTexture(GL_TEXTURE_2D, csmDepth_[1]);
            shader.setInt("uShadowCascade[1]", 9);

            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, csmDepth_[2]);
            shader.setInt("uShadowCascade[2]", 10);

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

            // --- Step 4: bind shadow map for the main pass ---
            //glActiveTexture(GL_TEXTURE8);
            //glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
            //shader.setInt("uShadowMap", 8);

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

    void Renderer::computeCSMSplits_(float camNear, float camFar) {
        // Practical split scheme
        for (int i = 0; i < kCascadeCount_; ++i) {
            float si = float(i + 1) / float(kCascadeCount_);
            float logSplit = camNear * std::pow(camFar / camNear, si);
            float uniSplit = camNear + (camFar - camNear) * si;
            csmSplits_[i] = glm::mix(uniSplit, logSplit, csmLambda_);
        }
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

    void Renderer::computeCSMMatrix_(int idx,
        const glm::mat4& camView,
        const glm::mat4& camProj,
        float splitNear, float splitFar,
        const glm::vec3& sunDir,
        glm::mat4& outLightVP)
    {
        // Build a projection for the slice and invert it to get the 8 view-space corners.
        glm::mat4 sliceProj = glm::perspectiveFovRH_NO(
            /*fovY*/ 2.f * atan(1.f / camProj[1][1]), float(window_.getWidth()), float(window_.getHeight()), splitNear, splitFar);
        glm::mat4 invSliceProj = glm::inverse(sliceProj);

        std::array<glm::vec3, 8> corners;
        // corners in VIEW space
        int k = 0;
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                {
                    glm::vec4 ndc = glm::vec4(x ? 1.f : -1.f, y ? 1.f : -1.f, z ? 1.f : -1.f, 1.f);
                    glm::vec4 v = invSliceProj * ndc;
                    v /= v.w;
                    corners[k++] = glm::vec3(v);
                }

        // Transform to WORLD
        glm::mat4 invView = glm::inverse(camView);
        for (auto& c : corners) c = glm::vec3(invView * glm::vec4(c, 1.0));

        // Light view matrix from sun direction (position far along direction).
        glm::vec3 center(0);
        for (auto& c : corners) center += c; center *= (1.f / 8.f);
        glm::vec3 lightPos = center - sunDir * 200.f; // pull back “sun”
        glm::mat4 lightView = glm::lookAtRH(lightPos, center, glm::vec3(0, 1, 0));

        // Find AABB of slice in light space
        glm::vec3 minB(1e9f), maxB(-1e9f);
        for (auto& c : corners) {
            glm::vec3 lc = glm::vec3(lightView * glm::vec4(c, 1.0));
            minB = glm::min(minB, lc);
            maxB = glm::max(maxB, lc);
        }

        // Optional pad to reduce shimmering
        const float pad = 5.f;
        minB -= pad; maxB += pad;

        glm::mat4 lightProj = glm::orthoRH_NO(minB.x, maxB.x, minB.y, maxB.y, -maxB.z - 500.f, -minB.z + 500.f);
        outLightVP = lightProj * lightView;
    }

    // --- Renders all cascades into csmDepth_[i] using the existing depth-only path.
    void Renderer::renderCSMShadowPass_(const glm::mat4& camView, const glm::mat4& camProj, Scene &scene)
    {
        // 1) compute split distances (view-space)
        // NOTE: near/far must match your camera’s real values used to build camProj
        const float camNear = 0.1f;      // keep in sync with your perspective() in run()
        const float camFar = 1000.0f;
        computeCSMSplits_(camNear, camFar);

        // 2) per-cascade matrix and render
        for (int i = 0; i < kCascadeCount_; ++i) {
            float splitNear = (i == 0) ? camNear : csmSplits_[i - 1];
            float splitFar = csmSplits_[i];

            computeCSMMatrix_(i, camView, camProj, splitNear, splitFar, sunDir_, csmLightVP_[i]);

            glViewport(0, 0, csmRes_, csmRes_);
            glBindFramebuffer(GL_FRAMEBUFFER, csmFBO_[i]);
            glClear(GL_DEPTH_BUFFER_BIT);
            glCullFace(GL_FRONT); // optional to reduce peter-panning
            scene.RenderShadowDepth(*shadowDepthShader_, csmLightVP_[i]); // same call you already use
            glCullFace(GL_BACK);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
} // namespace MyCoreEngine
