// LDR post-process chain: ping-pong routing and GL-state hygiene.
//
// Two invariants of the M1/M2/M4 post stack had no coverage at all, and both
// fail SILENTLY — the symptom is a black or frozen frame, or a corrupted later
// pass, with nothing logged and no test red.
//
// 1. ROUTING. Renderer::countLdrPostPasses_ tells the chain how many passes
//    will run; each enabled pass then consumes exactly one slot via
//    nextPostTarget(), and the last one resolves to defaultFBO. If a pass's own
//    enable predicate ever stops matching the count — a new guard, a shader
//    that failed to compile, an effect added to the count but returning early —
//    the final image is written into an off-screen ping-pong buffer and the
//    screen shows whatever was there before. These tests assert the exact
//    contract ("consumes one slot IFF enabled") over every combination.
//
// 2. GL STATE. The pipeline runs passes in a bare loop with no inter-pass
//    reset, so a pass that leaves blend/depth/cull changed corrupts the NEXT
//    pass and the next frame's opaque draw. That already shipped once
//    (SkyboxPass leaving GL_CULL_FACE off). Each pass is checked against the
//    baseline it was handed.

#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render/passes/OutlinePass.h"
#include "../Engine/src/render/passes/ColorGradePass.h"
#include "../Engine/src/render/passes/VignettePass.h"
#include "../Engine/src/render/passes/FXAAPass.h"

#include <memory>
#include <vector>

using namespace MyCoreEngine;

namespace {

constexpr int kSize = 32;

// Mirrors Renderer::countLdrPostPasses_ (private). Kept as one place the test
// states the expected predicate set, so a pass that silently stops consuming a
// slot shows up as a mismatch below.
int expectedLdrCount(const Scene& s) {
    int n = 0;
    const auto& p = s.PostFX();
    if (p.outline.enabled)    ++n;
    if (p.colorGrade.enabled) ++n;
    if (p.vignette.enabled)   ++n;
    if (s.GetAAEnabled())     ++n; // FXAA, always last
    return n;
}

class PostChainTest : public ::testing::Test {
protected:
    static GLFWwindow* win;
    // Two ping-pong buffers plus a stand-in "screen".
    GLuint texA = 0, fboA = 0, texB = 0, fboB = 0;
    GLuint screenTex = 0, screenFBO = 0;
    GLuint depthTex = 0;         // OutlinePass samples scene depth
    GLuint quadVAO = 0, quadVBO = 0;

    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kSize, kSize, "postchain-headless", nullptr, nullptr);
        ASSERT_NE(win, nullptr);
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
        // Engine.dll keeps its own GLAD table (see test_ibl.cpp).
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
    }
    static void TearDownTestSuite() {
        if (win) glfwDestroyWindow(win);
        glfwTerminate();
        win = nullptr;
    }

    static void makeColorTarget(GLuint& tex, GLuint& fbo, unsigned char fill) {
        std::vector<unsigned char> px(kSize * kSize * 4, fill);
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SetUp() override {
        // Distinct fills so "which buffer did the pass write into?" is decidable.
        makeColorTarget(texA, fboA, 40);
        makeColorTarget(texB, fboB, 80);
        makeColorTarget(screenTex, screenFBO, 200);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        std::vector<float> d(kSize * kSize, 1.0f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kSize, kSize, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, d.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        const float quad[] = {
            -1.f,  1.f, 0.f, 1.f,  -1.f, -1.f, 0.f, 0.f,   1.f, -1.f, 1.f, 0.f,
            -1.f,  1.f, 0.f, 1.f,   1.f, -1.f, 1.f, 0.f,   1.f,  1.f, 1.f, 1.f,
        };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    void TearDown() override {
        for (GLuint* t : { &texA, &texB, &screenTex, &depthTex })
            if (*t) { glDeleteTextures(1, t); *t = 0; }
        for (GLuint* f : { &fboA, &fboB, &screenFBO })
            if (*f) { glDeleteFramebuffers(1, f); *f = 0; }
        if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
        if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
    }

    PassContext makeCtx(const Scene& scene) {
        PassContext ctx{};
        ctx.defaultFBO = screenFBO;
        ctx.hdrDepthTex = depthTex;
        ctx.ldrFBO_A = fboA; ctx.ldrTex_A = texA;
        ctx.ldrFBO_B = fboB; ctx.ldrTex_B = texB;
        ctx.postSrcTex = texA;        // tonemap wrote buffer A
        ctx.postPassesLeft = expectedLdrCount(scene);
        ctx.fsQuadVAO = quadVAO;
        return ctx;
    }

    // The GL state the pipeline hands every pass (Renderer::Setup's baseline).
    static void setBaselineState() {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);
    }
    struct GLState {
        GLboolean depthTest, cull, blend, depthMask;
        GLint depthFunc, cullFace;
        bool operator==(const GLState& o) const {
            return depthTest == o.depthTest && cull == o.cull && blend == o.blend &&
                   depthMask == o.depthMask && depthFunc == o.depthFunc &&
                   cullFace == o.cullFace;
        }
    };
    static GLState captureState() {
        GLState s{};
        s.depthTest = glIsEnabled(GL_DEPTH_TEST);
        s.cull      = glIsEnabled(GL_CULL_FACE);
        s.blend     = glIsEnabled(GL_BLEND);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &s.depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &s.cullFace);
        return s;
    }

    static FrameParams frameParams() {
        FrameParams fp{};
        fp.viewportW = kSize;
        fp.viewportH = kSize;
        return fp;
    }
};

GLFWwindow* PostChainTest::win = nullptr;

