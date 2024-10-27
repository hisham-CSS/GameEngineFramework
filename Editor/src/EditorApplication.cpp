#include <iostream>
#include "Engine.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
	EditorApplication() {}
	~EditorApplication() {}
    
    void Initialize() {
        if (!MyCoreEngine::Window::Init()) {
            // Handle error
        }

        window = MyCoreEngine::Window::CreateWindow(1280, 720, "Editor");
        if (!window) {
            MyCoreEngine::Window::Terminate();
            // Handle error
        }

        MyCoreEngine::Window::MakeContextCurrent(window);
    }

    void Run() override {
        while (!MyCoreEngine::Window::WindowShouldClose(window)) {
            MyCoreEngine::Window::PollEvents();
            // Render
            MyCoreEngine::Window::SwapBuffers(window);
        }
    }

    void Cleanup() {
        MyCoreEngine::Window::DestroyWindow(window);
        MyCoreEngine::Window::Terminate();
    }

private:
    GLFWwindow* window;
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
	EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}
