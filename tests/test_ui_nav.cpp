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
#include "../Engine/src/ui/UINavSynth.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <cmath>
#include <unordered_map>
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

// ...and inside a scope that DECLINES it. A focus scope with no `on-back` --
// the shipped menu's verb column is exactly this -- declares a navigation
// region, not a back action. It used to consume back anyway, purely because it
// was on the stack, which made the reporting above unreachable in the one
// document that needed it: at the root menu B did nothing and the host was
// never told, so "close the menu" and "quit" had no way to exist.
TEST(UIFocusScope, BackInsideAScopeWithNoHandlerIsReportedToTheHost) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="verbs" focus-scope="true">)"
        R"(<Button name="v1" text="a"/><Button name="v2" text="b"/>)"
        R"(</Element></UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    doc.SyncFocusScopes();          // the push seeds focus on v1
    ASSERT_NE(doc.focused(), nullptr);
    const UIElement* was = doc.focused();

    EXPECT_FALSE(doc.UpdateNav(navBack()))
        << "a scope with no on-back swallowed back - the host can never see it";
    EXPECT_EQ(doc.focused(), was)
        << "declining back also blurred, stranding the page with nothing focused";
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


// =================================================== the keyboard (U27a) ===
//
// A keyboard drives this UI the way a pad does, and the two grammars reach the
// document through DIFFERENT doors on purpose:
//
//   ARROWS / ESCAPE / SPACE  ->  UIKeyboardState -> UpdateKeyboard, at the END
//       of the existing consumption chain, so a text caret, a tab strip, a
//       focused slider's notch and page scrolling each decline first.
//   WASD                     ->  InputMap -> UINavSynth -> UpdateNav, because
//       UIKey deliberately has no letters (a letter is text).
//
// Binding the arrows BOTH ways is the bug this arrangement exists to prevent:
// one press would arrive twice and a slider would move two notches.

namespace {

// A column of three buttons with a slider in the middle of them, plus a text
// field -- every consumer of an arrow key in one tree.
struct KeyRig {
    UIDocument doc;
    UIStyleSheet sheet;
    std::vector<std::string> errors;

    bool Load() {
        const char* css =
            "#col { width: 300px; flex-direction: column; align-items: flex-start; }"
            ".row { width: 300px; height: 40px; }";
        if (!sheet.ParseString(css, "t.cstyle")) return false;
        const char* xml =
            R"(<UI><Element name="col">)"
            R"(<Button name="top" class="row" text="TOP"/>)"
            R"(<Slider name="vol" class="row" min="0" max="1" value="0.5" key-step="0.1"/>)"
            R"(<TextField name="field" class="row" value="abc"/>)"
            R"(<Button name="bottom" class="row" text="BOTTOM"/>)"
            R"(</Element></UI>)";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        sheet.ApplyTo(doc.root());
        doc.Layout(600.f, 600.f);
        return true;
    }
    void Key(UIKey k) {
        UIKeyboardState kb;
        kb.keys.push_back(UIKeyEvent{ k });
        doc.UpdateKeyboard(kb, nullptr);
    }
    std::string focusName() {
        UIElement* f = doc.focused();
        return f ? f->name() : std::string("<none>");
    }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
};

} // namespace

// The new fallback: an arrow nothing else wanted moves focus. Before this, a
// focused Button received the KeyDown, every default action declined it, and
// the key fell off the end of the chain doing nothing -- the UI could be
// TABBED but not navigated, which is not how any menu behaves.
TEST(UIKeyboardNav, AnUnclaimedArrowMovesFocus) {
    KeyRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();
    r.doc.SetFocus(r.doc.root().Find("top"));

    r.Key(UIKey::Down);
    EXPECT_EQ(r.focusName(), "vol") << "Down on a button did not navigate";
}

