// `repeat=` — collection binding as a FIXED POOL with a sliding window.
//
// The tree is expanded once, at load, and never changes shape again. That is
// not a simplification, it is the point: UIElement::structureEpoch() is
// process-wide, so a list that grew and shrank its children would make every
// binder in every document re-collect and re-resolve on the frames it moved.
// The elements stand still and the DATA moves instead.
//
// The failures these tests exist to catch, in order of how expensive they were
// to find:
//
//  - a slot the window NEVER REACHES has no columns, so its bindings never
//    resolve, so UIBinder counts them unresolved forever — which permanently
//    arms its pending-retry check and turns every frame in which any source
//    moves into a full document re-collect. Seeding every column on every slot
//    at build time is the whole reason the feature is viable;
//  - a row column called `index` and an engine-published `index` would be the
//    SAME property, alternating every frame, pinning that slot hot forever.
//    Hence the '$' names;
//  - teardown driven by anything other than the names actually registered
//    leaves a freed UIDataSource* in the context, which sourceVersionSum()
//    dereferences on the very next frame;
//  - a template root with its own `if=` would lose the row's visibility
//    binding and leave surplus rows laid out, painted and clickable.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIRepeat.h"
#include "../Engine/src/ui/UIRepeatPool.h"
#include "ui_shipped_hud.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

const char* kBag = R"(<UI data-source="hud">
  <Element name="bag" repeat="inventory" repeat-count="4">
    <Element name="slot" class="slot">
      <Label name="slotName" text="{name}"/>
      <Label name="slotQty"  text="x{count}"/>
    </Element>
  </Element>
</UI>)";

UIList makeList(std::initializer_list<std::pair<const char*, long long>> items) {
    UIList l;
    for (const auto& it : items) {
        UIRecord& r = l.Add();
        r.SetString("name", it.first);
        r.SetInt("count", it.second);
    }
    return l;
}

// Loads markup from a string, builds the pools by hand, and resolves — the
// same order UIAssetDocument::Reload uses, without needing files on disk.
struct Rig {
    UIDataSource hud;
    UIBindingContext ctx;
    UIDocument doc;
    std::vector<UIRepeatSpec> specs;
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<UIRepeatPool>> pools;
    UIBinder binder;

    // Declared last so it is destroyed FIRST: it caches a UIDataSource* into
    // every slot, exactly as UIAssetDocument's member order guarantees.
    ~Rig() { binder.Clear(); }

    bool Load(const char* xml) {
        ctx.RegisterSource("hud", &hud);
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml", &specs)) return false;
        for (const auto& s : specs) {
            pools.push_back(std::make_unique<UIRepeatPool>());
            pools.back()->Build(s, ctx, errors, "t.cxml");
        }
        Refresh();
        binder.Rebuild(doc, ctx, "t.cxml");
        return true;
    }
    void Refresh() { for (auto& p : pools) p->Refresh(ctx); }
    // One frame, in the order UIWorld runs it.
    UIBindTick Frame() { Refresh(); return binder.UpdateToTarget(); }

    // The i'th clone of the first repeat.
    UIElement* slot(std::size_t i) {
        UIElement* bag = doc.root().Find("bag");
        return (bag && i < bag->children().size()) ? bag->children()[i].get() : nullptr;
    }
    std::string textOf(std::size_t i, const char* name) {
        UIElement* s = slot(i);
        if (!s) return "<no slot>";
        UIElement* l = s->Find(name);
        return l ? l->style().text : std::string("<no label>");
    }
    std::string firstError() const { return errors.empty() ? std::string() : errors[0]; }
    bool anyErrorContains(const char* needle) const {
        for (const auto& e : errors) {
            if (e.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

} // namespace

// ------------------------------------------------- grammar and load refusal

TEST(UIRepeat, ARepeatAttributeIsNotReportedAsAnUnknownAttribute) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
}

TEST(UIRepeat, AMisspeltRepeatAttributeIsStillAnUnknownAttribute) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-cont="4"><Element/></Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("unknown attribute 'repeat-cont'")) << r.firstError();
}

TEST(UIRepeat, RepeatWithoutACountIsRejectedBecauseThePoolSizeIsFixedAtLoad) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory"><Element/></Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("'repeat-count' is required")) << r.firstError();
}

TEST(UIRepeat, ACountOutsideTheRangeIsRejectedAndTheMessageNamesTheRange) {
    for (const char* bad : { "0", "65", "lots", "-1" }) {
        Rig r;
        const std::string xml = std::string(R"(<UI data-source="hud">
          <Element name="bag" repeat="inventory" repeat-count=")") + bad +
          R"("><Element/></Element></UI>)";
        EXPECT_FALSE(r.Load(xml.c_str())) << "accepted repeat-count='" << bad << "'";
        EXPECT_TRUE(r.anyErrorContains("expected a count 1..64")) << r.firstError();
    }
}

