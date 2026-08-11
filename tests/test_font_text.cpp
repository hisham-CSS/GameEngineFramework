// Font atlas baking + text layout (the 2D layer's text half).
//
// FONT SOURCE: the engine ships Roboto (staged to Exported/Fonts/Roboto.ttf),
// so these tests normally exercise the FONT THE ENGINE ACTUALLY USES — which
// makes them a real check that the vendored file bakes, not just that some
// system font does. Host fonts are only a fallback, and the tests SKIP with a
// clear message if neither exists, rather than passing vacuously. The
// pure-logic tests (UTF-8 decoding, invalid input) need no font and run
// everywhere.
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/render2d/Font.h"
#include "../Engine/src/render2d/Renderer2D.h"
#include "../Engine/src/ui/UIElement.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

// First readable candidate wins. The ENGINE'S OWN font comes first so the tests
// verify the file we actually ship; host fonts are only a fallback for a build
// tree where assets have not been staged.
std::string findSystemFont() {
    const char* candidates[] = {
        "Exported/Fonts/Roboto.ttf",
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


// ---- word wrap ------------------------------------------------------------
//
// Pure arithmetic over glyph advances, but it still needs a REAL baked font:
// the break points depend on the actual widths, and a stub with uniform
// advances would agree with any implementation, including a wrong one.

namespace {

// The wrapped lines as strings, which is what an assertion wants to read.
std::vector<std::string> wrapped(const Font& f, const std::string& s,
                                 float maxW, float scale = 1.0f) {
    std::vector<Font::Line> lines;
    f.WrapLines(s, maxW, scale, lines);
    std::vector<std::string> out;
    for (const Font::Line& l : lines) out.push_back(s.substr(l.begin, l.end - l.begin));
    return out;
}

} // namespace

TEST_F(FontTest, WrapWithNoLimitIsExactlyTheUnwrappedMeasure) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "the quick brown fox";
    const auto lines = wrapped(f, s, 0.0f);
    ASSERT_EQ(lines.size(), 1u) << "an unbounded wrap broke a line anyway";
    EXPECT_EQ(lines[0], s);

    const glm::vec2 a = f.Measure(s);
    const glm::vec2 b = f.MeasureWrapped(s, 0.0f);
    EXPECT_NEAR(a.x, b.x, 0.01f);
    EXPECT_NEAR(a.y, b.y, 0.01f);
}

TEST_F(FontTest, WrapBreaksAtSpacesAndDropsThem) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    // Wide enough for two short words, not three.
    const float w = f.Measure("quick brown ").x;
    const auto lines = wrapped(f, "the quick brown fox jumps", w);
    ASSERT_GE(lines.size(), 2u) << "nothing wrapped at all";

    for (const std::string& l : lines) {
        EXPECT_FALSE(l.empty()) << "an empty line came out of a wrap";
        EXPECT_NE(l.front(), ' ') << "a line began with the space it broke on: '" << l << "'";
        EXPECT_NE(l.back(), ' ')  << "a line kept the space it broke on: '" << l << "'";
    }

    // Nothing is lost: the words, in order, are the words that went in.
    std::string joined;
    for (const std::string& l : lines) { if (!joined.empty()) joined += ' '; joined += l; }
    EXPECT_EQ(joined, "the quick brown fox jumps") << "wrapping dropped or duplicated text";
}

TEST_F(FontTest, EveryWrappedLineActuallyFits) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "wrapping has exactly one job and this is it";
    for (float w : { 60.f, 100.f, 180.f, 400.f }) {
        std::vector<Font::Line> lines;
        f.WrapLines(s, w, 1.0f, lines);
        for (const Font::Line& l : lines) {
            // A line may exceed the limit ONLY when it is a single unbreakable
            // glyph -- which this sentence has none of.
            EXPECT_LE(l.width, w + 0.5f)
                << "a line overflowed a " << w << "px limit: '"
                << s.substr(l.begin, l.end - l.begin) << "'";
            // ...and the recorded width is the measured width.
            EXPECT_NEAR(l.width, f.Measure(s.substr(l.begin, l.end - l.begin)).x, 0.5f);
        }
    }
}

