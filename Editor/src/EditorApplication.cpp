#include <iostream>
#include <imgui.h>
#include "Engine.h"

class EditorApplication : public MyCoreEngine::Application {
public:
    EditorApplication() : m_Scene(new MyCoreEngine::Scene()) {}
    ~EditorApplication() {
        delete m_Scene;
    }

    void EditorApplication::Initialize() {
        if (!MyCoreEngine::Window::Init()) {
            std::cerr << "Failed to initialize GLFW\n";
            return;
        }

        // Set OpenGL context version before window creation
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        #ifdef __APPLE__
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        #endif

        window = MyCoreEngine::Window::CreateWindow(1280, 720, "Editor");
        if (!window) {
            MyCoreEngine::Window::Terminate();
            std::cerr << "Failed to create window\n";
            return;
        }

        MyCoreEngine::Window::MakeContextCurrent(window);

        // Enable VSync
        glfwSwapInterval(1);

        // Initialize Renderer
        if (!MyCoreEngine::Renderer::Init(window)) {
            std::cerr << "Failed to initialize Renderer\n";
            return;
        }

        // Set default clear color
        MyCoreEngine::Renderer::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });

        // Initialize ImGui after renderer
        if (!MyCoreEngine::GuiLayer::Init(window)) {
            std::cerr << "Failed to initialize ImGui\n";
            return;
        }

        // Create a test entity
        auto entity = m_Scene->CreateEntity();
    }

    void Run() override {
        while (!MyCoreEngine::Window::WindowShouldClose(window)) {
            MyCoreEngine::Renderer::Clear();
            MyCoreEngine::Window::SwapBuffers(window);
            MyCoreEngine::Window::PollEvents();
        }
    }

    void Cleanup() {
        MyCoreEngine::GuiLayer::Shutdown();
        MyCoreEngine::Window::DestroyWindow(window);
        MyCoreEngine::Window::Terminate();
    }

private:
    //void RenderScene() {
    //    // Iterate through entities with Transform and MeshComponent
    //    auto view = m_Scene->GetRegistry().view<MyCoreEngine::Transform, MyCoreEngine::MeshComponent>();
    //    for (auto entity : view) {
    //        const auto& [transform, mesh] = view.get<MyCoreEngine::Transform, MyCoreEngine::MeshComponent>(entity);
    //        // Render mesh with transform
    //    }
    //}

    GLFWwindow* window;
    MyCoreEngine::Scene* m_Scene;
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication() {
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}
