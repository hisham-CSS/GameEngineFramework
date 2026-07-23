#pragma once
#include <cstdio>
#include <filesystem>
#include <system_error>
#include "EditorImGuiLayer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace {
    // The Cat Splat editor identity: a cool charcoal base with a single warm
    // amber accent. Kept in ONE place so every panel, the custom title bar,
    // and any future tool read from the same palette instead of drifting into
    // stock ImGui blue. Tweak these five and the whole editor re-tints.
    constexpr ImVec4 kBg      = ImVec4(0.102f, 0.114f, 0.137f, 1.00f); // window
    constexpr ImVec4 kPanel   = ImVec4(0.137f, 0.153f, 0.184f, 1.00f); // child/frame
    constexpr ImVec4 kRaised  = ImVec4(0.180f, 0.200f, 0.239f, 1.00f); // header/button
    constexpr ImVec4 kText    = ImVec4(0.860f, 0.880f, 0.910f, 1.00f);
    constexpr ImVec4 kAccent  = ImVec4(0.945f, 0.631f, 0.251f, 1.00f); // warm amber

    ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }
    ImVec4 alpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

    void ApplyEditorTheme(ImGuiStyle& s) {
        // --- metrics: a touch of rounding + breathing room, so it reads as a
        // deliberate tool rather than the stock ImGui debug window. Window
        // rounding stays 0 (multi-viewport draws real OS windows).
        s.WindowRounding = 0.0f;
        s.ChildRounding = 4.0f;
        s.FrameRounding = 4.0f;
        s.PopupRounding = 4.0f;
        s.ScrollbarRounding = 4.0f;
        s.GrabRounding = 4.0f;
        s.TabRounding = 4.0f;
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 0.0f;
        s.PopupBorderSize = 1.0f;
        s.WindowPadding = ImVec2(10, 10);
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);
        s.ItemInnerSpacing = ImVec2(6, 4);
        s.ScrollbarSize = 12.0f;
        s.GrabMinSize = 10.0f;
        s.TabBarBorderSize = 1.0f;
        s.WindowMenuButtonPosition = ImGuiDir_None; // no stock collapse arrow

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text]                 = kText;
        c[ImGuiCol_TextDisabled]         = mix(kText, kBg, 0.55f);
        c[ImGuiCol_WindowBg]             = kBg;
        c[ImGuiCol_ChildBg]              = alpha(kPanel, 0.0f);
        c[ImGuiCol_PopupBg]              = mix(kBg, ImVec4(0, 0, 0, 1), 0.15f);
        c[ImGuiCol_Border]               = mix(kPanel, kText, 0.10f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg]              = kPanel;
        c[ImGuiCol_FrameBgHovered]       = mix(kPanel, kAccent, 0.18f);
        c[ImGuiCol_FrameBgActive]        = mix(kPanel, kAccent, 0.30f);
        c[ImGuiCol_TitleBg]              = mix(kBg, ImVec4(0, 0, 0, 1), 0.25f);
        c[ImGuiCol_TitleBgActive]        = kRaised;
        c[ImGuiCol_TitleBgCollapsed]     = mix(kBg, ImVec4(0, 0, 0, 1), 0.35f);
        c[ImGuiCol_MenuBarBg]            = mix(kBg, ImVec4(0, 0, 0, 1), 0.20f);
        c[ImGuiCol_ScrollbarBg]          = alpha(kBg, 0.0f);
        c[ImGuiCol_ScrollbarGrab]        = kRaised;
        c[ImGuiCol_ScrollbarGrabHovered] = mix(kRaised, kAccent, 0.30f);
        c[ImGuiCol_ScrollbarGrabActive]  = mix(kRaised, kAccent, 0.50f);
        c[ImGuiCol_CheckMark]            = kAccent;
        c[ImGuiCol_SliderGrab]           = kAccent;
        c[ImGuiCol_SliderGrabActive]     = mix(kAccent, kText, 0.25f);
        c[ImGuiCol_Button]               = kRaised;
        c[ImGuiCol_ButtonHovered]        = mix(kRaised, kAccent, 0.35f);
        c[ImGuiCol_ButtonActive]         = mix(kRaised, kAccent, 0.55f);
        c[ImGuiCol_Header]               = mix(kPanel, kAccent, 0.12f);
        c[ImGuiCol_HeaderHovered]        = mix(kPanel, kAccent, 0.28f);
        c[ImGuiCol_HeaderActive]         = mix(kPanel, kAccent, 0.40f);
        c[ImGuiCol_Separator]            = mix(kPanel, kText, 0.10f);
        c[ImGuiCol_SeparatorHovered]     = kAccent;
        c[ImGuiCol_SeparatorActive]      = kAccent;
        c[ImGuiCol_ResizeGrip]           = alpha(kAccent, 0.20f);
        c[ImGuiCol_ResizeGripHovered]    = alpha(kAccent, 0.55f);
        c[ImGuiCol_ResizeGripActive]     = alpha(kAccent, 0.85f);
        c[ImGuiCol_Tab]                  = mix(kBg, kPanel, 0.60f);
        c[ImGuiCol_TabHovered]           = mix(kPanel, kAccent, 0.35f);
        c[ImGuiCol_TabActive]            = kRaised;
        c[ImGuiCol_TabUnfocused]         = mix(kBg, kPanel, 0.40f);
        c[ImGuiCol_TabUnfocusedActive]   = mix(kRaised, kBg, 0.35f);
        c[ImGuiCol_DockingPreview]       = alpha(kAccent, 0.45f);
        c[ImGuiCol_DockingEmptyBg]       = kBg;
        c[ImGuiCol_PlotLines]            = mix(kText, kAccent, 0.4f);
        c[ImGuiCol_PlotHistogram]        = kAccent;
        c[ImGuiCol_TextSelectedBg]       = alpha(kAccent, 0.35f);
        c[ImGuiCol_NavHighlight]         = kAccent;
        c[ImGuiCol_DragDropTarget]       = kAccent;
    }
} // namespace

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
    ImGui::StyleColorsDark(); // base, then overlaid with the Cat Splat identity
    ImGuiStyle& style = ImGui::GetStyle();
    ApplyEditorTheme(style);
    // detached OS windows shouldn't look translucent/rounded against the desktop
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // First-run docking layout. With no saved session (no imgui.ini yet) ImGui
    // opens every panel free-floating, stacked on top of each other. Seed the
    // session ini from the shipped default so a fresh install starts in the
    // intended docked arrangement, and ImGui's own first-frame auto-load picks
    // it up normally. (We COPY the file rather than LoadIniSettingsFromDisk
    // here: pre-loading settings before the first NewFrame trips ImGui's
    // "settings not yet loaded" assert, whereas seeding the ini it is about to
    // read does not.) Once the user rearranges anything it auto-saves over this
    // copy, so the branch never runs again.
    {
        namespace fs = std::filesystem;
        const char* userIni = io.IniFilename ? io.IniFilename : "imgui.ini";
        const char* shipped = "Exported/Layouts/DefaultLayout.ini";
        std::error_code ec;
        if (!fs::exists(userIni, ec) && fs::exists(shipped, ec)) {
            fs::copy_file(shipped, userIni, fs::copy_options::overwrite_existing, ec);
            // Also expose it under Settings > Editor > Layouts so it can be
            // re-applied after the user moves things around (best-effort).
            fs::create_directories("Layouts", ec);
            if (!fs::exists("Layouts/DefaultLayout.ini", ec))
                fs::copy_file(shipped, "Layouts/DefaultLayout.ini",
                              fs::copy_options::overwrite_existing, ec);
        }
    }

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
