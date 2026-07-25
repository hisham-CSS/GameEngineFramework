// Font atlas baking + text layout (the 2D layer's text half).
//
// FONT SOURCE: the engine ships no .ttf yet (picking one is a licensing
// decision), so these tests bake a font found on the host and SKIP with a clear
// message when none is present. That keeps the suite honest on a bare CI box
// instead of silently passing — and the pure-logic tests (UTF-8 decoding,
// invalid-input handling) run everywhere because they need no font at all.
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render2d/Font.h"
#include "../Engine/src/render2d/Renderer2D.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

// First readable candidate wins. Windows and the usual Linux distro paths.
std::string findSystemFont() {
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    std::error_code ec;
    for (const char* p : candidates) {
        if (std::filesystem::exists(p, ec)) return p;
    }
    return {};
}

constexpr int kW = 128, kH = 64;

class FontTest : public ::testing::Test {
protected:
    static GLFWwindow* win;
    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kW, kH, "font-headless", nullptr, nullptr);
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
};
GLFWwindow* FontTest::win = nullptr;

#define REQUIRE_FONT(pathVar)                                                  \
    const std::string pathVar = findSystemFont();                              \
    if (pathVar.empty()) GTEST_SKIP() << "no system .ttf found on this host"

} // namespace

// ---- pure logic: runs with or without a font ------------------------------

TEST(FontUTF8, DecodesAllLengthsAndDegradesOnGarbage) {
    // ASCII, 2-byte (é), 3-byte (€), 4-byte (😀)
    auto cps = Font::DecodeUTF8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    ASSERT_EQ(cps.size(), 4u);
    EXPECT_EQ(cps[0], 0x41u);
    EXPECT_EQ(cps[1], 0xE9u);     // é
    EXPECT_EQ(cps[2], 0x20ACu);   // €
    EXPECT_EQ(cps[3], 0x1F600u);  // 😀

    EXPECT_TRUE(Font::DecodeUTF8("").empty());

    // Invalid input must yield U+FFFD and KEEP GOING — dropping the rest of the
    // string on one bad byte is how mojibake turns into a blank HUD.
    auto stray = Font::DecodeUTF8("\x80" "AB");
    ASSERT_EQ(stray.size(), 3u);
    EXPECT_EQ(stray[0], 0xFFFDu);
    EXPECT_EQ(stray[1], 'A');
    EXPECT_EQ(stray[2], 'B');

    auto truncated = Font::DecodeUTF8("A\xE2\x82"); // 3-byte lead, cut short
    ASSERT_GE(truncated.size(), 1u);
    EXPECT_EQ(truncated[0], 'A');
    EXPECT_EQ(truncated.back(), 0xFFFDu);

    auto badCont = Font::DecodeUTF8("\xC3" "Z"); // lead + non-continuation
    ASSERT_EQ(badCont.size(), 2u);
    EXPECT_EQ(badCont[0], 0xFFFDu);
    EXPECT_EQ(badCont[1], 'Z');
}

TEST(FontUTF8, InvalidFontFilesFailCleanly) {
    Font f;
    EXPECT_FALSE(f.LoadFromFile("no_such_font_12345.ttf", 16.f));
    EXPECT_FALSE(f.IsValid());
    // An invalid font must still be SAFE to use, not a null-deref waiting to
    // happen: measuring returns nothing and finding a glyph returns null.
    EXPECT_EQ(f.Measure("hello").x, 0.f);
    EXPECT_EQ(f.FindGlyph('A'), nullptr);

    // A real file that is not a font (this source file) must be rejected by the
    // validity check rather than fed to the packer.
    const char* junk = "not_a_font.ttf";
    { std::ofstream o(junk, std::ios::binary); o << "this is definitely not a font"; }
    Font g;
    EXPECT_FALSE(g.LoadFromFile(junk, 16.f));
    std::remove(junk);

    Font h;
    EXPECT_FALSE(h.LoadFromFile("no_such_font_12345.ttf", 0.f)) << "zero size";
}

// ---- baking + metrics: needs a font and a GL context ----------------------

