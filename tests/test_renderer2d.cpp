// Renderer2D: the general-purpose 2D layer the in-game UI is built on (and
// which a 2D game is meant to be able to use directly).
//
// The three things worth pinning are the ones that fail silently:
//  - BATCHING. If a state-change rule regresses, the picture still looks right
//    and only the draw-call count moves — invisible until a scene is slow.
//  - GL STATE. Anything left changed corrupts the next pass, because the
//    pipeline runs passes in a bare loop with no inter-pass reset.
//  - PROJECTION. Screen mode is top-left/+y-down (web convention) while world
//    mode is centre/+y-up (game convention); getting either backwards flips the
//    whole picture.
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render2d/Renderer2D.h"

#include <vector>

using namespace MyCoreEngine;

namespace {

constexpr int kW = 64, kH = 64;

class Renderer2DTest : public ::testing::Test {
protected:
    static GLFWwindow* win;
    GLuint fbo = 0, tex = 0;
    Renderer2D r2d;

    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kW, kH, "r2d-headless", nullptr, nullptr);
        ASSERT_NE(win, nullptr);
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded()); // Engine.dll's own table
    }
    static void TearDownTestSuite() {
        if (win) glfwDestroyWindow(win);
        glfwTerminate();
        win = nullptr;
    }

    void SetUp() override {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        glViewport(0, 0, kW, kH);
        ASSERT_TRUE(r2d.Init()) << "Renderer2D::Init failed — are the sprite2d "
                                   "shaders staged next to the test?";
    }
    void TearDown() override {
        r2d.Shutdown();
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex); tex = 0; }
    }

    void clear(float v = 0.f) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kW, kH);
        glClearColor(v, v, v, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    std::vector<unsigned char> readback() {
        std::vector<unsigned char> px(size_t(kW) * kH * 4, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }
    // Reads with a TOP-LEFT origin, matching screen-space convention, so the
    // assertions below read the way the draw calls do.
    static unsigned char red(const std::vector<unsigned char>& px, int x, int yTopDown) {
        const int yGL = kH - 1 - yTopDown;
        return px[(size_t(yGL) * kW + x) * 4];
    }
    static unsigned char green(const std::vector<unsigned char>& px, int x, int yTopDown) {
        const int yGL = kH - 1 - yTopDown;
        return px[(size_t(yGL) * kW + x) * 4 + 1];
    }
};
GLFWwindow* Renderer2DTest::win = nullptr;

} // namespace

// Quads sharing a texture must collapse into ONE draw call; a texture change
// must cost exactly one more. This is the whole point of a batcher, and a
// regression here is invisible except as a frame-rate drop.
TEST_F(Renderer2DTest, QuadsSharingATextureBatchIntoOneDrawCall) {
    clear();
    r2d.BeginScreen(kW, kH);
    for (int i = 0; i < 50; ++i) {
        r2d.DrawQuad({ float(i), 0.f }, { 1.f, 1.f }, { 1, 1, 1, 1 });
    }
    r2d.End();
    EXPECT_EQ(r2d.stats().quads, 50);
    EXPECT_EQ(r2d.stats().drawCalls, 1) << "untextured quads did not batch";

    // Alternating textures cannot batch: each switch is a draw call.
    clear();
    r2d.BeginScreen(kW, kH);
    for (int i = 0; i < 6; ++i) {
        const unsigned t = (i % 2 == 0) ? r2d.whiteTexture() : tex;
        r2d.DrawSprite({ float(i), 0.f }, { 1.f, 1.f }, t);
    }
    r2d.End();
    EXPECT_EQ(r2d.stats().quads, 6);
    EXPECT_EQ(r2d.stats().drawCalls, 6) << "texture switches must break the batch";
}

// Sort layer must reorder across submission order (painter's algorithm) while
// staying stable within a layer.
TEST_F(Renderer2DTest, SortLayerPaintsBackToFrontRegardlessOfSubmissionOrder) {
    clear();
    r2d.BeginScreen(kW, kH);
    // Submit the FOREGROUND first, on a higher layer: it must still win.
    r2d.DrawQuad({ 0.f, 0.f }, { 32.f, 32.f }, { 0, 1, 0, 1 }, /*layer*/5);
    r2d.DrawQuad({ 0.f, 0.f }, { 32.f, 32.f }, { 1, 0, 0, 1 }, /*layer*/1);
    r2d.End();

    const auto px = readback();
    EXPECT_GT(green(px, 4, 4), 200) << "higher layer did not paint on top";
    EXPECT_LT(red(px, 4, 4), 60);
}

// Screen space is top-left origin with +y DOWN (HTML/UI Toolkit convention).
// Getting this backwards flips every UI vertically.
TEST_F(Renderer2DTest, ScreenSpaceOriginIsTopLeftWithYDown) {
    clear();
    r2d.BeginScreen(kW, kH);
    // A quad at (0,0) sized 8x8 must cover the TOP-LEFT corner.
    r2d.DrawQuad({ 0.f, 0.f }, { 8.f, 8.f }, { 1, 0, 0, 1 });
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, 2, 2), 200) << "screen-space (0,0) is not the top-left";
    EXPECT_LT(red(px, 2, kH - 3), 60) << "the quad leaked to the bottom (y is inverted)";
}