// A word longer than the whole line is BROKEN, not allowed to overflow --
// DrawText walks the pen with no clipping, so an unbroken one paints across
// everything to its right.
TEST_F(FontTest, AWordLongerThanTheLineIsBrokenMidWord) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "Exported/Model/a_very_long_asset_path.obj";
    const float w = f.Measure("Exported").x;
    const auto lines = wrapped(f, s, w);
    ASSERT_GT(lines.size(), 1u) << "a too-long word was not broken";

    std::string joined;
    for (const std::string& l : lines) joined += l;
    EXPECT_EQ(joined, s) << "breaking mid-word lost or duplicated bytes";
}

// An authored newline is an instruction, not a hint.
TEST_F(FontTest, AnExplicitNewlineAlwaysBreaksEvenWhenItWouldFit) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const auto lines = wrapped(f, "a\nb", 10000.0f);
    ASSERT_EQ(lines.size(), 2u) << "an explicit newline was ignored";
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");

    // ...and a trailing one leaves a real blank line, so the paragraph is as
    // tall as it was written.
    const auto trailing = wrapped(f, "a\n", 10000.0f);
    ASSERT_EQ(trailing.size(), 2u);
    EXPECT_EQ(trailing[1], "");
    EXPECT_NEAR(f.MeasureWrapped("a\n", 0.0f).y, f.lineHeight() * 2.0f, 0.01f);
}

TEST_F(FontTest, WrappedHeightGrowsWithTheLineCountAndWidthIsTheWidestLine) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "one two three four five six seven eight";
    const float wide = f.Measure(s).x;
    const glm::vec2 unwrapped = f.MeasureWrapped(s, 0.0f);
    const glm::vec2 narrow = f.MeasureWrapped(s, wide * 0.34f);

    EXPECT_GT(narrow.y, unwrapped.y) << "wrapping did not make it taller";
    EXPECT_LT(narrow.x, unwrapped.x) << "wrapping did not make it narrower";
    EXPECT_LE(narrow.x, wide * 0.34f + 0.5f) << "the widest line overflowed";
    // Height is a whole number of lines, always.
    const float lines = narrow.y / f.lineHeight();
    EXPECT_NEAR(lines, std::round(lines), 0.001f);
}

// The empty string keeps its stable row height, wrapped or not -- layout wants
// a line box for a label whose binding has not arrived yet.
TEST_F(FontTest, AnEmptyStringStillMeasuresOneLineTall) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));
    const glm::vec2 m = f.MeasureWrapped("", 100.0f);
    EXPECT_FLOAT_EQ(m.x, 0.0f);
    EXPECT_NEAR(m.y, f.lineHeight(), 0.01f);
}

// SCALE is applied to the advances, so a wrap at scale 2 breaks in the same
// places a wrap at half the width does at scale 1.
TEST_F(FontTest, WrappingRespectsTheScale) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "alpha beta gamma delta epsilon";
    const auto small = wrapped(f, s, 100.0f, 1.0f);
    const auto big = wrapped(f, s, 200.0f, 2.0f);
    EXPECT_EQ(small, big) << "the same text at twice the scale in twice the "
                             "width broke differently";
}


// ---- wrap through LAYOUT --------------------------------------------------
//
// The rest of the UI suite lays out with NO font, so text measures zero and
// wrapping is inert there. These are the only tests that put a real font into
// yoga's measure callback, which is the only place the feature can actually be
// wrong: whether a label reports the height its wrapped text needs, and whether
// it stops reporting a width wider than the box it was given.

namespace {

using MyCoreEngine::ui::UIDocument;
using MyCoreEngine::ui::UIElement;
using MyCoreEngine::ui::Style;
using MyCoreEngine::ui::StyleLength;
using MyCoreEngine::ui::WhiteSpace;

const char* kLongText =
    "wrapping exists so that a long sentence stops painting over whatever "
    "happens to be sitting to the right of it";

} // namespace

