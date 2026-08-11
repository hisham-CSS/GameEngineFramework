// <TabView> — a tab strip and its panels, expanded ONCE at load.
//
// Switching tabs writes a bool and nothing else. The tree never changes shape,
// for the same reason `repeat=` expands at load: UIElement::structureEpoch() is
// process-wide, so building and destroying panels would make every binder in
// every document re-collect on the frames a user clicked a tab.
//
// The three decisions these tests defend, each with a concrete failure behind
// it:
//
//  - panel visibility is an injected Display BINDING, never a style().display
//    write. UIStyleSheet::Recascade resets Style wholesale and re-applies only
//    BINDINGS, so a raw write is erased the first time any :hover or :focus
//    rule touches the panel — and the hidden panel comes back;
//  - `selected` is a toggled CLASS, not a pseudo-class. A real :selected would
//    need a fifth bool on every UIElement, a parser entry, a compound-matcher
//    case and a fifth slot in the interaction styler's watch struct, to buy
//    only that an author cannot remove it by hand;
//  - switching away from a panel that owns focus moves focus to the header.
//    The document's own eviction runs in UpdateKeyboard, which only the
//    keyboard-target document executes, and even there it evicts to NULL — so
//    the next Tab would restart at the top of the whole HUD.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UITabView.h"
#include "../Engine/src/ui/UITabs.h"

#include <memory>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

const char* kThree = R"(<UI>
  <TabView name="demo">
    <Tab label="One"   name="p0"><Label name="l0" text="first"/></Tab>
    <Tab label="Two"   name="p1"><Button name="b1" text="go"/></Tab>
    <Tab label="Three" name="p2"><Label name="l2" text="third"/></Tab>
  </TabView>
</UI>)";

// Loads markup, builds the tab views by hand and resolves — the same order
// UIAssetDocument::Reload uses, without needing files on disk.
struct Rig {
    UIDataSource tabSrc;
    UIDataSource hud;      // the app source a bind-selected can point at
    UIBindingContext ctx;
    UIDocument doc;
    std::vector<UITabSpec> specs;
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<UITabView>> views;
    UIBinder binder;

    ~Rig() { binder.Clear(); }

    bool Load(const char* xml) {
        ctx.RegisterSource(kTabSourceName, &tabSrc);
        ctx.RegisterSource("hud", &hud);
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml", nullptr, &specs)) return false;
        for (const auto& s : specs) {
            views.push_back(std::make_unique<UITabView>());
            views.back()->Build(s, doc, tabSrc, ctx, errors, "t.cxml");
        }
        binder.Rebuild(doc, ctx, "t.cxml");
        return true;
    }
    UIBindTick Frame() {
        for (auto& v : views) v->Refresh();
        return binder.UpdateToTarget();
    }
    UITabView* view(std::size_t i = 0) { return i < views.size() ? views[i].get() : nullptr; }
    UIElement* find(const char* n) { return doc.root().Find(n); }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
    bool anyErrorContains(const char* needle) const {
        for (const auto& e : errors) {
            if (e.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

void clickAt(UIDocument& doc, const glm::vec2& p) {
    UIPointerState s;
    s.inside = true;
    s.position = p;
    s.buttonDown = true;  doc.UpdatePointer(s);
    s.buttonDown = false; doc.UpdatePointer(s);
}

void pressKey(UIDocument& doc, UIKey k) {
    UIKeyboardState kb;
    UIKeyEvent e;
    e.key = k;
    kb.keys.push_back(e);
    doc.UpdateKeyboard(kb);
}

} // namespace

// ------------------------------------------------------------ the expansion

TEST(UITabs, ATabViewExpandsIntoAStripAndOnePanelPerTab) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
    ASSERT_EQ(r.specs.size(), 1u);

    UIElement* view = r.find("demo");
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(view->children().size(), 4u) << "one strip plus three panels";
    EXPECT_EQ(view->children()[0]->type(), "TabStrip");
    EXPECT_EQ(view->children()[0]->children().size(), 3u);
    EXPECT_TRUE(view->HasClass("tab-view"));

    UITabView* tv = r.view();
    ASSERT_NE(tv, nullptr);
    EXPECT_EQ(tv->count(), 3u);
    EXPECT_EQ(tv->selected(), 0);
    EXPECT_EQ(tv->header(0)->style().text, "One");
    EXPECT_EQ(tv->header(2)->style().text, "Three");
    // A header must be reachable with Tab: the type-word rule that makes a
    // Button focusable covers only Button and TextField.
    EXPECT_TRUE(tv->header(0)->isFocusable());
    // Every other attribute on a <Tab> belongs to the PANEL.
    EXPECT_EQ(tv->panel(1), r.find("p1"));
    EXPECT_TRUE(tv->panel(1)->HasClass("tab-panel"));
}

TEST(UITabs, OnlyTheSelectedPanelIsVisibleBeforeTheFirstLayout) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    UITabView* tv = r.view();
    EXPECT_EQ(tv->panel(0)->style().display, DisplayMode::Flex);
    EXPECT_EQ(tv->panel(1)->style().display, DisplayMode::None);
    EXPECT_EQ(tv->panel(2)->style().display, DisplayMode::None);
}

TEST(UITabs, AnInitialSelectedAttributeChoosesThePanel) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI>
      <TabView name="demo" selected="2">
        <Tab label="One"><Label text="a"/></Tab>
        <Tab label="Two"><Label text="b"/></Tab>
        <Tab label="Three"><Label text="c"/></Tab>
      </TabView></UI>)")) << r.firstError();
    UITabView* tv = r.view();
    EXPECT_EQ(tv->selected(), 2);
    EXPECT_EQ(tv->panel(0)->style().display, DisplayMode::None);
    EXPECT_EQ(tv->panel(2)->style().display, DisplayMode::Flex);
}

