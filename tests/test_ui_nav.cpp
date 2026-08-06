// Directional navigation — the UI's third input state, and the whole of what a
// gamepad needs.
//
// The point of the design is that ALL of this is testable without a controller:
// the UI consumes intents, not devices, and the auto-repeat clock is a pure
// function of a held direction and dt. Only the last inch — GLFW telling us
// which physical button moved — is untestable here, and it is one line per
// binding.
#include <gtest/gtest.h>

#include "Engine.h"
#include "ui_shipped_hud.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UINav.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <cmath>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

UINavState move(UINavDir d) {
    UINavState n;
    n.moves.push_back(d);
    return n;
}

} // namespace

// ------------------------------------------------------- the repeat clock

// A NEW direction must fire immediately. Waiting out the delay before the first
// move is the difference between a menu that answers and one that feels broken.
TEST(UINavRepeat, ANewDirectionFiresAtOnceThenPausesThenRuns) {
    UINavRepeater r;
    r.delay = 0.4f;
    r.interval = 0.1f;

    EXPECT_EQ(r.Tick(UINavDir::Down, 0.016f), 1) << "the first press did nothing";
    // Through the initial pause: silence.
    int during = 0;
    for (int i = 0; i < 20; ++i) during += r.Tick(UINavDir::Down, 0.016f);  // 0.32s
    EXPECT_EQ(during, 0) << "it started repeating before the delay elapsed";

    // Past the delay, it runs at the interval.
    int after = 0;
    for (int i = 0; i < 30; ++i) after += r.Tick(UINavDir::Down, 0.016f);   // 0.48s
    EXPECT_GE(after, 3);
    EXPECT_LE(after, 6) << "it repeated far faster than the interval";
}

TEST(UINavRepeat, ReleasingStopsItAndTheNextPressIsImmediateAgain) {
    UINavRepeater r;
    EXPECT_EQ(r.Tick(UINavDir::Left, 0.016f), 1);
    EXPECT_EQ(r.Tick(UINavDir::None, 0.016f), 0);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(r.Tick(UINavDir::None, 0.016f), 0);
    EXPECT_EQ(r.Tick(UINavDir::Left, 0.016f), 1) << "a fresh press was not immediate";
}

// Changing direction mid-hold is a NEW direction, not a continuation.
TEST(UINavRepeat, ChangingDirectionFiresImmediately) {
    UINavRepeater r;
    ASSERT_EQ(r.Tick(UINavDir::Up, 0.016f), 1);
    EXPECT_EQ(r.Tick(UINavDir::Down, 0.016f), 1);
}

// A long frame must not turn into an unbounded burst of moves.
TEST(UINavRepeat, ALongStallIsBoundedRatherThanABurst) {
    UINavRepeater r;
    r.delay = 0.0f;
    r.interval = 0.01f;
    r.Tick(UINavDir::Down, 0.016f);
    const int n = r.Tick(UINavDir::Down, 10.0f);   // a ten-second stall
    EXPECT_LE(n, 9) << "a stall produced " << n << " moves";
}

TEST(UINavAxes, TheDominantAxisWinsSoADiagonalDoesNotFireBoth) {
    EXPECT_EQ(NavDirFromAxes(0.0f, 0.0f), UINavDir::None);
    EXPECT_EQ(NavDirFromAxes(0.2f, 0.2f), UINavDir::None) << "below threshold";
    EXPECT_EQ(NavDirFromAxes(0.9f, 0.1f), UINavDir::Right);
    EXPECT_EQ(NavDirFromAxes(-0.9f, 0.1f), UINavDir::Left);
    EXPECT_EQ(NavDirFromAxes(0.1f, 0.9f), UINavDir::Up);
    EXPECT_EQ(NavDirFromAxes(0.1f, -0.9f), UINavDir::Down);
    // A true diagonal picks one rather than both.
    const UINavDir d = NavDirFromAxes(0.8f, 0.8f);
    EXPECT_TRUE(d == UINavDir::Right || d == UINavDir::Up);
}

