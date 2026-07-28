// UI scaling: authored pixels -> screen pixels.
//
// THE RULE the whole feature rests on, and what every test here is defending:
//
//   Style is in AUTHORED units (reference-resolution pixels).
//   ComputedLayout, scroll offsets, pointer positions, ScrollIntoView and the
//   document origin are in REAL surface pixels, always, at every scale.
//
// The conversion happens where a Style length or a text measurement ENTERS
// layout, and nowhere else. The first design put it on the way OUT instead —
// multiplying ComputedLayout after the solve — and that is wrong in two ways
// this file pins down:
//
//   - it multiplies the document ORIGIN too, because readLayout_ seeds the
//     recursion with it, so a document occupying the right-hand quarter of a
//     3840px surface lands at 5760 and is never seen again;
//   - it puts scroll state in different units from the rects it is compared
//     against, because measureScroll_ runs BEFORE readLayout_ by a documented
//     load-bearing ordering.
//
// Neither can happen now, and TheDocumentOriginIsNotScaled is the regression
// test for the first.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIScale.h"

// The one scaling test that needs a real font — and therefore a GL context —
// lives in test_ui_layout.cpp, which already stands one up:
// ResizingTheSurfaceReMeasuresEveryTextLeaf.

#include <cmath>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

UIScaleSettings scaled(float refW = 1920.f, float refH = 1080.f, float match = 0.f) {
    UIScaleSettings s;
    s.mode = UIScaleMode::ScaleWithScreen;
    s.reference = { refW, refH };
    s.match = match;
    return s;
}

// Trees are built in C++ rather than markup ON PURPOSE: an inline style= is
// PARSED at load but only applied by the cascade, and these tests stand up a
// bare UIDocument with no stylesheet. Setting Style directly is both what
// test_ui_layout.cpp does and what makes the authored numbers visible here.
struct Doc {
    UIDocument doc;

    UIElement* Add(const char* name, float w, float h) {
        UIElement* el = doc.root().AddChild(name);
        el->style().width = StyleLength::Px(w);
        el->style().height = StyleLength::Px(h);
        return el;
    }
    void LayoutAt(float w, float h, const UIScaleSettings& s = {}) {
        doc.SetScaleSettings(s);
        doc.SetSurfaceSize({ w, h });
        doc.Layout(w, h);
    }
    UIElement* find(const char* n) { return doc.root().Find(n); }
};

// A scroller with more content than box, ready to grow bars.
UIElement* makeScroller(Doc& d, float barWidth = 0.0f) {
    UIElement* log = d.Add("log", 200.f, 60.f);
    log->style().overflowX = Overflow::Scroll;
    log->style().overflowY = Overflow::Scroll;
    if (barWidth > 0.0f) log->style().scrollbarWidth = barWidth;
    for (int i = 0; i < 3; ++i) {
        UIElement* row = log->AddChild("row");
        row->style().width = StyleLength::Px(100.f);
        row->style().height = StyleLength::Px(40.f);
    }
    return log;
}

} // namespace

// ------------------------------------------------------- the fatal survivor

// readLayout_ seeds its recursion with the document ORIGIN and accumulates, so
// scaling ITS output scales the origin too. A document pinned to the right-hand
// quarter of a 3840px surface sits at x=2880; the rejected design put it at
// 5760, and it was never seen again.
TEST(UIScale, TheDocumentOriginIsNotScaled) {
    Doc d;
    UIElement* box = d.Add("box", 100.f, 40.f);
    d.doc.SetOrigin({ 2880.f, 120.f });
    // Region is a quarter of the surface; the SCALE comes from the surface.
    d.doc.SetScaleSettings(scaled(1920.f, 1080.f, 0.0f));
    d.doc.SetSurfaceSize({ 3840.f, 2160.f });
    d.doc.Layout(960.f, 1080.f);
    ASSERT_FLOAT_EQ(d.doc.scale(), 2.0f);

    EXPECT_FLOAT_EQ(box->layout().position.x, 2880.f)
        << "the document origin was scaled - a non-fullscreen document just left the screen";
    EXPECT_FLOAT_EQ(box->layout().position.y, 120.f);
    // The BOX did scale, which is the whole point.
    EXPECT_FLOAT_EQ(box->layout().size.x, 200.f);
    EXPECT_FLOAT_EQ(box->layout().size.y, 80.f);
}

// ------------------------------------------------------------- what scales

