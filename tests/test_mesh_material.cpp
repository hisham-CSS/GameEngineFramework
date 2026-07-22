// Mesh material-index regression test.
//
// Mesh::MaterialIndex() is the key the editor's per-entity MaterialOverrides
// are stored and looked up under. It was silently stuck at 0 for every mesh
// (SetMaterial set the pointer but not the index), so overrides landed on
// slot 0 -- usually the empty import-default material -- and "make material
// unique" either did nothing (override on a slot nothing renders) or turned
// the mesh black (override on the textureless default). This pins the index
// to the material the mesh actually renders with.

#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Engine.h"

using namespace MyCoreEngine;

TEST(MeshMaterial, IndexMatchesRenderedMaterial) {
    ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "matidx", nullptr, nullptr);
    ASSERT_NE(win, nullptr);
    glfwMakeContextCurrent(win);
    ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());

    auto model = std::make_shared<Model>("Exported/Model/backpack.obj");
    ASSERT_FALSE(model->Meshes().empty());
    const auto& mats = model->Materials();

    bool sawTextured = false;
    for (const auto& mesh : model->Meshes()) {
        const size_t idx = mesh.MaterialIndex();
        // The reported index must point at a real slot...
        ASSERT_LT(idx, mats.size());
        // ...and it must be the SAME material object the mesh renders with. If
        // these diverge, an override keyed by MaterialIndex() edits a
        // different material than the one drawn.
        EXPECT_EQ(mesh.GetMaterial().get(), mats[idx].get())
            << "MaterialIndex() " << idx << " does not match the rendered material";
        if (mesh.GetMaterial() && mesh.GetMaterial()->hasAlbedo()) sawTextured = true;
    }
    // The backpack's meshes use the textured material (slot 1), not the empty
    // import default (slot 0). If this regresses to 0, overrides break.
    EXPECT_TRUE(sawTextured) << "no mesh resolved to a textured material";
    EXPECT_NE(model->Meshes()[0].MaterialIndex(), SIZE_MAX);

    glfwDestroyWindow(win);
    glfwTerminate();
}