TEST(UITabs, TheSelectedHeaderCarriesTheSelectedClassAndNoOtherHeaderDoes) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    UITabView* tv = r.view();
    auto marked = [&] {
        int n = 0;
        for (std::size_t i = 0; i < tv->count(); ++i) {
            if (tv->header(i)->HasClass(kTabSelectedClass)) ++n;
        }
        return n;
    };
    EXPECT_EQ(marked(), 1);
    EXPECT_TRUE(tv->header(0)->HasClass(kTabSelectedClass));

    tv->Select(2);
    EXPECT_EQ(marked(), 1);
    EXPECT_TRUE(tv->header(2)->HasClass(kTabSelectedClass));
    EXPECT_FALSE(tv->header(0)->HasClass(kTabSelectedClass));
}

// ---------------------------------------------------------------- switching

TEST(UITabs, SwitchingTabsDoesNotMoveTheStructureEpoch) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    const std::uint32_t epoch = UIElement::structureEpoch();
    r.view()->Select(1);
    r.Frame();
    r.view()->Select(2);
    r.Frame();
    EXPECT_EQ(UIElement::structureEpoch(), epoch)
        << "switching a tab changed the tree's shape - every binder in the "
           "process just re-collected";
}

TEST(UITabs, ClickingAHeaderSwitchesThePanelInTheSameFrame) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    UITabView* tv = r.view();
    // Give the headers real boxes so a click can land on one.
    for (std::size_t i = 0; i < tv->count(); ++i) {
        tv->header(i)->style().width = StyleLength::Px(80.f);
        tv->header(i)->style().height = StyleLength::Px(24.f);
    }
    r.find("demo")->children()[0]->style().direction = FlexDirection::Row;
    r.doc.Layout(400.f, 300.f);

    UIElement* h1 = tv->header(1);
    clickAt(r.doc, h1->layout().position + h1->layout().size * 0.5f);
    EXPECT_EQ(tv->selected(), 1) << "the click did not reach the header";
    // Select() writes the source immediately, so the binder picks the Display
    // change up on the very next pass — inside the same frame.
    const UIBindTick tick = r.binder.UpdateToTarget();
    EXPECT_TRUE(tick.wroteLayout) << "the panel swap did not force a re-layout";
    EXPECT_EQ(tv->panel(0)->style().display, DisplayMode::None);
    EXPECT_EQ(tv->panel(1)->style().display, DisplayMode::Flex);
}

// A hidden panel is display:none, and the focus walk skips those — so its
// controls must contribute no Tab stops.
TEST(UITabs, AHiddenPanelContributesNoTabStops) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.doc.Layout(400.f, 300.f);
    for (int i = 0; i < 4; ++i) {
        r.doc.FocusNext();
        EXPECT_NE(r.doc.focused(), r.find("b1"))
            << "Tab reached a button inside a hidden panel";
    }
}

