// `overflow: scroll` — content extent, the clamp, wheel routing, thumb capture.
//
// Pure CPU, no GL and no font. Three techniques make that possible, and they are
// the same ones the rest of the UI suite already uses:
//
//  - the scroll offset, the content extent and every layout().position are pure
//    output of Layout(), so long as children carry explicit px sizes and nothing
//    has to be measured from a font;
//  - CLIPPING is asserted through HitTest, which reads the same
//    `overflow != Visible` gate draw_ does — a scrolled-away child that is still
//    hittable would be a scrolled-away child that is still painted;
//  - the renderer half (that a clip rect really reaches glScissor) belongs to
//    test_renderer2d.cpp, which has a real GL fixture.
//
// The thing most of this file exists to pin is the ORDER inside Layout():
// measure and clamp from yoga's raw rects, THEN bake the offset into absolute
// positions. Measuring the other way round makes the extent shrink as you
// scroll, and the clamp converges on half the content.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIComponent.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UITextField.h"
#include "../Engine/src/ui/UIWorld.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// A 200x100 scroller holding `n` children of `childH` each. Explicit sizes
// everywhere, so the whole fixture is font-free.
struct ScrollDoc {
    UIDocument doc;
    UIElement* box = nullptr;

    explicit ScrollDoc(int n = 10, float childH = 50.f, float boxH = 100.f) {
        box = doc.root().AddChild("box");
        box->style().overflowX = box->style().overflowY = Overflow::Scroll;
        box->style().width = StyleLength::Px(200.f);
        box->style().height = StyleLength::Px(boxH);
        for (int i = 0; i < n; ++i) {
            UIElement* row = box->AddChild("row" + std::to_string(i));
            row->style().height = StyleLength::Px(childH);
            row->style().width = StyleLength::Px(180.f);
        }
        Layout();
    }
    void Layout() { doc.Layout(400.f, 400.f); }
    UIElement* row(int i) { return doc.root().Find("row" + std::to_string(i)); }

    // One frame of pointer input, then re-layout — which is what UIWorld does
    // when ConsumeScrollDirty reports a moved offset.
    void Wheel(float x, float y, glm::vec2 at = { 100.f, 50.f }) {
        UIPointerState p;
        p.position = at;
        p.inside = true;
        p.wheel = { x, y };
        doc.UpdatePointer(p);
        Layout();
    }
};

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary);
    o << text;
}

// A scroller with 300px of rows in a 100px box, authored so a UIWorld test can
// load it the way a real scene does.
const char* kListMarkup = R"(<UI>
  <Element name="box" style="overflow: scroll; width: 200px; height: 100px">
    <Element style="height: 50px"/><Element style="height: 50px"/>
    <Element style="height: 50px"/><Element style="height: 50px"/>
    <Element style="height: 50px"/><Element style="height: 50px"/>
  </Element>
</UI>)";

} // namespace

// --------------------------------------------------------------- the content

// The load-bearing mechanism. yoga's own YGOverflowScroll is layout-INERT (it
// produces byte-identical output to Visible), so what actually makes content
// exist is the engine forcing flex-shrink to 0 on a scroller's in-flow children.
// Without it flexbox squeezes ten 50px rows into a 100px box at 10px each, the
// extent equals the box, and there is nothing to scroll.
TEST(UIScroll, ScrollerChildrenKeepTheirNaturalSize) {
    ScrollDoc d;
    EXPECT_FLOAT_EQ(d.row(0)->layout().size.y, 50.f)
        << "the scroller's children were squeezed to fit — nothing to scroll";
    EXPECT_FLOAT_EQ(d.box->layout().size.y, 100.f) << "the scroller itself grew";
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 500.f);
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 400.f);
}

// A non-scroller must keep the old behaviour exactly: this is the control case
// that proves the flex-shrink override is scoped to scrollers.
TEST(UIScroll, ANonScrollerStillShrinksItsChildren) {
    ScrollDoc d;
    d.box->style().overflowX = d.box->style().overflowY = Overflow::Hidden;
    d.Layout();
    EXPECT_FLOAT_EQ(d.row(0)->layout().size.y, 10.f)
        << "flex-shrink was overridden on something that is not a scroller";
}

// Both non-zero on purpose: with either at zero the test would pass whether or
// not the other were handled.
TEST(UIScroll, ContentExtentIncludesTrailingMarginAndPadding) {
    ScrollDoc d(4, 50.f);
    d.row(3)->style().margin.bottom = 12.f;
    d.box->style().padding.bottom = 8.f;
    d.Layout();
    // 4*50 + 12 margin + 8 padding
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 220.f)
        << "the last row cannot be scrolled clear of the edge";
}

// ------------------------------------------------------------- the offset

TEST(UIScroll, OffsetMovesChildrenNotTheScroller) {
    ScrollDoc d;
    const glm::vec2 boxBefore = d.box->layout().position;
    const glm::vec2 rowBefore = d.row(2)->layout().position;

    ASSERT_TRUE(d.box->SetScrollOffset({ 0.f, 120.f }));
    d.Layout();

    EXPECT_EQ(d.box->layout().position, boxBefore) << "the scroller moved itself";
    EXPECT_FLOAT_EQ(d.row(2)->layout().position.y, rowBefore.y - 120.f);
    EXPECT_FLOAT_EQ(d.row(2)->layout().position.x, rowBefore.x) << "x drifted";
}

// The ordering test. With measure-after-offset the extent shrinks every pass and
// the offset walks toward the middle; this pins it as a fixed point.
TEST(UIScroll, ClampIsAFixedPointAcrossRepeatedLayouts) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 1e6f });     // clamps to max
    d.Layout();
    const float settled = d.box->scrollOffset().y;
    EXPECT_FLOAT_EQ(settled, 400.f);

    for (int i = 0; i < 5; ++i) d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, settled)
        << "the offset drifts when nothing is touching it — measure/readLayout "
           "are running in the wrong order";
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 500.f) << "the extent shrank as we scrolled";
}

// A binding that hides rows shrinks the content under a scrolled view. The
// offset has to come back into range on its own, or the list shows blank space
// nobody can scroll away from.
TEST(UIScroll, ShrinkingContentReclampsTheOffset) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 400.f });
    d.Layout();
    ASSERT_FLOAT_EQ(d.box->scrollOffset().y, 400.f);

    for (int i = 2; i < 10; ++i) d.row(i)->style().display = DisplayMode::None;
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 100.f);
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 0.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f) << "stranded past the end of the content";
}

// Flexbox puts OVERFLOW on the leading side when content is centred, so the top
// of the content sits at a negative offset. Clamping to [0, max] would make it
// permanently unreachable.
TEST(UIScroll, CentredOverflowIsStillReachable) {
    ScrollDoc d(4, 50.f);                     // 200 of content in a 100 box
    d.box->style().justify = Justify::Center;
    d.Layout();
    ASSERT_LT(d.row(0)->layout().position.y, d.box->layout().position.y)
        << "yoga did not centre the overflow; this test is not testing anything";

    EXPECT_LT(d.box->minScroll().y, 0.f) << "the leading overflow is unreachable";
    d.box->SetScrollOffset({ 0.f, d.box->minScroll().y });
    d.Layout();
    EXPECT_GE(d.row(0)->layout().position.y, d.box->layout().position.y - 0.5f)
        << "scrolling to the minimum did not reveal the first row";
}

// Scroll state deliberately lives OUTSIDE Style, because Recascade assigns a
// fresh Style{} on every :hover edge. In Style, hovering a row would jump the
// list back to the top.
TEST(UIScroll, OffsetSurvivesARecascade) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 200.f });
    d.Layout();

    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString("#box { overflow: scroll; width: 200px; height: 100px; }"));
    sheet.Recascade(*d.box);
    d.Layout();

    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f)
        << "a restyle reset the scroll position";
}

// The tab-switch guarantee: `if=` writes display rather than removing the
// element precisely so state survives being hidden.
TEST(UIScroll, OffsetSurvivesDisplayNone) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 250.f });
    d.Layout();

    d.box->style().display = DisplayMode::None;
    d.Layout();
    d.box->style().display = DisplayMode::Flex;
    d.Layout();

    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 250.f) << "hiding the panel lost its place";
}

// A class toggle can take `scroll` away at any moment while the offset outlives
// Style. If the offset kept applying, the children would stay displaced into a
// region nothing clips any more.
TEST(UIScroll, OffsetIsZeroedWhenOverflowStopsBeingScroll) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 200.f });
    d.Layout();
    const float scrolled = d.row(0)->layout().position.y;

    d.box->style().overflowX = d.box->style().overflowY = Overflow::Visible;
    d.Layout();
    EXPECT_GT(d.row(0)->layout().position.y, scrolled);
    EXPECT_FLOAT_EQ(d.row(0)->layout().position.y, d.box->layout().position.y)
        << "a de-scrolled element is still displacing its children";
}

// ------------------------------------------------------- absolute children