// ----------------------------------------------------------- focus moves

// With nothing focused, the first press must land SOMEWHERE. Otherwise a pad
// on a freshly opened menu does nothing and reads as a dead controller.
TEST(UINav, TheFirstMoveFocusesSomethingFromNothing) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(nullptr);
    ASSERT_EQ(hud.doc().focused(), nullptr);

    EXPECT_TRUE(hud.doc().UpdateNav(move(UINavDir::Down)));
    EXPECT_NE(hud.doc().focused(), nullptr)
        << "the first d-pad press on a fresh menu did nothing";
}

TEST(UINav, MovesWalkForwardsAndBackwards) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(nullptr);

    hud.doc().UpdateNav(move(UINavDir::Down));
    UIElement* first = hud.doc().focused();
    ASSERT_NE(first, nullptr);

    hud.doc().UpdateNav(move(UINavDir::Down));
    UIElement* second = hud.doc().focused();
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first) << "a second move did not advance focus";

    hud.doc().UpdateNav(move(UINavDir::Up));
    EXPECT_EQ(hud.doc().focused(), first) << "Up did not walk back";
}

TEST(UINav, ActivateIsTheSamePathAsAClick) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(hud.find("scoreButton"));
    ASSERT_EQ(hud.data().GetInt("score"), 0);

    UINavState n;
    n.activate = true;
    EXPECT_TRUE(hud.doc().UpdateNav(n));
    EXPECT_EQ(hud.data().GetInt("score"), 100)
        << "the pad's confirm did not run the button's action";
}

// Back means "back out of what I am in". Blurring is the one meaning true in
// every UI; anything beyond it is the game's to decide, so an unhandled back
// is REPORTED rather than invented.
TEST(UINav, BackBlursAndReportsWhetherItHadAnythingToDo) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(hud.find("scoreButton"));

    UINavState n;
    n.back = true;
    EXPECT_TRUE(hud.doc().UpdateNav(n)) << "back with focus reported nothing consumed";
    EXPECT_EQ(hud.doc().focused(), nullptr);

    EXPECT_FALSE(hud.doc().UpdateNav(n))
        << "back with NOTHING focused claimed to consume it - the host could "
           "never use it to close the menu";
}

// ------------------------------------------------- sliders and the pad

// The one place a pad and a keyboard have to agree: a stick pushed left on a
// focused slider changes the value, it does not jump to the previous control.
TEST(UINav, AFocusedSliderEatsTheAlongAxisMoves) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="a" text="a"/>)"
        R"(<Slider name="s" min="0" max="1" value="0.5" key-step="0.1"/>)"
        R"(<Button name="b" text="b"/></UI>)", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);

    UIElement* s = doc.root().Find("s");
    ASSERT_NE(s, nullptr);
    ASSERT_NE(s->slider(), nullptr);
    doc.SetFocus(s);

    doc.UpdateNav(move(UINavDir::Right));
    EXPECT_EQ(doc.focused(), s) << "the move left the slider instead of changing it";
    EXPECT_NEAR(s->slider()->value, 0.6f, 0.001f);

    doc.UpdateNav(move(UINavDir::Left));
    EXPECT_NEAR(s->slider()->value, 0.5f, 0.001f);

    // The CROSS axis still navigates, so a horizontal slider is not a trap.
    doc.UpdateNav(move(UINavDir::Down));
    EXPECT_NE(doc.focused(), s) << "Down did not leave a horizontal slider";
}

// ------------------------------------------------------------- the pages