// The decision-(b) test. This fails the day someone "simplifies" the injected
// Display binding into a style().display write: Recascade resets Style and
// re-applies only bindings, so the raw write would be erased and the hidden
// panel would reappear the first time a pseudo-class rule touched it.
TEST(UITabs, ARecascadeDoesNotResurrectAHiddenPanel) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    UITabView* tv = r.view();
    ASSERT_EQ(tv->panel(1)->style().display, DisplayMode::None);

    // Exactly what :hover restyling does to an element.
    UIStyleSheet sheet;
    sheet.Recascade(*tv->panel(1));
    // ...and the binder re-applies the bindings the cascade just wiped, which
    // is what UIInteractionStyler does immediately after its own Recascade.
    r.binder.ReapplyFor(tv->panel(1));
    EXPECT_EQ(tv->panel(1)->style().display, DisplayMode::None)
        << "a re-cascade brought a hidden panel back";
}

// -------------------------------------------------------------------- focus

// Otherwise focus drops to null and the next Tab restarts at the first
// focusable in the whole HUD, rather than at the tab just opened.
TEST(UITabs, SwitchingAwayFromAPanelThatOwnsFocusMovesFocusToTheHeader) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.view()->Select(1);
    r.Frame();
    r.doc.Layout(400.f, 300.f);

    UIElement* btn = r.find("b1");
    ASSERT_NE(btn, nullptr);
    r.doc.SetFocus(btn);
    ASSERT_EQ(r.doc.focused(), btn);

    r.view()->Select(2);
    EXPECT_EQ(r.doc.focused(), r.view()->header(2))
        << "focus was dropped instead of following the switch";
}

// The document evicts focus from a hidden subtree in UpdateKeyboard, which
// UIWorld calls only for the keyboard-target document — so a background or
// non-interactive document would keep :focus lit inside a hidden panel forever.
TEST(UITabs, FocusInsideAHiddenPanelIsNotLeftLitOnANonKeyboardDocument) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.view()->Select(1);
    r.Frame();
    r.doc.Layout(400.f, 300.f);
    r.doc.SetFocus(r.find("b1"));

    r.view()->Select(0);
    // No UpdateKeyboard call at all — this document is not the keyboard target.
    EXPECT_NE(r.doc.focused(), r.find("b1"))
        << "focus stayed inside a panel that is no longer visible";
}

TEST(UITabs, ArrowKeysMoveBetweenHeadersAndSelectAsTheyGo) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.doc.Layout(400.f, 300.f);
    UITabView* tv = r.view();
    r.doc.SetFocus(tv->header(0));

    pressKey(r.doc, UIKey::Right);
    EXPECT_EQ(tv->selected(), 1);
    EXPECT_EQ(r.doc.focused(), tv->header(1));

    pressKey(r.doc, UIKey::End);
    EXPECT_EQ(tv->selected(), 2);

    pressKey(r.doc, UIKey::Home);
    EXPECT_EQ(tv->selected(), 0);

    // Wraps, like a real tab strip.
    pressKey(r.doc, UIKey::Left);
    EXPECT_EQ(tv->selected(), 2);
}

// A key pressed inside a PANEL bubbles through the strip's ancestor chain and
// must not be stolen — a Left arrow in a text field moves the caret.
TEST(UITabs, AnArrowInsideAPanelIsNotStolenByTheStrip) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.doc.Layout(400.f, 300.f);
    UITabView* tv = r.view();
    tv->Select(1);
    r.Frame();
    r.doc.Layout(400.f, 300.f);
    r.doc.SetFocus(r.find("b1"));

    pressKey(r.doc, UIKey::Right);
    EXPECT_EQ(tv->selected(), 1) << "the strip consumed an arrow aimed at a panel";
}

// -------------------------------------------------------------- composition

// Passing nullptr for `specs` would fail every repeat inside a tab with
// repeat's NESTING message, naming a feature the author never used.
TEST(UITabs, ARepeatInsideATabPanelStillReachesTheRepeatSpecs) {
    UIDataSource hud, tabSrc;
    UIList inv;
    inv.Add().SetString("name", "Potion");
    hud.SetList("inventory", std::move(inv));

    UIBindingContext ctx;
    ctx.RegisterSource("hud", &hud);
    ctx.RegisterSource(kTabSourceName, &tabSrc);
    UIDocument doc;
    std::vector<UIRepeatSpec> repeats;
    std::vector<UITabSpec> tabs;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="hud">
      <TabView name="demo">
        <Tab label="Bag">
          <Element name="bag" repeat="inventory" repeat-count="2">
            <Element><Label name="l" text="{name}"/></Element>
          </Element>
        </Tab>
      </TabView></UI>)", errors, "t.cxml", &repeats, &tabs))
        << (errors.empty() ? "" : errors[0]);
    EXPECT_EQ(repeats.size(), 1u) << "a repeat inside a tab never reached the caller";
    EXPECT_EQ(tabs.size(), 1u);
}