// A deliberate divergence from CSS: there is no position:fixed or sticky here,
// and scrolling an inset:0 veil away would make it stop covering AND stop
// blocking clicks.
TEST(UIScroll, AbsoluteChildIsPinnedAndIsNotContent) {
    ScrollDoc d(4, 50.f);
    UIElement* veil = d.box->AddChild("veil");
    veil->style().position = PositionType::Absolute;
    veil->style().inset = { 0.f, 0.f, 0.f, 0.f };
    UIElement* deep = d.box->AddChild("deep");
    deep->style().position = PositionType::Absolute;
    deep->style().inset = { 0.f, 900.f, 0.f, 0.f };   // way below the box
    d.Layout();

    const float extentWithAbsolutes = d.box->contentSize().y;
    EXPECT_FLOAT_EQ(extentWithAbsolutes, 200.f)
        << "an absolutely positioned child invented scroll range containing nothing";

    const glm::vec2 veilBefore = veil->layout().position;
    d.box->SetScrollOffset({ 0.f, d.box->maxScroll().y });
    d.Layout();
    EXPECT_EQ(veil->layout().position, veilBefore) << "the veil scrolled away";
    // ...and it is still where clicks land.
    EXPECT_NE(d.doc.HitTest({ 100.f, 50.f }), nullptr);
}

// ------------------------------------------------------------- clipping

// The headless proxy for clipping: hitTest_ and draw_ read the same
// `overflow != Visible` gate, so a scrolled-away row that is still hittable
// would be a scrolled-away row that is still painted.
TEST(UIScroll, ScrolledAwayChildIsNotHittable) {
    ScrollDoc d;
    // Row 0 occupies the top of the box before scrolling.
    UIElement* top = d.doc.HitTest({ 100.f, 10.f });
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->name(), "row0");

    d.box->SetScrollOffset({ 0.f, 400.f });
    d.Layout();
    // Row 0 is now 400px above the box. A point where it USED to be must not
    // reach it, and a point where it is now must not reach it either.
    UIElement* after = d.doc.HitTest({ 100.f, 10.f });
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->name(), "row0") << "a row scrolled out of view is still clickable";
    EXPECT_EQ(d.doc.HitTest(d.row(0)->layout().position + glm::vec2(5.f, 5.f)), nullptr)
        << "a row outside the scroller's box is hittable where it is drawn nowhere";
}

// ------------------------------------------------------------ wheel input

TEST(UIScroll, WheelScrollsTheHoveredScroller) {
    ScrollDoc d;
    ASSERT_FLOAT_EQ(d.box->scrollOffset().y, 0.f);
    d.Wheel(0.f, -1.f);          // one notch "down": content moves up
    EXPECT_GT(d.box->scrollOffset().y, 0.f);
    const float one = d.box->scrollOffset().y;
    d.Wheel(0.f, -1.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, one * 2.f) << "notches are not linear";

    d.Wheel(0.f, 1.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, one) << "the wheel does not go back";
}

TEST(UIScroll, WheelStopsAtTheEndsWithoutOverscrolling) {
    ScrollDoc d;
    for (int i = 0; i < 50; ++i) d.Wheel(0.f, -1.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, d.box->maxScroll().y);
    for (int i = 0; i < 50; ++i) d.Wheel(0.f, 1.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f);
}

namespace {

// An inner scroller inside an outer one, each with range of its own.
struct NestedDoc {
    UIDocument doc;
    UIElement* outer = nullptr;
    UIElement* inner = nullptr;

    NestedDoc() {
        outer = doc.root().AddChild("outer");
        outer->style().overflowX = outer->style().overflowY = Overflow::Scroll;
        outer->style().width = StyleLength::Px(200.f);
        outer->style().height = StyleLength::Px(100.f);

        inner = outer->AddChild("inner");
        inner->style().overflowX = inner->style().overflowY = Overflow::Scroll;
        inner->style().width = StyleLength::Px(180.f);
        inner->style().height = StyleLength::Px(80.f);
        for (int i = 0; i < 4; ++i) {
            inner->AddChild("r" + std::to_string(i))->style().height = StyleLength::Px(50.f);
        }
        outer->AddChild("tail")->style().height = StyleLength::Px(300.f);
        doc.Layout(400.f, 400.f);
    }
    // Aimed at wherever the inner list currently IS: scrolling the outer one
    // moves it, and a hardcoded point would silently start hitting the tail.
    void Wheel(float y) {
        UIPointerState p;
        p.position = inner->layout().position + inner->layout().size * 0.5f;
        p.inside = true;
        p.wheel = { 0.f, y };
        doc.UpdatePointer(p);
        doc.Layout(400.f, 400.f);
    }
    // Long enough with no wheel that the next notch starts a new GESTURE.
    void Pause() { doc.AdvanceTime(0.5f); }
};

} // namespace

// Chaining is safe only WITH latching, which is the whole reason the two ship
// together. Mid-flick the element that first moved keeps the wheel, even once it
// runs out — otherwise reading a list to its bottom and rolling once more would
// carry the outer panel out from under the cursor.
TEST(UIScroll, AWheelGestureDoesNotChainMidFlick) {
    NestedDoc d;
    ASSERT_GT(d.inner->maxScroll().y, 0.f);
    ASSERT_GT(d.outer->maxScroll().y, 0.f);

    d.Wheel(-1.f);                       // starts a gesture on the inner list
    ASSERT_GT(d.inner->scrollOffset().y, 0.f);
    const float outerBefore = d.outer->scrollOffset().y;

    // Keep flicking, with no pause, well past the inner list's end.
    for (int i = 0; i < 10; ++i) d.Wheel(-1.f);

    EXPECT_FLOAT_EQ(d.inner->scrollOffset().y, d.inner->maxScroll().y);
    EXPECT_FLOAT_EQ(d.outer->scrollOffset().y, outerBefore)
        << "the gesture chained to the outer panel when the inner list ran out";
}

// A NEW gesture does chain, which is what a browser does and what makes a long
// page usable when the cursor happens to rest over an exhausted inner list.
TEST(UIScroll, ANewWheelGestureChainsToTheParent) {
    NestedDoc d;
    d.inner->SetScrollOffset({ 0.f, d.inner->maxScroll().y });
    d.doc.Layout(400.f, 400.f);
    const float outerBefore = d.outer->scrollOffset().y;

    d.Pause();
    d.Wheel(-1.f);

    EXPECT_GT(d.outer->scrollOffset().y, outerBefore)
        << "a fresh gesture over an exhausted list did not reach its container";
    EXPECT_FLOAT_EQ(d.inner->scrollOffset().y, d.inner->maxScroll().y)
        << "the inner list moved when it had no room";
}

// Chaining picks the first ancestor with ROOM IN THIS DIRECTION, not merely one
// that scrolls: at the bottom of the inner list, scrolling back up must stay
// with it rather than chaining.
TEST(UIScroll, ChainingIsDirectional) {
    NestedDoc d;
    d.inner->SetScrollOffset({ 0.f, d.inner->maxScroll().y });
    // A SMALL outer offset: enough that the outer could move up if the wheel
    // wrongly chained, but not so much that it pushes the inner list out from
    // under the cursor and makes the test prove nothing.
    d.outer->SetScrollOffset({ 0.f, 20.f });
    d.doc.Layout(400.f, 400.f);
    const float outerBefore = d.outer->scrollOffset().y;
    // The wheel aims at the inner list's centre, so that point has to still be
    // inside the outer box — otherwise the outer clips it away, the hit test
    // returns nothing, and the test proves nothing.
    const float aimY = d.inner->layout().position.y + d.inner->layout().size.y * 0.5f;
    ASSERT_GT(aimY, d.outer->layout().position.y);
    ASSERT_LT(aimY, d.outer->layout().position.y + d.outer->layout().size.y);

    d.Pause();
    d.Wheel(1.f);                        // back up: the inner list has room

    EXPECT_LT(d.inner->scrollOffset().y, d.inner->maxScroll().y);
    EXPECT_FLOAT_EQ(d.outer->scrollOffset().y, outerBefore)
        << "scrolling back chained away from a list that still had room";
}

// A handler may pre-empt the wheel, exactly as it may pre-empt a key before a
// text field edits.
TEST(UIScroll, AHandlerCanClaimTheWheel) {
    ScrollDoc d;
    int seen = 0;
    d.row(0)->OnWheel([&](UIEvent& e) { ++seen; e.StopPropagation(); });
    d.Wheel(0.f, -1.f, { 100.f, 10.f });
    EXPECT_EQ(seen, 1);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f) << "StopPropagation did not suppress the scroll";
}

TEST(UIScroll, WheelCarriesItsDeltaToHandlers) {
    ScrollDoc d;
    glm::vec2 got{ 0.f };
    d.box->OnWheel([&](UIEvent& e) { got = e.delta; });
    d.Wheel(2.f, -3.f);
    EXPECT_FLOAT_EQ(got.x, 2.f);
    EXPECT_FLOAT_EQ(got.y, -3.f);
}

TEST(UIScroll, PointerEventsNoneIsNotScrollable) {
    ScrollDoc d;
    d.box->style().pickable = false;
    d.Layout();
    d.Wheel(0.f, -1.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f)
        << "an inert subtree still swallowed the wheel";
}

