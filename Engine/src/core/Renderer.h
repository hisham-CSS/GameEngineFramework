#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "Model.h"
#include "Shader.h"

// camera
Camera camera(glm::vec3(0.0f, 10.0f, 0.0f));
Camera cameraSpy(glm::vec3(0.0f, 10.0f, 0.f));

// settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// camera
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

//due to mismatching function signatures - these functions have to be moved to the global scope of this file and live outside of the EditorApplication class
void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

class Renderer
{
public:
    void Init() {
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Editor", NULL, NULL);
        if (!window) {
            std::cout << "Failed to create GLFW Window" << std::endl;
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(window);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetCursorPosCallback(window, mouse_callback);
        glfwSetScrollCallback(window, scroll_callback);

        // tell GLFW to capture our mouse
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return;
        }

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    };
    void Update() {
        if (!window) return;

        // tell stb_image.h to flip loaded texture's on the y-axis (before loading model).
        stbi_set_flip_vertically_on_load(true);

        // configure global opengl state
        // -----------------------------
        glEnable(GL_DEPTH_TEST);

        camera.MovementSpeed = 20.f;

        // build and compile our shader zprogram
        // ------------------------------------
        Shader shader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");

        //load models
        // -----------
        Model loadedModel("Exported/Model/backpack.obj");

        Entity ourEntity(loadedModel);
        ourEntity.transform.setLocalPosition({ 0, 0, 0 });
        const float scale = 1.0;
        ourEntity.transform.setLocalScale({ scale, scale, scale });

        Entity* lastEntity = &ourEntity;
        for (unsigned int x = 0; x < 20; ++x)
        {
            for (unsigned int z = 0; z < 20; ++z)
            {
                ourEntity.addChild(loadedModel);
                lastEntity = ourEntity.children.back().get();

                //Set transform values
                lastEntity->transform.setLocalPosition({ x * 10.f - 100.f,  0.f, z * 10.f - 100.f });
            }
        }
        
        ourEntity.updateSelfAndChild();

        while (!glfwWindowShouldClose(window)) {

            // per-frame time logic
            // --------------------
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            //processing input
            ProcessInput(window);

            // render
            // ------
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // also clear the depth buffer now!

            // be sure to activate shader when setting uniforms/drawing objects
            shader.use();

            // view/projection transformations
            glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
            const Frustum camFrustum = createFrustumFromCamera(camera, (float)SCR_WIDTH / (float)SCR_HEIGHT, glm::radians(camera.Zoom), 0.1f, 100.0f);

            cameraSpy.ProcessMouseMovement(2, 0);

            glm::mat4 view = camera.GetViewMatrix();

            shader.setMat4("projection", projection);
            shader.setMat4("view", view);

            // draw our scene graph
            unsigned int total = 0, display = 0;
            ourEntity.drawSelfAndChild(camFrustum, shader, display, total);
            std::cout << "Total process in CPU : " << total << " / Total send to GPU : " << display << std::endl;

            //ourEntity.transform.setLocalRotation({ 0.f, ourEntity.transform.getLocalRotation().y + 20 * deltaTime, 0.f });
            ourEntity.updateSelfAndChild();

            // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
            // -------------------------------------------------------------------------------
            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        glfwTerminate();
    };

private:
    void HandleShaderError(unsigned int shader) {
        int  success;
        char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
        }
    };

    void Cleanup() {
        glfwDestroyWindow(window);
        glfwTerminate();
    };

    void ProcessInput(GLFWwindow* window) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
    };

    GLFWwindow* window;
};