TEST_F(FontTest, ALabelWrapsInsideAFixedWidthParentAndGrowsTaller) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(200.0f);
    UIElement* label = box->AddChild("label");
    label->setText(kLongText);

    doc.Layout(800.0f, 600.0f, &f);

    const float oneLine = f.lineHeight();
    EXPECT_LE(label->layout().size.x, 200.5f)
        << "the label reported itself wider than the box it was given";
    EXPECT_GT(label->layout().size.y, oneLine * 2.0f)
        << "a long sentence in a 200px box did not wrap (height "
        << label->layout().size.y << " vs one line " << oneLine << ")";

    // ...and the height really is a whole number of lines.
    const float lines = label->layout().size.y / oneLine;
    EXPECT_NEAR(lines, std::round(lines), 0.01f);
}

// `white-space: nowrap` keeps the OLD behaviour exactly: one line, clamped to
// the offer. That is what every existing document gets by asking for it.
TEST_F(FontTest, NoWrapStaysOnOneLineAndIsClampedToTheOffer) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(200.0f);
    UIElement* label = box->AddChild("label");
    label->setText(kLongText);
    label->style().whiteSpace = WhiteSpace::NoWrap;

    doc.Layout(800.0f, 600.0f, &f);

    EXPECT_NEAR(label->layout().size.y, f.lineHeight(), 1.0f)
        << "a nowrap label took more than one line";
    EXPECT_LE(label->layout().size.x, 200.5f)
        << "a nowrap label blew out the row it was in";
}

// A label with room to spare must be UNCHANGED by the feature -- this is the
// case every shipped document is in, and the reason wrapping can be the
// default at all.
TEST_F(FontTest, TextThatAlreadyFitsMeasuresIdenticallyWrappedOrNot) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    UIDocument wrap, nowrap;
    for (int i = 0; i < 2; ++i) {
        UIDocument& d = i ? nowrap : wrap;
        UIElement* box = d.root().AddChild("box");
        box->style().width = StyleLength::Px(600.0f);
        UIElement* label = box->AddChild("label");
        label->setText("MASTER VOLUME");
        if (i) label->style().whiteSpace = WhiteSpace::NoWrap;
        d.Layout(800.0f, 600.0f, &f);
    }
    UIElement* a = wrap.root().Find("label");
    UIElement* b = nowrap.root().Find("label");
    ASSERT_TRUE(a && b);
    EXPECT_NEAR(a->layout().size.x, b->layout().size.x, 0.01f)
        << "wrapping changed the width of text that already fitted";
    EXPECT_NEAR(a->layout().size.y, b->layout().size.y, 0.01f)
        << "wrapping changed the height of text that already fitted";
}

// A TEXT FIELD never wraps, whatever the stylesheet says: UITextEdit's caret,
// selection and Up/Down are all defined over '\n' lines, so a visual break the
// model knows nothing about would put the caret on the wrong row.
TEST_F(FontTest, ATextFieldDoesNotWrapEvenWhenAskedTo) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(200.0f);
    UIElement* field = box->AddChild("field");
    field->MakeTextField().setValue(kLongText);
    field->SyncTextFromEdit();
    field->style().whiteSpace = WhiteSpace::Normal;   // asked for, ignored

    doc.Layout(800.0f, 600.0f, &f);
    EXPECT_NEAR(field->layout().size.y, f.lineHeight(), 1.0f)
        << "a field wrapped, which would put its caret on the wrong row";
}


// ---- soft hyphens and paragraph fitting -----------------------------------

namespace {

// The wrapped lines as they would be DRAWN: the source slice plus the '-' a
// hyphenated break adds, which is not in the string.
std::vector<std::string> drawn(const Font& f, const std::string& s,
                               const Font::WrapOptions& opt) {
    std::vector<Font::Line> lines;
    f.WrapLines(s, opt, lines);
    std::vector<std::string> out;
    for (const Font::Line& l : lines) {
        std::string t = s.substr(l.begin, l.end - l.begin);
        if (l.hyphen) t += '-';
        out.push_back(t);
    }
    return out;
}

// U+00AD as UTF-8.
const char* kShy = "\xC2\xAD";

} // namespace