// ...and it is genuinely LAST. A focused slider's own notch is a default
// action ahead of it, so the arrow moves the VALUE and focus stays put.
TEST(UIKeyboardNav, ASliderKeepsItsOwnAxisArrowsAndMovesExactlyOneNotch) {
    KeyRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();
    UIElement* vol = r.doc.root().Find("vol");
    ASSERT_TRUE(vol && vol->slider());
    r.doc.SetFocus(vol);
    const float before = vol->slider()->value;

    r.Key(UIKey::Right);
    EXPECT_EQ(r.focusName(), "vol") << "an arrow the slider wanted also moved focus";
    // ONE step. Two would mean the key reached both UpdateKeyboard and
    // UpdateNav -- exactly what binding the arrows as nav actions would cause.
    EXPECT_NEAR(vol->slider()->value, before + 0.1f, 1e-5f)
        << "the slider moved by something other than one key-step";

    // The CROSS axis is not the slider's, so it still navigates.
    r.Key(UIKey::Down);
    EXPECT_EQ(r.focusName(), "field")
        << "the cross-axis arrow was swallowed by a horizontal slider";
}

// A text field owns the arrows ALONG its text, and only those.
//
// UITextEdit has always declined Up/Down in a single-line field on purpose --
// "Up must stay available to whatever contains a single-line field" is a
// comment older than this feature. Until now nothing WAS that container, so
// the key did nothing at all and a keyboard user who reached the PILOT field
// could only leave it with Tab. The fallback is the container it was waiting
// for, and the precedence falls out of the chain rather than being special-
// cased: the field declines, so navigation gets its turn.
TEST(UIKeyboardNav, ASingleLineFieldKeepsLeftAndRightButLetsUpAndDownNavigate) {
    KeyRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();
    r.doc.SetFocus(r.doc.root().Find("field"));

    // ALONG the text: the caret's, focus stays.
    r.Key(UIKey::Left);
    EXPECT_EQ(r.focusName(), "field") << "Left moved focus out of a field mid-caret";
    r.Key(UIKey::Right);
    EXPECT_EQ(r.focusName(), "field") << "Right moved focus out of a field mid-caret";

    // ACROSS it: meaningless in one line, so it is navigation.
    r.Key(UIKey::Down);
    EXPECT_EQ(r.focusName(), "bottom")
        << "Down in a single-line field went nowhere - the field is a dead end "
           "for anyone without a mouse";
}

// ...and a MULTI-LINE field keeps them, because there they mean something.
// Same chain, opposite outcome, decided by the field rather than by a test for
// the element's type.
TEST(UIKeyboardNav, AMultiLineFieldKeepsUpAndDownForItsOwnCaret) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><TextField name="notes" multiline="true" value="one&#10;two"/>)"
        R"(<Button name="after" text="AFTER"/></UI>)", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);
    doc.Layout(400.f, 400.f);
    UIElement* notes = doc.root().Find("notes");
    ASSERT_TRUE(notes && notes->textEdit());
    ASSERT_TRUE(notes->textEdit()->multiline())
        << "the rig did not actually build a multi-line field";
    doc.SetFocus(notes);

    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Down });
    doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(doc.focused(), notes)
        << "Down left a multi-line field instead of moving to the next line";
}

// ENTER activates. Space deliberately does NOT reach the UI at all: InputMap
// binds "Jump" to it and gameplay input is gated only on a TEXT FIELD having
// focus, so Space on a focused menu button pressed the button AND jumped.
TEST(UIKeyboardNav, EnterActivatesAndSpaceIsNotAUIKeyAtAll) {
    KeyRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();
    int clicks = 0;
    r.doc.root().Find("top")->OnClick([&](UIEvent&) { ++clicks; });
    r.doc.SetFocus(r.doc.root().Find("top"));

    r.Key(UIKey::Enter);
    EXPECT_EQ(clicks, 1) << "Enter did not press a focused button";

    // And nothing else in the chain activates: the guard against Space coming
    // back is that UIKey has no entry for it, so a host cannot map one.
}

