// Class-toggle bindings: `classes="low-health: {isLow}"`.
//
// Pure CPU. The subtle part is the same one `:hover` has — THE CASCADE HAS NO
// UNDO. Removing a class does not remove what its rules wrote, so the element
// has to be reset and re-cascaded, which in turn discards what every OTHER
// binding on it wrote and means they must be re-applied. That re-application
// includes the class binding itself, so it needs a reentrancy guard or it
// recurses into its own application.
#include <gtest/gtest.h>

#include "Engine.h"
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

struct ClassBound {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIStyleSheet sheet;
    UIBinder binder;
    std::vector<std::string> errors;

    bool Load(const std::string& css, const std::string& markup) {
        if (!sheet.ParseString(css, "t.uss")) return false;
        ctx.RegisterSource("s", &src);
        if (!UIMarkup::LoadInto(doc, markup, errors, "t.uxml")) return false;
        sheet.ApplyTo(doc.root());
        binder.Rebuild(doc, ctx, "t.uxml", &sheet);
        return true;
    }
    UIElement* el(const char* n) { return doc.root().Find(n); }
};

const char* kCss =
    ".box { background-color: #202020; width: 100px; height: 100px; }\n"
    ".low-health { background-color: #ff0000; }\n"
    ".boosted { background-color: #00ff00; }\n";

} // namespace

TEST(UIClassBind, TogglesAClassAndItsStyling) {
    ClassBound b;
    // Seeded BEFORE the load, the ordering the docs recommend: the first
    // binding pass then has a real value and nothing is reported unresolved.
    b.src.SetBool("isLow", false);
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    ASSERT_TRUE(b.binder.ok()) << b.binder.errors()[0];

    UIElement* e = b.el("e");
    ASSERT_NE(e, nullptr);
    EXPECT_FALSE(e->HasClass("low-health"));
    EXPECT_NEAR(e->style().backgroundColor.r, 0.125f, 0.01f);

    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();
    EXPECT_TRUE(e->HasClass("low-health"));
    EXPECT_NEAR(e->style().backgroundColor.r, 1.0f, 0.01f) << "the class's rule did not apply";

    // The part with no shortcut: removing the class must UNDO its styling, and
    // the cascade cannot undo — so the element is re-cascaded from scratch.
    b.src.SetBool("isLow", false);
    b.binder.UpdateToTarget();
    EXPECT_FALSE(e->HasClass("low-health"));
    EXPECT_NEAR(e->style().backgroundColor.r, 0.125f, 0.01f)
        << "the class was removed but its styling stayed";
}

TEST(UIClassBind, SeveralClassesInOneAttribute) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box"
                 classes="low-health: {isLow}; boosted: {hasBoost}"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    UIElement* e = b.el("e");

    b.src.SetBool("hasBoost", true);
    b.binder.UpdateToTarget();
    EXPECT_TRUE(e->HasClass("boosted"));
    EXPECT_FALSE(e->HasClass("low-health"));

    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();
    EXPECT_TRUE(e->HasClass("boosted")) << "the second toggle dropped the first";
    EXPECT_TRUE(e->HasClass("low-health"));
}

TEST(UIClassBind, LeadingBangNegates) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: !{alive}"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    b.src.SetBool("alive", true);
    b.binder.UpdateToTarget();
    EXPECT_FALSE(b.el("e")->HasClass("low-health"));
    b.src.SetBool("alive", false);
    b.binder.UpdateToTarget();
    EXPECT_TRUE(b.el("e")->HasClass("low-health"));
}

// A class binding re-cascades, which discards what every OTHER binding on the
// element wrote. They have to be re-applied, or toggling a class would blank a
// bound width until the next unrelated value change.
TEST(UIClassBind, OtherBindingsSurviveAToggle) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"
                 bind="width: {w | percent}"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    b.src.SetNumber("w", 0.75f);
    b.binder.UpdateToTarget();
    UIElement* e = b.el("e");
    ASSERT_FLOAT_EQ(e->style().width.value, 75.f);

    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();
    EXPECT_TRUE(e->HasClass("low-health"));
    EXPECT_FLOAT_EQ(e->style().width.value, 75.f)
        << "the re-cascade discarded a bound width and nothing put it back";
}

// The re-cascade re-applies this element's bindings, and the class binding IS
// one of them. Without a guard it would re-enter its own application.
TEST(UIClassBind, ToggleDoesNotRecurse) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box"
                 classes="low-health: {isLow}; boosted: {isLow}"/></UI>)"));
    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();   // must terminate
    EXPECT_TRUE(b.el("e")->HasClass("low-health"));
    EXPECT_TRUE(b.el("e")->HasClass("boosted"));
    SUCCEED();
}

// Writing the class it already has must not re-cascade: that would throw away
// and rebuild the element's whole Style every frame.
TEST(UIClassBind, AnUnchangedToggleDoesNothing) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"));
    b.src.SetBool("isLow", true);
    ASSERT_EQ(b.binder.UpdateToTarget().applied, 1u);
    // Same value again: the source version does not move, so nothing runs.
    b.src.SetBool("isLow", true);
    EXPECT_EQ(b.binder.UpdateToTarget().applied, 0u);

    // ...and even when the source DOES move for another reason, the toggle
    // itself reports no work because the class is already correct.
    b.src.SetInt("unrelated", 1);
    EXPECT_EQ(b.binder.UpdateToTarget().applied, 0u);
}