TEST_F(FontTest, BakesAnAtlasWithSaneMetrics) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f)) << "failed to bake " << path;
    EXPECT_TRUE(f.IsValid());
    EXPECT_NE(f.atlasTexture(), 0u);
    EXPECT_GT(f.atlasWidth(), 0);
    EXPECT_FLOAT_EQ(f.bakedPixelHeight(), 24.f);

    // stb's convention: ascent above the baseline is positive, descent below is
    // negative, and a line is taller than the requested pixel height is tall.
    EXPECT_GT(f.ascent(), 0.f);
    EXPECT_LT(f.descent(), 0.f);
    EXPECT_GT(f.lineHeight(), f.ascent());

    // Printable ASCII is baked; a control char is not.
    EXPECT_NE(f.FindGlyph('A'), nullptr);
    EXPECT_NE(f.FindGlyph('~'), nullptr);
    EXPECT_NE(f.FindGlyph(' '), nullptr);
    EXPECT_EQ(f.FindGlyph(0x1F600u), nullptr) << "emoji is not in the ASCII bake";

    // Space advances the pen but has no visible quad.
    const Glyph* sp = f.FindGlyph(' ');
    ASSERT_NE(sp, nullptr);
    EXPECT_GT(sp->advance, 0.f);
    EXPECT_FLOAT_EQ(sp->size.x * sp->size.y, 0.f) << "space should have no quad";
}

TEST_F(FontTest, MeasureTracksContentAndLines) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const glm::vec2 one = f.Measure("Hello");
    const glm::vec2 two = f.Measure("Hello Hello");
    EXPECT_GT(one.x, 0.f);
    EXPECT_GT(two.x, one.x) << "more text must measure wider";
    EXPECT_FLOAT_EQ(one.y, f.lineHeight()) << "single line height";

    // Empty text still occupies a line box — layout needs a stable row height.
    EXPECT_FLOAT_EQ(f.Measure("").y, f.lineHeight());
    EXPECT_FLOAT_EQ(f.Measure("").x, 0.f);

    // Newlines add lines and width is the WIDEST line, not the sum.
    const glm::vec2 multi = f.Measure("Hello\nHello Hello");
    EXPECT_FLOAT_EQ(multi.y, f.lineHeight() * 2.f);
    EXPECT_FLOAT_EQ(multi.x, two.x);

    // Scale is linear in both axes.
    const glm::vec2 half = f.Measure("Hello", 0.5f);
    EXPECT_NEAR(half.x, one.x * 0.5f, 0.01f);
    EXPECT_NEAR(half.y, one.y * 0.5f, 0.01f);
}

// Text must actually reach the framebuffer, sit INSIDE its box (the pos is the
// box top-left, not the baseline), and batch as ordinary sprites.
TEST_F(FontTest, DrawsInsideItsBoxAndBatches) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    GLuint tex = 0, fbo = 0;
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

    Renderer2D r2d;
    ASSERT_TRUE(r2d.Init());
    r2d.BeginScreen(kW, kH);
    r2d.DrawText(f, "Hello", { 4.f, 4.f }, { 1, 1, 1, 1 });
    r2d.End();

    std::vector<unsigned char> px(size_t(kW) * kH * 4, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    const glm::vec2 m = f.Measure("Hello");
    int lit = 0, litOutsideBox = 0;
    for (int yGL = 0; yGL < kH; ++yGL) {
        for (int x = 0; x < kW; ++x) {
            if (px[(size_t(yGL) * kW + x) * 4] < 40) continue;
            ++lit;
            const int yTop = kH - 1 - yGL;               // to top-left space
            const bool inBox = (x >= 3 && float(x) <= 4.f + m.x + 1.f &&
                                yTop >= 3 && float(yTop) <= 4.f + m.y + 1.f);
            if (!inBox) ++litOutsideBox;
        }
    }
    EXPECT_GT(lit, 20) << "no text was rasterised";
    EXPECT_EQ(litOutsideBox, 0)
        << "glyphs fell outside the measured box — pos must be the box top-left "
           "(one ascent above the baseline), and Measure must match what is drawn";

    // All glyphs share the atlas, so a whole string is ONE draw call.
    EXPECT_EQ(r2d.stats().drawCalls, 1) << "text did not batch into one draw call";

    r2d.Shutdown();
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
}
