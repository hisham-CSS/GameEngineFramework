// tests/test_render_passes.cpp
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define UNIT_TEST 1
#include "../src/render/passes/ShadowCSMPass.h"
#include "../src/render/passes/ForwardOpaquePass.h"
#include "../src/render/passes/TonemapPass.h"
#include "Engine.h"

using namespace MyCoreEngine;

namespace {
    struct GLFixture : ::testing::Test {
        static GLFWwindow* win;
        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            // keep it modest to maximize compatibility; glCreateShader is GL 2.0 anyway
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "headless", nullptr, nullptr);
            ASSERT_NE(nullptr, win);
            glfwMakeContextCurrent(win);
            // 1) Initialize GLAD **inside Engine.dll** (used by Shader & passes)
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded()) << "Engine GLAD init failed";
            
            // 2) Initialize GLAD **in this test EXE** as well (we call raw GL for FBO/VAO setup)
            ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1) << "Test GLAD init failed";
            
            // sanity: verify function pointers are non-null in THIS module
            ASSERT_NE(glad_glCreateShader, nullptr) << "Test GLAD not initialized (glCreateShader null)";
            
            ASSERT_NE(glGetString, nullptr);
            ASSERT_NE(glGetString(GL_VERSION), nullptr) << "no current GL context";
            // optional: disable vsync to keep tests deterministic
            glfwSwapInterval(0);
        }

        // Make sure every test runs with the context current on THIS thread.
        void SetUp() override {
            glfwMakeContextCurrent(win);
            // (Defensive) If another thread changed contexts, re-ensure both loaders:
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
            ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1);
            ASSERT_NE(glad_glCreateShader, nullptr);
        }

        static void TearDownTestSuite() {
            glfwMakeContextCurrent(nullptr);
            glfwDestroyWindow(win);
            glfwTerminate();
        }
        // small HDR FBO suitable for passes
        static void makeHDR(PassContext& ctx, int w = 64, int h = 64) {
            glGenFramebuffers(1, &ctx.hdrFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);

            glGenTextures(1, &ctx.hdrColorTex);
            glBindTexture(GL_TEXTURE_2D, ctx.hdrColorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glGenRenderbuffers(1, &ctx.hdrDepthRBO);
            glBindRenderbuffer(GL_RENDERBUFFER, ctx.hdrDepthRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx.hdrColorTex, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ctx.hdrDepthRBO);
            ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // fullscreen quad
            float quad[6 * 4] = {
                -1,-1, 0,0,  1,-1, 1,0,  1, 1, 1,1,
                -1,-1, 0,0,  1, 1, 1,1, -1, 1, 0,1
            };
            glGenVertexArrays(1, &ctx.fsQuadVAO);
            GLuint vbo; glGenBuffers(1, &vbo);
            glBindVertexArray(ctx.fsQuadVAO);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glBindVertexArray(0);

            ctx.defaultFBO = 0;
        }
    };
    GLFWwindow* GLFixture::win = nullptr;

    // Minimal no-op scene; we only need it for interface compatibility
    struct NullScene : public Scene {
        void RenderDepthCascade(Shader&, const glm::mat4&, float, float, const glm::mat4&) override {
            // draw nothing
        }
        void RenderScene(const Frustum&, Shader&, Camera&) override {
            // draw nothing
        }
    };

} // namespace

TEST_F(GLFixture, ShadowSplits_Monotonic_And_Resizes) {
    PassContext ctx{};
    GLFixture::makeHDR(ctx);

    // Depth shader for the pass (compile a trivial position-only shader you already ship)
    // Reuse your asset path or inline tiny depth shader if you prefer.
    Shader depthProg("Exported/Shaders/shadow_depth_vert.glsl", "Exported/Shaders/shadow_depth_frag.glsl");

    ShadowCSMPass pass;
    pass.setup(ctx); // creates depth shader internally if your setup does that; ok if redundant
    pass.setNumCascades(4);
    pass.setBaseResolution(1024);
    pass.setCascadeUpdateBudget(0); // update all
    pass.setMaxShadowDistance(200.f);

    Camera cam;
    cam.Position = { 0,2,5 };
    cam.Front = { 0,-0.3f,-1 }; cam.Front = glm::normalize(cam.Front);
    cam.Zoom = 60.f;

    FrameParams fp{};
    fp.view = cam.GetViewMatrix();
    fp.proj = glm::perspective(glm::radians(cam.Zoom), 1.0f, 0.1f, 1000.0f);
    fp.viewportW = 64; fp.viewportH = 64;

    ctx.sunDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));

    NullScene scene;

    ASSERT_TRUE(pass.execute(ctx, scene, cam, fp));
    auto s1 = pass.getDebugSnapshot();

    // Splits strictly increasing and within (near, far]
    for (int i = 0; i < s1.cascades - 1; ++i) {
        EXPECT_LT(s1.splitFar[i], s1.splitFar[i + 1]);
        EXPECT_GT(s1.splitFar[i], 0.1f);
        EXPECT_LE(s1.splitFar[i + 1], 200.f + 1e-3f);
    }

    // Now change base resolution -> ensureTargets_ reallocates and we redraw all
    pass.setBaseResolution(2048);
    ASSERT_TRUE(pass.execute(ctx, scene, cam, fp));
    auto s2 = pass.getDebugSnapshot();

    for (int i = 0; i < s2.cascades; ++i) {
        // GL reports actual texture size
        GLint w = 0, h = 0;
        glBindTexture(GL_TEXTURE_2D, s2.depthTex[i]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        EXPECT_EQ(w, 2048);
        EXPECT_EQ(h, 2048);
        EXPECT_EQ(s2.resPer[i], 2048);
    }
}