// Escape runs the SAME UIDocument::Back the pad's B runs. One notion of
// backing out, not a keyboard one and a controller one that drift apart.
TEST(UIKeyboardNav, EscapeRunsTheSameBackThePadDoes) {
    // The SAME rig the pad's back test uses, driven by a key instead. That is
    // the assertion: one tree, two grammars, identical outcome.
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();
    s.src.SetBool("open", true);
    s.Frame();
    ASSERT_EQ(s.focusName(), "p1");
    ASSERT_EQ(s.backs, 0);

    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Escape });
    s.doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(s.backs, 1) << "Escape did not reach the scope's on-back";

    s.Frame();
    EXPECT_EQ(s.focusName(), "v1")
        << "the stack did not pop after the app closed the panel";
}

// Escape inside a FIELD blurs the field rather than closing the panel around
// it. UITextEdit deliberately declines the key, so it falls through to Back,
// whose no-scope branch is a blur -- but the scope branch must not fire while
// the user is plainly still inside a control.
TEST(UIKeyboardNav, EscapeInAFieldBlursItRatherThanClosingThePanel) {
    KeyRig r;
    ASSERT_TRUE(r.Load()) << r.firstError();   // no focus scopes in this tree
    r.doc.SetFocus(r.doc.root().Find("field"));
    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Escape });
    r.doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(r.doc.focused(), nullptr) << "Escape did not blur the field";
}


// ================================================ the live device (U27b) ===
//
// Button prompts have to agree with the hands on the desk. UIWorld derives
// which grammar is live from the three input states it ALREADY receives --
// no fourth thing for a host to feed and forget -- and republishes it into the
// shared source, where markup binds it like any other value.

namespace {

std::string devStr(MyCoreEngine::UIWorld& w) {
    return w.shared().GetString("uiDevice");
}

MyCoreEngine::ui::UINavState padNav() {
    MyCoreEngine::ui::UINavState n;
    n.device = MyCoreEngine::ui::UINavDevice::Gamepad;
    n.moves.push_back(MyCoreEngine::ui::UINavDir::Down);
    return n;
}

} // namespace

TEST(UIDevicePrompts, ThePadTakesOverAndTheMouseTakesItBack) {
    ShippedHud hud;
    hud.Frame();
    // A fresh process assumes keyboard/mouse: that is what a desktop build
    // boots into, and guessing "pad" would show the wrong prompts to everyone
    // who never picks one up.
    EXPECT_EQ(devStr(hud.world), "keyboard");

    hud.world.SetNav(padNav());
    hud.Frame();
    EXPECT_EQ(hud.world.inputDevice(), MyCoreEngine::ui::UINavDevice::Gamepad);
    EXPECT_EQ(devStr(hud.world), "gamepad");

    // STICKY: silence must not flip it back. Otherwise the legend would flicker
    // between the two every time the player stopped to read it.
    hud.world.SetNav(MyCoreEngine::ui::UINavState{});
    hud.Frame();
    hud.Frame();
    EXPECT_EQ(devStr(hud.world), "gamepad") << "an idle frame stole the prompts back";

    // Reaching for the MOUSE is the switch, before any button is pressed.
    hud.Point(100.0f, 100.0f);
    hud.Frame();
    EXPECT_EQ(devStr(hud.world), "keyboard")
        << "moving the mouse did not take the prompts back from the pad";
}

TEST(UIDevicePrompts, AKeystrokeCountsAsTheKeyboard) {
    ShippedHud hud;
    hud.world.SetNav(padNav());
    hud.Frame();
    ASSERT_EQ(devStr(hud.world), "gamepad");

    MyCoreEngine::ui::UIKeyboardState kb;
    kb.keys.push_back(MyCoreEngine::ui::UIKeyEvent{ MyCoreEngine::ui::UIKey::Down });
    hud.world.SetKeyboard(kb);
    hud.world.SetNav(MyCoreEngine::ui::UINavState{});
    hud.Frame();
    EXPECT_EQ(devStr(hud.world), "keyboard") << "an arrow key did not claim the prompts";
}