TEST(UINavPaging, ShouldersStepTheTabViewAndWrap) {
    ShippedHud hud;
    hud.Frame();
    auto* ad = hud.assets();
    ASSERT_NE(ad, nullptr);
    UITabView* tv = ad->tabView("demo");
    ASSERT_NE(tv, nullptr);
    const int n = int(tv->count());
    ASSERT_GE(n, 2);

    tv->Select(0);
    ad->PageTabs(+1);
    EXPECT_EQ(tv->selected(), 1);

    // Wrapping rather than clamping: a pad user cycling right off the end
    // expects the first page, and there is no visible wall to bump into.
    tv->Select(n - 1);
    ad->PageTabs(+1);
    EXPECT_EQ(tv->selected(), 0) << "paging past the last tab did not wrap";

    tv->Select(0);
    ad->PageTabs(-1);
    EXPECT_EQ(tv->selected(), n - 1) << "paging before the first tab did not wrap";
}

TEST(UINav, AnEmptyNavStateIsANoOp) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(hud.find("scoreButton"));
    UIElement* before = hud.doc().focused();

    EXPECT_FALSE(hud.doc().UpdateNav(UINavState{}));
    EXPECT_EQ(hud.doc().focused(), before);
    EXPECT_EQ(hud.data().GetInt("score"), 0);
}


// ------------------------------------------- focus scopes / the stack (U25e)
//
// Reported as: menus opening "just seem to add navigation rather than taking
// the navigation and having a back button in a stack".
//
// FocusNext used to collect from the DOCUMENT root, so every visible control
// was in one flat ring: walking off the end of an open settings page wandered
// back up to the main verbs. A scope confines the ring to its own subtree.

namespace {

// A verb column and a panel, each a scope, with the panel gated on a bool --
// exactly the shape menu.cxml uses.
struct Scoped {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIBinder binder;
    std::vector<std::string> errors;
    int backs = 0;

    bool Load() {
        ctx.RegisterSource("s", &src);
        src.SetBool("open", false);
        src.AddAction("close", [this] { ++backs; src.SetBool("open", false); });
        const char* xml =
            R"(<UI data-source="s">)"
            R"(<Element name="verbs" focus-scope="true" if="!open">)"
            R"(  <Button name="v1" text="1"/><Button name="v2" text="2"/>)"
            R"(</Element>)"
            R"(<Element name="panel" focus-scope="true" if="open" on-back="close">)"
            R"(  <Button name="p1" text="a"/><Button name="p2" text="b"/>)"
            R"(</Element></UI>)";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        binder.Rebuild(doc, ctx, "t.cxml");
        binder.UpdateToTarget();
        doc.Layout(400.f, 400.f);
        return true;
    }
    // One frame: bindings settle, then nav runs (which syncs the stack).
    void Frame(const UINavState& n = UINavState{}) {
        binder.UpdateToTarget();
        doc.UpdateNav(n);
    }
    std::string focusName() {
        UIElement* f = doc.focused();
        return f ? f->name() : std::string("<none>");
    }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
};

UINavState navMove(UINavDir d) { UINavState n; n.moves.push_back(d); return n; }
UINavState navBack() { UINavState n; n.back = true; return n; }

} // namespace

TEST(UIFocusScope, NavigationStaysInsideTheOpenPanel) {
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();

    // Pushing a scope already SEEDS focus on its first control -- a panel
    // that opens with nothing focused cannot be used with a pad at all -- so
    // the first explicit move goes to the second one.
    EXPECT_EQ(s.focusName(), "v1") << "the root scope did not seed focus";
    s.Frame(navMove(UINavDir::Down));
    EXPECT_EQ(s.focusName(), "v2");

    // Open the panel. The stack pushes and focus moves INTO it.
    s.src.SetBool("open", true);
    s.Frame();
    EXPECT_EQ(s.focusName(), "p1") << "opening a panel did not take focus";

    // Walk the panel repeatedly: it must never escape into the verbs, which is
    // the entire complaint.
    for (int i = 0; i < 12; ++i) {
        s.Frame(navMove(UINavDir::Down));
        const std::string n = s.focusName();
        EXPECT_TRUE(n == "p1" || n == "p2")
            << "navigation escaped the open panel to " << n << " after "
            << i << " moves";
    }
}

