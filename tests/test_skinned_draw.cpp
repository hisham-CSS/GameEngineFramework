// THE SKINNED GPU PATH, PROBED PIXEL BY PIXEL (ROADMAP M3.2e; ADR-019 D1).
//
// Needs a GL context (a hidden GLFW window; CI runs it under Mesa llvmpipe),
// so it is labelled `gl`. What it pins is the half of skinning that lives on
// the card: a mesh's joints and weights reach attributes 5 and 6, the palette
// reaches the `uBones` block, the SKINNED variant of vertex.glsl moves the
// vertices a joint owns and nothing else, the SKINNED prepass and colour
// programs agree on gl_Position bit for bit (GL_EQUAL), and a posed joint moves
// the normal with it. Every assertion compares two renders in the same context
// (changed versus unchanged, or equal buffers) rather than an absolute colour,
// because llvmpipe and the dev GPU rasterise the same triangle a texel apart.
//
// The fixture is tests/fixtures/models/two_bone_strip.gltf: a strip 0.5 wide
// from y = 0 to y = 2 in the XY plane, facing +Z, bottom row on `root`, top row
// on `tip`, middle row half and half. Drawn under an orthographic camera at 16
// pixels per unit, x in [-2, 2], y in [-1, 3].
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

std::string modelFixturesDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = here / "tests" / "fixtures" / "models";
        if (fs::exists(candidate / "two_bone_strip.gltf")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/models";
}

constexpr int kW = 64, kH = 64;

// Test-local fragment stages, written beside the executable because Shader
// reads files. Both declare vertex.glsl's VS_OUT block.
const char* kFlatFrag =
    "#version 330 core\n"
    "in VS_OUT { vec2 uv; mat3 TBN; vec3 worldPos; float viewDepth; } fs_in;\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kNormalFrag =
    "#version 330 core\n"
    "in VS_OUT { vec2 uv; mat3 TBN; vec3 worldPos; float viewDepth; } fs_in;\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(abs(normalize(fs_in.TBN[2])), 1.0); }\n";

struct SkinnedDrawFixture : ::testing::Test {
    static GLFWwindow* win;
    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kW, kH, "headless", nullptr, nullptr);
        ASSERT_NE(nullptr, win);
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded()) << "Engine GLAD init failed";
        ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1) << "Test GLAD init failed";
        { std::ofstream f("skinned_test_flat_frag.glsl");   f << kFlatFrag; }
        { std::ofstream f("skinned_test_normal_frag.glsl"); f << kNormalFrag; }
    }
    static void TearDownTestSuite() {
        std::remove("skinned_test_flat_frag.glsl");
        std::remove("skinned_test_normal_frag.glsl");
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(win);
        glfwTerminate();
    }
    void SetUp() override {
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
        ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1);
        makeTarget();
        model = std::make_unique<Model>(modelFixturesDir() + "/two_bone_strip.gltf");
        ASSERT_TRUE(model->IsSkinned()) << "two_bone_strip.gltf did not load as a skinned model";
        ASSERT_EQ(model->GetSkeleton().joints.size(), 2u);
        ASSERT_EQ(model->Meshes().size(), 1u);
        ASSERT_TRUE(model->Meshes()[0].Skinned()) << "the mesh has no skin VBO";
        palette = std::make_unique<SkinPaletteUBO>();
        ASSERT_TRUE(palette->Valid());
    }
    void TearDown() override {
        model.reset();
        palette.reset();
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (color) glDeleteTextures(1, &color);
        if (depth) glDeleteTextures(1, &depth);
        fbo = color = depth = 0;
    }

    void makeTarget() {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenTextures(1, &depth);
        glBindTexture(GL_TEXTURE_2D, depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kW, kH, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // A skinned program from vertex.glsl and a test-local fragment stage, its
    // uBones block routed to the palette's binding point.
    std::unique_ptr<Shader> program(const char* fragPath) {
        auto s = std::make_unique<Shader>("Exported/Shaders/vertex.glsl", fragPath, "#define SKINNED 1");
        EXPECT_TRUE(s->isValid()) << "the SKINNED variant of vertex.glsl with " << fragPath << " did not build";
        EXPECT_TRUE(s->bindUniformBlock(SkinPaletteUBO::kBlockName, SkinPaletteUBO::kBinding))
            << "the SKINNED variant has no uBones block";
        return s;
    }

    void setCamera(Shader& s) {
        s.use();
        s.setMat4("model", glm::mat4(1.0f));
        s.setMat4("view", glm::mat4(1.0f));
        s.setMat4("projection", glm::ortho(-2.0f, 2.0f, -1.0f, 3.0f, -3.0f, 3.0f));
        s.setInt("uUseInstancing", 0);
    }

    void beginFrame() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kW, kH);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void drawStrip(Shader& s, const glm::mat4* mats, std::size_t count) {
        s.use();
        palette->Upload(mats, count);
        palette->Bind();
        glBindVertexArray(model->Meshes()[0].VAO());
        model->Meshes()[0].IssueDraw(0);
        glBindVertexArray(0);
    }

    std::array<std::uint8_t, 4> pixel(int x, int y) {
        std::array<std::uint8_t, 4> px{ 0, 0, 0, 0 };
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }
    std::vector<std::uint8_t> frame() {
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(kW) * kH * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        return buf;
    }

    GLuint fbo = 0, color = 0, depth = 0;
    std::unique_ptr<Model>          model;
    std::unique_ptr<SkinPaletteUBO> palette;
};
GLFWwindow* SkinnedDrawFixture::win = nullptr;

