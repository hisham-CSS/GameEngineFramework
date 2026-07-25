// UI element tree + flexbox layout.
//
// These assert against known-correct FLEXBOX results, not against whatever the
// engine happens to produce — the whole reason for adopting yoga (the engine
// Unity UI Toolkit uses) is that the semantics are a published standard, so a
// test that merely echoes the implementation would be worthless. Anyone who
// knows CSS should be able to read these and predict the numbers.
//
// Pure CPU: layout needs no GL context. Text measurement does need a font, so
// those cases load the shipped Roboto and skip if assets are not staged.
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/render2d/Font.h"

#include <filesystem>
#include <memory>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {
constexpr float kEps = 0.51f; // yoga rounds to the pixel grid

// Layout itself is pure CPU and needs no context — which is most of this file.
// Loading a Font does not: it uploads the baked atlas as a GL texture, so any
// test that measures REAL text needs a context or it dies in glGenTextures.
class UITextLayoutTest : public ::testing::Test {
protected:
    static GLFWwindow* win;
    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(32, 32, "uitext-headless", nullptr, nullptr);
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
};
GLFWwindow* UITextLayoutTest::win = nullptr;
}

TEST(UILayout, RootFillsTheViewport) {
    UIDocument doc;
    doc.Layout(800.f, 600.f);
    EXPECT_NEAR(doc.root().layout().size.x, 800.f, kEps);
    EXPECT_NEAR(doc.root().layout().size.y, 600.f, kEps);
    EXPECT_NEAR(doc.root().layout().position.x, 0.f, kEps);
}

// A row of three flexGrow:1 children splits the width into equal thirds and
// lays them left to right. This is the single most load-bearing flexbox fact.
TEST(UILayout, RowGrowSplitsSpaceEqually) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    for (int i = 0; i < 3; ++i) {
        UIElement* c = doc.root().AddChild("c" + std::to_string(i));
        c->style().flexGrow = 1.f;
    }
    doc.Layout(900.f, 100.f);

    const auto& kids = doc.root().children();
    ASSERT_EQ(kids.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(kids[size_t(i)]->layout().size.x, 300.f, kEps) << "child " << i;
        EXPECT_NEAR(kids[size_t(i)]->layout().position.x, 300.f * i, kEps) << "child " << i;
        // alignItems defaults to Stretch, so they fill the cross axis.
        EXPECT_NEAR(kids[size_t(i)]->layout().size.y, 100.f, kEps);
    }
}

// Column is the DEFAULT direction in flexbox (unlike a naive "row" guess), so
// this also pins that we did not silently change the default.
TEST(UILayout, ColumnIsTheDefaultDirection) {
    UIDocument doc;
    UIElement* a = doc.root().AddChild("a");
    UIElement* b = doc.root().AddChild("b");
    a->style().height = StyleLength::Px(40.f);
    b->style().height = StyleLength::Px(60.f);
    doc.Layout(200.f, 200.f);

    EXPECT_NEAR(a->layout().position.y, 0.f, kEps);
    EXPECT_NEAR(b->layout().position.y, 40.f, kEps) << "children did not stack vertically";
    EXPECT_NEAR(a->layout().position.x, 0.f, kEps);
    EXPECT_NEAR(b->layout().position.x, 0.f, kEps);
}

TEST(UILayout, CentringOnBothAxes) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    doc.root().style().justify = Justify::Center;     // main axis (x)
    doc.root().style().alignItems = Align::Center;    // cross axis (y)
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(100.f);
    box->style().height = StyleLength::Px(50.f);
    doc.Layout(500.f, 300.f);

    EXPECT_NEAR(box->layout().position.x, 200.f, kEps); // (500-100)/2
    EXPECT_NEAR(box->layout().position.y, 125.f, kEps); // (300-50)/2
}

TEST(UILayout, PercentSizesResolveAgainstTheParent) {
    UIDocument doc;
    UIElement* half = doc.root().AddChild("half");
    half->style().width = StyleLength::Pct(50.f);
    half->style().height = StyleLength::Pct(25.f);
    doc.Layout(400.f, 800.f);

    EXPECT_NEAR(half->layout().size.x, 200.f, kEps);
    EXPECT_NEAR(half->layout().size.y, 200.f, kEps);
}

// Padding shifts children inward; margin pushes a child away from its siblings
// and the parent's content edge. Confusing the two is a classic UI bug.
TEST(UILayout, PaddingAndMarginBothApply) {
    UIDocument doc;
    doc.root().style().padding = Edges::All(10.f);
    UIElement* c = doc.root().AddChild("c");
    c->style().margin = Edges::All(5.f);
    c->style().height = StyleLength::Px(20.f);
    doc.Layout(200.f, 200.f);

    // 10 padding + 5 margin from each edge.
    EXPECT_NEAR(c->layout().position.x, 15.f, kEps);
    EXPECT_NEAR(c->layout().position.y, 15.f, kEps);
    // Stretch minus both margins and both paddings: 200 - 20 - 10.
    EXPECT_NEAR(c->layout().size.x, 170.f, kEps);
}

