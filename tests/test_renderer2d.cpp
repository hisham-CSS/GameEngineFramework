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
#include "../Engine/src/ui/UIElement.h"

#include <string>
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

// A styled scrollbar has to reach the SCREEN, not just the Style struct.
//
// This is the one assertion the headless UI suite cannot make, and it caught a
// real bug: `scrollbar-color` and `scrollbar-thumb-color` parsed, validated and
// stored, while draw_ painted from two literals that happened to equal the
// defaults. Every parse-level test passed and the property did nothing. Only a
// pixel readback could tell the difference, which is why this test lives here
// with the GL fixture rather than next to the rest of the scroll tests.
TEST_F(Renderer2DTest, AStyledScrollbarReachesTheFramebuffer) {
    using namespace MyCoreEngine::ui;

    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().overflowX = box->style().overflowY = Overflow::Scroll;
    box->style().width = StyleLength::Px(float(kW));
    box->style().height = StyleLength::Px(float(kH));
    // Opaque pure red thumb on an opaque pure blue track, so a readback cannot
    // confuse them with each other, with the background, or with the defaults
    // (which are near-white and nearly transparent).
    box->style().scrollbarWidth = 10.0f;
    box->style().scrollbarMinThumb = 8.0f;
    box->style().scrollbarColor = { 0.0f, 0.0f, 1.0f, 1.0f };
    box->style().scrollbarThumbColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    // Content bigger than the box on BOTH axes, so both bars paint and this
    // test can pin that one element's colours cover both of them.
    for (int i = 0; i < 8; ++i) {
        UIElement* r = box->AddChild("r" + std::to_string(i));
        r->style().height = StyleLength::Px(40.f);
        r->style().width = StyleLength::Px(200.f);
    }
    doc.Layout(float(kW), float(kH));

    const UIScrollState* sc = box->scrollState();
    ASSERT_NE(sc, nullptr);
    ASSERT_GT(sc->thumbY.size.y, 0.f) << "no vertical bar to paint";
    ASSERT_GT(sc->thumbX.size.x, 0.f) << "no horizontal bar to paint";

    clear();
    r2d.BeginScreen(kW, kH);
    doc.Draw(r2d);
    r2d.End();
    const auto px = readback();

    // Inside the VERTICAL thumb: red, and not blue.
    const int vx = int(sc->thumbY.position.x + sc->thumbY.size.x * 0.5f);
    const int vy = int(sc->thumbY.position.y + sc->thumbY.size.y * 0.5f);
    EXPECT_GT(red(px, vx, vy), 200) << "the authored thumb colour never reached the screen";

    // Inside the HORIZONTAL thumb: red too. The four scrollbar properties are
    // per ELEMENT, not per axis — one declaration dresses both bars — and this
    // is the assertion that makes that a guarantee rather than a coincidence of
    // both defaults happening to match.
    const int hx = int(sc->thumbX.position.x + sc->thumbX.size.x * 0.5f);
    const int hy = int(sc->thumbX.position.y + sc->thumbX.size.y * 0.5f);
    EXPECT_GT(red(px, hx, hy), 200)
        << "the horizontal bar ignored the element's scrollbar-thumb-color";

    // On each track but clear of its thumb: blue, not red. Both thumbs sit at
    // offset 0, so sample the far end of each track.
    const int farY = int(sc->trackY.position.y + sc->trackY.size.y - 3.f);
    EXPECT_LT(red(px, vx, farY), 60) << "the vertical track took the thumb colour";
    const int farX = int(sc->trackX.position.x + sc->trackX.size.x - 3.f);
    EXPECT_LT(red(px, farX, hy), 60) << "the horizontal track took the thumb colour";
}

// ------------------------------------------------------- rounded boxes