// Pixel coordinates of a strip point at 16 px per unit: x in [-2, 2] -> 0..64,
// y in [-1, 3] -> 0..64.
int px(float x) { return static_cast<int>((x + 2.0f) * 16.0f); }
int py(float y) { return static_cast<int>((y + 1.0f) * 16.0f); }

} // namespace

TEST_F(SkinnedDrawFixture, ThePaletteMovesTheVerticesTheJointOwns) {
    auto flat = program("skinned_test_flat_frag.glsl");
    if (::testing::Test::HasFailure()) return;
    setCamera(*flat);

    // Rest: identity palette. The strip covers x in [-0.25, 0.25] for all y.
    const glm::mat4 rest[2] = { glm::mat4(1.0f), glm::mat4(1.0f) };
    beginFrame();
    drawStrip(*flat, rest, 2);
    const auto topRest    = pixel(px(0.0f), py(1.875f));
    const auto bottomRest = pixel(px(0.0f), py(0.125f));
    const auto rightRest  = pixel(px(0.95f), py(1.875f));
    EXPECT_GT(topRest[0], 128)    << "rest: the top of the strip is not drawn";
    EXPECT_GT(bottomRest[0], 128) << "rest: the bottom of the strip is not drawn";
    EXPECT_LT(rightRest[0], 128)  << "rest: nothing should be drawn one unit to the right";

    // Move tip by +1 in x: the top row (tip's alone) shifts a full unit, the
    // middle row (half and half) half a unit, and the bottom row (root's
    // alone) must not move at all.
    glm::mat4 posed[2] = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) };
    beginFrame();
    drawStrip(*flat, posed, 2);
    const auto topPosed    = pixel(px(0.0f), py(1.875f));
    const auto bottomPosed = pixel(px(0.0f), py(0.125f));
    const auto rightPosed  = pixel(px(0.95f), py(1.875f));
    EXPECT_LT(topPosed[0], 128)    << "posed: the top of the strip did not leave its rest position";
    EXPECT_GT(rightPosed[0], 128)  << "posed: the top of the strip did not arrive one unit to the right";
    EXPECT_GT(bottomPosed[0], 128) << "posed: root's vertices moved although only tip was posed";
}

