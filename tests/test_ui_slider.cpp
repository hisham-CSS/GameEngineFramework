// <Slider> — a real continuous value you can drag, and the generalised pointer
// capture underneath it.
//
// Pure CPU. The property that matters most is the one that used to be
// impossible: a press CAPTURES the pointer, so dragging past either end of the
// track keeps working. That is the gesture every user makes at 0% and 100%,
// and before this the control simply went dead the moment the cursor left its
// box — PointerMove is dispatched only to what is under the cursor THIS frame.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UISlider.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// A 200x20 horizontal slider at the origin of a 400x400 document, bound
// two-way to `vol`. 200px wide makes the cursor->value arithmetic readable:
// x=50 is 25%.
struct Rig {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIStyleSheet sheet;
    UIBinder binder;
    std::vector<std::string> errors;

    bool Load(const char* markup, const char* css =
                  "#s { width: 200px; height: 20px; }") {
        ctx.RegisterSource("s", &src);
        if (!sheet.ParseString(css, "t.cstyle")) return false;
        if (!UIMarkup::LoadInto(doc, markup, errors, "t.cxml")) return false;
        sheet.ApplyTo(doc.root());
        binder.Rebuild(doc, ctx, "t.cxml", &sheet);
        binder.UpdateToTarget();
        doc.Layout(400.f, 400.f);
        return true;
    }

    UIElement* slider() { return doc.root().Find("s"); }
    UISliderState& state() { return *slider()->slider(); }

    // One host frame in the order UIWorld runs it: pointer, then element ->
    // source, then source -> element, then layout.
    void Point(float x, float y, bool down, bool inside = true) {
        UIPointerState p;
        p.position = { x, y };
        p.inside = inside;
        p.buttonDown = down;
        doc.UpdatePointer(p);
        binder.UpdateToSource();
        binder.UpdateToTarget();
        if (doc.ConsumeScrollDirty()) doc.Layout(400.f, 400.f);
    }
    void Key(UIKey k) {
        UIKeyboardState kb;
        kb.keys.push_back(UIKeyEvent{ k });
        doc.UpdateKeyboard(kb);
        binder.UpdateToSource();
        binder.UpdateToTarget();
        if (doc.ConsumeScrollDirty()) doc.Layout(400.f, 400.f);
    }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
};

const char* kSimple =
    R"(<UI data-source="s"><Slider name="s" bind-value="vol" min="0" max="1"/></UI>)";

} // namespace

// ------------------------------------------------------------ the value

TEST(UISlider, QuantisesAndClampsThroughOnePlace) {
    UISliderState sl;
    sl.min = 0.0f; sl.max = 1.0f;
    EXPECT_TRUE(sl.SetValue(0.5f));
    EXPECT_FLOAT_EQ(sl.value, 0.5f);
    EXPECT_FALSE(sl.SetValue(0.5f)) << "an unchanged write reported a change";

    sl.SetValue(9.0f);
    EXPECT_FLOAT_EQ(sl.value, 1.0f) << "not clamped to max";
    sl.SetValue(-9.0f);
    EXPECT_FLOAT_EQ(sl.value, 0.0f) << "not clamped to min";

    // A step is what a "quality 1..5" slider wants and what a volume slider
    // must NOT have — the whole complaint that started this was a control that
    // moved in tenths.
    sl.step = 0.25f;
    sl.SetValue(0.6f);
    EXPECT_FLOAT_EQ(sl.value, 0.5f) << "not quantised to the step";
}

TEST(UISlider, NormalisesAgainstArbitraryBounds) {
    UISliderState sl;
    sl.min = 60.0f; sl.max = 110.0f;   // a field-of-view slider
    sl.SetNormalised(0.5f);
    EXPECT_FLOAT_EQ(sl.value, 85.0f);
    EXPECT_FLOAT_EQ(sl.normalised(), 0.5f);
    sl.SetValue(110.0f);
    EXPECT_FLOAT_EQ(sl.normalised(), 1.0f);
}

// ------------------------------------------------------------- the drag

TEST(UISlider, APressJumpsToTheValueUnderTheCursor) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();
    ASSERT_NE(r.slider(), nullptr);
    ASSERT_FLOAT_EQ(r.state().value, 0.0f);

    r.Point(50.f, 10.f, true);          // 25% along a 200px track
    EXPECT_NEAR(r.state().value, 0.25f, 0.001f)
        << "a press did not jump to the pressed value";
    EXPECT_NEAR(r.src.GetNumber("vol"), 0.25f, 0.001f)
        << "the value never reached the data source";
}

// THE POINT OF THE WHOLE CHANGE. Press, then drag the cursor far outside the
// element in both directions. Without a capture, PointerMove goes only to what
// is under the cursor, so the slider would stop responding the instant the
// cursor left it — at exactly 0% and 100%, where users always overshoot.
TEST(UISlider, ADragOutsideTheTrackKeepsWorking) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();

    r.Point(100.f, 10.f, true);
    ASSERT_NEAR(r.state().value, 0.5f, 0.001f);

    // Way past the right edge, and vertically off the element entirely.
    r.Point(9000.f, 5000.f, true);
    EXPECT_NEAR(r.state().value, 1.0f, 0.001f)
        << "the drag was lost the moment the cursor left the element";

    // ...and back past the left edge.
    r.Point(-9000.f, -5000.f, true);
    EXPECT_NEAR(r.state().value, 0.0f, 0.001f);

    // Release outside: the capture ends, and a later move does nothing.
    r.Point(-9000.f, -5000.f, false);
    r.Point(150.f, 10.f, false);
    EXPECT_NEAR(r.state().value, 0.0f, 0.001f)
        << "the slider kept tracking after the button was released";
}

