// Editor/src/ImGuiInputMap.h
#pragma once
#include "Engine.h"
#include "imgui.h"

// InputMap whose keyboard/mouse polls go through Dear ImGui instead of raw
// GLFW main-window state. ImGui aggregates input across every platform
// window, so with multi-viewports enabled the fly camera keeps working when
// the Viewport panel (or any panel) lives in a detached OS window on
// another monitor — raw glfwGetKey on the main window sees nothing there.
// Gamepad polling inherits the base implementation (it is global anyway).
class ImGuiInputMap final : public MyCoreEngine::InputMap {
protected:
    bool pollKey(GLFWwindow* /*window*/, int glfwKey) const override {
        const ImGuiKey k = toImGuiKey_(glfwKey);
        return k != ImGuiKey_None && ImGui::IsKeyDown(k);
    }

    bool pollMouseButton(GLFWwindow* /*window*/, int glfwButton) const override {
        // GLFW and ImGui agree on left=0 / right=1 / middle=2 / x1=3 / x2=4
        // (ImGui tracks ImGuiMouseButton_COUNT = 5 buttons)
        return glfwButton >= 0 && glfwButton < 5 && ImGui::IsMouseDown(glfwButton);
    }

private:
    // Cover every key ImGui tracks: bindings are a documented rebind API
    // ("Rebind via Application::input()"), so an unmapped key here would
    // silently never fire in the editor.
    static ImGuiKey toImGuiKey_(int key) {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
            return (ImGuiKey)(ImGuiKey_A + (key - GLFW_KEY_A));
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
            return (ImGuiKey)(ImGuiKey_0 + (key - GLFW_KEY_0));
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
            return (ImGuiKey)(ImGuiKey_F1 + (key - GLFW_KEY_F1));
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
            return (ImGuiKey)(ImGuiKey_Keypad0 + (key - GLFW_KEY_KP_0));
        switch (key) {
        case GLFW_KEY_UP:            return ImGuiKey_UpArrow;
        case GLFW_KEY_DOWN:          return ImGuiKey_DownArrow;
        case GLFW_KEY_LEFT:          return ImGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT:         return ImGuiKey_RightArrow;
        case GLFW_KEY_ESCAPE:        return ImGuiKey_Escape;
        case GLFW_KEY_SPACE:         return ImGuiKey_Space;
        case GLFW_KEY_ENTER:         return ImGuiKey_Enter;
        case GLFW_KEY_TAB:           return ImGuiKey_Tab;
        case GLFW_KEY_BACKSPACE:     return ImGuiKey_Backspace;
        case GLFW_KEY_INSERT:        return ImGuiKey_Insert;
        case GLFW_KEY_DELETE:        return ImGuiKey_Delete;
        case GLFW_KEY_HOME:          return ImGuiKey_Home;
        case GLFW_KEY_END:           return ImGuiKey_End;
        case GLFW_KEY_PAGE_UP:       return ImGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN:     return ImGuiKey_PageDown;
        case GLFW_KEY_CAPS_LOCK:     return ImGuiKey_CapsLock;
        case GLFW_KEY_SCROLL_LOCK:   return ImGuiKey_ScrollLock;
        case GLFW_KEY_NUM_LOCK:      return ImGuiKey_NumLock;
        case GLFW_KEY_PRINT_SCREEN:  return ImGuiKey_PrintScreen;
        case GLFW_KEY_PAUSE:         return ImGuiKey_Pause;
        case GLFW_KEY_APOSTROPHE:    return ImGuiKey_Apostrophe;
        case GLFW_KEY_COMMA:         return ImGuiKey_Comma;
        case GLFW_KEY_MINUS:         return ImGuiKey_Minus;
        case GLFW_KEY_PERIOD:        return ImGuiKey_Period;
        case GLFW_KEY_SLASH:         return ImGuiKey_Slash;
        case GLFW_KEY_SEMICOLON:     return ImGuiKey_Semicolon;
        case GLFW_KEY_EQUAL:         return ImGuiKey_Equal;
        case GLFW_KEY_LEFT_BRACKET:  return ImGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH:     return ImGuiKey_Backslash;
        case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT:  return ImGuiKey_GraveAccent;
        case GLFW_KEY_KP_DECIMAL:    return ImGuiKey_KeypadDecimal;
        case GLFW_KEY_KP_DIVIDE:     return ImGuiKey_KeypadDivide;
        case GLFW_KEY_KP_MULTIPLY:   return ImGuiKey_KeypadMultiply;
        case GLFW_KEY_KP_SUBTRACT:   return ImGuiKey_KeypadSubtract;
        case GLFW_KEY_KP_ADD:        return ImGuiKey_KeypadAdd;
        case GLFW_KEY_KP_ENTER:      return ImGuiKey_KeypadEnter;
        case GLFW_KEY_KP_EQUAL:      return ImGuiKey_KeypadEqual;
        case GLFW_KEY_LEFT_SHIFT:    return ImGuiKey_LeftShift;
        case GLFW_KEY_RIGHT_SHIFT:   return ImGuiKey_RightShift;
        case GLFW_KEY_LEFT_CONTROL:  return ImGuiKey_LeftCtrl;
        case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
        case GLFW_KEY_LEFT_ALT:      return ImGuiKey_LeftAlt;
        case GLFW_KEY_RIGHT_ALT:     return ImGuiKey_RightAlt;
        case GLFW_KEY_LEFT_SUPER:    return ImGuiKey_LeftSuper;
        case GLFW_KEY_RIGHT_SUPER:   return ImGuiKey_RightSuper;
        case GLFW_KEY_MENU:          return ImGuiKey_Menu;
        default:                     return ImGuiKey_None;
        }
    }
};