// ------------------------------------------------------------ scrollbars

TEST(UIScroll, ABarAppearsOnlyOnAnAxisThatScrolls) {
    ScrollDoc d;
    ASSERT_NE(d.box->scrollState(), nullptr);
    EXPECT_GT(d.box->scrollState()->thumbY.size.y, 0.f);
    EXPECT_FLOAT_EQ(d.box->scrollState()->thumbX.size.x, 0.f)
        << "a vertical list grew a horizontal scrollbar";
}

// Font::Measure accumulates fractional advances, so a row 0.4px wider than its
// box is a measurement artefact. Without a whole-pixel floor every vertical list
// would sprout a horizontal bar.
TEST(UIScroll, SubPixelOverflowIsNotAScrollableAxis) {
    ScrollDoc d(1, 50.f);
    d.row(0)->style().width = StyleLength::Px(200.4f);   // box is 200
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->maxScroll().x, 0.f);
    EXPECT_FLOAT_EQ(d.box->scrollState()->thumbX.size.x, 0.f);
}

TEST(UIScroll, ThumbTracksTheOffset) {
    ScrollDoc d;
    const float top = d.box->scrollState()->thumbY.position.y;
    d.box->SetScrollOffset({ 0.f, d.box->maxScroll().y });
    d.Layout();
    const auto& sc = *d.box->scrollState();
    EXPECT_GT(sc.thumbY.position.y, top);
    // At the end of travel the thumb's bottom edge meets the track's.
    EXPECT_NEAR(sc.thumbY.position.y + sc.thumbY.size.y,
                sc.trackY.position.y + sc.trackY.size.y, 0.5f);
}

// The bars are painted from draw_ and are invisible to hitTest_, so pressing one
// would otherwise reach the row underneath: its on-click fires, :active lights
// up, and focus leaves whatever field you were typing in.
TEST(UIScroll, ThumbPressDoesNotClickThrough) {
    ScrollDoc d;
    UIElement* row = d.row(0);
    row->setFocusable(true);
    int clicks = 0;
    row->OnClick([&](UIEvent&) { ++clicks; });

    const auto& sc = *d.box->scrollState();
    const glm::vec2 onThumb = sc.thumbY.position + sc.thumbY.size * 0.5f;

    UIPointerState p;
    p.position = onThumb;
    p.inside = true;
    p.buttonDown = true;
    d.doc.UpdatePointer(p);
    p.buttonDown = false;
    d.doc.UpdatePointer(p);

    EXPECT_EQ(clicks, 0) << "the press went through the scrollbar to the row";
    EXPECT_EQ(d.doc.focused(), nullptr) << "pressing the scrollbar moved focus";
}

TEST(UIScroll, ThumbDragMovesTheOffsetProportionally) {
    ScrollDoc d;
    const auto& sc = *d.box->scrollState();
    const float trackTop = sc.trackY.position.y;
    const float thumbH = sc.thumbY.size.y;
    const float free = sc.trackY.size.y - thumbH;
    ASSERT_GT(free, 0.f);

    UIPointerState p;
    p.inside = true;
    // Grab the thumb at its top edge...
    p.position = { sc.thumbY.position.x + 4.f, trackTop + 1.f };
    p.buttonDown = true;
    d.doc.UpdatePointer(p);
    // ...and drag halfway down the free travel.
    p.position.y = trackTop + 1.f + free * 0.5f;
    d.doc.UpdatePointer(p);
    d.Layout();

    EXPECT_NEAR(d.box->scrollOffset().y, d.box->maxScroll().y * 0.5f, 1.0f);

    p.buttonDown = false;
    d.doc.UpdatePointer(p);
    // Releasing ends the drag: moving again must not keep scrolling.
    const float after = d.box->scrollOffset().y;
    p.position.y = trackTop + free;
    d.doc.UpdatePointer(p);
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, after) << "the drag outlived the button";
}

// --------------------------------------------------------- focus follow

TEST(UIScroll, FocusIsScrolledIntoView) {
    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().overflowX = box->style().overflowY = Overflow::Scroll;
    box->style().width = StyleLength::Px(200.f);
    box->style().height = StyleLength::Px(100.f);
    for (int i = 0; i < 20; ++i) {
        UIElement* b = box->AddChild("b" + std::to_string(i));
        b->style().height = StyleLength::Px(30.f);
        b->setFocusable(true);
    }
    doc.Layout(400.f, 400.f);

    for (int i = 0; i < 10; ++i) doc.FocusNext();
    doc.Layout(400.f, 400.f);

    UIElement* f = doc.focused();
    ASSERT_NE(f, nullptr);
    const glm::vec2 lo = box->layout().position;
    const glm::vec2 hi = lo + box->layout().size;
    EXPECT_GE(f->layout().position.y, lo.y - 0.5f)
        << "Tab focused something above the visible box";
    EXPECT_LE(f->layout().position.y + f->layout().size.y, hi.y + 0.5f)
        << "Tab focused something below the visible box — a focus ring nobody can see";
}

// ------------------------------------------------------------- stylesheet

TEST(UIScroll, OverflowIsThreeValued) {
    struct Case { const char* value; Overflow want; };
    const Case cases[] = {
        { "visible", Overflow::Visible },
        { "hidden",  Overflow::Hidden  },
        { "scroll",  Overflow::Scroll  },
    };
    for (const Case& c : cases) {
        UIStyleSheet s;
        ASSERT_TRUE(s.ParseString(std::string("#e { overflow: ") + c.value + "; }"))
            << c.value << ": " << (s.errors().empty() ? "" : s.errors()[0]);
        UIElement e("e");
        s.ApplyToElement(e);
        EXPECT_EQ(e.style().overflowX, c.want) << c.value;
        EXPECT_EQ(e.style().overflowY, c.want) << c.value << " (shorthand must write BOTH axes)";
    }
}

// `auto` is RESERVED, not silently aliased onto `scroll`. A keyword can be added
// later; one whose meaning changed could never be trusted again.
TEST(UIScroll, OverflowAutoIsAReportedError) {
    UIStyleSheet s;
    EXPECT_FALSE(s.ParseString("#e { overflow: auto; }"));
    ASSERT_FALSE(s.errors().empty());
    EXPECT_NE(s.errors()[0].find("visible|hidden|scroll"), std::string::npos)
        << "the error names neither the property nor the legal words: " << s.errors()[0];
}

TEST(UIScroll, MarkupCanAuthorAScroller) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(
        <UI>
          <Element name="log" style="overflow: scroll; width: 200px; height: 100px">
            <Label style="height: 60px" text="a"/>
            <Label style="height: 60px" text="b"/>
          </Element>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    // An inline style= is REPLAYED by the cascade, not baked in at load, so a
    // sheet has to run even when it is empty — that is what makes an inline
    // style outrank every selector rule instead of being clobbered by one.
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());
    doc.Layout(400.f, 400.f);
    UIElement* log = doc.root().Find("log");
    ASSERT_NE(log, nullptr);
    EXPECT_EQ(log->style().overflowY, Overflow::Scroll);
    EXPECT_FLOAT_EQ(log->contentSize().y, 120.f);
}

TEST(UIScroll, OnWheelIsAnAuthorableEvent) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="e" on-wheel="spin"/></UI>)", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);

    UIDocument bad;
    errors.clear();
    EXPECT_FALSE(UIMarkup::LoadInto(bad,
        R"(<UI><Element on-scrollwheel="spin"/></UI>)", errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("wheel"), std::string::npos) << errors[0];
}

// --------------------------------------------------------------- UIWorld

// The pointer target and the keyboard target were one variable until scrolling
// arrived. That was harmless while the pointer only meant clicks — you had to
// click to move focus in the first place — but the wheel arrives with no click,
// so a background document holding focus would silently eat it.
TEST(UIScroll, WheelFollowsHoverNotFocus) {
    const std::string fieldDoc = "test_uiscroll_field.cxml";
    const std::string listDoc = "test_uiscroll_list.cxml";
    writeFile(fieldDoc, R"(<UI><TextField name="f" style="width: 80px; height: 24px"/></UI>)");
    writeFile(listDoc, kListMarkup);

    Scene scene;
    UIWorld world;

    const entt::entity a = scene.registry.create();
    { UIDocumentComponent ud; ud.markup = fieldDoc; ud.sortOrder = 0;
      scene.registry.emplace<UIDocumentComponent>(a, ud); }
    const entt::entity b = scene.registry.create();
    { UIDocumentComponent ud; ud.markup = listDoc; ud.sortOrder = 1;
      scene.registry.emplace<UIDocumentComponent>(b, ud); }

    world.Update(scene.registry, 400, 400, 0.016f);
    ASSERT_NE(world.document(a), nullptr);
    ASSERT_NE(world.document(b), nullptr);

    // Document A holds focus...
    UIDocument& da = world.document(a)->document();
    da.SetFocus(da.root().Find("f"));
    ASSERT_NE(da.focused(), nullptr)
        << "A must hold focus for this test to mean anything";

    UIElement* box = world.document(b)->document().root().Find("box");
    ASSERT_NE(box, nullptr);

    // ...while the pointer is over B's scroller.
    UIPointerState p;
    p.position = { 100.f, 50.f };
    p.inside = true;
    p.wheel = { 0.f, -2.f };
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);

    EXPECT_GT(box->scrollOffset().y, 0.f)
        << "the wheel went to the focused document instead of the hovered one";

    std::remove(fieldDoc.c_str());
    std::remove(listDoc.c_str());
}

