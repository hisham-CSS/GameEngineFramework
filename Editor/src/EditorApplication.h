#include "Engine.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

    void HandleShaderError(unsigned int shader);

    void Cleanup();

    void ProcessInput(GLFWwindow* window);

private:
    GLFWwindow* window;

    const char* vertexShaderSource = "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "void main()\n"
        "{\n"
        "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
        "}\0";

    const char* fragmentShaderSource = "#version 330 core\n"
        "out vec4 FragColor;\n"
        "void main()\n"
        "{\n"
        "FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
        "}\0";
};