// Closing returns you WHERE YOU WERE, not to the top of the list.
TEST(UIFocusScope, ClosingRestoresTheFocusThatOpenedIt) {
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();

    // Seeded on v1 by the push; one move lands on v2, which is where we open
    // the panel FROM and therefore where closing it must return.
    s.Frame(navMove(UINavDir::Down));
    const std::string opener = s.focusName();
    ASSERT_EQ(opener, "v2");

    s.src.SetBool("open", true);
    s.Frame();
    ASSERT_EQ(s.focusName(), "p1");

    s.src.SetBool("open", false);
    s.Frame();
    EXPECT_EQ(s.focusName(), opener)
        << "closing dumped focus somewhere other than the verb that opened it";
}

// Back does not close the panel itself -- a panel is visible because an if=
// reads app state, so the DOCUMENT asks and the APP decides.
TEST(UIFocusScope, BackInvokesTheScopesOnBackRatherThanClosingItDirectly) {
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();
    s.src.SetBool("open", true);
    s.Frame();
    ASSERT_EQ(s.focusName(), "p1");
    ASSERT_EQ(s.backs, 0);

    s.Frame(navBack());
    EXPECT_EQ(s.backs, 1) << "back did not reach the scope on-back handler";
    // The action set the bool; the panel hides and the stack pops next frame.
    s.Frame();
    EXPECT_EQ(s.focusName(), "v1")
        << "the stack did not pop after the app closed the panel";
}

// With no scope open, back is a blur and REPORTS that it had nothing to close,
// so a host can give it its own meaning.
TEST(UIFocusScope, BackWithNoScopeOpenIsJustABlur) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="b" text="b"/></UI>)", errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    doc.SetFocus(doc.root().Find("b"));

    EXPECT_TRUE(doc.UpdateNav(navBack()));
    EXPECT_EQ(doc.focused(), nullptr);
    EXPECT_FALSE(doc.UpdateNav(navBack()))
        << "back claimed to consume with nothing focused and no scope open";
}

// Nesting: a panel inside a panel pushes again, and backing out unwinds one
// level at a time rather than jumping to the root.
TEST(UIFocusScope, ScopesNestAndUnwindOneLevelAtATime) {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIBinder binder;
    std::vector<std::string> errors;
    ctx.RegisterSource("s", &src);
    src.SetBool("a", false);
    src.SetBool("b", false);

    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI data-source="s">)"
        R"(<Element name="root" focus-scope="true"><Button name="r1" text="r"/>)"
        R"(  <Element name="outer" focus-scope="true" if="a">)"
        R"(    <Button name="o1" text="o"/>)"
        R"(    <Element name="inner" focus-scope="true" if="b">)"
        R"(      <Button name="i1" text="i"/></Element>)"
        R"(  </Element></Element></UI>)", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);
    binder.Rebuild(doc, ctx, "t.cxml");

    const auto frame = [&](const UINavState& n = UINavState{}) {
        binder.UpdateToTarget();
        doc.Layout(400.f, 400.f);
        doc.UpdateNav(n);
    };
    const auto name = [&] {
        UIElement* f = doc.focused();
        return f ? f->name() : std::string("<none>");
    };

    frame();
    EXPECT_EQ(name(), "r1") << "the root scope did not take focus";

    src.SetBool("a", true);
    frame();
    EXPECT_EQ(name(), "o1");

    src.SetBool("b", true);
    frame();
    EXPECT_EQ(name(), "i1") << "the nested scope did not take focus";

    // Unwind ONE level: back to the outer panel, not to the root.
    src.SetBool("b", false);
    frame();
    EXPECT_EQ(name(), "o1") << "closing the inner scope skipped a level";

    src.SetBool("a", false);
    frame();
    EXPECT_EQ(name(), "r1");
}