// The wheel is a per-frame DELTA inside an otherwise level-triggered struct, so
// it must be consumed exactly once. A host that updates without a matching
// SetPointer would otherwise replay one flick forever.
TEST(UIScroll, WheelIsConsumedExactlyOnce) {
    const std::string m = "test_uiscroll_once.cxml";
    writeFile(m, kListMarkup);

    Scene scene;
    UIWorld world;
    const entt::entity e = scene.registry.create();
    { UIDocumentComponent ud; ud.markup = m;
      scene.registry.emplace<UIDocumentComponent>(e, ud); }
    world.Update(scene.registry, 400, 400, 0.016f);
    ASSERT_NE(world.document(e), nullptr);
    UIElement* box = world.document(e)->document().root().Find("box");
    ASSERT_NE(box, nullptr);
    ASSERT_GT(box->maxScroll().y, 0.f);

    UIPointerState p;
    p.position = { 100.f, 50.f };
    p.inside = true;
    p.wheel = { 0.f, -1.f };
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);
    const float after = box->scrollOffset().y;
    ASSERT_GT(after, 0.f);

    world.Update(scene.registry, 400, 400, 0.016f);   // no new SetPointer
    EXPECT_FLOAT_EQ(box->scrollOffset().y, after) << "one notch scrolled twice";

    std::remove(m.c_str());
}

// SetFocus defers its scroll-into-view to the next Layout, because
// ScrollIntoView needs absolute rects and two focus moves in one frame would
// have the second computing from rects the first invalidated. That deferral
// means a focus request can outlive the tree it named: click a button, save the
// markup, and the hot reload frees every element before the reveal runs.
//
// This crashed intermittently — freed memory sometimes still reads plausibly,
// which is exactly why it needs a test rather than a bug report.
TEST(UIScroll, APendingFocusRevealSurvivesTheTreeBeingRebuilt) {
    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().overflowX = box->style().overflowY = Overflow::Scroll;
    box->style().width = StyleLength::Px(200.f);
    box->style().height = StyleLength::Px(100.f);
    for (int i = 0; i < 8; ++i) {
        UIElement* b = box->AddChild("b" + std::to_string(i));
        b->style().height = StyleLength::Px(40.f);
        b->setFocusable(true);
    }
    doc.Layout(400.f, 400.f);

    // Focus something far down, so a reveal is genuinely pending...
    doc.SetFocus(doc.root().Find("b7"));
    ASSERT_NE(doc.focused(), nullptr);

    // ...then rebuild the tree before the next Layout, exactly as a hot reload
    // does. Every element the focus request named is now freed.
    doc.root().ClearChildren();
    doc.Layout(400.f, 400.f);        // must not read freed memory

    EXPECT_EQ(doc.focused(), nullptr) << "focus survived the tree that held it";
    SUCCEED();
}

// ===================================================== per-axis overflow

TEST(UIScroll, OverflowShorthandWritesBothAxes) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString("#e { overflow: scroll; }"));
    UIElement e("e");
    s.ApplyToElement(e);
    EXPECT_EQ(e.style().overflowX, Overflow::Scroll);
    EXPECT_EQ(e.style().overflowY, Overflow::Scroll);
}

// Source order within a rule decides, exactly as in CSS. This falls out of
// ApplyToElement replaying a rule's declarations in parse order — the test
// exists to pin that it IS modelled, because the shorthand relies on it.
TEST(UIScroll, LonghandAndShorthandResolveBySourceOrder) {
    {
        UIStyleSheet s;
        ASSERT_TRUE(s.ParseString("#e { overflow: hidden; overflow-y: scroll; }"));
        UIElement e("e"); s.ApplyToElement(e);
        EXPECT_EQ(e.style().overflowX, Overflow::Hidden);
        EXPECT_EQ(e.style().overflowY, Overflow::Scroll);
    }
    {
        UIStyleSheet s;
        ASSERT_TRUE(s.ParseString("#e { overflow-y: scroll; overflow: hidden; }"));
        UIElement e("e"); s.ApplyToElement(e);
        EXPECT_EQ(e.style().overflowX, Overflow::Hidden);
        EXPECT_EQ(e.style().overflowY, Overflow::Hidden)
            << "the shorthand came last and must win on both axes";
    }
}

// CSS computed-value rule. Not decoration here but forced: clipping is a RECT,
// so "clip Y but not X" has no representation in PushClipRect.
TEST(UIScroll, ALoneVisibleAxisIsPromoted) {
    UIElement e("e");
    e.style().overflowX = Overflow::Hidden;      // Y left Visible
    EXPECT_TRUE(ClipsBox(e.style()));
    EXPECT_EQ(ResolvedOverflowY(e.style()), Overflow::Scroll)
        << "the lone visible axis was not promoted, so the element half-clips";

    UIElement plain("p");
    EXPECT_FALSE(ClipsBox(plain.style()));
    EXPECT_FALSE(IsScroller(plain.style()));
}

// flex-shrink acts on the MAIN axis. A row scroller whose Y is hidden must
// still refuse to squeeze its children horizontally.
TEST(UIScroll, TheShrinkOverrideFollowsTheMainAxis) {
    UIDocument doc;
    UIElement* row = doc.root().AddChild("row");
    row->style().direction = FlexDirection::Row;
    row->style().overflowX = Overflow::Scroll;
    row->style().overflowY = Overflow::Hidden;
    row->style().width = StyleLength::Px(200.f);
    row->style().height = StyleLength::Px(60.f);
    for (int i = 0; i < 6; ++i) {
        row->AddChild("c" + std::to_string(i))->style().width = StyleLength::Px(80.f);
    }
    doc.Layout(400.f, 400.f);

    EXPECT_FLOAT_EQ(doc.root().Find("c0")->layout().size.x, 80.f)
        << "a row scroller had its children squeezed — nothing left to scroll";
    EXPECT_GT(row->maxScroll().x, 0.f);
    EXPECT_FLOAT_EQ(row->maxScroll().y, 0.f) << "a hidden axis must not scroll";
}

TEST(UIScroll, AHiddenAxisIsPinnedShut) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 200.f });
    d.Layout();
    ASSERT_FLOAT_EQ(d.box->scrollOffset().y, 200.f);

    d.box->style().overflowY = Overflow::Hidden;
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 0.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f)
        << "an axis that stopped scrolling kept a live offset";
    EXPECT_FLOAT_EQ(d.box->scrollState()->thumbY.size.y, 0.f);
}

// `auto` stays reserved. The error names the legal words EXACTLY, so appending
// a fourth keyword later cannot pass this test by accident.
TEST(UIScroll, OverflowAutoIsReportedOnEverySpelling) {
    for (const char* prop : { "overflow", "overflow-x", "overflow-y" }) {
        UIStyleSheet s;
        EXPECT_FALSE(s.ParseString(std::string("#e { ") + prop + ": auto; }")) << prop;
        ASSERT_FALSE(s.errors().empty()) << prop;
        EXPECT_NE(s.errors()[0].find(std::string(prop) + " must be one of visible|hidden|scroll"),
                  std::string::npos) << s.errors()[0];
    }
}

// ======================================================= scrollbar styling

TEST(UIScroll, ScrollbarPropertiesAreAuthorable) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString(
        "#e { scrollbar-width: 12px; scrollbar-min-thumb: 30px;"
        "     scrollbar-color: #101010; scrollbar-thumb-color: #ff0000;"
        "     scrollbar-visibility: always; scroll-behavior: smooth; }"))
        << (s.errors().empty() ? "" : s.errors()[0]);
    UIElement e("e"); s.ApplyToElement(e);
    EXPECT_FLOAT_EQ(e.style().scrollbarWidth, 12.f);
    EXPECT_FLOAT_EQ(e.style().scrollbarMinThumb, 30.f);
    EXPECT_FLOAT_EQ(e.style().scrollbarThumbColor.r, 1.f);
    EXPECT_EQ(e.style().scrollbarVisibility, ScrollbarVisibility::Always);
    EXPECT_EQ(e.style().scrollBehavior, ScrollBehavior::Smooth);
}

TEST(UIScroll, ScrollbarWidthRejectsPercentAndNegative) {
    for (const char* bad : { "50%", "-4px", "auto" }) {
        UIStyleSheet s;
        EXPECT_FALSE(s.ParseString(std::string("#e { scrollbar-width: ") + bad + "; }")) << bad;
        ASSERT_FALSE(s.errors().empty()) << bad;
        EXPECT_NE(s.errors()[0].find("scrollbar-width must be a non-negative pixel length"),
                  std::string::npos) << s.errors()[0];
    }
}