TEST(UIRepeat, RepeatCountWithoutRepeatIsRejectedRatherThanIgnored) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud"><Element repeat-count="4"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("only valid with 'repeat'")) << r.firstError();
}

TEST(UIRepeat, RepeatOffsetWithoutRepeatIsRejectedRatherThanIgnored) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud"><Element repeat-offset="scroll"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("only valid with 'repeat'")) << r.firstError();
}

TEST(UIRepeat, ARepeatWithTwoChildrenIsRejectedRatherThanRepeatingTheFirst) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="2"><Element/><Element/></Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("expected exactly one element child")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("found 2")) << r.firstError();
}

TEST(UIRepeat, ARepeatWithNoChildrenIsRejected) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="2"/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("found none")) << r.firstError();
}

TEST(UIRepeat, RepeatOnTheDocumentRootIsRejectedInsteadOfSilentlyDoingNothing) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud" repeat="inventory" repeat-count="2">
      <Element/></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("not valid on the document root")) << r.firstError();
}

TEST(UIRepeat, ANestedRepeatIsRejectedRatherThanMultiplyingTheTree) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="4">
        <Element>
          <Element repeat="inventory" repeat-count="4"><Element/></Element>
        </Element>
      </Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("nested 'repeat' is not supported")) << r.firstError();
}

// A template root with its own if= is the trap: skipping the row's visibility
// binding to keep the author's would leave every surplus slot laid out,
// painted and hit-testable — the exact opposite of what they asked for.
TEST(UIRepeat, ATemplateRootCarryingItsOwnIfIsRejectedRatherThanLosingThePresentBinding) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="4">
        <Element if="equipped"><Label text="{name}"/></Element>
      </Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("cannot carry its own 'if='")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("move the condition to a child")) << r.firstError();
}

TEST(UIRepeat, ATemplateRootCarryingItsOwnDataSourceIsRejected) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="4">
        <Element data-source="hud"><Label text="{name}"/></Element>
      </Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("cannot carry its own 'data-source='")) << r.firstError();
}

// resolvePush_ CREATES a missing write-back target, so a bare push path would
// resolve against the row and be silently overwritten by the next row copy.
TEST(UIRepeat, ABarePushPathInsideARepeatIsRejectedBecauseTheNextRowCopyOverwritesIt) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="4">
        <Element><Button push-hovered="hovering" text="{name}"/></Element>
      </Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("a bare path inside a repeat writes into the row"))
        << r.firstError();
}

TEST(UIRepeat, ABareActionNameInsideARepeatIsRejected) {
    Rig r;
    EXPECT_FALSE(r.Load(R"(<UI data-source="hud">
      <Element repeat="inventory" repeat-count="4">
        <Element><Button on-click="use" text="{name}"/></Element>
      </Element></UI>)"));
    EXPECT_TRUE(r.anyErrorContains("looks it up on the row")) << r.firstError();
}

TEST(UIRepeat, AQualifiedActionInsideARepeatIsAccepted) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    r.hud.AddAction("use", [] {});
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Button on-click="hud.use" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
}

TEST(UIRepeat, AFailedLoadReportsNoRepeatSpecsAndLeavesTheDocumentAlone) {
    Rig r;
    r.ctx.RegisterSource("hud", &r.hud);
    r.specs.push_back(UIRepeatSpec{});           // a sentinel the loader must not touch
    std::vector<std::string> errs;
    EXPECT_FALSE(UIMarkup::LoadInto(r.doc, R"(<UI><Element nmae="x"/></UI>)",
                                    errs, "t.cxml", &r.specs));
    EXPECT_EQ(r.specs.size(), 1u) << "specs were written for a tree that never committed";
    EXPECT_EQ(r.doc.root().children().size(), 0u);
}

// -------------------------------------------------------- expansion + scope

TEST(UIRepeat, TheTreeIsExpandedOnceAndSlidingTheWindowNeverMovesTheStructureEpoch) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 }, { "Rope", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();

    UIElement* bag = r.doc.root().Find("bag");
    ASSERT_NE(bag, nullptr);
    ASSERT_EQ(bag->children().size(), 4u) << "the pool is repeat-count elements, always";

    const std::uint32_t epoch = UIElement::structureEpoch();
    r.hud.SetList("inventory", makeList({ { "Sword", 1 }, { "Shield", 1 }, { "Torch", 9 } }));
    r.Frame();
    EXPECT_EQ(UIElement::structureEpoch(), epoch)
        << "changing the list changed the tree's shape - every binder in the "
           "process just re-collected";
    EXPECT_EQ(r.doc.root().Find("bag")->children().size(), 4u);
}

