#pragma once
#include <GLFW/glfw3.h>

struct ImGuiContext;

class EditorImGuiLayer {
public:
    ~EditorImGuiLayer() { Shutdown(); }
    void Init(GLFWwindow* window);  // create context + backends
    void BeginFrame();
    void EndFrame();                // Render draw data
    void Shutdown();

    bool WantCaptureKeyboard() const;
    bool WantCaptureMouse() const;

private:
    bool initialized_ = false;
};