TEST_F(SkinnedDrawFixture, PrepassOnAndOffDrawTheSamePixelsForASkinnedMesh) {
    auto flat    = program("skinned_test_flat_frag.glsl");
    auto prepass = std::make_unique<Shader>("Exported/Shaders/vertex.glsl", "Exported/Shaders/prepass_frag.glsl",
                                            "#define SKINNED 1");
    ASSERT_TRUE(prepass->isValid()) << "the SKINNED prepass program did not build";
    ASSERT_TRUE(prepass->bindUniformBlock(SkinPaletteUBO::kBlockName, SkinPaletteUBO::kBinding));
    if (::testing::Test::HasFailure()) return;
    setCamera(*flat);
    setCamera(*prepass);

    // A non-trivial pose, so the two programs have real arithmetic to agree on.
    const glm::mat4 pivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 posed[2] = { glm::mat4(1.0f),
                           pivot * glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 0, 1)) * glm::inverse(pivot) };

    // Without a prepass.
    beginFrame();
    drawStrip(*flat, posed, 2);
    const std::vector<std::uint8_t> plain = frame();

    // With: depth only through the SKINNED prepass, then colour under GL_EQUAL.
    beginFrame();
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    drawStrip(*prepass, posed, 2);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    drawStrip(*flat, posed, 2);
    const std::vector<std::uint8_t> prepassed = frame();

    ASSERT_EQ(plain.size(), prepassed.size());
    std::size_t differing = 0, lit = 0;
    for (std::size_t i = 0; i < plain.size(); i += 4) {
        if (plain[i] > 128) ++lit;
        if (plain[i] != prepassed[i]) ++differing;
    }
    EXPECT_GT(lit, 0u) << "nothing was drawn at all";
    EXPECT_EQ(differing, 0u)
        << differing << " pixels differ between the prepassed and the plain draw: the SKINNED prepass "
           "and colour programs no longer agree on gl_Position (invariant gl_Position lost, or the "
           "skin arithmetic diverged between the two)";
}

TEST_F(SkinnedDrawFixture, ALitSkinnedQuadShadesByItsPosedNormal) {
    auto normals = program("skinned_test_normal_frag.glsl");
    if (::testing::Test::HasFailure()) return;
    setCamera(*normals);

    // Rest: the strip faces +Z, so the normal-as-colour is pure blue everywhere.
    const glm::mat4 rest[2] = { glm::mat4(1.0f), glm::mat4(1.0f) };
    beginFrame();
    drawStrip(*normals, rest, 2);
    const auto topRest    = pixel(px(0.0f), py(1.5f));
    const auto bottomRest = pixel(px(0.0f), py(0.125f));
    EXPECT_GT(topRest[2], 200)    << "rest: the top of the strip does not face +Z";
    EXPECT_LT(topRest[1], 30);
    EXPECT_GT(bottomRest[2], 200) << "rest: the bottom of the strip does not face +Z";

    // Tilt tip 45 degrees about X around its own pivot (0, 1, 0): the upper
    // half folds toward the camera, its normal gains a y component, and the
    // bottom row, root's alone, keeps facing +Z.
    const glm::mat4 pivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 posed[2] = { glm::mat4(1.0f),
                           pivot * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(1, 0, 0)) * glm::inverse(pivot) };
    beginFrame();
    drawStrip(*normals, posed, 2);
    const auto topPosed    = pixel(px(0.0f), py(1.5f));
    const auto bottomPosed = pixel(px(0.0f), py(0.125f));
    EXPECT_GT(topPosed[1], 100)
        << "posed: the tilted top of the strip still shades with its rest normal (no y component)";
    EXPECT_LT(topPosed[2], 220)
        << "posed: the tilted top of the strip still shades as if facing +Z exactly";
    EXPECT_GT(bottomPosed[2], 200) << "posed: root's normal moved although only tip was posed";
    EXPECT_LT(bottomPosed[1], 30);
}
