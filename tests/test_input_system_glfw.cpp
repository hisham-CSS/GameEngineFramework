// tests/test_input_system_glfw.cpp
#include "Engine.h"
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
using namespace MyCoreEngine;

TEST(InputSystem, WorksWithHiddenWindow) {
    ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // don’t show a window
    GLFWwindow* win = glfwCreateWindow(320, 200, "Hidden", nullptr, nullptr);
    ASSERT_NE(win, nullptr);

    MyCoreEngine::InputSystem input(win);
    Camera cam(glm::vec3(0.f, 0.f, 3.f));
    // We can’t simulate key presses without more plumbing, but we can at least call update safely
    EXPECT_NO_THROW(input.update(cam, 0.016f));

    glfwDestroyWindow(win);
    glfwTerminate();
}