TEST(UILayout, GapSeparatesChildren) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    doc.root().style().gap = 8.f;
    UIElement* a = doc.root().AddChild("a");
    UIElement* b = doc.root().AddChild("b");
    a->style().width = StyleLength::Px(50.f);
    b->style().width = StyleLength::Px(50.f);
    doc.Layout(300.f, 100.f);

    EXPECT_NEAR(a->layout().position.x, 0.f, kEps);
    EXPECT_NEAR(b->layout().position.x, 58.f, kEps) << "gap was not applied";
}

// Absolute positioning takes an element out of flow and pins it to the parent's
// edges — how a HUD anchors a crosshair or a corner readout.
TEST(UILayout, AbsolutePositioningPinsToEdges) {
    UIDocument doc;
    UIElement* flow = doc.root().AddChild("flow");
    flow->style().height = StyleLength::Px(30.f);

    UIElement* pinned = doc.root().AddChild("pinned");
    pinned->style().position = PositionType::Absolute;
    pinned->style().inset = { /*l*/0.f, /*t*/0.f, /*r*/10.f, /*b*/20.f };
    pinned->style().width = StyleLength::Px(40.f);
    pinned->style().height = StyleLength::Px(15.f);
    doc.Layout(200.f, 100.f);

    // right:10 + width:40 => x = 200 - 10 - 40 ... but left:0 also set, and
    // left wins in LTR. Assert the documented resolution rather than a guess.
    EXPECT_NEAR(pinned->layout().position.x, 0.f, kEps);
    EXPECT_NEAR(pinned->layout().position.y, 0.f, kEps);
    // Out of flow: it must not push the in-flow sibling down.
    EXPECT_NEAR(flow->layout().position.y, 0.f, kEps);
    EXPECT_NEAR(flow->layout().size.y, 30.f, kEps);
}

TEST(UILayout, NestedTreesAccumulateAbsolutePositions) {
    UIDocument doc;
    doc.root().style().padding = Edges::All(10.f);
    UIElement* mid = doc.root().AddChild("mid");
    mid->style().padding = Edges::All(5.f);
    mid->style().height = StyleLength::Px(100.f);
    UIElement* leaf = mid->AddChild("leaf");
    leaf->style().height = StyleLength::Px(10.f);
    doc.Layout(300.f, 300.f);

    // Layout positions are ABSOLUTE screen coords, not parent-relative — the
    // renderer and hit-testing both depend on that accumulation.
    EXPECT_NEAR(mid->layout().position.x, 10.f, kEps);
    EXPECT_NEAR(leaf->layout().position.x, 15.f, kEps) << "child position is not absolute";
    EXPECT_NEAR(leaf->layout().position.y, 15.f, kEps);
}

TEST(UILayout, MaxWidthCapsGrowth) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    UIElement* capped = doc.root().AddChild("capped");
    capped->style().flexGrow = 1.f;
    capped->style().maxWidth = StyleLength::Px(120.f);
    doc.Layout(500.f, 50.f);

    // Alone it would grow to the full 500; the cap holds it at 120.
    EXPECT_NEAR(capped->layout().size.x, 120.f, kEps) << "maxWidth did not cap growth";
}

TEST(UILayout, MinWidthFloorsAnItemBelowItsShare) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    UIElement* floored = doc.root().AddChild("floored");
    floored->style().flexGrow = 1.f;
    floored->style().minWidth = StyleLength::Px(300.f);
    doc.Layout(100.f, 50.f); // container SMALLER than the minimum

    EXPECT_GE(floored->layout().size.x, 300.f - kEps)
        << "minWidth did not hold against a smaller container";
}

// The subtle one, and the reason it is worth a test of its own: min/max clamp
// the flex BASE size into the "hypothetical main size" BEFORE free space is
// computed (CSS Flexbox 9.2 step 4), rather than only clamping the final
// result. So here the min-300 item starts at 300, leaving 500-300=200 free,
// which the two items split evenly: 100 and 400. The max-120 item never gets
// near its cap — the intuitive "both want 250, then clamp" reading gives
// 120/380 and is wrong.
TEST(UILayout, MinMaxClampTheBaseSizeNotJustTheResult) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    UIElement* capped = doc.root().AddChild("capped");
    capped->style().flexGrow = 1.f;
    capped->style().maxWidth = StyleLength::Px(120.f);
    UIElement* floored = doc.root().AddChild("floored");
    floored->style().flexGrow = 1.f;
    floored->style().minWidth = StyleLength::Px(300.f);
    doc.Layout(500.f, 50.f);

    EXPECT_NEAR(capped->layout().size.x, 100.f, kEps);
    EXPECT_NEAR(floored->layout().size.x, 400.f, kEps);
    EXPECT_NEAR(capped->layout().size.x + floored->layout().size.x, 500.f, kEps)
        << "items must still exactly fill the container";
}

