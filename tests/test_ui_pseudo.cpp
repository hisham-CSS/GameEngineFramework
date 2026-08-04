// :hover and :active — state-dependent styling.
//
// Pure CPU. The subtle part is not matching, it is UNDOING: the cascade copies
// declarations into a Style and records nothing about what they overwrote, so
// there is no "remove the hover rule" operation. UIInteractionStyler resets the
// element and re-runs the whole cascade for its current state, and most of the
// tests below are about what that reset must NOT destroy — text, bindings — and
// about not doing it to elements no state rule can reach.
#include <gtest/gtest.h>

#include "Engine.h"
#include "ui_shipped_hud.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIInteractionStyler.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// One 100x100 button in a 400x400 viewport, styled by `css`, with the pointer
// driven directly so hover and press are exercised end to end.
struct Hoverable {
    UIDocument doc;
    UIStyleSheet sheet;
    UIBinder binder;
    UIBindingContext ctx;
    UIInteractionStyler styler;
    std::vector<std::string> errors;

    bool Load(const std::string& css, const std::string& markup =
                  R"(<UI><Element name="btn" class="btn" style="width: 100px; height: 100px"/></UI>)") {
        if (!sheet.ParseString(css, "t.cstyle")) return false;
        if (!UIMarkup::LoadInto(doc, markup, errors, "t.cxml")) return false;
        sheet.ApplyTo(doc.root());
        binder.Rebuild(doc, ctx, "t.cxml", &sheet);
        styler.Rebuild(doc, sheet, &binder);
        doc.Layout(400.f, 400.f);
        return true;
    }

    UIElement* btn() { return doc.root().Find("btn"); }

    // Moves the pointer and re-runs the styler, the way a host frame does.
    bool Point(float x, float y, bool down = false, bool inside = true) {
        UIPointerState p;
        p.position = { x, y };
        p.inside = inside;
        p.buttonDown = down;
        doc.UpdatePointer(p);
        const bool changed = styler.Update();
        if (changed) doc.Layout(400.f, 400.f);
        return changed;
    }
    bool Away() { return Point(-1.f, -1.f, false, false); }
};

const char* kBtnCss =
    ".btn { background-color: #202020; padding: 4px; }\n"
    ".btn:hover { background-color: #808080; }\n"
    ".btn:active { background-color: #ff0000; }\n";

float red(const UIElement* e) { return e->style().backgroundColor.r; }

} // namespace

// ------------------------------------------------------------- the parser

TEST(UIPseudoParse, ParsesHoverAndActiveAndCountsThemForSpecificity) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString(".btn:hover { color: red; } Button:active#ok { color: blue; }",
                              "t.cstyle"))
        << (s.errors().empty() ? "" : s.errors()[0]);
    ASSERT_EQ(s.rules().size(), 2u);

    const UICompound& a = s.rules()[0].selectors[0].subject();
    EXPECT_EQ(a.classes.size(), 1u);
    EXPECT_EQ(a.pseudo, std::uint8_t(UIPseudo::Hover));
    int ids = 0, cls = 0, types = 0;
    s.rules()[0].selectors[0].Specificity(ids, cls, types);
    // A pseudo-class counts as a class, exactly as in CSS — that is what makes
    // `.btn:hover` outrank `.btn` with no ordering tricks.
    EXPECT_EQ(ids, 0);
    EXPECT_EQ(cls, 2) << ".class + :hover must count as two";
    EXPECT_EQ(types, 0);

    const UICompound& b = s.rules()[1].selectors[0].subject();
    EXPECT_EQ(b.type, "Button");
    EXPECT_EQ(b.name, "ok");
    EXPECT_EQ(b.pseudo, std::uint8_t(UIPseudo::Active));
}

TEST(UIPseudoParse, CompoundStatesRequireBoth) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString(".btn:hover:active { color: red; }", "t.cstyle"))
        << (s.errors().empty() ? "" : s.errors()[0]);
    const UICompound& sel = s.rules()[0].selectors[0].subject();
    EXPECT_EQ(sel.pseudo,
              std::uint8_t(UIPseudo::Hover) | std::uint8_t(UIPseudo::Active));
    int ids = 0, cls = 0, types = 0;
    s.rules()[0].selectors[0].Specificity(ids, cls, types);
    EXPECT_EQ(cls, 3);
}