TEST(UITabs, ATabViewInsideATabPanelExpandsBothStrips) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI>
      <TabView name="outer">
        <Tab label="A">
          <TabView name="inner">
            <Tab label="A1"><Label text="x"/></Tab>
            <Tab label="A2"><Label text="y"/></Tab>
          </TabView>
        </Tab>
        <Tab label="B"><Label text="z"/></Tab>
      </TabView></UI>)")) << r.firstError();
    ASSERT_EQ(r.specs.size(), 2u);
    EXPECT_EQ(r.view(0)->count(), 2u);
    EXPECT_EQ(r.view(1)->count(), 2u);
}

// The index is readable from markup, which is what stands in for the two-way
// binding U20 deliberately does not ship.
TEST(UITabs, TheSelectedIndexIsReadableThroughTheReservedSource) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI>
      <TabView name="demo">
        <Tab label="One"><Label text="a"/></Tab>
        <Tab label="Two"><Label text="b"/></Tab>
      </TabView>
      <Label name="readout" text="TAB {__tabs.demo}"/>
    </UI>)")) << r.firstError();
    EXPECT_TRUE(r.binder.ok()) << (r.binder.errors().empty() ? "" : r.binder.errors()[0]);
    EXPECT_EQ(r.find("readout")->style().text, "TAB 0");

    r.view()->Select(1);
    r.Frame();
    EXPECT_EQ(r.find("readout")->style().text, "TAB 1");
}

TEST(UITabs, AnIdleTabViewAppliesNothing) {
    Rig r;
    ASSERT_TRUE(r.Load(kThree)) << r.firstError();
    r.Frame();
    EXPECT_EQ(r.Frame().applied, 0u) << "an untouched TabView re-applied its bindings";
}

// --------------------------------------------------------------- diagnostics

TEST(UITabs, AMissingTabViewNameIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><TabView><Tab label="A"><Label text="x"/></Tab></TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'name' is required on a <TabView>")) << r.firstError();
}

TEST(UITabs, ADuplicateTabViewNameIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI>
      <TabView name="demo"><Tab label="A"><Label text="x"/></Tab></TabView>
      <TabView name="demo"><Tab label="B"><Label text="y"/></Tab></TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("duplicates the name")) << r.firstError();
}

TEST(UITabs, ASelectedIndexOutOfRangeIsReportedRatherThanClamped) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI>
      <TabView name="demo" selected="5">
        <Tab label="A"><Label text="x"/></Tab>
        <Tab label="B"><Label text="y"/></Tab>
      </TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("expected an index 0..1")) << r.firstError();
}

TEST(UITabs, ATabViewWithNoTabsIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><TabView name="demo"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("found none")) << r.firstError();
}

TEST(UITabs, ANonTabChildOfATabViewIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI>
      <TabView name="demo">
        <Tab label="A"><Label text="x"/></Tab>
        <Element name="row"/>
      </TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("may only contain <Tab>")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("name='row'")) << r.firstError();
}

TEST(UITabs, ATabOutsideATabViewIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><Element><Tab label="A"/></Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("only valid as a direct child of a <TabView>"))
        << r.firstError();
}

TEST(UITabs, ATabWithNoLabelIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><TabView name="demo"><Tab><Label text="x"/></Tab></TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'label' is required on a <Tab>")) << r.firstError();
}

TEST(UITabs, LabelOnANonTabIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><Label label="oops" text="x"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'label' is only valid on a <Tab>")) << r.firstError();
}

TEST(UITabs, SelectedOnANonTabViewIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><Element selected="1"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'selected' is only valid on a <TabView>")) << r.firstError();
}

// The injected Display binding would silently overwrite the author's, leaving a
// condition that does nothing — the same reason a repeat template may not carry
// an if=.
TEST(UITabs, IfOnATabIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><TabView name="demo">
      <Tab label="A" if="alive"><Label text="x"/></Tab></TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("cannot carry its own 'if='")) << r.firstError();
}

TEST(UITabs, RepeatOnATabViewIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><TabView name="demo" repeat="list" repeat-count="2">
      <Tab label="A"><Label text="x"/></Tab></TabView></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'repeat' is not supported on a <TabView>")) << r.firstError();
}

