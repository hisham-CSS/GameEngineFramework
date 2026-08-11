// UIPass: the screen-space overlay that paints on top of the finished frame.
//
// The contract worth pinning is what makes it NOT a post-process:
//  - it must draw into ctx.defaultFBO (the final image), and
//  - it must NEVER consume an LDR ping-pong slot. If it ever did, the real
//    final image would be routed into an off-screen buffer and the screen would
//    show a stale frame — the silent-black-screen failure test_post_chain
//    exists to catch, reached from a different direction.
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "ui_shipped_hud.h"
#include "../Engine/src/render/passes/UIPass.h"
#include "../Engine/src/render2d/Renderer2D.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIStyle.h"

#include <vector>

using namespace MyCoreEngine;

namespace {

constexpr int kW = 64, kH = 64;

class UIPassTest : public ::testing::Test {
protected:
    static GLFWwindow* win;
    GLuint tex = 0, fbo = 0;
    Renderer2D r2d;

    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kW, kH, "uipass-headless", nullptr, nullptr);
        ASSERT_NE(win, nullptr);
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
    }
    static void TearDownTestSuite() {
        if (win) glfwDestroyWindow(win);
        glfwTerminate();
        win = nullptr;
    }

    void SetUp() override {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        glViewport(0, 0, kW, kH);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void TearDown() override {
        r2d.Shutdown();
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex); tex = 0; }
    }

    FrameParams fp() const {
        FrameParams f{};
        f.viewportW = kW;
        f.viewportH = kH;
        return f;
    }
    std::vector<unsigned char> readback() {
        std::vector<unsigned char> px(size_t(kW) * kH * 4, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }
};
GLFWwindow* UIPassTest::win = nullptr;

} // namespace

TEST_F(UIPassTest, DrawsOntoTheDefaultFBOAndNeverConsumesAChainSlot) {
    UIDrawFn draw = [](Renderer2D& r, int w, int h, float) {
        r.DrawQuad({ 0.f, 0.f }, { float(w) * 0.5f, float(h) * 0.5f }, { 1, 0, 0, 1 });
    };
    UIPass pass(&r2d, &draw);

    PassContext ctx{};
    ctx.defaultFBO = fbo;
    // Pretend a post chain is mid-flight. The UI overlay must not touch this.
    ctx.postPassesLeft = 3;
    ctx.ldrFBO_A = 12345; ctx.ldrTex_A = 999; // bogus: must never be used
    ctx.postSrcTex = 999;

    pass.setup(ctx);
    Scene scene; Camera cam;
    EXPECT_TRUE(pass.execute(ctx, scene, cam, fp()));

    EXPECT_EQ(ctx.postPassesLeft, 3)
        << "UIPass consumed an LDR ping-pong slot — it is an overlay, not a "
           "chain stage, and doing so routes the real final image off-screen";

    const auto px = readback();
    // Top-left quadrant painted (screen space is y-down), bottom-right clear.
    const auto at = [&](int x, int yTop) {
        const int yGL = kH - 1 - yTop;
        return px[(size_t(yGL) * kW + x) * 4];
    };
    EXPECT_GT(at(4, 4), 200) << "the overlay did not reach defaultFBO";
    EXPECT_LT(at(kW - 4, kH - 4), 40) << "the overlay painted outside its quad";
}

TEST_F(UIPassTest, IsANoOpWithoutACallback) {
    UIDrawFn empty; // unset
    UIPass pass(&r2d, &empty);
    PassContext ctx{};
    ctx.defaultFBO = fbo;
    ctx.postPassesLeft = 2;
    pass.setup(ctx);

    Scene scene; Camera cam;
    EXPECT_FALSE(pass.execute(ctx, scene, cam, fp()))
        << "a UI pass with no callback must report that it did nothing";
    EXPECT_EQ(ctx.postPassesLeft, 2);

    const auto px = readback();
    for (size_t i = 0; i < px.size(); i += 4) {
        ASSERT_LT(px[i], 20) << "something was painted with no callback set";
    }
}

// A degenerate viewport (minimised window, collapsed dock panel) must not draw
// or divide by zero.
TEST_F(UIPassTest, SkipsAZeroSizedViewport) {
    bool called = false;
    UIDrawFn draw = [&called](Renderer2D&, int, int, float) { called = true; };
    UIPass pass(&r2d, &draw);
    PassContext ctx{};
    ctx.defaultFBO = fbo;
    pass.setup(ctx);

    FrameParams f{};
    f.viewportW = 0;
    f.viewportH = 0;
    Scene scene; Camera cam;
    EXPECT_FALSE(pass.execute(ctx, scene, cam, f));
    EXPECT_FALSE(called) << "the UI callback ran for a zero-sized viewport";
}

// The pass runs inside the same bare pass loop as everything else, so it must
// hand back the GL state it was given.
TEST_F(UIPassTest, RestoresGLState) {
    UIDrawFn draw = [](Renderer2D& r, int, int, float) {
        r.DrawQuad({ 0.f, 0.f }, { 8.f, 8.f }, { 1, 1, 1, 1 });
    };
    UIPass pass(&r2d, &draw);
    PassContext ctx{};
    ctx.defaultFBO = fbo;
    pass.setup(ctx);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    const GLboolean d0 = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean c0 = glIsEnabled(GL_CULL_FACE);
    const GLboolean b0 = glIsEnabled(GL_BLEND);

    Scene scene; Camera cam;
    pass.execute(ctx, scene, cam, fp());

    EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), d0) << "GL_DEPTH_TEST leaked";
    EXPECT_EQ(glIsEnabled(GL_CULL_FACE), c0)  << "GL_CULL_FACE leaked";
    EXPECT_EQ(glIsEnabled(GL_BLEND), b0)      << "GL_BLEND leaked";
}

