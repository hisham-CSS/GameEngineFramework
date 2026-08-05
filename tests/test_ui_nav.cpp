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
