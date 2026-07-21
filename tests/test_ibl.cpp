// IBL bake tests. These need a real GL context (the baker renders), so they
// run against a hidden GLFW window like test_render_passes does.
//
// They exist because the whole IBL feature was previously DEAD in a way no
// test could see: the shader was correct, the plumbing was correct, and the
// textures were simply never generated. The assertions below are therefore
// about the *values* coming out, not just about calls succeeding — a bake that
// returns true but produces black is exactly the failure that shipped before.

#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render/IBLBaker.h"

#include <vector>

using namespace MyCoreEngine;

namespace {

    class IBLTest : public ::testing::Test {
    protected:
        static GLFWwindow* win;

        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "ibl-headless", nullptr, nullptr);
            ASSERT_NE(win, nullptr);
            glfwMakeContextCurrent(win);
            // Loads GLAD for THIS module (the test exe reads textures back).
            ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
            // ...and separately for Engine.dll, where IBLBaker actually makes
            // its GL calls. GLAD's function table is PER MODULE: without this
            // every GL entry point inside the DLL is null and the first bake
            // dies with an access violation that looks like a baker bug.
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
        }
        static void TearDownTestSuite() {
            if (win) glfwDestroyWindow(win);
            glfwTerminate();
            win = nullptr;
        }

        // Mean luminance of one face of a cubemap.
        static float faceLuminance(unsigned cube, int size, int face = 0) {
            std::vector<float> px(size_t(size) * size * 3, 0.f);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB,
                          GL_FLOAT, px.data());
            double sum = 0.0;
            for (size_t i = 0; i < px.size(); i += 3) {
                sum += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
            }
            return static_cast<float>(sum / (px.size() / 3));
        }
    };

    GLFWwindow* IBLTest::win = nullptr;

} // namespace

TEST_F(IBLTest, ProceduralSkyProducesUsableTextures) {
    IBLBaker baker;
    ASSERT_TRUE(baker.BakeProceduralSky({})) << baker.lastError();

    const IBLTextures& t = baker.textures();
    EXPECT_TRUE(t.valid());
    EXPECT_NE(t.environment, 0u);
    EXPECT_NE(t.irradiance, 0u);
    EXPECT_NE(t.prefiltered, 0u);
    EXPECT_NE(t.brdfLUT, 0u);

    // maxMip is a max mip INDEX, not a count. Default is 5 mips -> index 4.
    // frag.glsl does `roughness * uPrefilterMipCount`, so an off-by-one here
    // makes fully-rough surfaces sample a sharp mip and sparkle.
    EXPECT_FLOAT_EQ(t.maxMip, 4.0f);
}

TEST_F(IBLTest, IrradianceIsActuallyLit) {
    IBLBaker baker;
    ASSERT_TRUE(baker.BakeProceduralSky({})) << baker.lastError();

    // THE regression this whole feature was: everything wired up, bake
    // "succeeding", and the result black. A convolution that returns zero
    // gives a scene with no ambient at all, which is what shipped before.
    const float lum = faceLuminance(baker.textures().irradiance, 32);
    EXPECT_GT(lum, 0.01f) << "irradiance convolution produced black";
    EXPECT_LT(lum, 50.0f) << "irradiance is implausibly bright (sun not clamped?)";
}

TEST_F(IBLTest, SkyRespondsToSunDirection) {
    IBLBaker baker;
    IBLBaker::SkyParams noon;
    noon.sunDir = glm::vec3(0.f, -1.f, 0.f);      // sun overhead
    ASSERT_TRUE(baker.BakeProceduralSky(noon));
    const float lumNoon = faceLuminance(baker.textures().irradiance, 32);

    IBLBaker::SkyParams dark;
    dark.sunDir = glm::vec3(0.f, 1.f, 0.f);       // sun below the horizon
    dark.sunIntensity = 0.0f;
    ASSERT_TRUE(baker.BakeProceduralSky(dark));
    const float lumDark = faceLuminance(baker.textures().irradiance, 32);

    // Proves the sun parameters actually reach the bake rather than the sky
    // being a constant that merely looks plausible.
    EXPECT_GT(lumNoon, lumDark);
}