TEST(UISlider, AMoveWithNoPressDoesNotChangeTheValue) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();
    r.Point(150.f, 10.f, false);
    EXPECT_FLOAT_EQ(r.state().value, 0.0f) << "hovering moved the value";
}

// A slider inside a scroller must not scroll it: the capture returns early, so
// nothing underneath sees the press at all.
TEST(UISlider, ACapturedSliderSwallowsThePressFromWhatIsUnderIt) {
    Rig r;
    ASSERT_TRUE(r.Load(
        R"(<UI data-source="s"><Element name="row" on-click="boom">)"
        R"(<Slider name="s" bind-value="vol" min="0" max="1"/></Element></UI>)",
        "#row { width: 200px; height: 20px; } #s { width: 200px; height: 20px; }"))
        << r.firstError();

    int fired = 0;
    r.src.AddAction("boom", [&] { ++fired; });
    r.binder.Rebuild(r.doc, r.ctx, "t.cxml", &r.sheet);

    r.Point(100.f, 10.f, true);
    r.Point(100.f, 10.f, false);
    EXPECT_EQ(fired, 0) << "the element under the slider got a click";
    EXPECT_NEAR(r.state().value, 0.5f, 0.001f);
}

// ---------------------------------------------------------- the keyboard

TEST(UISlider, ArrowsStepAlongTheSlidersOwnAxis) {
    Rig r;
    ASSERT_TRUE(r.Load(
        R"(<UI data-source="s"><Slider name="s" bind-value="vol")"
        R"( min="0" max="1" key-step="0.1"/></UI>)")) << r.firstError();
    r.doc.SetFocus(r.slider());

    r.Key(UIKey::Right);
    EXPECT_NEAR(r.state().value, 0.1f, 0.001f);
    r.Key(UIKey::Right);
    EXPECT_NEAR(r.state().value, 0.2f, 0.001f);
    r.Key(UIKey::Left);
    EXPECT_NEAR(r.state().value, 0.1f, 0.001f);
    EXPECT_NEAR(r.src.GetNumber("vol"), 0.1f, 0.001f) << "keys did not reach the source";

    r.Key(UIKey::Home);
    EXPECT_FLOAT_EQ(r.state().value, 0.0f);
    r.Key(UIKey::End);
    EXPECT_FLOAT_EQ(r.state().value, 1.0f);
}

// A horizontal slider leaves Up/Down alone so they still reach navigation.
TEST(UISlider, AHorizontalSliderDoesNotSwallowTheCrossAxisArrows) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();
    r.doc.SetFocus(r.slider());
    r.state().SetValue(0.5f);
    r.Key(UIKey::Up);
    r.Key(UIKey::Down);
    EXPECT_FLOAT_EQ(r.state().value, 0.5f) << "the cross-axis arrows moved the value";
}

// ------------------------------------------------------- the two-way link

TEST(UISlider, TheSourceCanDriveTheSliderAndIsClampedByTheSameRules) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();

    r.src.SetNumber("vol", 0.75f);
    r.binder.UpdateToTarget();
    EXPECT_NEAR(r.state().value, 0.75f, 0.001f) << "the source did not drive the slider";

    // A source that writes out of range must not put the thumb off the track.
    r.src.SetNumber("vol", 3.0f);
    r.binder.UpdateToTarget();
    EXPECT_FLOAT_EQ(r.state().value, 1.0f)
        << "an out-of-range source value was not clamped by the slider's own rules";
}

TEST(UISlider, PushesANumberNotAString) {
    Rig r;
    ASSERT_TRUE(r.Load(kSimple)) << r.firstError();
    r.Point(100.f, 10.f, true);
    const UIValue v = r.src.Get("vol");
    EXPECT_EQ(v.kind, UIValue::Kind::Number)
        << "a slider pushed " << v.KindName() << " - every consumer would have to parse it";
}

// ------------------------------------------------------------ diagnostics

TEST(UISliderMarkup, RejectsSliderOnlyAttributesElsewhereAndViceVersa) {
    UIDocument doc;
    std::vector<std::string> errs;
    EXPECT_FALSE(UIMarkup::LoadInto(doc,
        R"(<UI><Element min="0"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("only valid on a <Slider>"), std::string::npos) << errs[0];

    errs.clear();
    UIDocument doc2;
    EXPECT_FALSE(UIMarkup::LoadInto(doc2,
        R"(<UI><Slider maxlength="4"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("only valid on a <TextField>"), std::string::npos) << errs[0];
}

TEST(UISliderMarkup, RejectsAZeroRangeAndABadBound) {
    UIDocument doc;
    std::vector<std::string> errs;
    EXPECT_FALSE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider min="1" max="1"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("no range"), std::string::npos) << errs[0];

    errs.clear();
    UIDocument doc2;
    EXPECT_FALSE(UIMarkup::LoadInto(doc2,
        R"(<UI><Slider min="lots"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("expected a number"), std::string::npos) << errs[0];
}

TEST(UISliderMarkup, ASliderIsFocusableByTypeLikeAFieldIs) {
    UIDocument doc;
    std::vector<std::string> errs;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Slider name="s"/></UI>)", errs, "t.cxml"))
        << (errs.empty() ? "" : errs[0]);
    ASSERT_NE(doc.root().Find("s"), nullptr);
    EXPECT_TRUE(doc.root().Find("s")->isFocusable())
        << "a slider you cannot focus cannot be driven by a keyboard or a pad";
}

TEST(UISliderMarkup, ClampsItsAuthoredStartingValue) {
    UIDocument doc;
    std::vector<std::string> errs;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" min="0" max="1" value="5"/></UI>)", errs, "t.cxml"))
        << (errs.empty() ? "" : errs[0]);
    EXPECT_FLOAT_EQ(doc.root().Find("s")->slider()->value, 1.0f);
}
