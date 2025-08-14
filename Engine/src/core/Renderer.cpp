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
        glEnable(GL_FRAMEBUFFER_SRGB); // linear -> sRGB on writes to default framebuffer

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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
            
            // Lazy-create the shadow depth shader after GL is ready
            if (!shadowDepthShader_) {
                shadowDepthShader_ = std::make_unique<Shader>(
                "shadow_depth_vert.glsl",
                "shadow_depth_frag.glsl");
            }
            // --- Shadow depth pass ---
            glViewport(0, 0, shadowSize_, shadowSize_);
            glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO_);
            glClear(GL_DEPTH_BUFFER_BIT);
            glCullFace(GL_FRONT); // optional: reduce peter-panning
            scene.RenderShadowDepth(*shadowDepthShader_, lightViewProj_);
            glCullFace(GL_BACK);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            
            int w, h;
            window_.getFramebufferSize(w, h);
            glViewport(0, 0, w, h);   // <-- restore main viewport

            // Restore main viewport
            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);
            shader.setMat4("uLightVP", lightViewProj_); // Step 4: provide to main shader

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
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
            shader.setInt("uShadowMap", 8);

            // Render your ECS/scene content
            const Frustum camFrustum = createFrustumFromCamera(camera_,
                (float)window_.getWidth() / window_.getHeight(),
                glm::radians(camera_.Zoom), 0.1f, 1000.0f);

            scene.RenderScene(camFrustum, shader, camera_);

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
    }

    void Renderer::SetIBLTextures(unsigned int irr, unsigned int pre, unsigned int lut, float mipCount) {
        iblIrradiance_ = irr;
        iblPrefiltered_ = pre;
        iblBRDFLUT_ = lut;
        iblPrefilterMipCount_ = mipCount;
    }

} // namespace MyCoreEngine