// The whole point of the box stream: a corner that is actually CUT. Every
// parse-level test in the world can pass while the shader paints a square, so
// this is the only assertion that can tell the feature apart from a no-op —
// the same reason AStyledScrollbarReachesTheFramebuffer lives here.
TEST_F(Renderer2DTest, ARadiusActuallyCutsTheCorner) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 20.0f;
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box);
    r2d.End();

    const auto px = readback();
    // The very corner is outside a 20px radius...
    EXPECT_LT(red(px, 1, 1), 40) << "the corner was not cut - this is a square";
    EXPECT_LT(red(px, kW - 2, 1), 40);
    EXPECT_LT(red(px, 1, kH - 2), 40);
    EXPECT_LT(red(px, kW - 2, kH - 2), 40);
    // ...and the middle, and the middle of each edge, are solidly inside it.
    EXPECT_GT(red(px, kW / 2, kH / 2), 200) << "the fill went missing";
    EXPECT_GT(red(px, kW / 2, 1), 200) << "the top edge was eroded";
    EXPECT_GT(red(px, 1, kH / 2), 200) << "the left edge was eroded";
}

// A radius larger than half the box must give a stadium, not an eroded or
// erased element. `border-radius: 9999px` is the pill idiom and has to mean
// something rather than being an error the author computes around.
TEST_F(Renderer2DTest, AnEnormousRadiusIsAStadiumRatherThanAnErasedElement) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 9999.0f;
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box);
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, kW / 2, kH / 2), 200) << "an enormous radius erased the element";
    EXPECT_LT(red(px, 1, 1), 40);
}

// The border is a band of the SAME distance field, which is what lets it share
// one quad and one layer with the fill. If it were a second quad, a child's
// layer could slip between the two.
TEST_F(Renderer2DTest, ABorderPaintsAtTheEdgeAndTheFillPaintsInside) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.borderPx = 6.0f;
    box.borderColor = { 0.f, 1.f, 0.f, 1.f };   // green rim
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box);   // red fill
    r2d.End();

    const auto px = readback();
    EXPECT_GT(green(px, kW / 2, 2), 200) << "no border at the top edge";
    EXPECT_LT(red(px, kW / 2, 2), 60)    << "the fill bled into the border";
    EXPECT_GT(red(px, kW / 2, kH / 2), 200) << "the border ate the fill";
    EXPECT_LT(green(px, kW / 2, kH / 2), 60);
}

// A translucent border must TINT the panel edge, not punch through it. A mix()
// would replace the fill's alpha with the border's and leave a hole.
TEST_F(Renderer2DTest, ATranslucentBorderDoesNotPunchAHoleThroughAnOpaquePanel) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.borderPx = 8.0f;
    box.borderColor = { 0.f, 1.f, 0.f, 0.5f };  // half-transparent green
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box);   // opaque red
    r2d.End();

    const auto px = readback();
    // Both channels present at the rim: the border tinted the fill rather than
    // replacing it. Against the black clear, a hole would read as near-zero red.
    EXPECT_GT(green(px, kW / 2, 3), 80) << "the border did not paint";
    EXPECT_GT(red(px, kW / 2, 3), 80)
        << "a translucent border punched through the panel instead of tinting it";
}

// A box needing neither radius nor border must take the PLAIN stream, so the
// feature costs nothing until it is used and a plain background still batches
// with glyphs exactly as it always did.
TEST_F(Renderer2DTest, APlainBoxDoesNotStartASecondBatch) {
    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle none;   // radius 0, border 0
    r2d.DrawBox({ 0.f, 0.f }, { 10.f, 10.f }, { 1.f, 0.f, 0.f, 1.f }, none);
    r2d.DrawQuad({ 20.f, 0.f }, { 10.f, 10.f }, { 0.f, 1.f, 0.f, 1.f });
    r2d.End();
    EXPECT_EQ(r2d.stats().drawCalls, 1)
        << "an unstyled DrawBox broke the batch - it should be a plain sprite";
}