// The shipped sample HUD must survive a missing font and any viewport shape —
// it is the first thing anyone runs, and a HUD that crashes or vanishes on an
// odd aspect ratio is worse than no sample at all. Driven the way a game drives
// it: a scene entity and a UIWorld.
TEST_F(UIPassTest, ShippedHudLaysOutAtAnyAspectAndWithoutAFont) {
    ShippedHud hud;
    hud.world.SetFont(nullptr);   // geometry works without one

    ASSERT_TRUE(r2d.Init());
    for (auto wh : { std::pair<int,int>{1280, 720}, {480, 800}, {2560, 1080}, {64, 64} }) {
        hud.data().SetNumber("health", 0.37f);
        hud.data().SetInt("score", 1234);
        hud.Frame(wh.first, wh.second);
        ASSERT_NE(hud.assets(), nullptr) << "the shipped HUD did not load";

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        r2d.BeginScreen(wh.first, wh.second);
        hud.world.Draw(r2d);      // must not crash
        r2d.End();

        // The crosshair is centred by flexbox, not by arithmetic: check it
        // actually tracks the viewport centre at every shape.
        auto* cross = hud.find("crosshair");
        ASSERT_NE(cross, nullptr);
        const glm::vec2 c = cross->layout().position + cross->layout().size * 0.5f;
        EXPECT_NEAR(c.x, wh.first * 0.5f, 1.5f) << wh.first << "x" << wh.second;
        EXPECT_NEAR(c.y, wh.second * 0.5f, 1.5f) << wh.first << "x" << wh.second;
    }

    // Health drives the fill width as a percentage of its track.
    auto* fill = hud.find("healthFill");
    auto* track = hud.find("healthTrack");
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(track, nullptr);
    const float wAt37 = fill->layout().size.x;
    hud.data().SetNumber("health", 1.0f);
    hud.Frame();
    EXPECT_GT(fill->layout().size.x, wAt37) << "health did not widen the bar";
    hud.data().SetNumber("health", 0.0f);
    hud.Frame();
    EXPECT_NEAR(fill->layout().size.x, 0.f, 0.51f) << "zero health should empty the bar";
}

// A GRADIENT IS ITS OWN REASON TO PAINT.
//
// UIElement's background emit was gated on `backgroundColor.a > 0 || shaped`,
// where `shaped` means "has a radius or a border". `shaped` predates gradients
// and the test only ever asked about the FROM stop -- so an element fading
// transparent -> opaque, with no radius and no border, emitted no quad at all
// and vanished, while the same two stops in the other order painted fine. No
// diagnostic either way.
TEST_F(UIPassTest, AGradientPaintsEvenWhenTheFromStopIsTransparent) {
    using namespace MyCoreEngine::ui;

    auto quadsFor = [this](const glm::vec4& from, const glm::vec4& to,
                           BackgroundGradient dir) {
        UIDocument doc;
        UIElement& e = *doc.root().AddChild("box");
        e.style().width = StyleLength::Px(40.f);
        e.style().height = StyleLength::Px(20.f);
        e.style().backgroundColor = from;
        e.style().backgroundColorTo = to;
        e.style().backgroundGradient = dir;
        doc.Layout(float(kW), float(kH));

        r2d.BeginScreen(kW, kH);
        const int before = r2d.stats().quads;
        doc.Draw(r2d);
        const int after = r2d.stats().quads;
        r2d.End();
        return after - before;
    };

    const glm::vec4 clear{ 0.f, 0.f, 0.f, 0.f };
    const glm::vec4 solid{ 0.2f, 0.6f, 0.9f, 1.f };

    // The control: opaque -> transparent already worked, because the FROM stop
    // is what the old gate looked at.
    EXPECT_GT(quadsFor(solid, clear, BackgroundGradient::Vertical), 0)
        << "an opaque->transparent gradient stopped painting -- this direction "
           "always worked, so something else broke";

    // The defect: the same two stops, the other way round.
    EXPECT_GT(quadsFor(clear, solid, BackgroundGradient::Vertical), 0)
        << "a transparent->opaque gradient emitted NO quad: the element is "
           "invisible, and only the FROM stop was consulted";
    EXPECT_GT(quadsFor(clear, solid, BackgroundGradient::Horizontal), 0)
        << "same defect on the horizontal axis";

    // And the thing that must NOT change: no gradient, no colour, no shape is
    // still nothing to draw. A container that exists only to lay children out
    // must not start costing a quad each.
    EXPECT_EQ(quadsFor(clear, clear, BackgroundGradient::None), 0)
        << "a fully transparent, unshaped element now emits a quad -- every "
           "layout-only container in every document just got more expensive";
    EXPECT_EQ(quadsFor(clear, clear, BackgroundGradient::Vertical), 0)
        << "a gradient between two TRANSPARENT stops paints nothing visible, "
           "so it must not cost a quad either";
}