// A scope whose remembered element has been destroyed must not resurrect a
// dangling pointer. Every other cached UIElement* here is revalidated; so is
// this one.
TEST(UIFocusScope, ARememberedElementThatDiedDoesNotComeBack) {
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();
    ASSERT_EQ(s.focusName(), "v1") << "the root scope did not seed focus";

    s.src.SetBool("open", true);
    s.Frame();
    ASSERT_EQ(s.focusName(), "p1");

    // The verb column loses the element the panel remembered.
    UIElement* verbs = s.doc.root().Find("verbs");
    ASSERT_NE(verbs, nullptr);
    ASSERT_NE(verbs->RemoveChild(s.doc.root().Find("v1")), nullptr);

    s.src.SetBool("open", false);
    s.Frame();
    UIElement* f = s.doc.focused();
    if (f) {
        EXPECT_NE(s.doc.root().Find(f->name()), nullptr) << "focus is dangling";
    }
    SUCCEED();
}

TEST(UIFocusScopeMarkup, RejectsANonBooleanFocusScope) {
    UIDocument doc;
    std::vector<std::string> errs;
    EXPECT_FALSE(UIMarkup::LoadInto(doc,
        R"(<UI><Element focus-scope="sometimes"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("focus-scope"), std::string::npos) << errs[0];
}

TEST(UIFocusScopeMarkup, RejectsAnUnknownOnEventAndNamesBack) {
    UIDocument doc;
    std::vector<std::string> errs;
    EXPECT_FALSE(UIMarkup::LoadInto(doc,
        R"(<UI><Element on-retreat="x"/></UI>)", errs, "t.cxml"));
    ASSERT_FALSE(errs.empty());
    EXPECT_NE(errs[0].find("back"), std::string::npos)
        << "the diagnostic does not list back: " << errs[0];
}


// ------------------------------------------- analog vs digital slider (U25f)
//
// Reported: "controller vol slider controls seem to snap to every 5 percent".
// They did -- the stick was being reported only as discrete d-pad-shaped moves,
// so a continuum was being driven in keyStep notches.
//
// A control that can use the analog value gets the analog value. Discrete moves
// still traverse a MENU, because a list has positions rather than a continuum.

namespace {

UINavState navStick(float x, float dt) {
    UINavState n;
    n.axisX = x;
    n.dt = dt;
    // The synthesiser reports BOTH from one stick: the repeater's discrete move
    // and the raw axis. Reproducing that here is the point -- the value must
    // not move twice.
    if (x > 0.5f)  n.moves.push_back(UINavDir::Right);
    if (x < -0.5f) n.moves.push_back(UINavDir::Left);
    return n;
}

} // namespace

TEST(UINavAnalog, AStickMovesASliderContinuouslyRatherThanInNotches) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" min="0" max="1" value="0" )"
        R"(key-step="0.05"/></UI>)", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    UIElement* s = doc.root().Find("s");
    ASSERT_NE(s, nullptr);
    s->slider()->analogSeconds = 1.0f;   // full range in one second
    doc.SetFocus(s);

    // 23 frames at 60Hz is 0.3833s, so at one range per second the value lands
    // on 0.3833 -- deliberately NOT a multiple of the 0.05 keyStep, which is
    // what makes the notch check below able to tell the two apart at all. (Half
    // a second would have landed on exactly 0.5, which IS a notch, and the
    // first version of this test could not have failed.)
    for (int i = 0; i < 23; ++i) doc.UpdateNav(navStick(1.0f, 1.0f / 60.0f));
    EXPECT_NEAR(s->slider()->value, 23.0f / 60.0f, 0.02f)
        << "a held stick did not move the slider at a RATE";

    // ...and it is not landing on notch boundaries.
    const float v = s->slider()->value;
    const float notch = v / 0.05f;
    EXPECT_GT(std::abs(notch - std::round(notch)), 1e-3f)
        << "the value is quantised to key-step, so the stick is still digital";
}

// The discrete move and the axis arrive together from ONE stick. If both were
// applied the slider would move twice as fast as asked.
TEST(UINavAnalog, TheAnalogPathSuppressesTheDiscreteMoveFromTheSameStick) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" min="0" max="1" value="0" key-step="0.25"/></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    UIElement* s = doc.root().Find("s");
    s->slider()->analogSeconds = 10.0f;   // deliberately slow
    doc.SetFocus(s);

    // One frame: the analog contribution is tiny. If the discrete move ALSO
    // applied, the value would jump by a whole 0.25 notch.
    doc.UpdateNav(navStick(1.0f, 1.0f / 60.0f));
    EXPECT_LT(s->slider()->value, 0.05f)
        << "the value moved by a key-step notch as well as by the stick";
}