// A silently dropped pseudo-class turns `.btn:focus` into a plain `.btn` rule
// that applies ALL the time, which reads as "the styling is just broken".
TEST(UIPseudoParse, AnUnknownPseudoClassIsReported) {
    UIStyleSheet s;
    // `:checked` needs a checkbox, which does not exist — a pseudo-class with
    // nothing behind it would be a selector that silently never matches.
    EXPECT_FALSE(s.ParseString(".btn:checked { color: red; }", "t.cstyle"));
    ASSERT_FALSE(s.errors().empty());
    EXPECT_NE(s.errors()[0].find("unknown pseudo-class ':checked'"), std::string::npos)
        << s.errors()[0];
    EXPECT_NE(s.errors()[0].find("hover|active"), std::string::npos) << s.errors()[0];
    // ...and the sheet is left untouched, like every other parse failure.
    EXPECT_TRUE(s.rules().empty());
}

// `.btn: { }` would otherwise be pushed as a plain `.btn` rule — the state
// selector silently deleted, so the "hover" styling applies all the time.
TEST(UIPseudoParse, ATrailingColonIsAnErrorNotAPlainRule) {
    for (const char* css : { ".btn: { color: red; }", ".btn:{ color: red; }",
                             ".a::hover { color: red; }" }) {
        UIStyleSheet s;
        EXPECT_FALSE(s.ParseString(css, "t.cstyle")) << css;
        ASSERT_FALSE(s.errors().empty()) << css;
        EXPECT_NE(s.errors()[0].find("no pseudo-class after it"), std::string::npos)
            << css << " -> " << s.errors()[0];
        EXPECT_TRUE(s.rules().empty()) << css;
    }
    // ...but a bare `:hover` is legitimate CSS — it means `*:hover`.
    UIStyleSheet ok;
    ASSERT_TRUE(ok.ParseString(":hover { color: red; }", "t.cstyle"))
        << (ok.errors().empty() ? "" : ok.errors()[0]);
    EXPECT_TRUE(ok.rules()[0].selectors[0].subject().type.empty());
    EXPECT_EQ(ok.rules()[0].selectors[0].subject().pseudo, std::uint8_t(UIPseudo::Hover));
}

// In CSS each selector in a comma list carries its own specificity, and a rule
// weighs as much as its strongest match. Taking the FIRST match instead makes
// `.btn, .btn:hover` weigh as a bare class — invisible until pseudo-classes
// exist to make two selectors in one list differ in strength.
TEST(UIPseudoParse, ARuleWeighsAsItsStrongestMatchingSelector) {
    Hoverable h;
    ASSERT_TRUE(h.Load(
        // Listed first, and its FIRST selector is a bare class. If the rule
        // took its weight from that, the later plain `.btn` rule would win on
        // source order and the hover colour would never appear.
        ".other, .btn:hover { background-color: #00ff00; }\n"
        ".btn { background-color: #202020; }\n"));
    EXPECT_NEAR(red(h.btn()), 0.125f, 0.01f) << "idle";

    h.Point(50.f, 50.f);
    EXPECT_NEAR(h.btn()->style().backgroundColor.g, 1.0f, 0.01f)
        << "the rule was weighed by its first selector rather than its strongest";
}

// Descendant selectors compose with pseudo-classes, which is the combination a
// real form needs: "a button inside this panel, while hovered".
TEST(UIPseudoParse, DescendantSelectorsComposeWithStates) {
    Hoverable h;
    ASSERT_TRUE(h.Load(
        ".btn { background-color: #202020; }\n"
        ".panel .btn:hover { background-color: #00ff00; }\n",
        R"(<UI>
             <Element name="panel" class="panel" style="width: 200px; height: 200px">
               <Element name="btn" class="btn" style="width: 100px; height: 100px"/>
             </Element>
           </UI>)"));
    EXPECT_NEAR(red(h.btn()), 0.125f, 0.01f);
    h.Point(50.f, 50.f);
    EXPECT_NEAR(h.btn()->style().backgroundColor.g, 1.0f, 0.01f)
        << "a descendant + state selector did not apply";
}

// Matching is state-aware, but "could this rule ever apply here" is not — that
// second question is what decides which elements are worth watching.
TEST(UIPseudoParse, MatchesVersusMatchesIgnoringState) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString(".btn:hover { color: red; }", "t.cstyle"));
    UIDocument doc;
    UIElement* e = doc.root().AddChild("e");
    e->AddClass("btn");

    const UISelector& sel = s.rules()[0].selectors[0];
    EXPECT_FALSE(sel.Matches(*e)) << "not hovered, so it must not match";
    EXPECT_TRUE(sel.MatchesIgnoringState(*e));
    EXPECT_TRUE(s.HasStateRuleFor(*e));

    UIElement* other = doc.root().AddChild("other");
    EXPECT_FALSE(s.HasStateRuleFor(*other)) << "no .btn class, so no state rule reaches it";
}