TEST(UIScroll, ScrollBarGeometryIsPure) {
    // A 100px box over 500px of content: the thumb is a fifth of the track,
    // which is below the 24px floor, so the floor is what shows.
    const ScrollBarRects r = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 400.f },
        8.f, 24.f, /*alwaysVisible=*/false);
    EXPECT_FLOAT_EQ(r.trackY.size.x, 8.f);
    EXPECT_FLOAT_EQ(r.trackY.size.y, 100.f);
    EXPECT_FLOAT_EQ(r.thumbY.size.y, 24.f) << "the min-thumb floor did not apply";
    EXPECT_FLOAT_EQ(r.thumbY.position.y, 0.f);
    EXPECT_FLOAT_EQ(r.thumbX.size.x, 0.f) << "a bar appeared on an axis with no range";

    // At the far end the thumb trailing edge meets the track trailing edge.
    const ScrollBarRects e = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 400.f }, { 0.f, 0.f }, { 0.f, 400.f },
        8.f, 24.f, false);
    EXPECT_FLOAT_EQ(e.thumbY.position.y + e.thumbY.size.y, 100.f);
}

// A bar wider than the element is not a bar.
TEST(UIScroll, ScrollbarWidthClampsToTheBox) {
    const ScrollBarRects r = ComputeScrollBars(
        { 0.f, 0.f }, { 100.f, 40.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 200.f },
        200.f, 24.f, false);
    EXPECT_LE(r.trackY.size.x, 20.f) << "the bar is wider than half the element";
    EXPECT_LE(r.thumbY.size.y, r.trackY.size.y);
}

TEST(UIScroll, ScrollbarWidthZeroPaintsNothing) {
    const ScrollBarRects r = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 400.f },
        0.f, 24.f, false);
    EXPECT_FLOAT_EQ(r.thumbY.size.y, 0.f) << "scrollbar-width: 0 must hide the bar";
}

// The two tracks used to span the full box and overlap in a bar-by-bar square,
// letting each thumb travel into the other track.
TEST(UIScroll, BothAxesLeaveACornerFree) {
    const ScrollBarRects r = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 300.f, 400.f },
        8.f, 24.f, false);
    ASSERT_GT(r.thumbX.size.x, 0.f);
    ASSERT_GT(r.thumbY.size.y, 0.f);
    EXPECT_FLOAT_EQ(r.trackY.size.y, 92.f) << "the vertical track runs into the corner";
    EXPECT_FLOAT_EQ(r.trackX.size.x, 192.f) << "the horizontal track runs into the corner";
}

// `always` is what CSS spells `overflow: scroll` as against `auto`. It lives on
// the bar because the bar is the only thing it affects.
TEST(UIScroll, AlwaysVisiblePaintsABarWithNoRange) {
    const ScrollBarRects autoBar = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 0.f },
        8.f, 24.f, /*alwaysVisible=*/false);
    EXPECT_FLOAT_EQ(autoBar.thumbY.size.y, 0.f);

    const ScrollBarRects always = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 0.f },
        8.f, 24.f, /*alwaysVisible=*/true);
    EXPECT_GT(always.thumbY.size.y, 0.f);
    EXPECT_FLOAT_EQ(always.thumbY.size.y, always.trackY.size.y)
        << "with no range the thumb should fill its track";
}

// ===================================================== the wheel axis-lock

namespace {

// A horizontal strip: a hotbar, a card carousel, an inventory row. The common
// non-list scroller in a HUD, and the one a mouse cannot reach without this.
struct RowDoc {
    UIDocument doc;
    UIElement* strip = nullptr;

    explicit RowDoc(bool alsoVertical = false) {
        strip = doc.root().AddChild("strip");
        strip->style().direction = FlexDirection::Row;
        strip->style().overflowX = Overflow::Scroll;
        strip->style().overflowY = alsoVertical ? Overflow::Scroll : Overflow::Hidden;
        strip->style().width = StyleLength::Px(200.f);
        strip->style().height = StyleLength::Px(alsoVertical ? 40.f : 60.f);
        for (int i = 0; i < 6; ++i) {
            UIElement* c = strip->AddChild("c" + std::to_string(i));
            c->style().width = StyleLength::Px(80.f);
            c->style().height = StyleLength::Px(60.f);
        }
        doc.Layout(400.f, 400.f);
    }
    void Wheel(float x, float y, bool shift = false) {
        UIPointerState p;
        p.position = { 100.f, 20.f };
        p.inside = true;
        p.wheel = { x, y };
        p.shift = shift;
        doc.UpdatePointer(p);
        doc.Layout(400.f, 400.f);
    }
};

} // namespace

// A mouse has ONE wheel. Before this a row-only scroller was completely dead to
// it: a vertical notch is {0, -1}, which fails the X test, and the element fails
// the Y test too, so the wheel walked straight past to an ancestor — leaving a
// panel with a visible, draggable thumb that the wheel would not move.
TEST(UIScroll, AVerticalWheelScrollsARowOnlyScroller) {
    RowDoc d;
    ASSERT_GT(d.strip->maxScroll().x, 0.f);
    ASSERT_FLOAT_EQ(d.strip->maxScroll().y, 0.f);

    d.Wheel(0.f, -1.f);
    EXPECT_GT(d.strip->scrollOffset().x, 0.f)
        << "a row-only scroller is unreachable with a normal mouse wheel";
    EXPECT_FLOAT_EQ(d.strip->scrollOffset().y, 0.f);
}

// ...but only when the element cannot use the vertical wheel itself. A scroller
// with range on BOTH axes must not have its vertical wheel hijacked sideways.
TEST(UIScroll, AVerticalWheelDoesNotHijackATwoAxisScroller) {
    RowDoc d(/*alsoVertical=*/true);
    ASSERT_GT(d.strip->maxScroll().x, 0.f);
    ASSERT_GT(d.strip->maxScroll().y, 0.f);

    d.Wheel(0.f, -1.f);
    EXPECT_GT(d.strip->scrollOffset().y, 0.f);
    EXPECT_FLOAT_EQ(d.strip->scrollOffset().x, 0.f) << "the vertical wheel leaked into x";
}

// Shift+wheel is the desktop convention for reaching a horizontal scroller. The
// swap happens once, in the default action, so the Game view and the shipped
// player cannot disagree — each host only reports whether shift is held.
TEST(UIScroll, ShiftWheelSwapsTheAxes) {
    RowDoc d(/*alsoVertical=*/true);
    d.Wheel(0.f, -1.f, /*shift=*/true);
    EXPECT_GT(d.strip->scrollOffset().x, 0.f) << "shift+wheel did not reach the x axis";
    EXPECT_FLOAT_EQ(d.strip->scrollOffset().y, 0.f);
}

// ==================================================== the track is clickable

// The tracks are painted but were invisible to every hit path, so a press on one
// fell through to the row underneath — the identical bug the thumb path was
// written to kill, still live across most of the bar's area.
TEST(UIScroll, TrackPressDoesNotClickThrough) {
    ScrollDoc d;
    UIElement* row = d.row(0);
    row->setFocusable(true);
    int clicks = 0;
    row->OnClick([&](UIEvent&) { ++clicks; });

    const auto& sc = *d.box->scrollState();
    ASSERT_GT(sc.trackY.size.y, 0.f);
    // Below the thumb, so squarely on the track.
    const glm::vec2 onTrack{ sc.trackY.position.x + sc.trackY.size.x * 0.5f,
                             sc.trackY.position.y + sc.trackY.size.y - 4.f };

    UIPointerState p;
    p.position = onTrack;
    p.inside = true;
    p.buttonDown = true;
    d.doc.UpdatePointer(p);
    p.buttonDown = false;
    d.doc.UpdatePointer(p);

    EXPECT_EQ(clicks, 0) << "the press went through the scrollbar track to the row";
    EXPECT_EQ(d.doc.focused(), nullptr) << "pressing the track moved focus to the row";
}

TEST(UIScroll, TrackPressPagesTowardTheClick) {
    ScrollDoc d;
    const auto& sc = *d.box->scrollState();
    const glm::vec2 below{ sc.trackY.position.x + 2.f,
                           sc.trackY.position.y + sc.trackY.size.y - 4.f };

    UIPointerState p;
    p.position = below;
    p.inside = true;
    p.buttonDown = true;
    d.doc.UpdatePointer(p);
    d.Layout();

    // 90% of a 100px box.
    EXPECT_NEAR(d.box->scrollOffset().y, 90.f, 0.5f)
        << "a press below the thumb did not page down";

    // Holding does NOT repeat: that would need a held-duration clock, and the
    // manual says so rather than leaving a held press looking broken.
    const float afterOne = d.box->scrollOffset().y;
    for (int i = 0; i < 5; ++i) { d.doc.UpdatePointer(p); d.Layout(); }
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, afterOne) << "a held press repeated";

    // Releasing and pressing again pages again.
    p.buttonDown = false; d.doc.UpdatePointer(p); d.Layout();
    p.buttonDown = true;  d.doc.UpdatePointer(p); d.Layout();
    EXPECT_GT(d.box->scrollOffset().y, afterOne);
}