TEST_F(IBLTest, BakesFromTheExampleHDRi) {
    IBLBaker baker;
    // Staged next to the test binary by test_runtime_deps.
    ASSERT_TRUE(baker.BakeFromFile("Exported/Env/studio.hdr")) << baker.lastError();

    EXPECT_TRUE(baker.textures().valid());
    const float lum = faceLuminance(baker.textures().irradiance, 32);
    EXPECT_GT(lum, 0.01f) << "HDRi produced no irradiance";
}

TEST_F(IBLTest, TheDefaultSceneEnvironmentActuallyBakes) {
    // Every new scene defaults to a shipped HDRi, and a missing file falls
    // back to the procedural sky SILENTLY. That fallback is good behaviour and
    // bad for regressions: rename or drop the asset and every scene quietly
    // loses its intended sky with nothing failing. This is the check that
    // makes the default a promise rather than a hope.
    const EnvironmentSettings defaults{};
    ASSERT_EQ(defaults.source, EnvironmentSettings::Source::HDRi);
    ASSERT_FALSE(defaults.hdriPath.empty());

    IBLBaker baker;
    EXPECT_TRUE(baker.BakeFromFile(defaults.hdriPath))
        << "the default environment '" << defaults.hdriPath
        << "' did not load: " << baker.lastError();
    EXPECT_GT(faceLuminance(baker.textures().irradiance, 32), 0.01f);
}

TEST_F(IBLTest, MissingHDRiFailsAndKeepsThePreviousBake) {
    IBLBaker baker;
    ASSERT_TRUE(baker.BakeProceduralSky({}));
    const unsigned goodIrr = baker.textures().irradiance;
    ASSERT_NE(goodIrr, 0u);

    EXPECT_FALSE(baker.BakeFromFile("Exported/Env/does_not_exist.hdr"));
    EXPECT_FALSE(baker.lastError().empty()) << "failure must be reportable";

    // A mistyped path in the Inspector must not black out a scene that was
    // lit a moment ago.
    EXPECT_EQ(baker.textures().irradiance, goodIrr);
    EXPECT_TRUE(baker.textures().valid());
}

TEST_F(IBLTest, RejectsHDRiPathOutsideTheProject) {
    // hdriPath is untrusted scene content. A traversal or absolute path must be
    // refused before stbi ever opens it (shared containment gate, PathSandbox),
    // and — like a mistyped path — must leave the previous bake lit.
    IBLBaker baker;
    ASSERT_TRUE(baker.BakeProceduralSky({}));
    const unsigned goodIrr = baker.textures().irradiance;
    ASSERT_NE(goodIrr, 0u);

    for (const char* evil : { "../../evil.hdr",
                              R"(..\..\evil.hdr)",
                              "Exported/Env/../../../etc/passwd",
                              "C:/Windows/System32/evil.hdr",
                              "/etc/passwd" }) {
        EXPECT_FALSE(baker.BakeFromFile(evil)) << "accepted out-of-project HDRi: " << evil;
        EXPECT_NE(baker.lastError().find("rejected"), std::string::npos)
            << "rejection must be reported for: " << evil;
        // the good bake from before the attempt survives untouched
        EXPECT_EQ(baker.textures().irradiance, goodIrr) << "rejected path disturbed the bake: " << evil;
    }
}

TEST_F(IBLTest, BakeRestoresGLState) {
    // The baker runs mid-frame-loop, so leaving GL altered corrupts the very
    // next pass in ways that look nothing like an IBL bug.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glViewport(0, 0, 64, 64);

    IBLBaker baker;
    ASSERT_TRUE(baker.BakeProceduralSky({}));

    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    EXPECT_EQ(vp[2], 64);
    EXPECT_EQ(vp[3], 64) << "viewport left at a cube face size";
    EXPECT_TRUE(glIsEnabled(GL_DEPTH_TEST));
    EXPECT_TRUE(glIsEnabled(GL_CULL_FACE));
    EXPECT_FALSE(glIsEnabled(GL_BLEND));

    GLint fbo = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    EXPECT_EQ(fbo, 0) << "capture FBO left bound";
}