// Builds the chain in the pipeline's insertion order.
std::vector<std::unique_ptr<IRenderPass>> makeLdrChain() {
    std::vector<std::unique_ptr<IRenderPass>> v;
    v.push_back(std::make_unique<OutlinePass>());
    v.push_back(std::make_unique<ColorGradePass>());
    v.push_back(std::make_unique<VignettePass>());
    v.push_back(std::make_unique<FXAAPass>());
    return v;
}

void configure(Scene& s, bool outline, bool grade, bool vignette, bool fxaa) {
    auto& p = s.PostFX();
    p.outline.enabled = outline;
    p.colorGrade.enabled = grade;
    p.vignette.enabled = vignette;
    s.SetAAEnabled(fxaa);
}

} // namespace

// THE routing invariant, over all 16 on/off combinations: every enabled pass
// consumes exactly one slot, every disabled pass consumes none, and the chain
// ends with the budget exactly spent. A mismatch here is precisely the desync
// that silently sends the final image to an off-screen buffer.
TEST_F(PostChainTest, EveryPassConsumesOneSlotIffEnabled) {
    for (int mask = 0; mask < 16; ++mask) {
        const bool outline  = (mask & 1) != 0;
        const bool grade    = (mask & 2) != 0;
        const bool vignette = (mask & 4) != 0;
        const bool fxaa     = (mask & 8) != 0;

        Scene scene;
        configure(scene, outline, grade, vignette, fxaa);
        Camera cam;
        PassContext ctx = makeCtx(scene);
        const int expected = expectedLdrCount(scene);
        ASSERT_EQ(ctx.postPassesLeft, expected);

        auto chain = makeLdrChain();
        const bool wants[4] = { outline, grade, vignette, fxaa };
        const char* names[4] = { "Outline", "ColorGrade", "Vignette", "FXAA" };

        for (size_t i = 0; i < chain.size(); ++i) {
            chain[i]->setup(ctx);
            const int before = ctx.postPassesLeft;
            chain[i]->execute(ctx, scene, cam, frameParams());
            const int consumed = before - ctx.postPassesLeft;
            EXPECT_EQ(consumed, wants[i] ? 1 : 0)
                << names[i] << " consumed " << consumed << " chain slots while "
                << (wants[i] ? "ENABLED" : "DISABLED") << " (mask " << mask << ")"
                << " — its predicate no longer agrees with countLdrPostPasses_, so the"
                << " final image lands in an off-screen buffer";
        }

        EXPECT_EQ(ctx.postPassesLeft, 0)
            << "chain budget not exactly spent for mask " << mask
            << ": the last enabled pass did not resolve to defaultFBO";
    }
}

// With effects enabled the final pass must write to the screen FBO, not leave
// the image sitting in a ping-pong buffer. Asserted by content: the screen
// starts at a value no pass would coincidentally reproduce.
TEST_F(PostChainTest, FinalPassLandsOnTheDefaultFBO) {
    Scene scene;
    configure(scene, /*outline*/true, /*grade*/true, /*vignette*/true, /*fxaa*/true);
    Camera cam;
    PassContext ctx = makeCtx(scene);

    auto chain = makeLdrChain();
    for (auto& p : chain) {
        p->setup(ctx);
        p->execute(ctx, scene, cam, frameParams());
    }

    std::vector<unsigned char> px(kSize * kSize * 4, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, screenFBO);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // The screen was seeded to 200; the chain's source was 40. Anything derived
    // from the source is far from 200, so "still 200 everywhere" means nothing
    // was ever written to the screen.
    bool wrote = false;
    for (size_t i = 0; i < px.size(); i += 4) {
        if (px[i] != 200) { wrote = true; break; }
    }
    EXPECT_TRUE(wrote) << "nothing reached defaultFBO — the chain wrote its final "
                          "image into an off-screen ping-pong buffer";
}

// No pass may leave GL state changed: the pipeline has no inter-pass reset, so
// a leak corrupts the next pass and the next frame's opaque draw.
TEST_F(PostChainTest, PassesRestoreGLState) {
    const char* names[4] = { "Outline", "ColorGrade", "Vignette", "FXAA" };
    for (size_t i = 0; i < 4; ++i) {
        Scene scene;
        // Enable only the pass under test, so it is the sole (final) pass.
        configure(scene, i == 0, i == 1, i == 2, i == 3);
        Camera cam;
        PassContext ctx = makeCtx(scene);

        auto chain = makeLdrChain();
        chain[i]->setup(ctx);

        setBaselineState();
        const GLState before = captureState();
        chain[i]->execute(ctx, scene, cam, frameParams());
        const GLState after = captureState();

        EXPECT_TRUE(before == after)
            << names[i] << " changed GL state and did not restore it "
            << "(depthTest " << (int)before.depthTest << "->" << (int)after.depthTest
            << ", cull " << (int)before.cull << "->" << (int)after.cull
            << ", blend " << (int)before.blend << "->" << (int)after.blend
            << ", depthMask " << (int)before.depthMask << "->" << (int)after.depthMask
            << ") — the pipeline runs passes in a bare loop, so this corrupts the next one";
    }
}

// A disabled pass must be a true no-op: it must not touch the chain budget
// (covered above) and must not write to any target either.
TEST_F(PostChainTest, DisabledPassesLeaveTheImageUntouched) {
    Scene scene;
    configure(scene, false, false, false, false); // nothing enabled
    Camera cam;
    PassContext ctx = makeCtx(scene);
    EXPECT_EQ(ctx.postPassesLeft, 0) << "no effects => no chain";

    auto chain = makeLdrChain();
    for (auto& p : chain) {
        p->setup(ctx);
        p->execute(ctx, scene, cam, frameParams());
    }

    std::vector<unsigned char> px(kSize * kSize * 4, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, screenFBO);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (size_t i = 0; i < px.size(); i += 4) {
        ASSERT_EQ(px[i], 200) << "a disabled post pass wrote to the screen";
    }
}