// World space is centre-origin with +y UP (2D-game convention) — the other half
// of the deliberate split.
TEST_F(Renderer2DTest, WorldSpaceIsCentreOriginWithYUp) {
    clear();
    Camera2D cam; // at the origin, zoom 1 => 1 world unit == 1 pixel
    r2d.BeginWorld(cam, kW, kH);
    // +y is UP, so this quad must land in the TOP half of the image.
    r2d.DrawQuad({ 0.f, 0.f }, { 8.f, 8.f }, { 1, 0, 0, 1 });
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, kW / 2 + 2, kH / 2 - 4), 200)
        << "world-space +y did not go up (or the origin is not centred)";
    EXPECT_LT(red(px, kW / 2 + 2, kH / 2 + 4), 60);
}

// A clip rect must actually cut geometry, and nested rects must INTERSECT so a
// child can never draw outside its parent.
TEST_F(Renderer2DTest, ClipRectsCutAndNest) {
    clear();
    r2d.BeginScreen(kW, kH);
    r2d.PushClipRect({ 0.f, 0.f }, { 16.f, 16.f });
    r2d.DrawQuad({ 0.f, 0.f }, { 64.f, 64.f }, { 1, 0, 0, 1 }); // covers everything
    r2d.PopClipRect();
    r2d.End();

    auto px = readback();
    EXPECT_GT(red(px, 4, 4), 200) << "inside the clip rect was not drawn";
    EXPECT_LT(red(px, 32, 32), 60) << "drawing escaped the clip rect";

    // Nested: the inner rect asks for more than the outer allows.
    clear();
    r2d.BeginScreen(kW, kH);
    r2d.PushClipRect({ 0.f, 0.f }, { 16.f, 16.f });
    r2d.PushClipRect({ 0.f, 0.f }, { 48.f, 48.f }); // must be clamped to 16x16
    r2d.DrawQuad({ 0.f, 0.f }, { 64.f, 64.f }, { 1, 0, 0, 1 });
    r2d.PopClipRect();
    r2d.PopClipRect();
    r2d.End();

    px = readback();
    EXPECT_GT(red(px, 4, 4), 200);
    EXPECT_LT(red(px, 24, 24), 60)
        << "a nested clip rect escaped its parent (rects must intersect)";
}

// The pipeline has no inter-pass reset: anything left modified corrupts the
// next pass and the next frame's opaque draw.
TEST_F(Renderer2DTest, RestoresGLState) {
    clear();
    // A deliberately non-default baseline, so a pass that "restores" by
    // assuming defaults is caught too.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    const GLboolean depth0 = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull0  = glIsEnabled(GL_CULL_FACE);
    const GLboolean blend0 = glIsEnabled(GL_BLEND);
    const GLboolean sciss0 = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean mask0 = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &mask0);

    r2d.BeginScreen(kW, kH);
    r2d.PushClipRect({ 0.f, 0.f }, { 8.f, 8.f });
    r2d.DrawQuad({ 0.f, 0.f }, { 4.f, 4.f }, { 1, 1, 1, 1 });
    r2d.PopClipRect();
    r2d.DrawQuad({ 0.f, 0.f }, { 4.f, 4.f }, { 1, 1, 1, 1 });
    r2d.End();

    EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), depth0)   << "GL_DEPTH_TEST leaked";
    EXPECT_EQ(glIsEnabled(GL_CULL_FACE), cull0)     << "GL_CULL_FACE leaked";
    EXPECT_EQ(glIsEnabled(GL_BLEND), blend0)        << "GL_BLEND leaked";
    EXPECT_EQ(glIsEnabled(GL_SCISSOR_TEST), sciss0) << "GL_SCISSOR_TEST leaked";
    GLboolean mask1 = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &mask1);
    EXPECT_EQ(mask1, mask0) << "depth write mask leaked";
}

// Alpha must composite, not replace — the entire UI depends on it.
TEST_F(Renderer2DTest, AlphaBlends) {
    clear();
    r2d.BeginScreen(kW, kH);
    r2d.DrawQuad({ 0.f, 0.f }, { 32.f, 32.f }, { 1, 0, 0, 1 });        // opaque red
    r2d.DrawQuad({ 0.f, 0.f }, { 32.f, 32.f }, { 0, 1, 0, 0.5f });     // half green
    r2d.End();

    const auto px = readback();
    const unsigned char r = red(px, 4, 4), g = green(px, 4, 4);
    EXPECT_GT(g, 80)  << "the translucent quad did not blend in";
    EXPECT_GT(r, 80)  << "the translucent quad replaced instead of blending";
}

// Untextured draws must not need a bound texture from the caller, and a fully
// transparent quad must not paint.
TEST_F(Renderer2DTest, TransparentQuadsDoNotPaint) {
    clear();
    r2d.BeginScreen(kW, kH);
    r2d.DrawQuad({ 0.f, 0.f }, { 32.f, 32.f }, { 1, 0, 0, 0.f }); // alpha 0
    r2d.End();

    const auto px = readback();
    EXPECT_LT(red(px, 4, 4), 20) << "a fully transparent quad still painted";
}