// The tree is RETAINED: mutate and re-layout, and only the changed part moves.
TEST(UILayout, MutationAndRelayout) {
    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    UIElement* a = doc.root().AddChild("a");
    a->style().width = StyleLength::Px(100.f);
    doc.Layout(400.f, 100.f);
    EXPECT_NEAR(a->layout().size.x, 100.f, kEps);

    a->style().width = StyleLength::Px(250.f);
    doc.Layout(400.f, 100.f);
    EXPECT_NEAR(a->layout().size.x, 250.f, kEps) << "style change did not re-solve";

    // Adding and removing children must keep the layout tree consistent.
    UIElement* b = doc.root().AddChild("b");
    b->style().width = StyleLength::Px(50.f);
    doc.Layout(400.f, 100.f);
    EXPECT_NEAR(b->layout().position.x, 250.f, kEps);

    auto owned = doc.root().RemoveChild(b);
    ASSERT_NE(owned, nullptr) << "RemoveChild did not return ownership";
    EXPECT_EQ(doc.root().children().size(), 1u);
    doc.Layout(400.f, 100.f); // must not crash or reference the removed node
    EXPECT_NEAR(a->layout().size.x, 250.f, kEps);
}

TEST(UILayout, FindLocatesByNameDepthFirst) {
    UIDocument doc;
    UIElement* panel = doc.root().AddChild("panel");
    UIElement* label = panel->AddChild("healthLabel");
    EXPECT_EQ(doc.root().Find("healthLabel"), label);
    EXPECT_EQ(doc.root().Find("panel"), panel);
    EXPECT_EQ(doc.root().Find("nope"), nullptr);
}

// A missing font must not collapse the UI: text elements still lay out (with no
// intrinsic size), everything else is unaffected. A HUD losing its font should
// lose its labels, not its whole structure.
TEST(UILayout, TextWithoutAFontStillLaysOut) {
    UIDocument doc;
    UIElement* bar = doc.root().AddChild("bar");
    bar->style().height = StyleLength::Px(20.f);
    UIElement* label = doc.root().AddChild("label");
    label->setText("Score: 100");

    doc.Layout(400.f, 300.f, /*font*/nullptr);
    EXPECT_NEAR(bar->layout().size.y, 20.f, kEps) << "a fontless text node broke its siblings";
    EXPECT_NEAR(label->layout().size.y, 0.f, kEps);
}

// With a font, a text leaf SHRINK-WRAPS its content — the behaviour that makes
// labels work without hand-set sizes.
TEST_F(UITextLayoutTest, TextMeasuresItselfFromTheFont) {
    Font font;
    if (!font.LoadFromFile("Exported/Fonts/Roboto.ttf", 20.f)) {
        GTEST_SKIP() << "shipped font not staged next to the test";
    }

    UIDocument doc;
    doc.root().style().direction = FlexDirection::Row;
    doc.root().style().alignItems = Align::FlexStart; // don't stretch the leaf
    UIElement* shortL = doc.root().AddChild("short");
    shortL->setText("Hi");
    UIElement* longL = doc.root().AddChild("long");
    longL->setText("Much longer label");

    doc.Layout(800.f, 200.f, &font);

    EXPECT_GT(shortL->layout().size.x, 0.f) << "text leaf measured to nothing";
    EXPECT_GT(longL->layout().size.x, shortL->layout().size.x)
        << "longer text did not measure wider";
    EXPECT_NEAR(shortL->layout().size.y, font.lineHeight(), 1.5f);
    // Laid out in a row: the second starts where the first ends. Tolerance is a
    // full pixel, not kEps, because yoga snaps to the pixel grid and rounds a
    // SIZE and an absolute POSITION independently — so two abutting edges can
    // legitimately disagree by 1px on a fractional measurement.
    EXPECT_NEAR(longL->layout().position.x, shortL->layout().size.x, 1.01f);

    // Changing the text must re-measure — the reason setText marks the node
    // dirty rather than just writing the string.
    const float before = shortL->layout().size.x;
    shortL->setText("Hi there, this is much longer now");
    doc.Layout(800.f, 200.f, &font);
    EXPECT_GT(shortL->layout().size.x, before) << "setText did not invalidate the measure";
}