// --------------------------------------------------------- the styler

TEST(UIPseudoStyler, HoverAppliesAndUnappliesAsThePointerMoves) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss)) << (h.errors.empty() ? "" : h.errors[0]);
    ASSERT_NE(h.btn(), nullptr);
    EXPECT_NEAR(red(h.btn()), 0.125f, 0.01f) << "idle colour";

    EXPECT_TRUE(h.Point(50.f, 50.f));
    EXPECT_NEAR(red(h.btn()), 0.502f, 0.01f) << ":hover did not apply";

    // The part with no CSS equivalent to lean on: there is no "undo the hover
    // rule", so leaving has to restore the idle colour by re-cascading.
    EXPECT_TRUE(h.Away());
    EXPECT_NEAR(red(h.btn()), 0.125f, 0.01f) << ":hover did not un-apply";
}

TEST(UIPseudoStyler, ActiveAppliesWhilePressedAndBeatsHover) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss));
    h.Point(50.f, 50.f);
    ASSERT_NEAR(red(h.btn()), 0.502f, 0.01f);

    h.Point(50.f, 50.f, /*down=*/true);
    EXPECT_NEAR(red(h.btn()), 1.0f, 0.01f) << ":active must win while pressed";

    h.Point(50.f, 50.f, /*down=*/false);
    EXPECT_NEAR(red(h.btn()), 0.502f, 0.01f) << "release must fall back to :hover";
}

// Nothing changed, so nothing should be re-cascaded — a HUD sitting still must
// not pay for this feature every frame.
TEST(UIPseudoStyler, AnUnchangedStateRestylesNothing) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss));
    EXPECT_FALSE(h.Point(300.f, 300.f)) << "moving over nothing restyled something";
    EXPECT_TRUE(h.Point(50.f, 50.f));
    EXPECT_FALSE(h.Point(60.f, 60.f)) << "moving WITHIN the element restyled again";
    EXPECT_EQ(h.styler.lastRestyled(), 0u);
}

// Only elements a pseudo rule could reach are watched at all. That is both the
// performance story and the scope of the re-cascade's one caveat.
TEST(UIPseudoStyler, OnlyElementsAStateRuleCanReachAreWatched) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss, R"(<UI>
          <Element name="btn" class="btn" style="width: 100px; height: 100px"/>
          <Element name="plain" style="width: 100px; height: 100px"/>
        </UI>)"));
    EXPECT_EQ(h.styler.watchedCount(), 1u)
        << "an element no :hover rule targets must not be watched";
}

// The reset discards everything, including text — and text is not a cascadable
// property, so no rule would put it back.
TEST(UIPseudoStyler, TextSurvivesARestyle) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss,
        R"(<UI><Label name="btn" class="btn" text="PRESS ME" style="width: 100px; height: 100px"/></UI>)"));
    ASSERT_EQ(h.btn()->style().text, "PRESS ME");
    const std::uint32_t rev = h.btn()->textRevision();

    h.Point(50.f, 50.f);
    EXPECT_EQ(h.btn()->style().text, "PRESS ME") << "hovering destroyed the label's text";
    h.Away();
    EXPECT_EQ(h.btn()->style().text, "PRESS ME");
    // Carried across as data rather than through setText: the content never
    // actually changed, so it must not dirty the measurement on every hover.
    EXPECT_EQ(h.btn()->textRevision(), rev) << "a restyle forced a needless re-measure";
}