TEST(UIScroll, TrackPressAboveTheThumbPagesUp) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 300.f });
    d.Layout();
    const auto& sc = *d.box->scrollState();
    const glm::vec2 above{ sc.trackY.position.x + 2.f, sc.trackY.position.y + 2.f };

    UIPointerState p;
    p.position = above;
    p.inside = true;
    p.buttonDown = true;
    d.doc.UpdatePointer(p);
    d.Layout();
    EXPECT_NEAR(d.box->scrollOffset().y, 210.f, 0.5f) << "a press above the thumb did not page up";
}

TEST(UIScroll, PressingTheBarFocusesAFocusableScroller) {
    ScrollDoc d;
    d.box->setFocusable(true);
    const auto& sc = *d.box->scrollState();

    UIPointerState p;
    p.position = sc.thumbY.position + sc.thumbY.size * 0.5f;
    p.inside = true;
    p.buttonDown = true;
    d.doc.UpdatePointer(p);

    EXPECT_EQ(d.doc.focused(), d.box)
        << "dragging the thumb then pressing PageDown would be a coin flip";
}

// ===================================================== keyboard scrolling

namespace {

// UIDocument is deliberately non-copyable, so the fixture is focused in place
// rather than returned by value.
void focusTheScroller(ScrollDoc& d) {
    d.box->setFocusable(true);
    d.doc.SetFocus(d.box);
    d.Layout();
}

void SendKey(UIDocument& doc, UIKey k) {
    UIKeyboardState kb;
    UIKeyEvent e;
    e.key = k;
    kb.keys.push_back(e);
    doc.UpdateKeyboard(kb);
}

} // namespace

TEST(UIScroll, PageKeysScrollTheFocusedScroller) {
    ScrollDoc d;
    focusTheScroller(d);
    ASSERT_EQ(d.doc.focused(), d.box);

    SendKey(d.doc, UIKey::PageDown);
    d.Layout();
    EXPECT_NEAR(d.box->scrollOffset().y, 90.f, 0.5f) << "PageDown did nothing";

    SendKey(d.doc, UIKey::PageUp);
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f);
}

TEST(UIScroll, HomeAndEndJumpToTheEnds) {
    ScrollDoc d;
    focusTheScroller(d);
    SendKey(d.doc, UIKey::End);
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, d.box->maxScroll().y);

    SendKey(d.doc, UIKey::Home);
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, d.box->minScroll().y);
}

// A page key reaches the nearest scrollable ANCESTOR, so focusing a row inside
// the list still pages the list.
TEST(UIScroll, PageKeysReachTheNearestScrollableAncestor) {
    ScrollDoc d;
    d.row(0)->setFocusable(true);
    d.doc.SetFocus(d.row(0));
    d.Layout();

    SendKey(d.doc, UIKey::PageDown);
    d.Layout();
    EXPECT_GT(d.box->scrollOffset().y, 0.f)
        << "a focused row did not page its container";
}

// The documented asymmetry, and it matches a browser textarea: a focused field
// keeps Home/End for its own line, but lets PageUp/PageDown page its container.
TEST(UIScroll, AFocusedFieldKeepsHomeEndButPassesPageKeys) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(
        <UI>
          <Element name="box" style="overflow: scroll; width: 200px; height: 100px">
            <TextField name="f" multiline="true" value="one two three"
                       style="width: 180px; height: 40px"/>
            <Element style="height: 60px"/><Element style="height: 60px"/>
            <Element style="height: 60px"/>
          </Element>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());
    doc.Layout(400.f, 400.f);

    UIElement* box = doc.root().Find("box");
    UIElement* f = doc.root().Find("f");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(f, nullptr);
    ASSERT_GT(box->maxScroll().y, 0.f);

    doc.SetFocus(f);
    f->textEdit()->SetCaret(4);

    // End belongs to the FIELD: the caret moves and the container does not.
    SendKey(doc, UIKey::End);
    doc.Layout(400.f, 400.f);
    EXPECT_FLOAT_EQ(box->scrollOffset().y, 0.f)
        << "End escaped the field and scrolled its container";

    // PageDown belongs to the CONTAINER: UITextEdit deliberately does not
    // consume it, so it falls through to the scroll default action.
    SendKey(doc, UIKey::PageDown);
    doc.Layout(400.f, 400.f);
    EXPECT_GT(box->scrollOffset().y, 0.f)
        << "PageDown was swallowed by the focused field";
}

// A page key with nothing scrollable in the chain must stay unconsumed, or it
// would silently swallow a shortcut an app wanted.
TEST(UIScroll, PageKeysAreNotConsumedWithoutAScroller) {
    UIDocument doc;
    UIElement* b = doc.root().AddChild("b");
    b->style().width = StyleLength::Px(50.f);
    b->style().height = StyleLength::Px(50.f);
    b->setFocusable(true);
    doc.Layout(400.f, 400.f);
    doc.SetFocus(b);

    int seen = 0;
    b->OnKeyDown([&](UIEvent& e) { if (e.key == UIKey::PageDown) ++seen; });
    SendKey(doc, UIKey::PageDown);
    EXPECT_EQ(seen, 1) << "the handler must still see the key";
    SUCCEED();
}

// ====================================================== smooth scrolling

// OPT-IN, and that is the whole design. Layout() is otherwise a pure function
// of the tree, and making every scroll depend on the clock would be a change
// every existing caller had to reason about — so the default stays instant and
// the 40-odd tests above never advance a clock.
TEST(UIScroll, InstantIsTheDefault) {
    ScrollDoc d;
    EXPECT_EQ(d.box->style().scrollBehavior, ScrollBehavior::Instant);
    d.box->SetScrollOffset({ 0.f, 200.f });
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f)
        << "an instant scroll must land before any time passes";
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f);
}

TEST(UIScroll, SmoothMovesTheTargetAndEasesTowardIt) {
    ScrollDoc d;
    d.box->style().scrollBehavior = ScrollBehavior::Smooth;
    d.Layout();

    ASSERT_TRUE(d.box->SetScrollOffset({ 0.f, 200.f }));
    // Nothing has moved yet: the destination changed, not the view.
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f);
    ASSERT_NE(d.box->scrollState(), nullptr);
    EXPECT_FLOAT_EQ(d.box->scrollState()->target.y, 200.f);

    d.doc.AdvanceTime(0.016f);
    const float afterOne = d.box->scrollOffset().y;
    EXPECT_GT(afterOne, 0.f) << "the ease never started";
    EXPECT_LT(afterOne, 200.f) << "one frame covered the whole distance";

    // ...and it arrives, rather than asymptoting forever.
    for (int i = 0; i < 200; ++i) d.doc.AdvanceTime(0.016f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f)
        << "an exponential approach must snap, or the document relayouts forever";
}

// Frame-rate independence: the same elapsed time must cover the same distance
// whether it arrives in one step or eight.
TEST(UIScroll, TheEaseIsFrameRateIndependent) {
    ScrollDoc a, b;
    for (ScrollDoc* d : { &a, &b }) {
        d->box->style().scrollBehavior = ScrollBehavior::Smooth;
        d->Layout();
        d->box->SetScrollOffset({ 0.f, 400.f });
    }
    a.doc.AdvanceTime(0.16f);                              // one slow frame
    for (int i = 0; i < 8; ++i) b.doc.AdvanceTime(0.02f);  // eight fast ones

    EXPECT_NEAR(a.box->scrollOffset().y, b.box->scrollOffset().y, 1.0f)
        << "the same flick would travel differently at 60Hz and 144Hz";
}

// The host has to keep laying out while an animation runs, or the offset moves
// and nothing on screen follows it.
TEST(UIScroll, AnimatingKeepsTheDocumentDirty) {
    ScrollDoc d;
    d.box->style().scrollBehavior = ScrollBehavior::Smooth;
    d.Layout();
    d.box->SetScrollOffset({ 0.f, 300.f });
    d.doc.ConsumeScrollDirty();          // clear whatever the write set

    d.doc.AdvanceTime(0.016f);
    EXPECT_TRUE(d.doc.ConsumeScrollDirty()) << "an animating scroller did not ask for a relayout";

    for (int i = 0; i < 200; ++i) d.doc.AdvanceTime(0.016f);
    d.doc.ConsumeScrollDirty();
    d.doc.AdvanceTime(0.016f);
    EXPECT_FALSE(d.doc.ConsumeScrollDirty())
        << "a settled scroller keeps the document dirty forever";
}

// Content shrinking mid-animation must not leave the ease pulling toward
// somewhere unreachable.
TEST(UIScroll, TheTargetIsClampedWhenContentShrinks) {
    ScrollDoc d;
    d.box->style().scrollBehavior = ScrollBehavior::Smooth;
    d.Layout();
    d.box->SetScrollOffset({ 0.f, 400.f });
    d.doc.AdvanceTime(0.016f);

    for (int i = 2; i < 10; ++i) d.row(i)->style().display = DisplayMode::None;
    d.Layout();
    ASSERT_FLOAT_EQ(d.box->maxScroll().y, 0.f);
    EXPECT_FLOAT_EQ(d.box->scrollState()->target.y, 0.f) << "the target outran the content";

    for (int i = 0; i < 200; ++i) d.doc.AdvanceTime(0.016f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 0.f);
}