TEST(UIRepeat, UnqualifiedHolesInsideARepeatReadTheRow) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 }, { "Rope", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_EQ(r.textOf(0, "slotName"), "Potion");
    EXPECT_EQ(r.textOf(0, "slotQty"), "x3");
    EXPECT_EQ(r.textOf(1, "slotName"), "Rope");
}

TEST(UIRepeat, AQualifiedHoleInsideARepeatStillReachesTheOuterSource) {
    Rig r;
    r.hud.SetString("currency", "gold");
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    // Custom delimiter: the markup itself contains `)"`, which would otherwise
    // close a plain R"( ... )" literal in the middle of an attribute.
    ASSERT_TRUE(r.Load(R"XML(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Label name="cur" text="{name} ({hud.currency})"/></Element>
      </Element></UI>)XML")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "cur"), "Potion (gold)");
}

TEST(UIRepeat, TwoRepeatsOverTheSameListDoNotShareSlotSources) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="a" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{name}"/></Element>
      </Element>
      <Element name="b" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    ASSERT_EQ(r.pools.size(), 2u);
    EXPECT_NE(r.pools[0]->spec().namePrefix, r.pools[1]->spec().namePrefix)
        << "both repeats generated the same slot names, so one overwrote the other";
    EXPECT_NE(r.pools[0]->slotSource(0), r.pools[1]->slotSource(0));
}

// ------------------------------------------------------- the retryPending trap

// The headline. A slot the window never reaches has no row, so without seeding
// its columns at build time its bindings never resolve — and UIBinder counts
// an unresolved entry forever, which permanently arms retryPending.
TEST(UIRepeat, EverySlotResolvesAtLoadEvenWhenTheListIsEmpty) {
    Rig r;   // no SetList at all
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_EQ(r.binder.unresolvedCount(), 0u)
        << "a slot with no row left its bindings unresolved";
}

TEST(UIRepeat, ASlotTheWindowNeverReachesDoesNotForceARebuildEveryFrame) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));   // 1 row, 4 slots
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    ASSERT_EQ(r.binder.unresolvedCount(), 0u) << r.firstError();
    r.binder.consumeRecollected();          // clear the load-time latch

    // An unrelated source moves, which is what arms retryPending.
    for (int i = 0; i < 5; ++i) {
        r.hud.SetInt("health", 90 + i);
        const UIBindTick tick = r.Frame();
        EXPECT_FALSE(r.binder.consumeRecollected())
            << "frame " << i << ": an unreached slot forced a full re-collect";
        EXPECT_LT(tick.applied, r.binder.bindingCount())
            << "frame " << i << ": every binding re-applied";
    }
}

TEST(UIRepeat, LoadingAPooledDocumentLeavesNoErrorsSoOkStaysTrue) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_TRUE(r.binder.ok()) << (r.binder.errors().empty() ? "" : r.binder.errors()[0]);
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
}

TEST(UIRepeat, SlotsBeyondTheListAreHiddenBeforeTheFirstLayout) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 }, { "Rope", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_EQ(r.slot(0)->style().display, DisplayMode::Flex);
    EXPECT_EQ(r.slot(1)->style().display, DisplayMode::Flex);
    EXPECT_EQ(r.slot(2)->style().display, DisplayMode::None) << "an empty slot is visible";
    EXPECT_EQ(r.slot(3)->style().display, DisplayMode::None);
}

TEST(UIRepeat, AHiddenSlotIsNeitherHitTestableNorTabFocusable) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="3">
        <Element style="width: 100px; height: 20px;">
          <Button name="use" text="{name}"/>
        </Element>
      </Element></UI>)")) << r.firstError();
    r.doc.Layout(200.0f, 200.0f);

    // Two Tab presses: the only focusable element is slot 0's button, so focus
    // must come back to it rather than landing in a hidden slot.
    r.doc.FocusNext();
    UIElement* first = r.doc.focused();
    ASSERT_NE(first, nullptr) << "nothing was focusable at all";
    r.doc.FocusNext();
    EXPECT_EQ(r.doc.focused(), first) << "Tab reached a button in a hidden slot";
}

// --------------------------------------------------------- per-frame behaviour

TEST(UIRepeat, AnUnchangedListRewritesNoSlotAndAppliesNoBinding) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 }, { "Rope", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    r.Frame();                                   // settle
    EXPECT_EQ(r.Frame().applied, 0u) << "an idle pool re-applied its bindings";
    // Re-Setting the identical list must not wake it either: gameplay rebuilding
    // its inventory every frame is the normal case.
    r.hud.SetList("inventory", makeList({ { "Potion", 3 }, { "Rope", 1 } }));
    EXPECT_EQ(r.Frame().applied, 0u) << "an equal list was treated as a change";
}

