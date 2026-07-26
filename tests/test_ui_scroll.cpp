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
        box->style().overflow = Overflow::Scroll;
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
    d.box->style().overflow = Overflow::Hidden;
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

    d.box->style().overflow = Overflow::Visible;
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

// No chaining. The target is the nearest ancestor that IS scrollable on the
// axis, not the nearest that can still MOVE on it — otherwise an inner list
// resting at its bottom (the resting state of every log) would teleport the
// outer panel on the next notch.
TEST(UIScroll, WheelDoesNotChainToTheParent) {
    UIDocument doc;
    UIElement* outer = doc.root().AddChild("outer");
    outer->style().overflow = Overflow::Scroll;
    outer->style().width = StyleLength::Px(200.f);
    outer->style().height = StyleLength::Px(100.f);

    UIElement* inner = outer->AddChild("inner");
    inner->style().overflow = Overflow::Scroll;
    inner->style().width = StyleLength::Px(180.f);
    inner->style().height = StyleLength::Px(80.f);
    for (int i = 0; i < 4; ++i) {
        inner->AddChild("r" + std::to_string(i))->style().height = StyleLength::Px(50.f);
    }
    // Something to give the OUTER scroller range of its own.
    outer->AddChild("tail")->style().height = StyleLength::Px(300.f);
    doc.Layout(400.f, 400.f);
    ASSERT_GT(inner->maxScroll().y, 0.f);
    ASSERT_GT(outer->maxScroll().y, 0.f);

    // Park the inner one at its end, then keep scrolling over it.
    inner->SetScrollOffset({ 0.f, inner->maxScroll().y });
    doc.Layout(400.f, 400.f);
    const float outerBefore = outer->scrollOffset().y;

    UIPointerState p;
    p.position = { 100.f, 40.f };            // over the inner list
    p.inside = true;
    p.wheel = { 0.f, -3.f };
    doc.UpdatePointer(p);
    doc.Layout(400.f, 400.f);

    EXPECT_FLOAT_EQ(outer->scrollOffset().y, outerBefore)
        << "the wheel chained to the outer panel when the inner list ran out";
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
    box->style().overflow = Overflow::Scroll;
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
        EXPECT_EQ(e.style().overflow, c.want) << c.value;
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
        </UI>)", errors, "t.uxml")) << (errors.empty() ? "" : errors[0]);
    // An inline style= is REPLAYED by the cascade, not baked in at load, so a
    // sheet has to run even when it is empty — that is what makes an inline
    // style outrank every selector rule instead of being clobbered by one.
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());
    doc.Layout(400.f, 400.f);
    UIElement* log = doc.root().Find("log");
    ASSERT_NE(log, nullptr);
    EXPECT_EQ(log->style().overflow, Overflow::Scroll);
    EXPECT_FLOAT_EQ(log->contentSize().y, 120.f);
}

TEST(UIScroll, OnWheelIsAnAuthorableEvent) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="e" on-wheel="spin"/></UI>)", errors, "t.uxml"))
        << (errors.empty() ? "" : errors[0]);

    UIDocument bad;
    errors.clear();
    EXPECT_FALSE(UIMarkup::LoadInto(bad,
        R"(<UI><Element on-scrollwheel="spin"/></UI>)", errors, "t.uxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("wheel"), std::string::npos) << errors[0];
}

// --------------------------------------------------------------- UIWorld

// The pointer target and the keyboard target were one variable until scrolling
// arrived. That was harmless while the pointer only meant clicks — you had to
// click to move focus in the first place — but the wheel arrives with no click,
// so a background document holding focus would silently eat it.
TEST(UIScroll, WheelFollowsHoverNotFocus) {
    const std::string fieldDoc = "test_uiscroll_field.uxml";
    const std::string listDoc = "test_uiscroll_list.uxml";
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
    const std::string m = "test_uiscroll_once.uxml";
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