// Turning smoothness off mid-flight lands immediately rather than freezing
// part-way with no clock left to finish the journey.
TEST(UIScroll, SwitchingToInstantMidAnimationLands) {
    ScrollDoc d;
    d.box->style().scrollBehavior = ScrollBehavior::Smooth;
    d.Layout();
    d.box->SetScrollOffset({ 0.f, 300.f });
    d.doc.AdvanceTime(0.016f);
    ASSERT_LT(d.box->scrollOffset().y, 300.f);

    d.box->style().scrollBehavior = ScrollBehavior::Instant;
    d.doc.AdvanceTime(0.016f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 300.f);
}

// A document with nothing animating must not pay for the walk.
TEST(UIScroll, AdvanceTimeIsInertWithoutAnAnimation) {
    ScrollDoc d;
    d.box->SetScrollOffset({ 0.f, 200.f });
    d.Layout();
    d.doc.ConsumeScrollDirty();
    for (int i = 0; i < 10; ++i) d.doc.AdvanceTime(0.016f);
    EXPECT_FALSE(d.doc.ConsumeScrollDirty());
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f);
}

TEST(UIScroll, ScrollBehaviorIsAuthorable) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString("#a { scroll-behavior: smooth; } #b { scroll-behavior: auto; }"))
        << (s.errors().empty() ? "" : s.errors()[0]);
    UIElement a("a"), b("b");
    s.ApplyToElement(a);
    s.ApplyToElement(b);
    EXPECT_EQ(a.style().scrollBehavior, ScrollBehavior::Smooth);
    // CSS spells instant `auto`, and here it is unambiguous — there is no third
    // behaviour it could mean, unlike `overflow: auto`.
    EXPECT_EQ(b.style().scrollBehavior, ScrollBehavior::Instant);

    UIStyleSheet bad;
    EXPECT_FALSE(bad.ParseString("#a { scroll-behavior: swoosh; }"));
    ASSERT_FALSE(bad.errors().empty());
    EXPECT_NE(bad.errors()[0].find("scroll-behavior must be one of"), std::string::npos)
        << bad.errors()[0];
}

// ============================================== the text field's own bar

namespace {

// A multiline field with a known text extent. No font in this suite, so the
// measurement is supplied directly — which is exactly why UITextEdit records it
// rather than re-deriving it at every use.
struct FieldBarDoc {
    UIDocument doc;
    UIElement* f = nullptr;

    FieldBarDoc() {
        std::vector<std::string> errors;
        UIMarkup::LoadInto(doc, R"(<UI><TextField name="f" multiline="true"
                                     style="width: 200px; height: 60px; padding: 5px"/></UI>)",
                           errors, "t.cxml");
        UIStyleSheet sheet;
        sheet.ApplyTo(doc.root());
        doc.Layout(400.f, 400.f);
        f = doc.root().Find("f");
    }
    UITextEdit* edit() { return f ? f->textEdit() : nullptr; }
    // Pretend the font measured this much text inside the padding box.
    void Measured(float w, float h) {
        edit()->setMeasured({ w, h }, { 190.f, 50.f });
        doc.Layout(400.f, 400.f);
    }
};

} // namespace

TEST(UIScroll, AMultilineFieldPaintsABarForItsText) {
    FieldBarDoc d;
    ASSERT_NE(d.edit(), nullptr);
    d.Measured(150.f, 200.f);            // taller than the 50px content box

    ASSERT_NE(d.f->scrollState(), nullptr);
    EXPECT_GT(d.f->scrollState()->thumbY.size.y, 0.f) << "no bar for overflowing text";
    EXPECT_FLOAT_EQ(d.f->scrollState()->thumbX.size.x, 0.f)
        << "the text fits horizontally; there should be no bar";
    EXPECT_FLOAT_EQ(d.f->maxScroll().y, 150.f);
}

TEST(UIScroll, ASingleLineFieldNeverPaintsABar) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><TextField name="f" style="width: 200px; height: 30px"/></UI>)",
        errors, "t.cxml"));
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());
    doc.Layout(400.f, 400.f);
    UIElement* f = doc.root().Find("f");
    ASSERT_NE(f, nullptr);
    f->textEdit()->setMeasured({ 900.f, 16.f }, { 200.f, 30.f });
    doc.Layout(400.f, 400.f);
    // No native single-line input has a bar, and one would eat 8px of the field.
    EXPECT_EQ(f->scrollState(), nullptr);
}

// The bar and the glyphs must read from the SAME number. A bar backed by
// scroll_->offset would track the cursor perfectly while the text stayed put.
TEST(UIScroll, TheFieldBarAndTheTextShareOneOffset) {
    FieldBarDoc d;
    d.Measured(150.f, 200.f);

    ASSERT_TRUE(d.f->SetScrollOffset({ 0.f, 60.f }));
    EXPECT_FLOAT_EQ(d.edit()->textScroll().y, 60.f)
        << "the scroll did not reach the text";
    d.doc.Layout(400.f, 400.f);
    EXPECT_FLOAT_EQ(d.f->scrollOffset().y, 60.f) << "the bar and the text disagree";

    // ...and it clamps to the measured text, not to some element extent.
    d.f->SetScrollOffset({ 0.f, 1e6f });
    EXPECT_FLOAT_EQ(d.edit()->textScroll().y, 150.f);
}

TEST(UIScroll, TheWheelScrollsAMultilineField) {
    FieldBarDoc d;
    d.Measured(150.f, 200.f);

    UIPointerState p;
    p.position = d.f->layout().position + d.f->layout().size * 0.5f;
    p.inside = true;
    p.wheel = { 0.f, -1.f };
    d.doc.UpdatePointer(p);
    d.doc.Layout(400.f, 400.f);

    EXPECT_GT(d.edit()->textScroll().y, 0.f)
        << "a field with a draggable bar the wheel cannot move is a strange control";
}

// A field is a text leaf, so it never smooth-scrolls: it follows its caret, and
// a caret that arrived somewhere the view was still travelling toward would be
// worse than no animation at all.
TEST(UIScroll, AFieldIgnoresSmoothBehaviour) {
    FieldBarDoc d;
    d.f->style().scrollBehavior = ScrollBehavior::Smooth;
    d.Measured(150.f, 200.f);

    ASSERT_TRUE(d.f->SetScrollOffset({ 0.f, 80.f }));
    EXPECT_FLOAT_EQ(d.edit()->textScroll().y, 80.f)
        << "a field must land immediately whatever scroll-behavior says";
}

// ================================================ the subtree AABB / culling
//
// Culling is COST ONLY: the AABB is a deliberate superset of what a subtree
// paints, so the tests here are about what it must never exclude. The visible
// half (that fewer quads reach the renderer) belongs to test_renderer2d.cpp,
// which has a GL fixture; what is assertable here is the geometry the cull
// decides from.

TEST(UIScroll, TheSubtreeAABBCoversEveryChild) {
    ScrollDoc d(4, 50.f);
    const ComputedLayout& box = d.box->layout();
    // 4 rows of 50 in a 100px box: the content runs to 200, well past the box.
    EXPECT_FLOAT_EQ(box.subtreeMax.y - box.subtreeMin.y, 200.f)
        << "the union stopped at the element's own rect";
    EXPECT_LE(box.subtreeMin.y, box.position.y);
    EXPECT_GE(box.subtreeMax.y, d.row(3)->layout().position.y + 50.f);
}

// An absolutely positioned child may legitimately paint OUTSIDE its parent, and
// culling on the parent's own rect would drop it.
TEST(UIScroll, TheSubtreeAABBCoversAnEscapingAbsoluteChild) {
    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(100.f);
    box->style().height = StyleLength::Px(100.f);
    UIElement* out = box->AddChild("out");
    out->style().position = PositionType::Absolute;
    out->style().inset = { 300.f, 300.f, 0.f, 0.f };   // left/top far outside
    out->style().width = StyleLength::Px(40.f);
    out->style().height = StyleLength::Px(40.f);
    doc.Layout(600.f, 600.f);

    EXPECT_GE(box->layout().subtreeMax.x, out->layout().position.x + 40.f)
        << "an escaping absolute child is outside its parent's AABB and would be culled";
    EXPECT_GE(box->layout().subtreeMax.y, out->layout().position.y + 40.f);
}

// display:none paints nothing, so it must not inflate an ancestor's AABB and
// defeat the cull for everything around it.
TEST(UIScroll, AHiddenChildDoesNotInflateTheAABB) {
    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().width = StyleLength::Px(100.f);
    box->style().height = StyleLength::Px(100.f);
    UIElement* ghost = box->AddChild("ghost");
    ghost->style().position = PositionType::Absolute;
    ghost->style().inset = { 500.f, 500.f, 0.f, 0.f };
    ghost->style().width = StyleLength::Px(40.f);
    ghost->style().height = StyleLength::Px(40.f);
    ghost->style().display = DisplayMode::None;
    doc.Layout(900.f, 900.f);

    EXPECT_LT(box->layout().subtreeMax.x, 200.f)
        << "a hidden child inflated the AABB, so nothing around it can ever be culled";
}