TEST(UIScale, AScaledLayoutIsTheUnscaledLayoutTimesTheScale) {
    auto build = [](Doc& d) {
        d.doc.root().style().direction = FlexDirection::Column;
        d.doc.root().style().alignItems = Align::FlexStart;
        d.doc.root().style().padding = { 10.f, 10.f, 10.f, 10.f };
        UIElement* a = d.Add("a", 100.f, 20.f);
        a->style().margin = { 4.f, 4.f, 4.f, 4.f };
        d.Add("b", 60.f, 30.f);
    };
    Doc one, two;
    build(one);
    build(two);
    one.LayoutAt(1920.f, 1080.f, scaled());                    // scale 1
    two.LayoutAt(3840.f, 2160.f, scaled(1920.f, 1080.f, 0.f)); // scale 2
    ASSERT_FLOAT_EQ(one.doc.scale(), 1.0f);
    ASSERT_FLOAT_EQ(two.doc.scale(), 2.0f);

    for (const char* n : { "a", "b" }) {
        UIElement* p = one.find(n);
        UIElement* q = two.find(n);
        ASSERT_NE(p, nullptr) << n;
        ASSERT_NE(q, nullptr) << n;
        EXPECT_FLOAT_EQ(q->layout().size.x, p->layout().size.x * 2.f) << n;
        EXPECT_FLOAT_EQ(q->layout().size.y, p->layout().size.y * 2.f) << n;
        EXPECT_FLOAT_EQ(q->layout().position.x, p->layout().position.x * 2.f) << n;
        EXPECT_FLOAT_EQ(q->layout().position.y, p->layout().position.y * 2.f) << n;
    }
}

// A percentage is already relative to a parent that has itself been scaled.
// Scaling it too compounds, and a 50%-wide bar becomes 100% at scale 2.
TEST(UIScale, APercentWidthIsNotScaledTwice) {
    Doc d;
    d.doc.root().style().alignItems = Align::FlexStart;
    UIElement* track = d.Add("track", 400.f, 20.f);
    UIElement* fill = track->AddChild("fill");
    fill->style().width = StyleLength::Pct(50.f);
    fill->style().height = StyleLength::Pct(100.f);
    d.LayoutAt(3840.f, 2160.f, scaled(1920.f, 1080.f, 0.f));
    ASSERT_FLOAT_EQ(d.doc.scale(), 2.0f);

    EXPECT_FLOAT_EQ(track->layout().size.x, 800.f);
    EXPECT_FLOAT_EQ(fill->layout().size.x, 400.f)
        << "a percentage was scaled on top of its already-scaled parent";
}

// -------------------------------------------------------------- scrollbars

// A 6px bar — which is what hud.cstyle authors — must be 12 real px at scale 2.
// Left unscaled it stays a sliver against doubled content, and the drag still
// works, so it reads as a styling mistake rather than a scaling one.
TEST(UIScale, AScaledScrollbarKeepsItsAuthoredFractionOfTheTrack) {
    Doc one, two;
    makeScroller(one, 6.f);
    makeScroller(two, 6.f);
    one.LayoutAt(1920.f, 1080.f, scaled());
    two.LayoutAt(3840.f, 2160.f, scaled(1920.f, 1080.f, 0.f));

    const UIScrollState* a = one.find("log")->scrollState();
    const UIScrollState* b = two.find("log")->scrollState();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_GT(a->trackY.size.x, 0.f) << "the unscaled case did not even produce a bar";
    EXPECT_FLOAT_EQ(b->trackY.size.x, a->trackY.size.x * 2.f)
        << "the scrollbar stayed at its authored width while everything else grew";
}

// The thumb's LENGTH is the visible fraction of the content. A real box fed a
// virtual span made the thumb describe a list that was not there — and the drag
// still worked, so nothing looked wrong until you measured it.
TEST(UIScale, AScaledThumbLengthStillReportsTheRealContentFraction) {
    Doc one, two;
    makeScroller(one);
    makeScroller(two);
    one.LayoutAt(1920.f, 1080.f, scaled());
    two.LayoutAt(3840.f, 2160.f, scaled(1920.f, 1080.f, 0.f));

    const UIScrollState* a = one.find("log")->scrollState();
    const UIScrollState* b = two.find("log")->scrollState();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_GT(a->trackY.size.y, 0.f);
    ASSERT_GT(a->thumbY.size.y, 0.f);
    // The SAME fraction of the track at both scales: content and box grew
    // together, so the thumb must not change its share.
    EXPECT_NEAR(b->thumbY.size.y / b->trackY.size.y,
                a->thumbY.size.y / a->trackY.size.y, 0.02f)
        << "the thumb lies about how much content there is";
}