// Bindings wrote into the Style the reset just threw away, so they have to run
// again — otherwise hovering a bound bar snaps it back to its .cstyle default
// until the next value change.
TEST(UIPseudoStyler, BindingsAreReappliedAfterARestyle) {
    UIDocument doc;
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        ".bar { width: 10%; background-color: #202020; }\n"
        ".bar:hover { background-color: #808080; }\n", "t.cstyle"));

    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="s">
          <Element name="bar" class="bar" bind="width: {health | percent}"
                   style="height: 100px"/>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource src;
    src.SetNumber("health", 0.75f);
    UIBindingContext ctx;
    ctx.RegisterSource("s", &src);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml", &sheet);
    sheet.ApplyTo(doc.root());
    binder.Rebuild(doc, ctx, "t.cxml", &sheet);
    UIInteractionStyler styler;
    styler.Rebuild(doc, sheet, &binder);

    UIElement* bar = doc.root().Find("bar");
    ASSERT_NE(bar, nullptr);
    ASSERT_FLOAT_EQ(bar->style().width.value, 75.f);

    doc.Layout(400.f, 400.f);
    UIPointerState p;
    p.inside = true;
    p.position = { 50.f, 50.f };
    doc.UpdatePointer(p);
    ASSERT_TRUE(styler.Update());

    EXPECT_NEAR(bar->style().backgroundColor.r, 0.502f, 0.01f) << ":hover did not apply";
    EXPECT_FLOAT_EQ(bar->style().width.value, 75.f)
        << "the re-cascade discarded the bound width and nothing put it back";
}

// :hover applies up the ancestor chain, matching CSS and matching what
// UpdatePointer already maintains.
TEST(UIPseudoStyler, HoverAppliesToAncestorsToo) {
    Hoverable h;
    ASSERT_TRUE(h.Load(
        ".panel { background-color: #202020; }\n"
        ".panel:hover { background-color: #00ff00; }\n"
        ".btn { background-color: #202020; }\n"
        ".btn:hover { background-color: #808080; }\n",
        R"(<UI>
             <Element name="panel" class="panel" style="width: 200px; height: 200px">
               <Element name="btn" class="btn" style="width: 100px; height: 100px"/>
             </Element>
           </UI>)"));
    EXPECT_EQ(h.styler.watchedCount(), 2u);

    h.Point(50.f, 50.f);
    UIElement* panel = h.doc.root().Find("panel");
    ASSERT_NE(panel, nullptr);
    EXPECT_NEAR(panel->style().backgroundColor.g, 1.0f, 0.01f)
        << "the containing panel is hovered too, as in CSS";
    EXPECT_NEAR(red(h.btn()), 0.502f, 0.01f);
}

// A state rule may change a BOX, not only a colour, which is why a restyle
// asks the host to lay out again.
TEST(UIPseudoStyler, AStateRuleCanChangeLayoutNotJustColour) {
    Hoverable h;
    ASSERT_TRUE(h.Load(
        ".btn { width: 100px; height: 100px; }\n"
        ".btn:hover { width: 150px; }\n",
        R"(<UI><Element name="btn" class="btn"/></UI>)"));
    ASSERT_FLOAT_EQ(h.btn()->layout().size.x, 100.f);
    h.Point(50.f, 50.f);
    EXPECT_FLOAT_EQ(h.btn()->layout().size.x, 150.f)
        << "the restyle did not reach the layout";
}

// Same guard as the binder, for the same reason: handlers may restructure the
// tree during UpdatePointer, which is the call immediately before Update().
TEST(UIPseudoStyler, ARestructuredTreeForcesAReCollect) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss, R"(<UI>
          <Element name="btn" class="btn" style="width: 100px; height: 100px"/>
          <Element name="btn2" class="btn" style="width: 100px; height: 100px"/>
        </UI>)"));
    EXPECT_EQ(h.styler.watchedCount(), 2u);

    UIElement* doomed = h.doc.root().Find("btn2");
    ASSERT_NE(doomed, nullptr);
    h.doc.root().RemoveChild(doomed);   // ownership returned and dropped

    h.styler.Update();                  // must re-collect, not dereference
    EXPECT_EQ(h.styler.watchedCount(), 1u) << "a stale watch entry survived";
}

