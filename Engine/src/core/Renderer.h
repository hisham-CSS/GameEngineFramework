#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Core.h"

#include "Model.h"
#include "Shader.h"
#include "Window.h"

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
            // Set callbacks – use lambdas or static member functions that retrieve the Renderer pointer
            GLFWwindow* glfwWindow = window_.getGLFWwindow();
            glfwSetFramebufferSizeCallback(glfwWindow, framebufferSizeCallback);
            glfwSetCursorPosCallback(glfwWindow, mouseCallback);
            glfwSetScrollCallback(glfwWindow, scrollCallback);
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
                throw std::runtime_error("Failed to initialize GLAD");

            glViewport(0, 0, width, height);
            glEnable(GL_DEPTH_TEST);

            // Associate this Renderer instance with the GLFW window
            glfwSetWindowUserPointer(glfwWindow, this);
        }

        void run() {
            // Load resources once during initialization:
            stbi_set_flip_vertically_on_load(true);
            Shader shader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
            Model loadedModel("Exported/Model/backpack.obj");

            // Setup scene (consider moving this to a Scene or EntityManager class)
            Entity rootEntity(loadedModel);
            rootEntity.transform.setLocalPosition({ 0.f, 0.f, 0.f });
            rootEntity.transform.setLocalScale({ 1.f, 1.f, 1.f });
            for (unsigned int x = 0; x < 20; ++x) {
                for (unsigned int z = 0; z < 20; ++z) {
                    rootEntity.addChild(loadedModel);
                    Entity* child = rootEntity.children.back().get();
                    child->transform.setLocalPosition({ x * 10.f - 100.f, 0.f, z * 10.f - 100.f });
                }
            }
            rootEntity.updateSelfAndChild();

            // Main render loop
            while (!window_.shouldClose()) {
                updateDeltaTime();
                processInput();

                // Clear and draw
                glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                shader.use();
                glm::mat4 projection = glm::perspective(glm::radians(camera_.Zoom),
                    (float)window_.getWidth() / window_.getHeight(),
                    0.1f, 100.0f);
                glm::mat4 view = camera_.GetViewMatrix();
                shader.setMat4("projection", projection);
                shader.setMat4("view", view);

                // Draw the scene graph (frustum culling, etc. as needed)
                unsigned int total = 0, display = 0;
                const Frustum camFrustum = createFrustumFromCamera(camera_,
                    (float)window_.getWidth() / window_.getHeight(),
                    glm::radians(camera_.Zoom), 0.1f, 100.0f);
                rootEntity.drawSelfAndChild(camFrustum, shader, display, total);
                std::cout << "CPU Processed: " << total << " / GPU Draw Calls: " << display << std::endl;

                // Update scene if needed
                rootEntity.updateSelfAndChild();

                window_.swapBuffers();
                window_.pollEvents();
            }
        }

    private:
        Window window_;
        Camera camera_;
        // Optionally, if you have a secondary camera:
        //Camera cameraSpy_{ glm::vec3(0.0f, 10.0f, 0.0f) };

        float lastX_, lastY_;
        bool firstMouse_;
        float deltaTime_, lastFrame_;

        // Update deltaTime based on glfwGetTime
        void updateDeltaTime() {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime_ = currentFrame - lastFrame_;
            lastFrame_ = currentFrame;
        }

        // Process input inside the Renderer (could be moved to a separate InputManager)
        void processInput() {
            GLFWwindow* glfwWindow = window_.getGLFWwindow();
            if (glfwGetKey(glfwWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(glfwWindow, true);
            if (glfwGetKey(glfwWindow, GLFW_KEY_W) == GLFW_PRESS)
                camera_.ProcessKeyboard(FORWARD, deltaTime_);
            if (glfwGetKey(glfwWindow, GLFW_KEY_S) == GLFW_PRESS)
                camera_.ProcessKeyboard(BACKWARD, deltaTime_);
            if (glfwGetKey(glfwWindow, GLFW_KEY_A) == GLFW_PRESS)
                camera_.ProcessKeyboard(LEFT, deltaTime_);
            if (glfwGetKey(glfwWindow, GLFW_KEY_D) == GLFW_PRESS)
                camera_.ProcessKeyboard(RIGHT, deltaTime_);
        }

        // --- Static Callback Functions ---
        // These retrieve the Renderer instance via the GLFW user pointer

        static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
            glViewport(0, 0, width, height);
        }

        static void mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
            Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
            if (!renderer)
                return;

            float xpos = static_cast<float>(xposIn);
            float ypos = static_cast<float>(yposIn);

            if (renderer->firstMouse_) {
                renderer->lastX_ = xpos;
                renderer->lastY_ = ypos;
                renderer->firstMouse_ = false;
            }
            float xoffset = xpos - renderer->lastX_;
            float yoffset = renderer->lastY_ - ypos; // reversed since y-coordinates go from bottom to top

            renderer->lastX_ = xpos;
            renderer->lastY_ = ypos;

            renderer->camera_.ProcessMouseMovement(xoffset, yoffset);
        }

        static void scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
            Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
            if (renderer) {
                renderer->camera_.ProcessMouseScroll(static_cast<float>(yoffset));
            }
        }
    };
};