// ----------------------------------------------------------- scroll amounts

// A wheel notch is an AUTHORED amount — "about three rows". Rows are twice as
// tall at scale 2, so a notch must travel twice as far in real pixels to keep
// covering the same three rows.
TEST(UIScale, AWheelNotchCoversTheSameAuthoredRowsAtEveryScale) {
    auto notchTravel = [](float surfaceW, float surfaceH) {
        Doc d;
        UIElement* log = makeScroller(d);
        d.LayoutAt(surfaceW, surfaceH, scaled(1920.f, 1080.f, 0.f));
        UIPointerState p;
        p.inside = true;
        p.position = log->layout().position + log->layout().size * 0.5f;
        p.wheel = { 0.f, -1.f };
        d.doc.UpdatePointer(p);
        const UIScrollState* sc = log->scrollState();
        return sc ? sc->target.y : 0.f;
    };
    const float atOne = notchTravel(1920.f, 1080.f);
    const float atTwo = notchTravel(3840.f, 2160.f);
    ASSERT_GT(atOne, 0.f) << "the wheel did not scroll at all";
    EXPECT_NEAR(atTwo, atOne * 2.f, 0.5f)
        << "a notch covered a different number of authored rows at scale 2";
}

// ------------------------------------------------------------- input space

// Hit-testing needs no inverse mapping, because ComputedLayout is already in
// real surface pixels. If that ever stopped being true, this says so.
TEST(UIScale, HitTestNeedsNoInverseMappingAtScale) {
    Doc d;
    d.doc.root().style().alignItems = Align::FlexStart;
    UIElement* box = d.Add("box", 100.f, 40.f);
    d.LayoutAt(3840.f, 2160.f, scaled(1920.f, 1080.f, 0.f));
    ASSERT_FLOAT_EQ(box->layout().size.x, 200.f);

    // A point inside the SCALED box, in real surface pixels...
    EXPECT_EQ(d.doc.HitTest({ 150.f, 60.f }), box);
    // ...and one past its right edge, which lands on the root instead. Not
    // nullptr: the root fills the viewport and is pickable, so "missed the box"
    // and "hit nothing" are different answers.
    EXPECT_NE(d.doc.HitTest({ 250.f, 60.f }), box)
        << "a point past the scaled box still hit it - the rect was not scaled";
}

// ------------------------------------------------------ the regression floor

// Constant mode must be identical to the path that existed before any of this.
// An innocent expression reorder inside sx_ would show up here first.
TEST(UIScale, ConstantModeIsBitIdenticalToTheUnscaledPath) {
    Doc d;
    d.doc.root().style().direction = FlexDirection::Column;
    d.doc.root().style().alignItems = Align::FlexStart;
    d.doc.root().style().padding = { 16.f, 16.f, 16.f, 16.f };
    UIElement* a = d.Add("a", 220.f, 18.f);
    a->style().margin = { 0.f, 12.f, 0.f, 0.f };   // left, top, right, bottom
    d.doc.Layout(1280.f, 720.f);                   // no scale settings at all

    EXPECT_FLOAT_EQ(d.doc.scale(), 1.0f);
    EXPECT_FLOAT_EQ(a->layout().size.x, 220.f);
    EXPECT_FLOAT_EQ(a->layout().size.y, 18.f);
    EXPECT_FLOAT_EQ(a->layout().position.x, 16.f);
    EXPECT_FLOAT_EQ(a->layout().position.y, 28.f);
}

// A document never told the surface behaves exactly as it always did — which is
// every existing test, and any host that does not call SetSurfaceSize.
TEST(UIScale, ADocumentThatIsNeverToldTheSurfaceFallsBackToItsViewport) {
    Doc d;
    d.doc.root().style().alignItems = Align::FlexStart;
    UIElement* box = d.Add("box", 100.f, 40.f);
    d.doc.SetScaleSettings(scaled(1920.f, 1080.f, 0.f));
    d.doc.Layout(3840.f, 2160.f);   // no SetSurfaceSize
    EXPECT_FLOAT_EQ(d.doc.scale(), 2.0f) << "the viewport was not used as the fallback surface";
    EXPECT_FLOAT_EQ(box->layout().size.x, 200.f);
}