// The glyph names markup actually binds. Text rather than art, so a game with
// no icons still reads correctly; the two bools are there for one that has art.
TEST(UIDevicePrompts, TheGlyphNamesAndGatingBoolsFollowTheDevice) {
    ShippedHud hud;
    hud.Frame();
    const auto str  = [&](const char* k) { return hud.world.shared().GetString(k); };
    const auto flag = [&](const char* k) { return hud.world.shared().GetBool(k); };

    EXPECT_EQ(str("uiGlyphSelect"), "ENTER");
    EXPECT_EQ(str("uiGlyphBack"), "ESC");
    EXPECT_EQ(str("uiGlyphNav"), "WASD");
    EXPECT_TRUE(flag("uiKeyboard"));
    EXPECT_FALSE(flag("uiPad"));

    hud.world.SetNav(padNav());
    hud.Frame();
    EXPECT_EQ(str("uiGlyphSelect"), "A");
    EXPECT_EQ(str("uiGlyphBack"), "B");
    EXPECT_EQ(str("uiGlyphNav"), "L STICK");
    EXPECT_TRUE(flag("uiPad"));
    EXPECT_FALSE(flag("uiKeyboard"))
        << "both gating bools were true at once - a document would show two "
           "sets of prompts stacked on each other";
}


// ------------------------------------------- WASD through the synth (U27a)

namespace {

// The real default bindings over a scripted device, so these assert the
// SHIPPED configuration rather than a convenient one.
class NavFakeInput : public MyCoreEngine::InputMap {
public:
    std::unordered_map<int, bool> keys;
    bool padPresent = false;
    GLFWgamepadstate pad{};
protected:
    bool pollKey(GLFWwindow*, int key) const override {
        auto it = keys.find(key);
        return it != keys.end() && it->second;
    }
    bool pollMouseButton(GLFWwindow*, int) const override { return false; }
    bool pollGamepad(GLFWgamepadstate& out) const override {
        if (padPresent) out = pad;
        return padPresent;
    }
};

} // namespace

TEST(UINavSynthKeyboard, WASDProducesMovesThroughTheSameRepeatClockAsThePad) {
    NavFakeInput in;
    MyCoreEngine::BindDefaultActions(in);
    MyCoreEngine::UINavSynth synth;

    in.keys[GLFW_KEY_S] = true;
    in.update(nullptr);
    MyCoreEngine::ui::UINavState n = synth.Poll(in, 0.016f);
    ASSERT_EQ(n.moves.size(), 1u) << "S did not produce a move";
    EXPECT_EQ(n.moves[0], UINavDir::Down);
    EXPECT_EQ(n.device, UINavDevice::KeyboardMouse)
        << "a key was attributed to the pad, so the prompts would say 'PRESS A'";

    // HELD: the UI's own repeat clock, not the operating system's. Silence
    // through the initial delay is the proof it went through UINavRepeater --
    // OS key repeat would have produced an event here.
    int during = 0;
    for (int i = 0; i < 20; ++i) {
        in.update(nullptr);
        during += int(synth.Poll(in, 0.016f).moves.size());
    }
    EXPECT_EQ(during, 0) << "a held key repeated before the nav delay elapsed";
}