TEST(UITabs, ATabViewAtTheDocumentRootIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<TabView name="demo"><Tab label="A"><Label text="x"/></Tab></TabView>)"));
    EXPECT_TRUE(r.anyErrorContains("cannot be the document root")) << r.firstError();
}

TEST(UITabs, AFailedLoadReportsNoTabSpecs) {
    UIDocument doc;
    std::vector<UITabSpec> tabs;
    tabs.push_back(UITabSpec{});          // a sentinel the loader must not touch
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element nmae="x"/></UI>)",
                                    errors, "t.cxml", nullptr, &tabs));
    EXPECT_EQ(tabs.size(), 1u) << "specs were written for a tree that never committed";
}

// ------------------------------------------------- two-way `bind-selected`
//
// Driven by UITabView rather than by UIBinder, because that is where the
// selection already lives. The tempting shortcut — reusing Kind::Value —
// resolves cleanly, reports ok(), and then never writes anything, because that
// path reads a UITextEdit and bails when there is not one. These tests are what
// stop anyone taking it.

namespace {
const char* kBound = R"(<UI>
  <TabView name="demo" bind-selected="hud.activeTab">
    <Tab label="One"   name="p0"><Label text="a"/></Tab>
    <Tab label="Two"   name="p1"><Label text="b"/></Tab>
    <Tab label="Three" name="p2"><Label text="c"/></Tab>
  </TabView></UI>)";
} // namespace

TEST(UITabs, ABoundSelectionIsCreatedOnTheSourceWhenTheAppNeverDeclaredIt) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
    // Created, like every other write-back target: the TabView owns this value,
    // so an app should not have to declare it before the UI can publish one.
    ASSERT_GE(r.hud.IndexOf("activeTab"), 0) << "the property was never created";
    EXPECT_EQ(r.hud.GetInt("activeTab"), 0);
}

TEST(UITabs, ClickingATabWritesTheIndexBackToTheSource) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    r.view()->Select(2);
    r.Frame();
    EXPECT_EQ(r.hud.GetInt("activeTab"), 2)
        << "the selection never reached the source - this is the silent no-op "
           "the Kind::Value shortcut would have shipped";
}

TEST(UITabs, WritingTheSourceOpensThatTab) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    r.hud.SetInt("activeTab", 1);
    r.Frame();
    EXPECT_EQ(r.view()->selected(), 1);
    EXPECT_EQ(r.view()->panel(1)->style().display, DisplayMode::Flex);
    EXPECT_EQ(r.view()->panel(0)->style().display, DisplayMode::None);
}

// Without comparing against the LAST SEEN source value — rather than against our
// own selection — a click would be reverted by the stale source on the very
// next frame, which is the classic two-way binding bug.
TEST(UITabs, AClickIsNotRevertedByTheStaleSourceOnTheNextFrame) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    r.view()->Select(2);
    for (int i = 0; i < 5; ++i) {
        r.Frame();
        ASSERT_EQ(r.view()->selected(), 2) << "reverted on frame " << i;
    }
    EXPECT_EQ(r.hud.GetInt("activeTab"), 2);
}

// A game that writes a nonsense index must end up agreeing with what is on
// screen, rather than being left holding a value the UI ignored.
TEST(UITabs, AnOutOfRangeSourceValueIsClampedAndWrittenBack) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    r.hud.SetInt("activeTab", 99);
    r.Frame();
    EXPECT_EQ(r.view()->selected(), 2);
    r.Frame();
    EXPECT_EQ(r.hud.GetInt("activeTab"), 2)
        << "the source was left disagreeing with the visible tab";
}

// A saved menu re-opens where it was, rather than snapping to the markup
// default for a frame and then jumping.
TEST(UITabs, AnExistingSourceValueWinsOverTheMarkupDefaultAtLoad) {
    Rig r;
    r.hud.SetInt("activeTab", 2);
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    EXPECT_EQ(r.view()->selected(), 2) << "the saved selection was overwritten at load";
    EXPECT_EQ(r.view()->panel(2)->style().display, DisplayMode::Flex);
}

TEST(UITabs, AnIdleBoundTabViewWritesNothing) {
    Rig r;
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    r.Frame();
    const std::uint32_t v = r.hud.version();
    r.Frame();
    r.Frame();
    EXPECT_EQ(r.hud.version(), v) << "an untouched two-way link wrote every frame";
}