// A D-PAD has no analog value, so it must still move in notches -- a digital
// control needs a digital grain or it cannot be aimed at all.
TEST(UINavAnalog, ADPadStillMovesInNotches) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" min="0" max="1" value="0" key-step="0.1"/></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    UIElement* s = doc.root().Find("s");
    doc.SetFocus(s);

    // A d-pad press: a discrete move with NO axis deflection.
    UINavState n;
    n.moves.push_back(UINavDir::Right);
    n.dt = 1.0f / 60.0f;
    doc.UpdateNav(n);
    EXPECT_NEAR(s->slider()->value, 0.1f, 1e-4f)
        << "a d-pad press did not move by exactly one key-step";
}

// A stick below the threshold does nothing at all, so a resting pad with a
// little drift does not creep the volume.
TEST(UINavAnalog, ARestingStickDoesNotCreepTheValue) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" min="0" max="1" value="0.5"/></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    UIElement* s = doc.root().Find("s");
    doc.SetFocus(s);

    for (int i = 0; i < 120; ++i) doc.UpdateNav(navStick(0.2f, 1.0f / 60.0f));
    EXPECT_FLOAT_EQ(s->slider()->value, 0.5f) << "a resting stick crept the value";
}

// clear() must drop the axes too, or one frame of deflection would keep driving
// a focused slider with the pad sitting untouched on the desk.
TEST(UINavAnalog, ClearingTheStateDropsTheAxes) {
    UINavState n = navStick(1.0f, 0.016f);
    ASSERT_GT(n.axisX, 0.0f);
    n.clear();
    EXPECT_FLOAT_EQ(n.axisX, 0.0f);
    EXPECT_FLOAT_EQ(n.axisY, 0.0f);
    EXPECT_FLOAT_EQ(n.dt, 0.0f);
    EXPECT_TRUE(n.empty());
}


// ------------------------------------------ spatial navigation (U26e)
//
// Reported: "horizontally spaced buttons in a row still consume the up and down
// inputs ... you have to press down 3 times to get past the graphical settings".
//
// Exactly right, and it is what FocusMove was left as a seam for. Tab order is
// one-dimensional; a row of three chips shares a y, so a linear walk treats each
// as "after" the last and Down stepped through all three. Geometry does not: a
// sibling at the same y is not below anything.

namespace {

// A column: one button, a ROW of three, then another button. The shape the
// complaint is about.
struct Grid {
    UIDocument doc;
    std::vector<std::string> errors;
    UIStyleSheet sheet;

    bool Load() {
        const char* css =
            "#col { width: 400px; flex-direction: column; }"
            "#row { width: 400px; flex-direction: row; }"
            ".item { width: 120px; height: 40px; }"
            ".chip { width: 100px; height: 32px; }";
        if (!sheet.ParseString(css, "t.cstyle")) return false;
        const char* xml =
            R"(<UI><Element name="col">)"
            R"(<Button name="above" class="item" text="a"/>)"
            R"(<Element name="row" class="row">)"
            R"(  <Button name="low"  class="chip" text="LOW"/>)"
            R"(  <Button name="med"  class="chip" text="MED"/>)"
            R"(  <Button name="high" class="chip" text="HIGH"/>)"
            R"(</Element>)"
            R"(<Button name="below" class="item" text="b"/>)"
            R"(</Element></UI>)";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        sheet.ApplyTo(doc.root());
        doc.Layout(600.f, 600.f);
        return true;
    }
    std::string focusName() {
        UIElement* f = doc.focused();
        return f ? f->name() : std::string("<none>");
    }
    void Move(UINavDir d) { doc.UpdateNav(move(d)); }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
};

} // namespace