// THE POINT OF THE SOURCE FILTER. Typing a pilot name must not walk the menu,
// and the pad must keep working while you do it -- a pad types nothing, so it
// has no reason to go quiet. Unbinding would kill both halves at once.
TEST(UINavSynthKeyboard, TypingSilencesWASDAndLeavesTheDPadAlone) {
    NavFakeInput in;
    MyCoreEngine::BindDefaultActions(in);
    in.padPresent = true;
    MyCoreEngine::UINavSynth synth;

    in.keys[GLFW_KEY_D] = true;      // the letter, mid-word
    in.update(nullptr);
    MyCoreEngine::ui::UINavState n = synth.Poll(in, 0.016f, /*allowKeyboard=*/false);
    EXPECT_TRUE(n.moves.empty())
        << "typing 'd' into a name field navigated the menu right";

    // The d-pad, with the key still held: still live.
    in.pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = GLFW_PRESS;
    in.update(nullptr);
    n = synth.Poll(in, 0.016f, /*allowKeyboard=*/false);
    ASSERT_EQ(n.moves.size(), 1u)
        << "the pad went quiet too, so a controller could not leave a text field";
    EXPECT_EQ(n.moves[0], UINavDir::Down);
    EXPECT_EQ(n.device, UINavDevice::Gamepad);
}

// The stick is pad activity even when it produces no discrete move, so easing
// a slider a few percent still flips the prompts.
TEST(UINavSynthKeyboard, AnAnalogNudgeBelowTheMoveThresholdStillClaimsTheDevice) {
    NavFakeInput in;
    MyCoreEngine::BindDefaultActions(in);
    in.padPresent = true;
    in.pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.30f;   // past deadzone, under 0.5
    in.update(nullptr);

    MyCoreEngine::UINavSynth synth;
    MyCoreEngine::ui::UINavState n = synth.Poll(in, 0.016f);
    EXPECT_TRUE(n.moves.empty()) << "a small deflection produced a discrete move";
    EXPECT_NE(n.axisX, 0.0f)     << "the analog value never reached the state";
    EXPECT_EQ(n.device, UINavDevice::Gamepad)
        << "a stick that was moving the value did not claim the prompts";
}


// ------------------------------------------------- what the review found

// BACK LEAVES THE FIELD FIRST. A text field you are typing in is the innermost
// thing you are in, ahead of any panel around it.
//
// The shipped menu is exactly this shape: PILOT sits inside settingsPanel,
// which is a scope WITH an on-back. Without the text-edit branch in Back(),
// one Escape mid-name matched the scope branch and threw away the whole page.
TEST(UIKeyboardNav, BackLeavesATextFieldBeforeItClosesThePanelAroundIt) {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIBinder binder;
    std::vector<std::string> errors;
    int backs = 0;
    ctx.RegisterSource("s", &src);
    src.SetBool("open", true);
    src.AddAction("close", [&] { ++backs; src.SetBool("open", false); });

    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI data-source="s">)"
        R"(<Element name="panel" focus-scope="true" if="open" on-back="close">)"
        R"(<TextField name="pilot" value="ada"/><Button name="ok" text="OK"/>)"
        R"(</Element></UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    binder.Rebuild(doc, ctx, "t.cxml");
    binder.UpdateToTarget();
    doc.Layout(400.f, 400.f);
    doc.UpdateNav(UINavState{});               // pushes the scope

    UIElement* pilot = doc.root().Find("pilot");
    ASSERT_TRUE(pilot && pilot->textEdit());
    doc.SetFocus(pilot);

    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Escape });
    doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(backs, 0)
        << "Escape mid-name closed the whole settings page instead of leaving "
           "the field";
    EXPECT_EQ(doc.focused(), nullptr) << "Escape did not leave the field";

    // ...and a SECOND press, now that nothing is focused, closes the panel.
    doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(backs, 1) << "a second Escape did not close the panel";
}

// The pad's B is the same gesture and must have the same two steps -- the
// branch is in Back(), not in the Escape hook, precisely so it cannot drift.
TEST(UIKeyboardNav, ThePadsBackAlsoLeavesTheFieldFirst) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="panel" focus-scope="true">)"
        R"(<TextField name="pilot" value="ada"/></Element></UI>)", errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    doc.SyncFocusScopes();
    doc.SetFocus(doc.root().Find("pilot"));

    UINavState n;
    n.back = true;
    EXPECT_TRUE(doc.UpdateNav(n));
    EXPECT_EQ(doc.focused(), nullptr) << "B did not leave the field";
}