// Re-collecting seeds each entry with the state the element has RIGHT NOW,
// which would silently absorb a state change that happened in the same frame as
// the structural one — leaving the element looking un-hovered until the pointer
// leaves and comes back. A PointerEnter handler that touches the tree is the
// realistic way to hit it.
TEST(UIPseudoStyler, AStateChangeIsNotLostToASameFrameStructuralChange) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss, R"(<UI>
          <Element name="btn" class="btn" style="width: 100px; height: 100px"/>
          <Element name="spare" style="width: 10px; height: 10px"/>
        </UI>)"));
    ASSERT_NEAR(red(h.btn()), 0.125f, 0.01f);

    // Entering the button removes a DIFFERENT element, bumping the structure
    // epoch on the very frame hover turns on.
    UIElement* btn = h.btn();
    ASSERT_NE(btn, nullptr);
    btn->OnPointerEnter([&](UIEvent&) {
        if (UIElement* spare = h.doc.root().Find("spare")) h.doc.root().RemoveChild(spare);
    });

    EXPECT_TRUE(h.Point(50.f, 50.f)) << "the restyle was skipped entirely";
    EXPECT_NEAR(red(btn), 0.502f, 0.01f)
        << "hover styling was lost to a same-frame structural change";
}

// The other direction, and the one a "restyle only elements in a non-default
// state" fix would miss: the element's Style still holds the PREVIOUS state's
// declarations, and the cascade has no undo, so leaving hover on an
// epoch-bumped frame must still re-cascade.
TEST(UIPseudoStyler, HoverEndingOnAStructuralFrameStillUnapplies) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss));
    h.Point(50.f, 50.f);
    ASSERT_NEAR(red(h.btn()), 0.502f, 0.01f);

    // Any tree in the process bumps the epoch — it is deliberately global.
    { UIDocument other; other.root().AddChild("x"); }

    EXPECT_TRUE(h.Away()) << "the re-collect skipped the restyle";
    EXPECT_NEAR(red(h.btn()), 0.125f, 0.01f)
        << "the element stayed stuck in its hover styling";
}

// An app that restructures every frame (damage popups, a scrolling list) would
// take the re-collect branch on every single frame. The feature must still
// work there, not silently die for the whole run.
TEST(UIPseudoStyler, WorksEvenWhenTheEpochMovesEveryFrame) {
    Hoverable h;
    ASSERT_TRUE(h.Load(kBtnCss));
    { UIDocument churn; churn.root().AddChild("a"); }
    EXPECT_TRUE(h.Point(50.f, 50.f));
    EXPECT_NEAR(red(h.btn()), 0.502f, 0.01f) << ":hover never applied at all";

    { UIDocument churn; churn.root().AddChild("b"); }
    EXPECT_TRUE(h.Point(50.f, 50.f, /*down=*/true));
    EXPECT_NEAR(red(h.btn()), 1.0f, 0.01f) << ":active never applied at all";
}

// ------------------------------------------------- the shipped HUD

TEST(UIPseudoHud, TheShippedButtonStylesItselfOnHoverAndPress) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr) << "the shipped HUD did not load";

    UIElement* btn = hud.find("scoreButton");
    ASSERT_NE(btn, nullptr);
    // Nothing in C++ touches this element — it is watched purely because
    // hud.cstyle carries .btn:hover and .btn:active. Nine in total: three .btn
    // (+100, and the inventory's PREV/NEXT), three .tab headers (which carry
    // :hover and :focus rules), .field:* and .notes:focus for the two text
    // fields, and .log:focus for the scrolling panel.
    //
    // The count is asserted rather than merely observed because "watched" is
    // the whole cost model here: only elements some pseudo-class rule could
    // reach are watched at all, so a number that quietly climbed would mean the
    // filter had stopped filtering — and a TabView generates its headers, so it
    // is exactly the kind of feature that could add watched elements without
    // anyone writing one.
    EXPECT_EQ(hud.assets()->styler().watchedCount(), 10u);

    const glm::vec4 idle = btn->style().backgroundColor;
    const glm::vec2 c = btn->layout().position + btn->layout().size * 0.5f;
    ASSERT_GT(btn->layout().size.x, 0.f);

    hud.Point(c.x, c.y);
    hud.Frame();
    const glm::vec4 hovered = btn->style().backgroundColor;
    EXPECT_NE(hovered, idle) << "the shipped .btn:hover rule did nothing";

    hud.Point(c.x, c.y, /*down=*/true);
    hud.Frame();
    EXPECT_NE(btn->style().backgroundColor, hovered)
        << "the shipped .btn:active rule did nothing";

    hud.Point(-1.f, -1.f, false, /*inside=*/false);
    hud.Frame();
    EXPECT_EQ(btn->style().backgroundColor, idle) << "the button never returned to idle";
}

