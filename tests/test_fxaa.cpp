// FXAA behaviour tests against a real GL context.
//
// The assertions are deliberately two-sided. "Did the image change?" is a weak
// test that a shader blurring the ENTIRE frame would pass just as happily as a
// correct one — and an over-eager post filter that softens flat areas is the
// most common way FXAA goes wrong. So these check both that a hard edge gains
// intermediate values AND that flat regions come through untouched.

#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render/passes/FXAAPass.h"

#include <vector>

using namespace MyCoreEngine;

namespace {

    constexpr int kSize = 64;

    class FXAATest : public ::testing::Test {
    protected:
        static GLFWwindow* win;
        GLuint srcTex = 0, dstTex = 0, dstFBO = 0, quadVAO = 0, quadVBO = 0;

        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "fxaa-headless", nullptr, nullptr);
            ASSERT_NE(win, nullptr);
            glfwMakeContextCurrent(win);
            ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
            // Engine.dll has its own GLAD table; FXAAPass makes its GL calls
            // from inside the DLL. (See the note in test_ibl.cpp.)
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
        }
        static void TearDownTestSuite() {
            if (win) glfwDestroyWindow(win);
            glfwTerminate();
            win = nullptr;
        }

        // A SHALLOW-SLOPED edge, not an axis-aligned one.
        //
        // That distinction is the whole point: a perfectly vertical edge lands
        // exactly on a pixel boundary, so there is no sub-pixel coverage left
        // to recover and FXAA correctly leaves it alone. What FXAA exists to
        // fix is the STAIRCASE a sloped edge produces — here one 4px-wide step
        // per row of slope. Testing the axis-aligned case instead just asserts
        // that a correct implementation is broken.
        static int edgeRowAt(int x) { return kSize / 2 + x / 4; }

        void makeSource() {
            std::vector<unsigned char> px(kSize * kSize * 4, 0);
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const unsigned char v = (y > edgeRowAt(x)) ? 255 : 0;
                    const size_t i = (size_t(y) * kSize + x) * 4;
                    px[i + 0] = px[i + 1] = px[i + 2] = v;
                    px[i + 3] = 255;
                }
            }
            glGenTextures(1, &srcTex);
            glBindTexture(GL_TEXTURE_2D, srcTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, px.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        void makeTarget() {
            glGenTextures(1, &dstTex);
            glBindTexture(GL_TEXTURE_2D, dstTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenFramebuffers(1, &dstFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, dstTex, 0);
            ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER),
                      (GLenum)GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void makeQuad() {
            const float quad[] = {
                -1.f,-1.f, 0.f,0.f,  1.f,-1.f, 1.f,0.f,  1.f,1.f, 1.f,1.f,
                -1.f,-1.f, 0.f,0.f,  1.f, 1.f, 1.f,1.f, -1.f,1.f, 0.f,1.f,
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

        void SetUp() override {
            makeSource();
            makeTarget();
            makeQuad();
        }
        void TearDown() override {
            if (srcTex) glDeleteTextures(1, &srcTex);
            if (dstTex) glDeleteTextures(1, &dstTex);
            if (dstFBO) glDeleteFramebuffers(1, &dstFBO);
            if (quadVBO) glDeleteBuffers(1, &quadVBO);
            if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
            srcTex = dstTex = dstFBO = quadVBO = quadVAO = 0;
        }

        std::vector<unsigned char> runPass(bool enabled) {
            PassContext ctx{};
            ctx.defaultFBO = dstFBO;
            ctx.ldrTex_A = srcTex;       // the chain source (tonemap's output)
            ctx.postSrcTex = srcTex;     // what nextPostTarget() hands the pass
            ctx.postPassesLeft = 1;      // FXAA is the sole/final pass -> defaultFBO
            ctx.fsQuadVAO = quadVAO;

            FrameParams fp{};
            fp.viewportW = kSize;
            fp.viewportH = kSize;

            FXAAPass pass;
            pass.setup(ctx);

            // Clear to mid-grey so "the pass did nothing" is distinguishable
            // from "the pass wrote black".
            glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
            glViewport(0, 0, kSize, kSize);
            glClearColor(0.5f, 0.5f, 0.5f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);

            Scene scene;
            scene.SetAAEnabled(enabled); // FXAA now gates on the scene AA toggle
            Camera cam;
            pass.execute(ctx, scene, cam, fp);

            std::vector<unsigned char> out(kSize * kSize * 4, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
            glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return out;
        }

        static unsigned char at(const std::vector<unsigned char>& px, int x, int y) {
            return px[(size_t(y) * kSize + x) * 4];
        }
    };

    GLFWwindow* FXAATest::win = nullptr;

} // namespace

TEST_F(FXAATest, SoftensAStaircasedEdge) {
    const auto before = runPass(false); // pass disabled: source is untouched
    const auto out = runPass(true);

    // The source is pure black or pure white everywhere. After FXAA the
    // staircase must carry intermediate values.
    int intermediate = 0;
    for (int x = 4; x < kSize - 4; ++x) {
        for (int dy = -2; dy <= 2; ++dy) {
            const int y = edgeRowAt(x) + dy;
            if (y < 1 || y >= kSize - 1) continue;
            const unsigned char v = at(out, x, y);
            if (v > 20 && v < 235) { ++intermediate; break; }
        }
    }
    EXPECT_GT(intermediate, 10)
        << "the staircase was not antialiased (only " << intermediate
        << " softened columns)";
}

TEST_F(FXAATest, LeavesFlatRegionsAlone) {
    const auto out = runPass(true);

    // Far from the edge the image must survive untouched. A filter that
    // smooths here is softening the whole frame, which reads as "the game
    // looks blurry now" and is the usual way FXAA is mis-tuned. This is the
    // half of the contract a naive "did the image change?" test misses.
    for (int x = 4; x < kSize - 4; ++x) {
        const int e = edgeRowAt(x);
        for (int y = 4; y < e - 6; ++y) {
            ASSERT_LE(at(out, x, y), 4) << "flat black modified at " << x << "," << y;
        }
        for (int y = e + 7; y < kSize - 4; ++y) {
            ASSERT_GE(at(out, x, y), 251) << "flat white modified at " << x << "," << y;
        }
    }
}

TEST_F(FXAATest, DisabledPassLeavesTheTargetUntouched) {
    const auto out = runPass(false);

    // With post-AA off, tonemap wrote the output directly and this pass must
    // not run -- reading an LDR texture nobody filled would show as a black
    // or garbage frame. The mid-grey clear must survive.
    EXPECT_NEAR(at(out, 4, 4), 128, 2);
    EXPECT_NEAR(at(out, kSize - 4, kSize - 4), 128, 2);
}

TEST_F(FXAATest, EdgeStaysInTheRightPlace) {
    const auto out = runPass(true);

    // Antialiasing must soften the transition, not MOVE it. A sign error in
    // the blend offset shifts the whole edge while still producing perfectly
    // plausible-looking gradients.
    const int x = kSize / 2;
    const int e = edgeRowAt(x);
    EXPECT_LE(at(out, x, e - 10), 4) << "edge drifted upward";
    EXPECT_GE(at(out, x, e + 10), 251) << "edge drifted downward";
}