TEST(UITabs, BindSelectedNamingAnUnknownSourceIsReported) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI><TabView name="demo" bind-selected="nope.tab">
      <Tab label="A"><Label text="x"/></Tab></TabView></UI>)")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("unknown data source 'nope'")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("registered:")) << r.firstError();
}

// Reported rather than dropped: a link that silently only worked one way looks
// exactly like a UI that was never wired up.
TEST(UITabs, BindSelectedToAReadOnlyPropertyIsReported) {
    Rig r;
    int backing = 1;
    r.hud.Observe("activeTab", [&] { return UIValue::Int(backing); });   // no setter
    ASSERT_TRUE(r.Load(kBound)) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("read-only")) << r.firstError();
    // ...and the link is not half-installed: clicking still moves the UI.
    r.view()->Select(2);
    r.Frame();
    EXPECT_EQ(r.view()->selected(), 2);
}

TEST(UITabs, BindSelectedOnANonTabViewIsReported) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI><Element bind-selected="hud.tab"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'bind-selected' is only valid on a <TabView>"))
        << r.firstError();
}

TEST(UITabs, ABareBindSelectedPathUsesTheScopeSource) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <TabView name="demo" bind-selected="activeTab">
        <Tab label="A"><Label text="x"/></Tab>
        <Tab label="B"><Label text="y"/></Tab>
      </TabView></UI>)")) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
    r.view()->Select(1);
    r.Frame();
    EXPECT_EQ(r.hud.GetInt("activeTab"), 1);
}

// THE SELECTED HEADER MUST ACTUALLY RESTYLE.
//
// UITabView writes `.tab-selected` onto the chosen header with AddClass, and a
// comment claimed AddClass/RemoveClass "re-cascade on their own". They do not:
// both only record the class, and the cascade has no undo. So the class was on
// the element, the rule was in the sheet, and the header never changed
// appearance -- the one visual affordance a tab strip has.
TEST(UITabView, SelectingAHeaderRestylesIt) {
    UIDataSource tabSrc, hud;
    UIBindingContext ctx;
    UIDocument doc;
    std::vector<UITabSpec> specs;
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<UITabView>> views;
    UIBinder binder;
    UIStyleSheet sheet;

    ctx.RegisterSource(kTabSourceName, &tabSrc);
    ctx.RegisterSource("hud", &hud);

    ASSERT_TRUE(sheet.ParseString(
        ".selected { background-color: #ff0000; }", "t.cstyle"));

    const char* xml =
        R"(<UI><TabView name="tv"><Tab label="One"><Element name="p1"/></Tab>)"
        R"(<Tab label="Two"><Element name="p2"/></Tab></TabView></UI>)";
    ASSERT_TRUE(UIMarkup::LoadInto(doc, xml, errors, "t.cxml", nullptr, &specs))
        << (errors.empty() ? std::string() : errors[0]);

    for (const auto& sp : specs) {
        views.push_back(std::make_unique<UITabView>());
        views.back()->Build(sp, doc, tabSrc, ctx, errors, "t.cxml", &binder);
    }
    sheet.ApplyTo(doc.root());
    binder.Rebuild(doc, ctx, "t.cxml", &sheet);
    binder.UpdateToTarget();

    UITabView* tv = views.empty() ? nullptr : views[0].get();
    ASSERT_NE(tv, nullptr);
    ASSERT_EQ(tv->count(), 2u);

    UIElement* h0 = tv->header(0);
    UIElement* h1 = tv->header(1);
    ASSERT_NE(h0, nullptr); ASSERT_NE(h1, nullptr);
    ASSERT_FLOAT_EQ(h0->style().backgroundColor.r, 1.0f)
        << "header 0 starts selected, so the sheet should already have styled it";
    ASSERT_FLOAT_EQ(h1->style().backgroundColor.r, 0.0f)
        << "header 1 is unselected, so no rule should have touched it";

    // Select the second tab.
    tv->Select(1);
    for (auto& v : views) v->Refresh();
    binder.UpdateToTarget();

    EXPECT_FLOAT_EQ(h1->style().backgroundColor.r, 1.0f)
        << "the newly selected header did not pick up `.selected` -- the "
           "class was added but nothing re-ran the cascade";
    EXPECT_FLOAT_EQ(h0->style().backgroundColor.r, 0.0f)
        << "the DESELECTED header kept its selected styling -- the cascade has "
           "no undo, so dropping the class alone changes nothing";
}