static void orthoBoundsFromMatrix(const glm::mat4& M, float& l, float& r, float& b, float& t) {
    // Invert OpenGL ortho:
    // M[0][0] = 2/(r-l); M[1][1] = 2/(t-b); M[3][0]=-(r+l)/(r-l); M[3][1]=-(t+b)/(t-b)
    float sx = M[0][0], sy = M[1][1];
    float tx = M[3][0], ty = M[3][1];
    float rl = 2.0f / sx;
    float tb = 2.0f / sy;
    l = -(tx + 1.0f) * rl * 0.5f;
    r = rl + l;
    b = -(ty + 1.0f) * tb * 0.5f;
    t = tb + b;
}

TEST_F(GLFixture, ShadowCenterSnap_StableAcrossResChange) {
    PassContext ctx{}; GLFixture::makeHDR(ctx);
    ShadowCSMPass pass;
	pass.setup(ctx);
    pass.setNumCascades(4);
    pass.setCascadeUpdateBudget(0);
    pass.setBaseResolution(1024);
    pass.setMaxShadowDistance(150.f);

    Camera cam; cam.Position = { 0,1,4 }; cam.Front = glm::normalize(glm::vec3(0, -0.2f, -1)); cam.Zoom = 60.f;
    FrameParams fp{}; fp.view = cam.GetViewMatrix();
    fp.proj = glm::perspective(glm::radians(cam.Zoom), 1.0f, 0.1f, 1000.0f);
    fp.viewportW = 64; fp.viewportH = 64;
    NullScene scene;
    ctx.sunDir = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.1f));

    pass.execute(ctx, scene, cam, fp);
    auto a = pass.getDebugSnapshot();
    pass.setBaseResolution(2048);
    pass.execute(ctx, scene, cam, fp);
    auto b = pass.getDebugSnapshot();

    // For cascade 0, center should be ~unchanged (center-snap)
    float l0, r0, b0, t0; orthoBoundsFromMatrix(a.lightVP[0], l0, r0, b0, t0);
    float l1, r1, b1, t1; orthoBoundsFromMatrix(b.lightVP[0], l1, r1, b1, t1);
    const float cx0 = 0.5f * (l0 + r0), cy0 = 0.5f * (b0 + t0);
    const float cx1 = 0.5f * (l1 + r1), cy1 = 0.5f * (b1 + t1);
    // width/height can be signed depending on handedness; use absolute size
    const float width0 = std::abs(r0 - l0);
    const float height0 = std::abs(t0 - b0);
    const int   res0 = (a.resPer[0] > 0) ? a.resPer[0] : 1;
    const int   res1 = (b.resPer[0] > 0) ? b.resPer[0] : 1;
    const float width1 = std::abs(r1 - l1);
    const float height1 = std::abs(t1 - b1);
    const float texX0 = width0 / float(res0);
    const float texY0 = height0 / float(res0);
    const float texX1 = width1 / float(res1);
    const float texY1 = height1 / float(res1);
    // Allow up to ~1 texel (with a small safety factor for rounding)
    const float tolX = 1.1f * std::max(texX0, texX1) + 1e-5f;
    const float tolY = 1.1f * std::max(texY0, texY1) + 1e-5f;
    EXPECT_NEAR(cx0, cx1, tolX);
    EXPECT_NEAR(cy0, cy1, tolY);
    // Optional: width/height should remain effectively constant
    EXPECT_NEAR(width0, width1, std::max(texX0, texX1) * 2.0f);
    EXPECT_NEAR(height0, height1, std::max(texY0, texY1) * 2.0f);
}

TEST_F(GLFixture, ForwardBinds_DepthTextures_ToUnits) {
    PassContext ctx{}; GLFixture::makeHDR(ctx);
    // stub tonemap shader so Forward pass can set other uniforms; not used here
    Shader dummy("Exported/Shaders/tonemap_vert.glsl", "Exported/Shaders/tonemap_frag.glsl");
    // Create a dummy main shader too (your regular PBR shader path)
    Shader mainShader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");

    ShadowCSMPass csm; 
	csm.setup(ctx);
    csm.setNumCascades(3); csm.setBaseResolution(512); csm.setCascadeUpdateBudget(0);
    Camera cam; cam.Position = { 0,1,3 }; cam.Front = glm::normalize(glm::vec3(0, -0.2f, -1)); cam.Zoom = 60.f;
    FrameParams fp{}; fp.view = cam.GetViewMatrix();
    fp.proj = glm::perspective(glm::radians(cam.Zoom), 1.0f, 0.1f, 1000.0f);
    fp.viewportW = 64; fp.viewportH = 64;
    NullScene scene;
    ctx.sunDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
    csm.execute(ctx, scene, cam, fp);
    auto snap = csm.getDebugSnapshot();

    // Configure PassContext as Renderer would
    ctx.csm.enabled = true;
    ctx.csm.cascades = snap.cascades;
    for (int i = 0; i < snap.cascades; ++i) {
        ctx.csm.lightVP[i] = snap.lightVP[i];
        ctx.csm.splitFar[i] = snap.splitFar[i];
        ctx.csm.depthTex[i] = snap.depthTex[i];
        ctx.csm.resPer[i] = snap.resPer[i];
    }
    ctx.splitBlend = 0.0f; ctx.csmDebug = 0;

    ForwardOpaquePass fwd(mainShader);
    fwd.setup(ctx);
    fwd.execute(ctx, scene, cam, fp);

    // Verify GL binding state for units 8..(8+cascades-1)
    for (int i = 0; i < snap.cascades; ++i) {
        const int unit = 8 + i;
        glActiveTexture(GL_TEXTURE0 + unit);
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        EXPECT_EQ((GLuint)bound, ctx.csm.depthTex[i]) << "Unit " << unit << " not bound to cascade " << i;
    }
}