// Toggling a class changes which state rules can reach the element, so a
// `:hover` rule on the toggled class has to work too.
TEST(UIClassBind, ComposesWithStateRules) {
    ClassBound b;
    ASSERT_TRUE(b.Load(
        ".box { background-color: #202020; width: 100px; height: 100px; }\n"
        ".low-health:hover { background-color: #ff0000; }\n",
        R"(<UI data-source="s">
             <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);

    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();
    b.doc.Layout(400.f, 400.f);

    UIInteractionStyler styler;
    styler.Rebuild(b.doc, b.sheet, &b.binder);
    // The element only became reachable by the state rule when it gained the
    // class, so the watch list has to be rebuilt after a toggle — which the
    // structure epoch forces, since AddClass bumps nothing but Rebuild is what
    // the styler does on any epoch move. Rebuild explicitly here.
    EXPECT_EQ(styler.watchedCount(), 1u);

    UIPointerState p;
    p.inside = true;
    p.position = { 10.f, 10.f };
    b.doc.UpdatePointer(p);
    ASSERT_TRUE(styler.Update());
    EXPECT_NEAR(b.el("e")->style().backgroundColor.r, 1.0f, 0.01f);
}

TEST(UIClassBind, MalformedAttributesAreReported) {
    struct Case { const char* markup; const char* needle; };
    for (const Case& c : {
             Case{ R"(<UI><Element classes="low-health"/></UI>)", "has no ':'" },
             Case{ R"(<UI><Element classes="low-health: true"/></UI>)", "use class=" },
             Case{ R"(<UI><Element classes=".low: {x}"/></UI>)", "not a plain class name" },
             Case{ R"(<UI><Element classes="a b: {x}"/></UI>)", "not a plain class name" } }) {
        UIDocument doc;
        std::vector<std::string> errors;
        EXPECT_FALSE(UIMarkup::LoadInto(doc, c.markup, errors, "t.uxml")) << c.markup;
        ASSERT_FALSE(errors.empty()) << c.markup;
        EXPECT_NE(errors[0].find(c.needle), std::string::npos)
            << c.markup << " -> " << errors[0];
    }
}

TEST(UIClassBind, ANonBoolConditionIsReported) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"));
    b.src.SetString("isLow", "yes");   // a string is never a bool
    b.binder.UpdateToTarget();
    EXPECT_FALSE(b.binder.ok());
    ASSERT_FALSE(b.binder.errors().empty());
    EXPECT_NE(b.binder.errors()[0].find("needs a bool"), std::string::npos)
        << b.binder.errors()[0];
}

// A document with no stylesheet still records the class — there is simply
// nothing to re-cascade.
TEST(UIClassBind, WorksWithoutAStylesheet) {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    ctx.RegisterSource("s", &src);
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="s">
        <Element name="e" classes="lit: {on}"/></UI>)", errors, "t.uxml"));
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");   // no sheet
    src.SetBool("on", true);
    binder.UpdateToTarget();
    EXPECT_TRUE(doc.root().Find("e")->HasClass("lit"));
}

// Reloading rebuilds the tree from markup, so a class the binding added is gone
// until the binding runs again — and Rebuild's force-apply is what runs it.
TEST(UIClassBind, AToggledClassIsRestoredAfterARebuild) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"));
    b.src.SetBool("isLow", true);
    b.binder.UpdateToTarget();
    ASSERT_TRUE(b.el("e")->HasClass("low-health"));

    b.binder.Rebuild(b.doc, b.ctx, "t.uxml", &b.sheet);
    EXPECT_TRUE(b.el("e")->HasClass("low-health"))
        << "the force-apply did not restore the toggled class";
}

// Markup that binds `{ammo}` before gameplay ever writes it is ordinary. Only
// registering a SOURCE bumps the context revision, so without an explicit retry
// such a binding would report once and stay dead for the whole run — the exact
// silent-never-updates failure this system exists to avoid.
TEST(UIClassBind, ABindingResolvesWhenItsPropertyArrivesLater) {
    ClassBound b;
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {ammoOut}"/></UI>)"));
    EXPECT_FALSE(b.binder.ok()) << "an unknown property must be reported at load";
    EXPECT_EQ(b.binder.unresolvedCount(), 1u);

    // The source was already registered; only the PROPERTY is new.
    b.src.SetBool("ammoOut", true);
    b.binder.UpdateToTarget();
    EXPECT_EQ(b.binder.unresolvedCount(), 0u) << "the binding never resolved";
    EXPECT_TRUE(b.binder.ok());
    EXPECT_TRUE(b.el("e")->HasClass("low-health"));
}

// The retry must not cost anything once everything resolves — otherwise every
// value write would re-collect the whole tree.
TEST(UIClassBind, AResolvedDocumentDoesNotRecollectOnEveryWrite) {
    ClassBound b;
    b.src.SetBool("isLow", false);
    ASSERT_TRUE(b.Load(kCss, R"(<UI data-source="s">
        <Element name="e" class="box" classes="low-health: {isLow}"/></UI>)"));
    ASSERT_TRUE(b.binder.ok());
    UIElement* before = b.el("e");

    for (int i = 0; i < 5; ++i) {
        b.src.SetInt("noise", i);
        b.binder.UpdateToTarget();
    }
    // A re-collect would rebuild the entry list; the element itself is stable
    // either way, so assert on the thing that would actually churn.
    EXPECT_EQ(b.binder.bindingCount(), 1u);
    EXPECT_EQ(b.el("e"), before);
}