// The run breaks on stream kind, so a rounded panel and a plain quad cost two
// calls. Pinned so the cost of the seam stays visible and cannot creep.
TEST_F(Renderer2DTest, ARoundedBoxAndAPlainQuadAreTwoBatches) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 4.0f;
    r2d.DrawBox({ 0.f, 0.f }, { 10.f, 10.f }, { 1.f, 0.f, 0.f, 1.f }, box);
    r2d.DrawQuad({ 20.f, 0.f }, { 10.f, 10.f }, { 0.f, 1.f, 0.f, 1.f });
    r2d.End();
    EXPECT_EQ(r2d.stats().drawCalls, 2);
}

// The uniforms are re-set on every run because a shader switch resets neither
// them nor the buffer bindings. If uViewProj were left hoisted, the plain quad
// AFTER a box run would be drawn with whatever the box shader left behind -
// which is nothing, so it would vanish or land somewhere absurd.
TEST_F(Renderer2DTest, APlainQuadAfterARoundedBoxStillLandsWhereItShould) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "box shader unavailable";

    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 4.0f;
    r2d.DrawBox({ 0.f, 0.f }, { 16.f, 16.f }, { 0.f, 1.f, 0.f, 1.f }, box);
    // Bottom-right quadrant, well away from the box.
    r2d.DrawQuad({ 40.f, 40.f }, { 16.f, 16.f }, { 1.f, 0.f, 0.f, 1.f });
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, 48, 48), 200)
        << "the plain run after a box run was drawn with a stale projection";
    EXPECT_GT(green(px, 8, 8), 200) << "the box itself did not paint";
}

// ------------------------------------------------------------- CoverRegion

// Pure arithmetic, asserted directly with hand-supplied numbers — no GL, no
// document — for the same reason ComputeScrollBars and ComputeUIScale are.
TEST(Renderer2DCover, AMatchingAspectCropsNothing) {
    const TexRegion r = CoverRegion({ 200.f, 100.f }, { 400.f, 200.f });
    EXPECT_FLOAT_EQ(r.uvMin.x, 0.f);
    EXPECT_FLOAT_EQ(r.uvMin.y, 0.f);
    EXPECT_FLOAT_EQ(r.uvMax.x, 1.f);
    EXPECT_FLOAT_EQ(r.uvMax.y, 1.f);
}

TEST(Renderer2DCover, AWiderImageIsCroppedOnTheSidesAndStaysCentred) {
    // Box 1:1, image 2:1 -> keep the middle half horizontally.
    const TexRegion r = CoverRegion({ 100.f, 100.f }, { 200.f, 100.f });
    EXPECT_FLOAT_EQ(r.uvMin.x, 0.25f);
    EXPECT_FLOAT_EQ(r.uvMax.x, 0.75f);
    EXPECT_FLOAT_EQ(r.uvMin.y, 0.f);
    EXPECT_FLOAT_EQ(r.uvMax.y, 1.f);
}

TEST(Renderer2DCover, ATallerImageIsCroppedTopAndBottom) {
    const TexRegion r = CoverRegion({ 100.f, 100.f }, { 100.f, 200.f });
    EXPECT_FLOAT_EQ(r.uvMin.y, 0.25f);
    EXPECT_FLOAT_EQ(r.uvMax.y, 0.75f);
    EXPECT_FLOAT_EQ(r.uvMin.x, 0.f);
    EXPECT_FLOAT_EQ(r.uvMax.x, 1.f);
}

// A collapsed panel or an image that failed to decode must show the whole
// picture rather than dividing by zero and producing NaN UVs.
TEST(Renderer2DCover, ADegenerateBoxOrImageReturnsTheWholeImage) {
    for (auto pair : { std::pair<glm::vec2, glm::vec2>{ { 0.f, 100.f }, { 10.f, 10.f } },
                       std::pair<glm::vec2, glm::vec2>{ { 100.f, 0.f }, { 10.f, 10.f } },
                       std::pair<glm::vec2, glm::vec2>{ { 100.f, 100.f }, { 0.f, 10.f } } }) {
        const TexRegion r = CoverRegion(pair.first, pair.second);
        EXPECT_FLOAT_EQ(r.uvMin.x, 0.f);
        EXPECT_FLOAT_EQ(r.uvMax.y, 1.f);
    }
}