// The AABB is recomputed every layout, so scrolling moves it with the content —
// otherwise a list scrolled away would keep claiming its original bounds.
TEST(UIScroll, TheAABBFollowsTheScrollOffset) {
    ScrollDoc d;
    const float before = d.box->layout().subtreeMax.y;
    d.box->SetScrollOffset({ 0.f, 400.f });
    d.Layout();
    EXPECT_LT(d.box->layout().subtreeMax.y, before)
        << "the AABB did not move with the content it describes";
}

// Drawing must survive being handed a viewport nothing lands in — the cull is
// the first thing that would break, and it must fail closed (draw nothing)
// rather than crash.
TEST(UIScroll, DrawingWithEverythingOffScreenIsSafe) {
    ScrollDoc d;
    d.doc.SetOrigin({ -10000.f, -10000.f });
    d.Layout();
    SUCCEED();   // the assertion is that Layout with an off-screen origin is fine
    EXPECT_LT(d.box->layout().subtreeMax.x, 0.f);
}

// ============================================ declared content extent

// The real blocker for virtualisation is not collection binding: it is that the
// extent is DERIVED from the children that exist. Keep six rows of a thousand
// and the range collapses, the thumb vanishes and the wheel walks past.
TEST(UIScroll, AWindowOfLiveRowsMeasuresOnlyItself) {
    ScrollDoc d(6, 50.f);
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 300.f);
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 200.f)
        << "six live rows is six rows of content — which is the whole problem";
}

TEST(UIScroll, ADeclaredExtentOverridesTheMeasurement) {
    ScrollDoc d(6, 50.f);                       // a window of 6 live rows...
    d.box->SetContentExtent({ 0.f, 50000.f });  // ...standing in for 1000
    d.Layout();

    EXPECT_FLOAT_EQ(d.box->contentSize().y, 50000.f);
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 49900.f);
    EXPECT_GT(d.box->scrollState()->thumbY.size.y, 0.f) << "no thumb for declared content";

    // ...and it is scrollable across the whole declared range, which is what
    // makes a hand-rolled virtual list possible at all.
    d.box->SetScrollOffset({ 0.f, 25000.f });
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 25000.f);
}

TEST(UIScroll, ClearingTheDeclarationGoesBackToMeasuring) {
    ScrollDoc d(6, 50.f);
    d.box->SetContentExtent({ 0.f, 50000.f });
    d.Layout();
    ASSERT_FLOAT_EQ(d.box->maxScroll().y, 49900.f);
    // Scrolled well past what the live rows measure, so the reclamp below has
    // something to do.
    d.box->SetScrollOffset({ 0.f, 25000.f });
    d.Layout();
    ASSERT_FLOAT_EQ(d.box->scrollOffset().y, 25000.f);

    d.box->ClearContentExtent();
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->contentSize().y, 300.f);
    EXPECT_FLOAT_EQ(d.box->scrollOffset().y, 200.f)
        << "dropping the declaration left the offset past the real content";
}

// Authored intent, not derived state: it survives the element temporarily not
// being a scroller, exactly as the offset survives a :hover restyle.
TEST(UIScroll, ADeclaredExtentSurvivesLosingScrollability) {
    ScrollDoc d(6, 50.f);
    d.box->SetContentExtent({ 0.f, 5000.f });
    d.Layout();
    ASSERT_GT(d.box->maxScroll().y, 4000.f);

    d.box->style().overflowX = d.box->style().overflowY = Overflow::Visible;
    d.Layout();
    EXPECT_FLOAT_EQ(d.box->maxScroll().y, 0.f);

    d.box->style().overflowX = d.box->style().overflowY = Overflow::Scroll;
    d.Layout();
    EXPECT_GT(d.box->maxScroll().y, 4000.f) << "the declaration was thrown away";
}

// A hand-rolled virtual list end to end, with no bindings anywhere: declare the
// extent, keep a window of live rows, and place them from scrollOffset(). This
// is the pattern the manual documents, so it is worth having a test that it
// actually works.
TEST(UIScroll, AHandRolledVirtualListScrollsThroughAThousandRows) {
    constexpr int kTotal = 1000;
    constexpr float kRowH = 20.f;
    constexpr int kWindow = 8;

    UIDocument doc;
    UIElement* box = doc.root().AddChild("box");
    box->style().overflowX = box->style().overflowY = Overflow::Scroll;
    box->style().width = StyleLength::Px(200.f);
    box->style().height = StyleLength::Px(100.f);
    for (int i = 0; i < kWindow; ++i) {
        UIElement* r = box->AddChild("r" + std::to_string(i));
        r->style().position = PositionType::Absolute;
        r->style().height = StyleLength::Px(kRowH);
        r->style().width = StyleLength::Px(180.f);
    }
    box->SetContentExtent({ 0.f, kTotal * kRowH });
    doc.Layout(400.f, 400.f);
    ASSERT_FLOAT_EQ(box->maxScroll().y, kTotal * kRowH - 100.f);

    // Scroll to the middle and place the window, the way an app would.
    box->SetScrollOffset({ 0.f, 10000.f });
    doc.Layout(400.f, 400.f);
    const int first = int(box->scrollOffset().y / kRowH);
    EXPECT_EQ(first, 500);
    for (int i = 0; i < kWindow; ++i) {
        box->children()[std::size_t(i)]->style().inset.top =
            float(first + i) * kRowH - box->scrollOffset().y;
    }
    doc.Layout(400.f, 400.f);

    // Row 500 sits at the top of the box, and the window covers it.
    UIElement* top = doc.root().Find("r0");
    ASSERT_NE(top, nullptr);
    EXPECT_NEAR(top->layout().position.y, box->layout().position.y, 0.5f)
        << "the placed window did not line up with the scroll offset";
    EXPECT_GT(box->scrollState()->thumbY.size.y, 0.f);
}

// ------------------------------------------------- the rounded-corner inset

// A scroller with a radius: the bars must pull in from the ends of the box, or
// they paint their square ends OUTSIDE the rounded silhouette. Clipping cannot
// save it — clipping is the axis-aligned scissor, and the corner it would have
// to cut is not.
TEST(UIScrollBars, ACornerInsetPullsBothTracksInsideTheRoundedSilhouette) {
    const glm::vec2 pos{ 0.f, 0.f }, size{ 200.f, 100.f };
    const glm::vec2 off{ 0.f, 0.f }, lo{ 0.f, 0.f }, hi{ 120.f, 120.f };

    const ScrollBarRects plain =
        ComputeScrollBars(pos, size, off, lo, hi, 8.f, 20.f, false, 0.0f);
    const ScrollBarRects inset =
        ComputeScrollBars(pos, size, off, lo, hi, 8.f, 20.f, false, 12.0f);

    ASSERT_GT(plain.trackY.size.y, 0.f);
    EXPECT_FLOAT_EQ(inset.trackY.position.y, plain.trackY.position.y + 12.f)
        << "the vertical track still starts at the very top of the box";
    EXPECT_FLOAT_EQ(inset.trackY.size.y, plain.trackY.size.y - 24.f)
        << "the vertical track was not shortened at BOTH ends";
    EXPECT_FLOAT_EQ(inset.trackX.position.x, plain.trackX.position.x + 12.f);
    EXPECT_FLOAT_EQ(inset.trackX.size.x, plain.trackX.size.x - 24.f);
}

// An absurd radius must not collapse the track to nothing and take the whole
// scrollbar with it.
TEST(UIScrollBars, AnEnormousCornerInsetIsClampedRatherThanErasingTheTrack) {
    const ScrollBarRects r = ComputeScrollBars(
        { 0.f, 0.f }, { 200.f, 100.f }, { 0.f, 0.f }, { 0.f, 0.f }, { 0.f, 120.f },
        8.f, 20.f, false, /*cornerInset=*/9999.0f);
    EXPECT_GT(r.trackY.size.y, 0.f) << "an enormous radius erased the scrollbar";
}

// Zero inset must be byte-identical to the pre-U22 behaviour: the parameter
// defaults to 0, and every existing caller and test relies on that.
TEST(UIScrollBars, AZeroCornerInsetIsIdenticalToTheUnroundedBars) {
    const glm::vec2 pos{ 5.f, 7.f }, size{ 200.f, 100.f };
    const ScrollBarRects a = ComputeScrollBars(pos, size, { 0.f, 10.f }, { 0.f, 0.f },
                                               { 0.f, 120.f }, 8.f, 20.f, false);
    const ScrollBarRects b = ComputeScrollBars(pos, size, { 0.f, 10.f }, { 0.f, 0.f },
                                               { 0.f, 120.f }, 8.f, 20.f, false, 0.0f);
    EXPECT_EQ(a.trackY.position, b.trackY.position);
    EXPECT_EQ(a.trackY.size, b.trackY.size);
    EXPECT_EQ(a.thumbY.position, b.thumbY.position);
    EXPECT_EQ(a.thumbY.size, b.thumbY.size);
}