TEST(UIRepeat, SlidingTheWindowRewritesOnlyTheSlotsWhoseRowChanged) {
    Rig r;
    r.hud.SetInt("scroll", 0);
    r.hud.SetList("inventory", makeList({ { "a", 1 }, { "b", 2 }, { "c", 3 }, { "d", 4 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2" repeat-offset="scroll">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "l"), "a");
    EXPECT_EQ(r.textOf(1, "l"), "b");

    r.hud.SetInt("scroll", 1);
    r.Frame();
    EXPECT_EQ(r.textOf(0, "l"), "b");
    EXPECT_EQ(r.textOf(1, "l"), "c");
}

// props_ never shrinks and there is no way to remove a property, so a column
// the new row lacks would otherwise keep rendering the PREVIOUS row's value,
// with no version bump and no diagnostic to explain it.
TEST(UIRepeat, AColumnMissingFromTheNewRowIsClearedRatherThanKeepingThePreviousRows) {
    Rig r;
    UIList l;
    { UIRecord& a = l.Add(); a.SetString("name", "Sword"); a.SetString("rune", "fire"); }
    { UIRecord& b = l.Add(); b.SetString("name", "Rope"); }   // no rune
    r.hud.SetInt("scroll", 0);
    r.hud.SetList("inventory", std::move(l));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="1" repeat-offset="scroll">
        <Element><Label name="l" text="{name}/{rune}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "l"), "Sword/fire");

    r.hud.SetInt("scroll", 1);
    r.Frame();
    EXPECT_EQ(r.textOf(0, "l"), "Rope/") << "the previous row's rune survived";
}

TEST(UIRepeat, AnAbsoluteIndexIsNotPublishedUnlessTheTemplateReadsIt) {
    Rig plain;
    plain.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(plain.Load(kBag)) << plain.firstError();
    EXPECT_FALSE(plain.pools[0]->spec().readsIndex);
    EXPECT_FALSE(plain.pools[0]->spec().readsCount);

    Rig reads;
    reads.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(reads.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{$index}/{$count}"/></Element>
      </Element></UI>)")) << reads.firstError();
    EXPECT_TRUE(reads.pools[0]->spec().readsIndex);
    EXPECT_TRUE(reads.pools[0]->spec().readsCount);
    EXPECT_EQ(reads.textOf(0, "l"), "0/1");
}

TEST(UIRepeat, ANegativeOffsetIsClampedInsteadOfIndexingBeforeTheFirstRow) {
    Rig r;
    r.hud.SetInt("scroll", -5);
    r.hud.SetList("inventory", makeList({ { "a", 1 }, { "b", 2 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2" repeat-offset="scroll">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "l"), "a");
}

// A fixed pool showing a PARTIAL window is the one thing this shape must never
// do - the elements are supposed to stand still. Without the upper clamp the
// tail of a list is unreachable as a group: a 12-row list through a 5-slot pool
// ends at offset 11 showing one item and four blanks, and the panel appears to
// shrink as you approach the end.
TEST(UIRepeat, AnOffsetPastTheEndIsClampedToTheLastFullWindow) {
    Rig r;
    r.hud.SetInt("scroll", 0);
    r.hud.SetList("inventory", makeList({ { "a", 1 }, { "b", 2 }, { "c", 3 }, { "d", 4 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2" repeat-offset="scroll">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();

    r.hud.SetInt("scroll", 99);
    r.Frame();
    EXPECT_EQ(r.textOf(0, "l"), "c") << "the window ran off the end instead of stopping";
    EXPECT_EQ(r.textOf(1, "l"), "d");
    EXPECT_EQ(r.slot(0)->style().display, DisplayMode::Flex);
    EXPECT_EQ(r.slot(1)->style().display, DisplayMode::Flex);
}

// Every step of a walk from one end to the other shows a FULL window, which is
// the property the clamp exists for - not just the final position.
TEST(UIRepeat, EveryOffsetOfAWalkThroughTheListShowsAFullWindow) {
    Rig r;
    r.hud.SetInt("scroll", 0);
    UIList l;
    for (int i = 0; i < 12; ++i) l.Add().SetString("name", "i" + std::to_string(i));
    r.hud.SetList("inventory", std::move(l));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="5" repeat-offset="scroll">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();

    for (int sel = 0; sel < 12; ++sel) {
        r.hud.SetInt("scroll", sel);
        r.Frame();
        for (std::size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(r.slot(i)->style().display, DisplayMode::Flex)
                << "offset " << sel << ": slot " << i << " went blank";
        }
        // The row the offset names is always somewhere in the window, which is
        // what makes an offset usable as a CURSOR rather than only a scroll.
        const std::string want = "i" + std::to_string(sel);
        bool visible = false;
        for (std::size_t i = 0; i < 5; ++i) {
            if (r.textOf(i, "l") == want) visible = true;
        }
        EXPECT_TRUE(visible) << "offset " << sel << ": row " << sel << " is not on screen";
    }
}

// A list SHORTER than the pool cannot fill it, so the surplus still hides - the
// clamp keeps the window full, it does not invent rows.
TEST(UIRepeat, AListShorterThanThePoolStillHidesTheSurplus) {
    Rig r;
    r.hud.SetInt("scroll", 9);
    r.hud.SetList("inventory", makeList({ { "a", 1 }, { "b", 2 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="4" repeat-offset="scroll">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "l"), "a") << "a short list was pushed off the top";
    EXPECT_EQ(r.slot(1)->style().display, DisplayMode::Flex);
    EXPECT_EQ(r.slot(2)->style().display, DisplayMode::None);
    EXPECT_EQ(r.slot(3)->style().display, DisplayMode::None);
}

TEST(UIRepeat, AnEmptyListHidesEverySlot) {
    Rig r;
    r.hud.SetList("inventory", UIList{});
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(r.slot(i)->style().display, DisplayMode::None) << "slot " << i;
    }
}

TEST(UIRepeat, TheListSourceIsReFoundEachFrameSoARePointedSourceIsNotReadThroughAStalePointer) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "old", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_EQ(r.textOf(0, "slotName"), "old");

    UIDataSource other;
    other.SetList("inventory", makeList({ { "new", 2 } }));
    r.ctx.RegisterSource("hud", &other);        // same name, different object
    r.Frame();
    EXPECT_EQ(r.textOf(0, "slotName"), "new")
        << "the pool kept reading the source it cached at build time";
}

// ------------------------------------------------------------ reserved names

// A row column called `index` and an engine-published `index` would resolve to
// the SAME property. They would alternate every frame, bump the version every
// frame, and pin that slot's bindings hot for the life of the process.
TEST(UIRepeat, ARowColumnCalledIndexIsReadBackUnchanged) {
    Rig r;
    UIList l;
    { UIRecord& a = l.Add(); a.SetInt("index", 7); a.SetString("name", "The Drowned Keep"); }
    r.hud.SetList("chapters", std::move(l));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="chapters" repeat-count="2">
        <Element><Label name="l" text="Chapter {index}: {name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_EQ(r.textOf(0, "l"), "Chapter 7: The Drowned Keep");
}

TEST(UIRepeat, ARowColumnCalledPresentDoesNotPinItsSlotHotForever) {
    Rig r;
    UIList l;
    { UIRecord& a = l.Add(); a.SetBool("present", false); a.SetString("name", "Ghost"); }
    r.hud.SetList("inventory", std::move(l));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    // A row column named `present` must not hide the row it belongs to.
    EXPECT_EQ(r.slot(0)->style().display, DisplayMode::Flex);
    r.Frame();
    EXPECT_EQ(r.Frame().applied, 0u) << "the slot re-applies on every idle frame";
}

// ---------------------------------------------------- lifetime and hot reload

TEST(UIRepeat, DeletingTheRepeatUnregistersEverySlotSourceItRegistered) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    const std::string slot0 = r.pools[0]->spec().slotSourceName(0);
    ASSERT_NE(r.ctx.Find(slot0), nullptr);

    // What Reload does when the author deletes the repeat: the new spec list is
    // EMPTY, so nothing but registeredNames_ knows those names ever existed.
    r.binder.Clear();
    for (auto& p : r.pools) p->Teardown(r.ctx);
    r.pools.clear();
    EXPECT_EQ(r.ctx.Find(slot0), nullptr)
        << "a freed slot source is still registered - sourceVersionSum() will "
           "dereference it next frame";
    EXPECT_EQ(r.ctx.sourceVersionSum(), r.hud.version());
}

TEST(UIRepeat, ShrinkingTheCountRemovesTheSurplusSlotSources) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    const std::string slot3 = r.pools[0]->spec().slotSourceName(3);
    ASSERT_NE(r.ctx.Find(slot3), nullptr);

    UIRepeatSpec smaller = r.pools[0]->spec();
    smaller.count = 2;
    r.binder.Clear();
    r.pools[0]->Teardown(r.ctx);
    r.pools[0]->Build(smaller, r.ctx, r.errors, "t.cxml");
    EXPECT_EQ(r.ctx.Find(slot3), nullptr) << "the surplus slot stayed registered";
    EXPECT_NE(r.ctx.Find(smaller.slotSourceName(1)), nullptr);
}

TEST(UIRepeat, TheGeneratedSlotSourcesAreNotNamedInTheRegisteredList) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    const std::vector<std::string> names = r.ctx.sourceNames();
    ASSERT_EQ(names.size(), 1u) << "generated slot names leaked into diagnostics";
    EXPECT_EQ(names[0], "hud");
    // Still findable, which is what the ordinary binding path needs.
    EXPECT_NE(r.ctx.Find(r.pools[0]->spec().slotSourceName(0)), nullptr);
}

// -------------------------------------------------------------- diagnostics

// Adding repeat= re-scopes every unqualified hole beneath it, so a hole that
// used to read the outer source now reads the row. It cannot fail at RESOLVE
// time — every template column is seeded on every slot, which is what keeps an
// unreached slot from pinning the binder — so it would otherwise render empty
// forever with nothing said. The check lives where the data is, and is asked
// once per repeat rather than once per clone.
TEST(UIRepeat, ABareHoleThatMissesTheRowIsReportedOnceNotOncePerSlot) {
    Rig r;
    r.hud.SetString("playerName", "Ada");
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="8">
        <Element><Label name="slotLabel" text="{playerName}"/></Element>
      </Element></UI>)")) << r.firstError();

    ASSERT_EQ(r.errors.size(), 1u)
        << "expected exactly one line for one authored mistake, got "
        << r.errors.size() << ": " << r.firstError();
    const std::string& e = r.errors[0];
    EXPECT_NE(e.find("repeat 'inventory'"), std::string::npos) << e;
    EXPECT_NE(e.find("no row has a column 'playerName'"), std::string::npos) << e;
    EXPECT_NE(e.find("row columns: count, name"), std::string::npos) << e;
    EXPECT_NE(e.find("{source.playerName}"), std::string::npos)
        << "the message does not say how to reach the outer source: " << e;
    EXPECT_EQ(e.find("repeat#"), std::string::npos)
        << "a generated slot-source name leaked into an authored diagnostic: " << e;
    // The four $-columns are engine plumbing; listing them as "row columns"
    // would send the author hunting for them in their own data.
    EXPECT_EQ(e.find("$present"), std::string::npos) << e;
}

// An EMPTY list says nothing about what its rows will carry, so there is
// nothing honest to report — reporting anyway would fire on every HUD that
// builds its markup before gameplay fills the list.
TEST(UIRepeat, AnEmptyListAtLoadDoesNotAccuseTheTemplateOfReadingBadColumns) {
    Rig r;
    r.hud.SetList("inventory", UIList{});
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
}

// A column only SOME rows carry is legitimate, so the check is the union over
// every row, not row 0.
TEST(UIRepeat, AColumnCarriedByOnlySomeRowsIsNotReportedAsMissing) {
    Rig r;
    UIList l;
    { UIRecord& a = l.Add(); a.SetString("name", "Rope"); }
    { UIRecord& b = l.Add(); b.SetString("name", "Sword"); b.SetString("rune", "fire"); }
    r.hud.SetList("inventory", std::move(l));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{name}/{rune}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_TRUE(r.errors.empty()) << r.firstError();
}

// The one path that still reaches the binder's row-aware message: a $-column
// is engine plumbing and is NOT seeded from the template, so a misspelt one
// genuinely fails to resolve.
TEST(UIRepeat, AMisspeltEngineColumnIsReportedAsARowColumnNotAnInternalSourceName) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "Potion", 3 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="2">
        <Element><Label name="l" text="{$idx}"/></Element>
      </Element></UI>)")) << r.firstError();
    ASSERT_FALSE(r.binder.errors().empty()) << "a misspelt $-column said nothing";
    const std::string& e = r.binder.errors()[0];
    EXPECT_NE(e.find("repeat 'inventory'"), std::string::npos) << e;
    EXPECT_NE(e.find("row has no column '$idx'"), std::string::npos) << e;
    EXPECT_EQ(e.find("repeat#"), std::string::npos)
        << "a generated slot-source name leaked into an authored diagnostic: " << e;
}

TEST(UIRepeat, AListIsNamedInTheHasNoPropertyMessageInsteadOfBeingDenied) {
    Rig r;
    r.hud.SetInt("score", 1);
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Label name="carry" text="Carrying {inventory} items"/></UI>)")) << r.firstError();
    ASSERT_FALSE(r.binder.errors().empty());
    const std::string& e = r.binder.errors()[0];
    EXPECT_NE(e.find("lists: inventory"), std::string::npos)
        << "the engine denied a name the author can see in their own C++: " << e;
    EXPECT_NE(e.find("read with repeat="), std::string::npos) << e;
}

TEST(UIRepeat, RepeatNamingAPropertyRatherThanAListSaysWhichItIs) {
    Rig r;
    r.hud.SetInt("score", 1);
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(R"(<UI data-source="hud">
      <Element name="bag" repeat="score" repeat-count="2">
        <Element><Label text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("has no list 'score'")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("'score' is a property")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("has lists: inventory")) << r.firstError();
}

TEST(UIRepeat, RepeatNamingAnUnknownSourceIsReportedOnceWithTheRegisteredNames) {
    Rig r;
    ASSERT_TRUE(r.Load(R"(<UI>
      <Element name="bag" repeat="nope.inventory" repeat-count="2">
        <Element><Label text="{name}"/></Element>
      </Element></UI>)")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("unknown data source 'nope'")) << r.firstError();
    EXPECT_TRUE(r.anyErrorContains("registered: hud")) << r.firstError();
}

TEST(UIRepeat, TheInjectedPresentBindingIsLabelledSoDescribeDoesNotLookLikeAuthoredMarkup) {
    Rig r;
    r.hud.SetList("inventory", makeList({ { "a", 1 } }));
    ASSERT_TRUE(r.Load(kBag)) << r.firstError();
    bool found = false;
    for (const std::string& line : r.binder.Describe()) {
        if (line.find("repeat present") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found) << "the row's visibility binding is indistinguishable from an if=";
}

// --------------------------------------------------------------- data model

TEST(UIRepeat, SettingAnUnchangedListBumpsNothing) {
    UIDataSource s;
    s.SetList("inv", makeList({ { "a", 1 } }));
    const int li = s.ListIndexOf("inv");
    ASSERT_GE(li, 0);
    const std::uint32_t v = s.ListVersionAt(li);
    s.SetList("inv", makeList({ { "a", 1 } }));
    EXPECT_EQ(s.ListVersionAt(li), v) << "an equal list was treated as a change";
    s.SetList("inv", makeList({ { "a", 2 } }));
    EXPECT_NE(s.ListVersionAt(li), v);
}

// No binding can read a list, so moving the whole-source stamp would re-apply
// every unrelated health/score binding for a change none of them can see.
TEST(UIRepeat, SettingAListDoesNotReApplyTheSourcesOtherBindings) {
    UIDataSource s;
    s.SetInt("score", 1);
    const std::uint32_t before = s.version();
    s.SetList("inv", makeList({ { "a", 1 } }));
    EXPECT_EQ(s.version(), before) << "SetList moved the whole-source version";
}

TEST(UIRepeat, ARecordComparesByContentNotByColumnOrder) {
    UIRecord a, b;
    a.SetString("name", "x"); a.SetInt("count", 2);
    b.SetInt("count", 2);     b.SetString("name", "x");
    EXPECT_EQ(a, b) << "a row rebuilt in a different order read as changed";
    b.SetInt("count", 3);
    EXPECT_NE(a, b);
}

TEST(UIRepeat, SettingASecondListDoesNotInvalidateAReadOfTheFirst) {
    UIDataSource s;
    s.SetList("a", makeList({ { "one", 1 } }));
    const int la = s.ListIndexOf("a");
    // The read surface is index-based precisely so that growing lists_ cannot
    // invalidate anything the caller is holding.
    for (int i = 0; i < 16; ++i) s.SetList("l" + std::to_string(i), makeList({ { "x", 0 } }));
    UIValue v;
    ASSERT_TRUE(s.ListValueAt(la, 0, "name", v));
    EXPECT_EQ(v.ToDisplayString(), "one");
}

// --------------------------------------------------------------- the ceiling

// A full pool with a deep template: 64 slots x 16 labels x 2 holes = 2048
// holes plus a $present each, all indexed out of one flat pool. Entry's offsets
// were 16-bit and resolve_ assigns pool sizes into them WITHOUT a range check,
// while every bounds check downstream catches an index that is out of range
// rather than one that wrapped — and a wrapped index is in range, so it reads
// somebody else's hole and looks like data.
TEST(UIRepeat, EverySlotOfAFullPoolReadsItsOwnRow) {
    Rig r;
    UIList l;
    for (int i = 0; i < 64; ++i) {
        UIRecord& row = l.Add();
        row.SetString("name", "item" + std::to_string(i));
        row.SetInt("count", i);
    }
    r.hud.SetList("inventory", std::move(l));

    std::string xml = R"(<UI data-source="hud">
      <Element name="bag" repeat="inventory" repeat-count="64"><Element>)";
    for (int i = 0; i < 16; ++i) {
        xml += "<Label name=\"l" + std::to_string(i) + "\" text=\"{name}x{count}\"/>";
    }
    xml += "</Element></Element></UI>";
    ASSERT_TRUE(r.Load(xml.c_str())) << r.firstError();
    EXPECT_TRUE(r.binder.ok()) << (r.binder.errors().empty() ? "" : r.binder.errors()[0]);

    for (int slot : { 0, 1, 31, 62, 63 }) {
        const std::string want = "item" + std::to_string(slot) + "x" + std::to_string(slot);
        EXPECT_EQ(r.textOf(std::size_t(slot), "l0"), want) << "slot " << slot;
        EXPECT_EQ(r.textOf(std::size_t(slot), "l15"), want)
            << "slot " << slot << ", last label - a hole index wrapped or drifted";
    }
}

// ------------------------------------------------------- the shipped sample
//
// Through UIWorld, which is the only path that runs UpdateRepeats where a game
// actually runs it: once before the pre-hit-test layout, once in the main loop
// after the hot-reload poll.

TEST(UIRepeat, TheShippedHudExpandsItsInventoryPoolAndSlidesTheWindow) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr);
    EXPECT_TRUE(hud.assets()->ok()) << (hud.assets()->errors().empty()
                                            ? "" : hud.assets()->errors()[0]);
    ASSERT_EQ(hud.assets()->repeatPoolCount(), 1u) << "the sample lost its repeat";

    UIElement* bag = hud.find("bag");
    ASSERT_NE(bag, nullptr);
    ASSERT_EQ(bag->children().size(), 5u) << "repeat-count is 5 in hud.cxml";

    auto rowText = [&](std::size_t i, const char* cls) {
        for (const auto& c : bag->children()[i]->children()) {
            if (c->HasClass(cls)) return c->style().text;
        }
        return std::string("<missing>");
    };
    EXPECT_EQ(rowText(0, "bag-name"), "Potion of Haste");
    EXPECT_EQ(rowText(0, "bag-index"), "0");
    EXPECT_EQ(rowText(1, "bag-name"), "Iron Key");

    // The action the NEXT button is wired to, invoked the same way a click does.
    UIDataSource& src = hud.world.shared();
    ASSERT_TRUE(src.InvokeAction(src.ActionIndexOf("invNext")));
    hud.Frame();
    EXPECT_EQ(rowText(0, "bag-name"), "Iron Key") << "the window did not slide";
    EXPECT_EQ(rowText(0, "bag-index"), "1");

    // The tree kept its shape: the elements stood still, only their data moved.
    EXPECT_EQ(hud.find("bag")->children().size(), 5u);
}

// Walking the sample's cursor from the first item to the last must never make
// the panel appear to shrink. The sample is where anyone would notice it first.
TEST(UIRepeat, TheShippedHudKeepsAFullWindowAtEveryPositionOfItsCursor) {
    ShippedHud hud;
    hud.Frame();
    UIDataSource& src = hud.world.shared();
    const int li = src.ListIndexOf("inventory");
    ASSERT_GE(li, 0);
    const long long rows = (long long)src.ListRowCount(li);
    ASSERT_GT(rows, 5) << "the sample list must outrun its pool for this to mean anything";

    for (long long sel = 0; sel < rows; ++sel) {
        src.SetInt("invSelected", sel);
        hud.Frame();
        UIElement* bag = hud.find("bag");
        ASSERT_NE(bag, nullptr);
        for (std::size_t i = 0; i < bag->children().size(); ++i) {
            EXPECT_EQ(bag->children()[i]->style().display, DisplayMode::Flex)
                << "cursor " << sel << ": slot " << i << " went blank";
        }
    }
}

// The selected row is marked by a class driven from the row data, so exactly
// one row wears it wherever the cursor is - including at both ends, where the
// window stops moving and the cursor keeps going.
TEST(UIRepeat, TheShippedHudMarksExactlyOneRowSelectedAtEveryCursorPosition) {
    ShippedHud hud;
    hud.Frame();
    UIDataSource& src = hud.world.shared();
    const long long rows = (long long)src.ListRowCount(src.ListIndexOf("inventory"));
    const int next = src.ActionIndexOf("invNext");
    ASSERT_GE(next, 0);

    for (long long sel = 0; sel < rows; ++sel) {
        UIElement* bag = hud.find("bag");
        ASSERT_NE(bag, nullptr);
        int marked = 0;
        for (const auto& row : bag->children()) {
            if (row->HasClass("row-selected")) ++marked;
        }
        EXPECT_EQ(marked, 1) << "cursor " << sel << ": " << marked << " rows marked";
        src.InvokeAction(next);
        hud.Frame();
    }
}

// A list shorter than the pool is the only case where a slot is empty, and the
// sample must still handle it.
TEST(UIRepeat, TheShippedHudHidesTheSurplusWhenTheInventoryIsShorterThanThePool) {
    ShippedHud hud;
    hud.Frame();
    UIDataSource& src = hud.world.shared();
    ui::UIList two;
    two.Add().SetString("name", "Only");
    two.Add().SetString("name", "Two");
    src.SetList("inventory", std::move(two));
    hud.Frame();

    UIElement* bag = hud.find("bag");
    ASSERT_NE(bag, nullptr);
    EXPECT_EQ(bag->children()[0]->style().display, DisplayMode::Flex);
    EXPECT_EQ(bag->children()[1]->style().display, DisplayMode::Flex);
    for (std::size_t i = 2; i < bag->children().size(); ++i) {
        EXPECT_EQ(bag->children()[i]->style().display, DisplayMode::None) << "slot " << i;
    }
}
