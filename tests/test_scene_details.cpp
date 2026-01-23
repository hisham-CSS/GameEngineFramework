#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Engine.h"
#include "../src/core/Scene.h"
#include "../src/core/Shader.h"
#include "../src/core/Renderer.h" // for EnsureGLADLoaded?
#include <glm/gtc/matrix_transform.hpp>

using namespace MyCoreEngine;

namespace {

#include <fstream>

    struct SceneFixture : ::testing::Test {
        static GLFWwindow* win;
        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "headless", nullptr, nullptr);
            ASSERT_NE(nullptr, win);
            glfwMakeContextCurrent(win);
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
            
            // Create dummy.obj for testing
            std::ofstream val("dummy.obj");
            val << "v -0.5 0 0.5\n"
                << "v  0.5 0 0.5\n"
                << "v  0.5 0 -0.5\n"
                << "f 1 2 3\n";
            val.close();
        }
        static void TearDownTestSuite() {
            glfwDestroyWindow(win);
            glfwTerminate();
            // remove("dummy.obj"); // optional cleanup
        }
    };
    GLFWwindow* SceneFixture::win = nullptr;

    // Helper to create a dummy mesh
    std::shared_ptr<Model> createCubeModel() {
        // We can't easily create a full Model without files, but we can hack a Mesh?
        // Actually, Mesh is RAII.
        // Let's rely on the fact that Scene checks "if (!mc.model) continue;"
        // We need a valid Model.
        // We can create a Model with 0 meshes if we assume the culling happens before mesh iteration?
        // No, Scene.cpp iterates meshes to push DrawItems.
        // So we need at least 1 mesh.
        // Since we are testing logic, we can try to use a "Mock" model if possible,
        // but Model is a concrete class.
        // Ideally we load a tiny obj from disk, but we might not have one guaranteed.
        // Let's skip the "Add to bucket" check for strictly culling if we can't make a model?
        // Wait, "test_render_passes.cpp" uses a "MiniDepthScene" which overrides RenderDepthCascade.
        // Here we want to test Scene::RenderShadowsCombined relative to Registry.
        return nullptr; // TODO
    }
}

// Subclass Scene to inspect protected/private state if necessary
// OR just check the callback invocation count.
class TestableScene : public Scene {
public:
    // We can't access shadowCascadeItems_ easily as it is private and no getter.
    // But we can check if the CALLBACK is called.
    // If bucket is empty, callback is NOT called.
    // So we can verify culling by counting callback invocations for specific cascades.
};

// We need a valid Model to pass the "if (!mc.model)" check.
// We can use the existing "AssetManager" or manual Model construction if headers allow.
// Model.h has "void AddMesh(Mesh&& mesh)".
// Mesh has ctor from vertices.

TEST_F(SceneFixture, RenderShadowsCombined_CullsByZ) {
    TestableScene scene;
    
    // Create a manual model? No, Model is file-based.
    // We expect "dummy.obj" to be present (created by test setup or manually)
    // For now, we assume the test runner ensures this file exists.
    auto model = std::make_shared<Model>("dummy.obj");

    // Entity A: At Z = -10 (Camera Space)
    // Entity B: At Z = -50
    
    // Fix AABB ctor: needs min/max
    AABB b(glm::vec3(-1,-1,-1), glm::vec3(1,1,1));

    auto eA = scene.createEntity();
    eA.addComponent<Transform>().position = glm::vec3(0, 0, -10);
    eA.addComponent<ModelComponent>().model = model;
    eA.addComponent<AABB>(b);

    auto eB = scene.createEntity();
    eB.addComponent<Transform>().position = glm::vec3(0, 0, -50);
    eB.addComponent<ModelComponent>().model = model;
    eB.addComponent<AABB>(b);
    
    scene.UpdateTransforms();

    // Setup Cascades
    // Cascade 0: Near=0, Far=20  (Should contain A)
    // Cascade 1: Near=20, Far=100 (Should contain B)
    
    std::vector<Scene::CascadeParam> params(2);
    // Light VP: identity for simplicity (AABB check passes if we make it huge)
    // We want to test Z-split culling primarily.
    // Make LightVP cover everything so frustum check passes.
    glm::mat4 hugeOrtho = glm::ortho(-1000.f, 1000.f, -1000.f, 1000.f, -1000.f, 1000.f);
    params[0].lightVP = hugeOrtho;
    params[0].splitNear = 0.f;
    params[0].splitFar = 20.f;
    params[0].viewMatrix = glm::identity<glm::mat4>(); 
    
    params[1].lightVP = hugeOrtho;
    params[1].splitNear = 20.f;
    params[1].splitFar = 100.f;
    params[1].viewMatrix = glm::identity<glm::mat4>(); 

    // Dummy shader
    Shader shader("Exported/Shaders/shadow_depth_vert.glsl", "Exported/Shaders/shadow_depth_frag.glsl"); // reuse existing paths

    int calls[2] = {0, 0};
    scene.RenderShadowsCombined(shader, params, [&](int i) {
        if (i >= 0 && i < 2) calls[i]++;
    });

    // We expect both to be called because each cascade has at least 1 item.
    EXPECT_EQ(calls[0], 1);
    EXPECT_EQ(calls[1], 1);
}

TEST_F(SceneFixture, RenderShadowsCombined_RespectsSplits) {
    TestableScene scene;
    auto model = std::make_shared<Model>("dummy.obj");

    // Entity at Z = -10
    auto eA = scene.createEntity();
    eA.addComponent<Transform>().position = glm::vec3(0, 0, -10);
    eA.addComponent<ModelComponent>().model = model;
    AABB b(glm::vec3(-1,-1,-1), glm::vec3(1,1,1));
    eA.addComponent<AABB>(b);
    scene.UpdateTransforms();

    // Cascade 0: [0, 5] -> Entity is at 10 (depth). Should NOT be here.
    // Cascade 1: [5, 15] -> Entity is at 10. Should be here.
    
    std::vector<Scene::CascadeParam> params(2);
    glm::mat4 hugeOrtho = glm::ortho(-1000.f, 1000.f, -1000.f, 1000.f, -1000.f, 1000.f);
    
    params[0].lightVP = hugeOrtho;
    params[0].splitNear = 0.f;
    params[0].splitFar = 5.f;
    params[0].viewMatrix = glm::identity<glm::mat4>();

    params[1].lightVP = hugeOrtho;
    params[1].splitNear = 5.f;
    params[1].splitFar = 15.f;
    params[1].viewMatrix = glm::identity<glm::mat4>();

    Shader shader("Exported/Shaders/shadow_depth_vert.glsl", "Exported/Shaders/shadow_depth_frag.glsl");

    int calls[2] = {0, 0};
    scene.RenderShadowsCombined(shader, params, [&](int i) {
        calls[i]++;
    });

    EXPECT_EQ(calls[0], 0) << "Entity at Z=10 included in [0, 5] cascade";
    EXPECT_EQ(calls[1], 1) << "Entity at Z=10 NOT included in [5, 15] cascade";
}