// THE COMPLAINT, directly. One press past the whole row, not three.
TEST(UISpatialNav, DownStepsPastAWholeRowInOnePress) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();
    g.doc.SetFocus(g.doc.root().Find("above"));

    g.Move(UINavDir::Down);
    EXPECT_EQ(g.focusName(), "low") << "Down did not reach the row";

    g.Move(UINavDir::Down);
    EXPECT_EQ(g.focusName(), "below")
        << "Down walked ALONG the row instead of past it - this is the reported "
           "bug: three presses to clear three chips";
}

// ...and the row is still reachable sideways, which is the other half: skipping
// it vertically is only correct if left/right still works within it.
TEST(UISpatialNav, LeftAndRightMoveWithinTheRow) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();
    g.doc.SetFocus(g.doc.root().Find("low"));

    g.Move(UINavDir::Right);
    EXPECT_EQ(g.focusName(), "med");
    g.Move(UINavDir::Right);
    EXPECT_EQ(g.focusName(), "high");
    g.Move(UINavDir::Left);
    EXPECT_EQ(g.focusName(), "med");
}

// Up out of the row lands on the thing above it, not on a sibling chip.
TEST(UISpatialNav, UpLeavesTheRowRatherThanWalkingIt) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();
    g.doc.SetFocus(g.doc.root().Find("high"));
    g.Move(UINavDir::Up);
    EXPECT_EQ(g.focusName(), "above");
}

// NO WRAP. Down at the bottom does nothing, which is what every console UI does
// and what stops a menu feeling like a carousel.
TEST(UISpatialNav, TheEdgesDoNotWrap) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();

    g.doc.SetFocus(g.doc.root().Find("below"));
    g.Move(UINavDir::Down);
    EXPECT_EQ(g.focusName(), "below") << "Down wrapped round from the bottom";

    g.doc.SetFocus(g.doc.root().Find("above"));
    g.Move(UINavDir::Up);
    EXPECT_EQ(g.focusName(), "above") << "Up wrapped round from the top";
}

// Right from the LAST chip must not jump to another row just because something
// over there happens to be to the right. Nothing is to its right in its band.
TEST(UISpatialNav, RightFromTheEndOfARowDoesNotJumpRows) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();
    g.doc.SetFocus(g.doc.root().Find("high"));
    g.Move(UINavDir::Right);
    EXPECT_EQ(g.focusName(), "high");
}

// Geometry cannot answer before a Layout. Falling back to the linear walk keeps
// a pad working; trapping focus would look like a dead controller.
TEST(UISpatialNav, FallsBackToTheLinearWalkWithNoGeometry) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="a" text="a"/><Button name="b" text="b"/></UI>)",
        errors, "t.cxml"));
    // NO Layout call: every rect is degenerate.
    doc.SetFocus(doc.root().Find("a"));
    doc.UpdateNav(move(UINavDir::Down));
    EXPECT_EQ(doc.focused(), doc.root().Find("b"))
        << "with no geometry the move did nothing - focus is trapped";
}


// ------------------------------------------- the nearest row wins (U26f)
//
// Reported: "when i scroll passed quality settings - it jumps over vsync option
// to the text box".
//
// The exact geometry of the shipped settings page: a LEFT-aligned row of chips,
// then a row whose only control is pushed to the FAR RIGHT (its row is
// space-between), then a FULL-WIDTH field. Scored as one number, the field wins
// on a zero off-axis distance despite being much further away, and the
// right-aligned control is skipped. No penalty value fixes that -- it is a false
// comparison between a near row and a far one.