// --------------------------------------------------------- two-stop fills
//
// The gradient is not a shader feature: vColor is already interpolated across
// the quad (declared without `flat` in both vertex shaders, and both fragment
// stages multiply by it), so writing two corner colours instead of one IS the
// whole thing. These read actual PIXELS, because that claim is only worth
// anything if the rasteriser really does the interpolation.

TEST_F(Renderer2DTest, AVerticalGradientRampsFromTopToBottom) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "no box shader on this driver";
    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 0.0f;
    box.gradient = Renderer2D::BoxGradient::Vertical;
    box.fillTo = { 0.f, 0.f, 0.f, 1.f };                 // black at the bottom
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box, 0, TexRegion{}, 0);   // red at the top
    r2d.End();

    const auto px = readback();
    const int top = red(px, kW / 2, 1);
    const int mid = red(px, kW / 2, kH / 2);
    const int bot = red(px, kW / 2, kH - 2);
    EXPECT_GT(top, 200) << "the first stop is not at the top";
    EXPECT_LT(bot, 55)  << "the second stop is not at the bottom";
    EXPECT_GT(mid, 60);
    EXPECT_LT(mid, 195) << "the middle is not interpolated - it is a flat fill "
                           "or a hard split, so vColor is not ramping";
}

TEST_F(Renderer2DTest, AHorizontalGradientRampsLeftToRight) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "no box shader on this driver";
    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.gradient = Renderer2D::BoxGradient::Horizontal;
    box.fillTo = { 0.f, 0.f, 0.f, 1.f };
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box, 0, TexRegion{}, 0);
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, 1, kH / 2), 200)      << "the first stop is not on the left";
    EXPECT_LT(red(px, kW - 2, kH / 2), 55)  << "the second stop is not on the right";
    // ...and it does NOT ramp on the other axis.
    EXPECT_NEAR(red(px, kW / 2, 2), red(px, kW / 2, kH - 3), 12)
        << "a horizontal gradient also ramped vertically";
}

// The degrade to DrawSprite carries ONE tint for the whole quad, so a gradient
// on an unrounded, unbordered box would silently come out flat if it took that
// path. It has to be suppressed.
TEST_F(Renderer2DTest, AGradientSuppressesTheFlatSpriteDegrade) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "no box shader on this driver";
    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;      // NO radius, NO border: the degrade case
    box.gradient = Renderer2D::BoxGradient::Vertical;
    box.fillTo = { 0.f, 0.f, 0.f, 1.f };
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box, 0, TexRegion{}, 0);
    r2d.End();

    const auto px = readback();
    EXPECT_GT(red(px, kW / 2, 1) - red(px, kW / 2, kH - 2), 150)
        << "an unrounded gradient box came out FLAT - it took the DrawSprite "
           "degrade, which has only one tint";
}

// The default is None on every box, so this cannot change any existing caller.
TEST_F(Renderer2DTest, WithoutAGradientTheFillIsStillFlat) {
    if (!r2d.supportsRoundedBoxes()) GTEST_SKIP() << "no box shader on this driver";
    clear();
    r2d.BeginScreen(kW, kH);
    Renderer2D::BoxStyle box;
    box.radiusPx = 6.0f;
    box.fillTo = { 0.f, 1.f, 0.f, 1.f };   // set, but gradient is None
    r2d.DrawBox({ 0.f, 0.f }, { float(kW), float(kH) },
                { 1.f, 0.f, 0.f, 1.f }, box, 0, TexRegion{}, 0);
    r2d.End();

    const auto px = readback();
    EXPECT_NEAR(red(px, kW / 2, kH / 4), red(px, kW / 2, 3 * kH / 4), 4)
        << "fillTo leaked into a box whose gradient is None";
    EXPECT_LT(green(px, kW / 2, kH / 2), 40) << "the second stop was used anyway";
}
