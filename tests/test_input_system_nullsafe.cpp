// tests/test_input_system_nullsafe.cpp
#include "Engine.h"
#include <glm/glm.hpp>
#include <gtest/gtest.h>
using namespace MyCoreEngine;

TEST(InputSystem, NullWindowIsSafe) {
    MyCoreEngine::InputSystem input; // no window set
    Camera cam(glm::vec3(0.f, 0.f, 3.f));
    EXPECT_NO_THROW(input.update(cam, 0.016f));
}
