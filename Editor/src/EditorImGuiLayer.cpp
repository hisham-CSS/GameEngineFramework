#pragma once
#include "EditorImGuiLayer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

void EditorImGuiLayer::Init(GLFWwindow* window) {
    if (initialized_) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // vcpkg imgui[docking-experimental]
    // Multi-viewports: panels dragged outside the main window become real
    // OS windows (any monitor) and can dock together there. Editor camera
    // input must come through ImGui (viewport-aware) — raw polling of the
    // main GLFW window goes dead when a panel has focus elsewhere.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    // Unity-style: windows move/undock only when dragged by their tab or
    // title bar — body drags never yank a docked panel around (essential for
    // gizmo dragging inside the viewport).
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    // detached OS windows shouldn't look translucent/rounded against the desktop
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    initialized_ = true;
}

void EditorImGuiLayer::BeginFrame() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorImGuiLayer::EndFrame() {
    if (!initialized_) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // render/refresh the detached platform windows, then restore the main
    // GL context (the platform pass leaves a secondary context current)
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

void EditorImGuiLayer::Shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool EditorImGuiLayer::WantCaptureKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}
bool EditorImGuiLayer::WantTextInput() const {
    return ImGui::GetIO().WantTextInput;
}
bool EditorImGuiLayer::WantCaptureMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}