// ESCAPE AND B SEE THE SAME SCOPE STACK.
//
// SyncFocusScopes has one caller inside UpdateNav, and UIWorld runs the
// keyboard pass FIRST -- so for one frame after a panel opened, Escape
// dispatched Back into the scope that had just been hidden. That scope is the
// verb column, which has no on-back, so the press was swallowed and the panel
// stayed open. B, arriving after the sync, closed it. Same gesture, two
// outcomes, one frame apart.
TEST(UIKeyboardNav, EscapeOnTheFrameAPanelOpensStillReachesThatPanel) {
    Scoped s;
    ASSERT_TRUE(s.Load()) << s.firstError();
    s.Frame();
    ASSERT_EQ(s.focusName(), "v1");

    // Open it BETWEEN frames, the way a click handler does, then press Escape
    // on the very next frame with no nav pass in between.
    s.src.SetBool("open", true);
    s.binder.UpdateToTarget();
    s.doc.Layout(400.f, 400.f);

    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Escape });
    s.doc.UpdateKeyboard(kb, nullptr);
    EXPECT_EQ(s.backs, 1)
        << "Escape one frame after the panel opened went to the OLD scope - "
           "the panel stays open and the press is reported unhandled";
}


// THE MOVE PROMPT NAMES THE THING THAT WORKS.
//
// Reported from a real session: navigate onto a text field with WASD and those
// four keys immediately become letters, so the legend is telling you to press
// something that types "w". The arrows do not have that problem -- Up and Down
// still navigate out of a single-line field -- so that is what the prompt has
// to say while a field holds focus.
TEST(UIDevicePrompts, TheMovePromptSaysARROWSWhileATextFieldHasFocus) {
    ShippedHud hud;
    hud.Frame();
    const auto nav = [&] { return hud.world.shared().GetString("uiGlyphNav"); };
    ASSERT_EQ(nav(), "WASD");
    EXPECT_FALSE(hud.world.shared().GetBool("uiTyping"));

    // Focus the HUD's own field, the way navigating onto it would.
    UIElement* field = nullptr;
    for (UIElement* e : { hud.find("nameField"), hud.find("pilot"),
                          hud.find("playerField") }) {
        if (e && e->textEdit()) { field = e; break; }
    }
    ASSERT_NE(field, nullptr) << "the shipped HUD has no text field to focus";
    hud.doc().SetFocus(field);
    hud.Frame();

    EXPECT_TRUE(hud.world.shared().GetBool("uiTyping"));
    EXPECT_EQ(nav(), "ARROWS")
        << "the legend still says WASD while a field is eating W, A, S and D";

    // ...and back again the moment focus leaves it.
    hud.doc().SetFocus(nullptr);
    hud.Frame();
    EXPECT_EQ(nav(), "WASD");
    EXPECT_FALSE(hud.world.shared().GetBool("uiTyping"));
}

// A stick types nothing, so the pad's prompt must NOT change in a field.
TEST(UIDevicePrompts, ThePadsMovePromptIsUnaffectedByATextField) {
    ShippedHud hud;
    hud.world.SetNav(padNav());
    hud.Frame();
    ASSERT_EQ(hud.world.shared().GetString("uiGlyphNav"), "L STICK");

    UIElement* field = nullptr;
    for (UIElement* e : { hud.find("nameField"), hud.find("pilot"),
                          hud.find("playerField") }) {
        if (e && e->textEdit()) { field = e; break; }
    }
    ASSERT_NE(field, nullptr);
    hud.doc().SetFocus(field);
    hud.world.SetNav(padNav());
    hud.Frame();
    EXPECT_EQ(hud.world.shared().GetString("uiGlyphNav"), "L STICK")
        << "a text field changed the PAD's prompt, which types nothing";
}
