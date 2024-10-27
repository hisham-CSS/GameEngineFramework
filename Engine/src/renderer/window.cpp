#include "Window.h"

namespace MyCoreEngine {
    int Window::Init() {
        return glfwInit();
    }

    void Window::Terminate() {
        glfwTerminate();
    }

    GLFWwindow* Window::CreateWindow(int width, int height, const char* title) {
        return glfwCreateWindow(width, height, title, nullptr, nullptr);
    }

    void Window::DestroyWindow(GLFWwindow* window) {
        glfwDestroyWindow(window);
    }

    void Window::MakeContextCurrent(GLFWwindow* window) {
        glfwMakeContextCurrent(window);
    }

    void Window::SwapBuffers(GLFWwindow* window) {
        glfwSwapBuffers(window);
    }

    void Window::PollEvents() {
        glfwPollEvents();
    }

    bool Window::WindowShouldClose(GLFWwindow* window) {
        return glfwWindowShouldClose(window);
    }
}