// A soft hyphen is INVISIBLE until it is used. The atlas has no glyph for it,
// so it never reaches the pen; what it costs is only paid on the line that
// breaks there.
TEST_F(FontTest, ASoftHyphenIsInvisibleUntilTheLineBreaksAtIt) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string plain = "extraordinary";
    const std::string shy = std::string("extra") + kShy + "ordinary";

    // Wide enough for the whole word: the soft hyphen changes nothing at all.
    Font::WrapOptions wide;
    wide.maxWidthPx = 10000.0f;
    wide.softHyphens = true;
    const auto whole = drawn(f, shy, wide);
    ASSERT_EQ(whole.size(), 1u);
    EXPECT_EQ(whole[0], shy) << "an unused soft hyphen changed the line";
    EXPECT_NEAR(f.MeasureWrapped(shy, wide).x, f.Measure(plain).x, 0.01f)
        << "an unused soft hyphen took up width";
}

TEST_F(FontTest, BreakingAtASoftHyphenDrawsARealHyphen) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string shy = std::string("extra") + kShy + "ordinary";
    Font::WrapOptions opt;
    // Wide enough for BOTH halves: a limit that fits "extra-" but not
    // "ordinary" would hard-break the second line and produce three.
    opt.maxWidthPx = std::max(f.Measure("extra-").x, f.Measure("ordinary").x) + 2.0f;
    opt.softHyphens = true;

    const auto lines = drawn(f, shy, opt);
    ASSERT_EQ(lines.size(), 2u) << "it did not break at the soft hyphen";
    EXPECT_EQ(lines[0], "extra-") << "the break did not draw its hyphen";
    EXPECT_EQ(lines[1], "ordinary");

    // ...and the reported width INCLUDES that hyphen, or a right-aligned line
    // would hang off its box by one glyph.
    std::vector<Font::Line> raw;
    f.WrapLines(shy, opt, raw);
    EXPECT_NEAR(raw[0].width, f.Measure("extra-").x, 0.5f);
}

// OFF BY DEFAULT: a string carrying soft hyphens is inert until the author
// asks for them, so pasted text cannot start breaking in surprising places.
TEST_F(FontTest, SoftHyphensAreIgnoredUnlessEnabled) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string shy = std::string("extra") + kShy + "ordinary";
    Font::WrapOptions opt;
    opt.maxWidthPx = std::max(f.Measure("extra-").x, f.Measure("ordinary").x) + 2.0f;
    opt.softHyphens = false;

    const auto lines = drawn(f, shy, opt);
    for (const std::string& l : lines) {
        EXPECT_EQ(l.find('-'), std::string::npos)
            << "a hyphen appeared with soft hyphens disabled: '" << l << "'";
    }
}

// ---- balanced fitting -----------------------------------------------------

// The case the algorithm exists for: greedy leaves a single short word alone
// on the last line, balanced spreads the paragraph instead.
TEST_F(FontTest, BalancedFittingEvensOutALineGreedyLeavesNearlyEmpty) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "the quick brown fox jumps over it";
    // A width that makes greedy pack the first line and strand the tail.
    const float w = f.Measure("the quick brown fox jumps").x;

    Font::WrapOptions greedy;
    greedy.maxWidthPx = w;
    greedy.fit = Font::WrapFit::Greedy;
    Font::WrapOptions balanced = greedy;
    balanced.fit = Font::WrapFit::Balanced;

    std::vector<Font::Line> g, b;
    f.WrapLines(s, greedy, g);
    f.WrapLines(s, balanced, b);

    ASSERT_EQ(g.size(), 2u) << "the fixture no longer produces two greedy lines";
    ASSERT_EQ(b.size(), 2u) << "balancing changed the LINE COUNT, which it must not";

    // Raggedness: the spread between the longest and shortest line, last
    // line included, is what balancing is for.
    const float gSpread = std::abs(g[0].width - g[1].width);
    const float bSpread = std::abs(b[0].width - b[1].width);
    EXPECT_LT(bSpread, gSpread)
        << "balanced was no more even than greedy (" << bSpread << " vs "
        << gSpread << ")";
}

