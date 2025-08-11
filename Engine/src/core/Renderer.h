#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Core.h"

#include "Model.h"
#include "Shader.h"
#include "Window.h"
#include "Camera.h"
#include "Entity.h"
#include "Scene.h"
#include "Components.h"
#include "InputSystem.h"
#include "Event.h"
#include "EventBus.h"


namespace MyCoreEngine
{
    class ENGINE_API Renderer {
    public:
        Renderer(int width, int height)
            : window_(width, height, "Editor"),
            camera_(glm::vec3(0.0f, 10.0f, 0.0f)),
            lastX_(width / 2.0f),
            lastY_(height / 2.0f),
            firstMouse_(true),
            deltaTime_(0.0f),
            lastFrame_(0.0f)
        {
            GLFWwindow* glfwWindow = window_.getGLFWwindow();

            // 1) Associate Renderer* with the window BEFORE any callbacks are set
            glfwSetWindowUserPointer(glfwWindow, this);

            // 2) Now it’s safe to register callbacks
            glfwSetFramebufferSizeCallback(glfwWindow, framebufferSizeCallback);
            glfwSetCursorPosCallback(glfwWindow, mouseCallback);
            glfwSetScrollCallback(glfwWindow, scrollCallback);

            // 3) Wire input system after we actually have a window
            inputSystem_.setWindow(glfwWindow);

            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
                throw std::runtime_error("Failed to initialize GLAD");

            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);
        }

        void run(Scene& scene, Shader& shader) {
            // Main render loop
            while (!window_.shouldClose()) {
                updateDeltaTime();

                // Centralised input handling
                inputSystem_.update(camera_, deltaTime_);

                // Update transforms (this could be a system that iterates over all Transform instances)
                scene.UpdateTransforms();

                // Clear and draw
                glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                //TODO: Make the shader also consider a skybox setting and a PBR sort of
                //starting point
                shader.use();
                glm::mat4 projection = glm::perspective(glm::radians(camera_.Zoom),
                    (float)window_.getWidth() / window_.getHeight(),
                    0.1f, 1000.0f);
                glm::mat4 view = camera_.GetViewMatrix();
                shader.setMat4("projection", projection);
                shader.setMat4("view", view);

                unsigned int total = 0, display = 0;
                const Frustum camFrustum = createFrustumFromCamera(camera_,
                    (float)window_.getWidth() / window_.getHeight(),
                    glm::radians(camera_.Zoom), 0.1f, 1000.0f);

                scene.RenderScene(camFrustum, shader, display, total);

                std::cout << "CPU Processed: " << total << " / GPU Draw Calls: " << display << std::endl;

                window_.swapBuffers();
                window_.pollEvents();
            }
        }

    private:
        Window window_;

        //our inital camera
        Camera camera_;
        // Optionally, if you have a secondary camera:
        //Camera cameraSpy_{ glm::vec3(0.0f, 10.0f, 0.0f) };

        // Input system handles keyboard/mouse polling and updates the camera
        InputSystem inputSystem_;

        //mouse callback variables
        float lastX_, lastY_;
        bool firstMouse_;

        //time and frame information
        float deltaTime_, lastFrame_;

        // Update deltaTime based on glfwGetTime
        void updateDeltaTime() {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime_ = currentFrame - lastFrame_;
            lastFrame_ = currentFrame;
        }

        // --- Static Callback Functions ---
        // These retrieve the Renderer instance via the GLFW user pointer

        static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
            glViewport(0, 0, width, height);
            MyCoreEngine::EventBus::Get().publish(MyCoreEngine::WindowResizeEvent{ width, height });
        }

        static void mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
            Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
            if (!renderer) return;

            float xpos = static_cast<float>(xposIn);
            float ypos = static_cast<float>(yposIn);

            if (renderer->firstMouse_) {
                renderer->lastX_ = xpos;
                renderer->lastY_ = ypos;
                renderer->firstMouse_ = false;
            }
            float xoffset = xpos - renderer->lastX_;
            float yoffset = renderer->lastY_ - ypos;

            renderer->lastX_ = xpos;
            renderer->lastY_ = ypos;

            // Existing behaviour
            renderer->camera_.ProcessMouseMovement(xoffset, yoffset);

            // New: publish absolute coordinates to the bus
            MyCoreEngine::EventBus::Get().publish(MyCoreEngine::MouseMoveEvent{ xpos, ypos });
        }

        static void scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
            Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
            if (renderer) {
                renderer->camera_.ProcessMouseScroll(static_cast<float>(yoffset));
            }
            MyCoreEngine::EventBus::Get().publish(MyCoreEngine::MouseScrollEvent{ static_cast<float>(yoffset) });
        }

    };
};