namespace {

// 600 wide, mirroring the panel: chips at the left, a right-aligned toggle one
// row down, a full-width field below that.
struct SkipRig {
    UIDocument doc;
    UIStyleSheet sheet;
    std::vector<std::string> errors;

    bool Load() {
        const char* css =
            "#panel { width: 600px; flex-direction: column; align-items: flex-start; }"
            "#chips { flex-direction: row; gap: 8; }"
            ".chip  { width: 100px; height: 32px; }"
            "#optrow { width: 600px; flex-direction: row; justify-content: space-between;"
            "          align-items: center; padding: 4px 0; }"
            "#field { width: 600px; height: 40px; margin: 20px 0 0 0; }";
        if (!sheet.ParseString(css, "t.cstyle")) return false;
        const char* xml =
            R"(<UI><Element name="panel">)"
            R"(<Element name="chips">)"
            R"(  <Button name="low" class="chip" text="LOW"/>)"
            R"(  <Button name="med" class="chip" text="MED"/>)"
            R"(  <Button name="high" class="chip" text="HIGH"/>)"
            R"(</Element>)"
            R"(<Element name="optrow">)"
            R"(  <Label name="vsyncLabel" text="VSYNC"/>)"
            R"(  <Button name="vsync" class="chip" text="ON"/>)"
            R"(</Element>)"
            R"(<TextField name="field"/>)"
            R"(</Element></UI>)";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        sheet.ApplyTo(doc.root());
        doc.Layout(800.f, 800.f);
        return true;
    }
    std::string focusName() {
        UIElement* f = doc.focused();
        return f ? f->name() : std::string("<none>");
    }
    void Move(UINavDir d) { doc.UpdateNav(move(d)); }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
};

} // namespace

TEST(UISpatialNav, ARightAlignedControlIsNotSkippedForAFullWidthOneBelowIt) {
    SkipRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();

    // Sanity: the geometry really is the awkward one. The toggle must be far to
    // the right of the chips, and the field must overlap them.
    UIElement* high  = r.doc.root().Find("high");
    UIElement* vsync = r.doc.root().Find("vsync");
    UIElement* field = r.doc.root().Find("field");
    ASSERT_TRUE(high && vsync && field);
    ASSERT_GT(vsync->layout().position.x,
              high->layout().position.x + high->layout().size.x + 100.f)
        << "the test layout does not reproduce the reported one";
    ASSERT_LT(field->layout().position.x, high->layout().position.x + 1.f)
        << "the field does not overlap the chips, so there is nothing to steal";

    r.doc.SetFocus(high);
    r.Move(UINavDir::Down);
    EXPECT_EQ(r.focusName(), "vsync")
        << "Down skipped the right-aligned toggle and jumped to the text field - "
           "the reported bug";

    r.Move(UINavDir::Down);
    EXPECT_EQ(r.focusName(), "field") << "and the field is still reachable after it";
}

// The same rule read upwards.
TEST(UISpatialNav, UpFromAFullWidthControlReachesTheRowJustAboveIt) {
    SkipRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();
    r.doc.SetFocus(r.doc.root().Find("field"));
    r.Move(UINavDir::Up);
    EXPECT_EQ(r.focusName(), "vsync") << "Up skipped the row directly above";
}

// Stage one must not swallow stage two: within the nearest row, the closest
// across still wins. From LOW, the next row down is the toggle's -- and there
// is only one control in it -- but from the FIELD going up, the toggle row and
// the chip row are distinct and the nearer must win.
TEST(UISpatialNav, WithinTheChosenRowTheNearestAcrossStillWins) {
    Grid g;
    ASSERT_TRUE(g.Load()) << g.firstError();
    g.doc.SetFocus(g.doc.root().Find("below"));
    g.Move(UINavDir::Up);
    // The chip row is the nearest row up; within it, `low` is nearest across to
    // `below` (both start at x=0).
    EXPECT_EQ(g.focusName(), "low")
        << "the nearest row was chosen but the wrong member of it";
}