// BALANCING MUST NOT COST A LINE. Using more lines is always "more even" and
// always wrong -- it is the degenerate solution the last-line-is-free rule
// exists to forbid.
TEST_F(FontTest, BalancedNeverUsesMoreLinesThanGreedy) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s =
        "wrapping exists so that a long sentence stops painting over whatever "
        "happens to be sitting to the right of it";
    for (float w : { 80.f, 140.f, 220.f, 380.f, 700.f }) {
        Font::WrapOptions greedy;
        greedy.maxWidthPx = w;
        Font::WrapOptions balanced = greedy;
        balanced.fit = Font::WrapFit::Balanced;

        std::vector<Font::Line> g, b;
        f.WrapLines(s, greedy, g);
        f.WrapLines(s, balanced, b);
        EXPECT_EQ(b.size(), g.size())
            << "at " << w << "px balanced used " << b.size()
            << " lines where greedy used " << g.size();
        for (const Font::Line& l : b) {
            EXPECT_LE(l.width, w + 0.5f)
                << "a balanced line overflowed " << w << "px";
        }
    }
}

// Balancing preserves the text, exactly as greedy does.
TEST_F(FontTest, BalancedFittingLosesNoText) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "one two three four five six seven eight nine ten";
    Font::WrapOptions opt;
    opt.maxWidthPx = 150.0f;
    opt.fit = Font::WrapFit::Balanced;

    std::vector<Font::Line> lines;
    f.WrapLines(s, opt, lines);
    std::string joined;
    for (const Font::Line& l : lines) {
        if (!joined.empty()) joined += ' ';
        joined += s.substr(l.begin, l.end - l.begin);
    }
    EXPECT_EQ(joined, s) << "balancing dropped or duplicated text";
}

// An authored newline still bounds a paragraph: balancing may not move text
// across one, or a two-stanza label would reflow into one block.
TEST_F(FontTest, BalancingNeverMovesTextAcrossAnAuthoredNewline) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string s = "alpha beta gamma\ndelta epsilon zeta";
    Font::WrapOptions opt;
    opt.maxWidthPx = 10000.0f;      // everything fits; only the \n may break
    opt.fit = Font::WrapFit::Balanced;

    const auto lines = drawn(f, s, opt);
    ASSERT_EQ(lines.size(), 2u) << "the authored newline was lost or doubled";
    EXPECT_EQ(lines[0], "alpha beta gamma");
    EXPECT_EQ(lines[1], "delta epsilon zeta");
}


// The style properties reach the font. Without this the enums could be parsed,
// stored and never consulted -- which is exactly what a stylesheet property
// that does nothing looks like from the outside.
TEST_F(FontTest, TheStyleFlagsForHyphensAndBalanceReachTheWrapper) {
    REQUIRE_FONT(path);
    Font f;
    ASSERT_TRUE(f.LoadFromFile(path, 24.f));

    const std::string shy = std::string("extra") + kShy + "ordinary";
    const float w = std::max(f.Measure("extra-").x, f.Measure("ordinary").x) + 2.0f;

    // hyphens: none (the default) -- one long line, no break to take.
    {
        UIDocument doc;
        UIElement* box = doc.root().AddChild("box");
        box->style().width = MyCoreEngine::ui::StyleLength::Px(w);
        UIElement* l = box->AddChild("l");
        l->setText(shy);
        doc.Layout(800.f, 600.f, &f);
        EXPECT_GT(l->layout().size.y, f.lineHeight() * 1.5f)
            << "with hyphens off the word should hard-break, not fit";
    }
    // hyphens: manual -- it breaks at the soft hyphen instead, so exactly two.
    {
        UIDocument doc;
        UIElement* box = doc.root().AddChild("box");
        box->style().width = MyCoreEngine::ui::StyleLength::Px(w);
        UIElement* l = box->AddChild("l");
        l->setText(shy);
        l->style().hyphens = MyCoreEngine::ui::Hyphens::Manual;
        doc.Layout(800.f, 600.f, &f);
        EXPECT_NEAR(l->layout().size.y, f.lineHeight() * 2.0f, 1.0f)
            << "hyphens: manual did not reach the wrapper";
    }
}