// Hover styling must not need a bind callback, a cached pointer, or any C++ at
// all — that is the whole reason it moved into the stylesheet.
TEST(UIPseudoHud, HoverStateSurvivesAHotReload) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr);

    hud.data().SetInt("score", 700);
    ASSERT_TRUE(hud.assets()->Reload());
    EXPECT_TRUE(hud.assets()->binder().ok())
        << (hud.assets()->binder().errors().empty() ? ""
                                                    : hud.assets()->binder().errors()[0]);
    EXPECT_EQ(hud.assets()->styler().watchedCount(), 10u)
        << "the watch list was not rebuilt after a reload";
    hud.Frame();
    EXPECT_EQ(hud.find("scoreLabel")->style().text, "SCORE 700")
        << "the reload lost the model";

    UIElement* btn = hud.find("scoreButton");
    ASSERT_NE(btn, nullptr);
    const glm::vec4 idle = btn->style().backgroundColor;
    const glm::vec2 c = btn->layout().position + btn->layout().size * 0.5f;
    hud.Point(c.x, c.y);
    hud.Frame();
    EXPECT_NE(btn->style().backgroundColor, idle) << "hover stopped working after a reload";
}


// -------------------------------- state on a NON-SUBJECT compound (U23a)
//
// `.panel:hover .label` reads the PANEL's state and styles the LABEL. Both
// halves of that were broken until U23a, independently:
//
//  - HasStateRuleFor asked only the selector's SUBJECT for a pseudo, so the
//    panel went on no watch list and hovering it triggered nothing at all;
//  - and the re-cascade covered one element, so even a watched panel would not
//    have restyled the label.

TEST(UIPseudoState, AnAncestorsHoverIsWatchedAndReachesTheDescendant) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        ".panel { width: 100px; height: 100px; }\n"
        ".label { color: rgba(10, 10, 10, 1); }\n"
        ".panel:hover .label { color: rgba(250, 200, 60, 1); }\n", "t.cstyle"));

    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="panel" class="panel">)"
        R"(<Label name="lbl" class="label" text="hi"/></Element></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    EXPECT_TRUE(sheet.HasStateRuleFor(*doc.root().Find("panel")))
        << "the panel is not watched, so its hover triggers no restyle at all";

    UIBindingContext ctx;
    UIBinder binder;
    UIInteractionStyler styler;
    sheet.ApplyTo(doc.root());
    binder.Rebuild(doc, ctx, "t.cxml", &sheet);
    styler.Rebuild(doc, sheet, &binder);
    doc.Layout(400.f, 400.f);

    UIElement* lbl = doc.root().Find("lbl");
    ASSERT_NEAR(lbl->style().textColor.r, 10.0f / 255.0f, 0.01f);

    UIPointerState p;
    p.position = { 50.f, 50.f };
    p.inside = true;
    doc.UpdatePointer(p);
    styler.Update();
    EXPECT_NEAR(lbl->style().textColor.r, 250.0f / 255.0f, 0.01f)
        << "hovering the ancestor did not reach the descendant";

    p.position = { -1.f, -1.f };
    p.inside = false;
    doc.UpdatePointer(p);
    styler.Update();
    EXPECT_NEAR(lbl->style().textColor.r, 10.0f / 255.0f, 0.01f)
        << "the descendant kept the hover colour after the pointer left";
}

// The mirror image, and the reason the query is "non-subject" rather than "any
// part": in `.panel .btn:hover` the state is the BUTTON's. Watching the panel
// too would restyle a subtree on every pointer crossing for no reason.
TEST(UIPseudoState, AnAncestorWithNoPseudoOfItsOwnIsNotWatched) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(".panel .btn:hover { color: red; }", "t.cstyle"));
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="panel" class="panel">)"
        R"(<Element name="b" class="btn"/></Element></UI>)", errors, "t.cxml"));
    EXPECT_TRUE(sheet.HasStateRuleFor(*doc.root().Find("b")));
    EXPECT_FALSE(sheet.HasStateRuleFor(*doc.root().Find("panel")));
}
