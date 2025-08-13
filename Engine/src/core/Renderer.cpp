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
        loadPendingModels_();   // safe now
        glfwSetWindowUserPointer(window_.getGLFWwindow(), this);
        glfwSetScrollCallback(window_.getGLFWwindow(), &Renderer::ScrollThunk_);
        glfwSetFramebufferSizeCallback(window_.getGLFWwindow(), &Renderer::FramebufferSizeThunk_);
    }

    void Renderer::loadPendingModels_() {
        for (const auto& path : pendingModels_) {
            models_.emplace_back(std::make_unique<Model>(path));
        }
        pendingModels_.clear();
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

            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);

            // Render your ECS/scene content
            unsigned int total = 0, display = 0;
            const Frustum camFrustum = createFrustumFromCamera(camera_,
                (float)window_.getWidth() / window_.getHeight(),
                glm::radians(camera_.Zoom), 0.1f, 1000.0f);

            scene.RenderScene(camFrustum, shader, camera_, display, total);

            // Render any models owned by the renderer (optional convenience)
            for (auto& m : models_) {
                m->Draw(shader);
            }

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

} // namespace MyCoreEngine
