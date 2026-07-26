// Element -> source binding: `push-hovered` / `push-pressed` / `push-focused`,
// and a text field's two-way `bind-value`.
//
// Pure CPU. The design constraint worth stating: there is NO general two-way
// binding, and that is not laziness. You cannot un-format a rendered string
// back into a value, so anything carrying a converter chain or literal text
// ("SCORE {score}") is one-directional by construction. Only a bare path can go
// both ways, which is why these attributes take a path and nothing else.
//
// The rest is about not writing when nothing changed, not looping when a value
// round-trips, and reporting a read-only target instead of dropping the write.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

struct Bound {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIStyleSheet sheet;
    UIBinder binder;
    std::vector<std::string> errors;

    bool Load(const std::string& markup) {
        ctx.RegisterSource("s", &src);
        if (!UIMarkup::LoadInto(doc, markup, errors, "t.uxml")) return false;
        sheet.ApplyTo(doc.root());
        binder.Rebuild(doc, ctx, "t.uxml", &sheet);
        doc.Layout(400.f, 400.f);
        return true;
    }
    UIElement* el(const char* n) { return doc.root().Find(n); }

    // One host frame's worth of input plus the publish pass that follows it.
    void Point(float x, float y, bool down = false, bool inside = true) {
        UIPointerState p;
        p.position = { x, y };
        p.inside = inside;
        p.buttonDown = down;
        doc.UpdatePointer(p);
        binder.UpdateToSource();
    }
    void Type(const std::string& s) {
        UIKeyboardState kb;
        kb.text = s;
        doc.UpdateKeyboard(kb);
        binder.UpdateToSource();
    }
};

} // namespace

// ------------------------------------------------------------- push-<state>

TEST(UITwoWay, PushHoveredAndPressedReachTheSource) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="btn" push-hovered="isOver" push-pressed="firing"
                 style="width: 100px; height: 100px"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    ASSERT_TRUE(b.binder.ok()) << b.binder.errors()[0];

    EXPECT_FALSE(b.src.GetBool("isOver"));
    b.Point(50.f, 50.f);
    EXPECT_TRUE(b.src.GetBool("isOver")) << "hover never reached the model";
    EXPECT_FALSE(b.src.GetBool("firing"));

    b.Point(50.f, 50.f, /*down=*/true);
    EXPECT_TRUE(b.src.GetBool("firing")) << "a held button never reached the model";

    b.Point(50.f, 50.f, /*down=*/false);
    EXPECT_FALSE(b.src.GetBool("firing"));
    b.Point(-1.f, -1.f, false, /*inside=*/false);
    EXPECT_FALSE(b.src.GetBool("isOver")) << "hover never cleared";
}

TEST(UITwoWay, PushFocusedFollowsKeyboardFocus) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="f" focusable="true" push-focused="typing"
                 style="width: 100px; height: 100px"/></UI>)"));
    ASSERT_TRUE(b.binder.ok()) << b.binder.errors()[0];

    b.doc.SetFocus(b.el("f"));
    b.binder.UpdateToSource();
    EXPECT_TRUE(b.src.GetBool("typing"));
    b.doc.ClearFocus();
    b.binder.UpdateToSource();
    EXPECT_FALSE(b.src.GetBool("typing"));
}

// An unchanged element must not write. Otherwise every frame bumps the source
// version and wakes every binding reading it.
TEST(UITwoWay, AnUnchangedStateWritesNothing) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="btn" push-hovered="isOver"
                 style="width: 100px; height: 100px"/></UI>)"));
    b.Point(50.f, 50.f);
    const auto v = b.src.version();
    EXPECT_EQ(b.binder.UpdateToSource().applied, 0u);
    EXPECT_EQ(b.binder.UpdateToSource().applied, 0u);
    EXPECT_EQ(b.src.version(), v) << "an idle frame bumped the source";
}

// The property need not exist first: the element OWNS this value, so the app
// should not have to declare it before the UI can publish it.
TEST(UITwoWay, AMissingTargetPropertyIsCreatedNotReported) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="btn" push-hovered="brandNew"
                 style="width: 100px; height: 100px"/></UI>)"));
    EXPECT_TRUE(b.binder.ok()) << b.binder.errors()[0];
    EXPECT_TRUE(b.src.Has("brandNew"));
}

// ...but a READ-ONLY property is reported, because a push that silently did
// nothing looks exactly like a UI that is not wired up.
TEST(UITwoWay, AReadOnlyTargetIsReported) {
    Bound b;
    b.src.Observe("locked", [] { return UIValue::Bool(false); });   // no setter
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="btn" push-hovered="locked"
                 style="width: 100px; height: 100px"/></UI>)"));
    EXPECT_FALSE(b.binder.ok());
    ASSERT_FALSE(b.binder.errors().empty());
    EXPECT_NE(b.binder.errors()[0].find("read-only"), std::string::npos)
        << b.binder.errors()[0];
    // The rest of the document still runs.
    b.Point(50.f, 50.f);
    SUCCEED();
}

TEST(UITwoWay, MalformedPushAttributesFailTheLoad) {
    struct Case { const char* markup; const char* needle; };
    for (const Case& c : {
             Case{ R"(<UI><Element push-wiggled="x"/></UI>)", "unknown state 'wiggled'" },
             Case{ R"(<UI><Element push-hovered=""/></UI>)", "empty path" },
             Case{ R"(<UI><Element push-hovered="a.b.c"/></UI>)", "more than one '.'" } }) {
        UIDocument doc;
        std::vector<std::string> errors;
        EXPECT_FALSE(UIMarkup::LoadInto(doc, c.markup, errors, "t.uxml")) << c.markup;
        ASSERT_FALSE(errors.empty()) << c.markup;
        EXPECT_NE(errors[0].find(c.needle), std::string::npos) << errors[0];
    }
}

TEST(UITwoWay, AQualifiedPushPathNamesItsOwnSource) {
    UIDocument doc;
    UIDataSource a, other;
    UIBindingContext ctx;
    ctx.RegisterSource("a", &a);
    ctx.RegisterSource("other", &other);
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="a">
        <Element name="btn" push-hovered="other.isOver"
                 style="width: 100px; height: 100px"/></UI>)", errors, "t.uxml"));
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml", &sheet);
    ASSERT_TRUE(binder.ok()) << binder.errors()[0];
    doc.Layout(400.f, 400.f);

    UIPointerState p;
    p.inside = true;
    p.position = { 50.f, 50.f };
    doc.UpdatePointer(p);
    binder.UpdateToSource();
    EXPECT_TRUE(other.GetBool("isOver"));
    EXPECT_FALSE(a.Has("isOver")) << "it wrote to the inherited source instead";
}

// --------------------------------------------------------------- bind-value

TEST(UITwoWay, TypingIntoAFieldReachesTheSource) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <TextField name="f" bind-value="playerName"
                   style="width: 200px; height: 30px"/></UI>)"))
        << (b.errors.empty() ? "" : b.errors[0]);
    ASSERT_TRUE(b.binder.ok()) << b.binder.errors()[0];

    b.doc.SetFocus(b.el("f"));
    b.Type("Ada");
    EXPECT_EQ(b.src.GetString("playerName"), "Ada");
    b.Type("!");
    EXPECT_EQ(b.src.GetString("playerName"), "Ada!");
}

TEST(UITwoWay, WritingTheSourceReachesTheField) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <TextField name="f" bind-value="playerName"
                   style="width: 200px; height: 30px"/></UI>)"));
    b.src.SetString("playerName", "Grace");
    b.binder.UpdateToTarget();
    ASSERT_NE(b.el("f")->textEdit(), nullptr);
    EXPECT_EQ(b.el("f")->textEdit()->value(), "Grace");
    EXPECT_EQ(b.el("f")->style().text, "Grace") << "the drawn text did not follow";
}

// The dangerous shape: source -> element -> source. Both directions are
// equality-gated, so it has to settle immediately rather than oscillate.
TEST(UITwoWay, ARoundTripSettlesAndDoesNotOscillate) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <TextField name="f" bind-value="v" style="width: 200px; height: 30px"/></UI>)"));
    b.doc.SetFocus(b.el("f"));
    b.Type("hi");
    ASSERT_EQ(b.src.GetString("v"), "hi");

    const auto settled = b.src.version();
    for (int i = 0; i < 5; ++i) {
        b.binder.UpdateToTarget();
        EXPECT_EQ(b.binder.UpdateToSource().applied, 0u) << "frame " << i;
    }
    EXPECT_EQ(b.src.version(), settled) << "the value ping-ponged between the two";
    EXPECT_EQ(b.el("f")->textEdit()->value(), "hi");
}

// Writing the same string back would clamp the caret for no reason, so the
// source -> element half is gated on inequality.
TEST(UITwoWay, AnUnchangedSourceDoesNotDisturbTheCaret) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <TextField name="f" bind-value="v" style="width: 200px; height: 30px"/></UI>)"));
    b.doc.SetFocus(b.el("f"));
    b.Type("hello");
    UITextEdit* ed = b.el("f")->textEdit();
    ed->MoveToStart(false);
    ASSERT_EQ(ed->caret(), 0u);

    // Something unrelated bumps the source version.
    b.src.SetInt("unrelated", 1);
    b.binder.UpdateToTarget();
    EXPECT_EQ(ed->caret(), 0u) << "an unrelated write moved the caret";
    EXPECT_EQ(ed->value(), "hello");
}

TEST(UITwoWay, BindValueOnlyAppliesToAField) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Label name="l" bind-value="v"/></UI>)",
                                    errors, "t.uxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("only valid on a <TextField>"), std::string::npos)
        << errors[0];
}

// A push target must survive a hot reload, because the tree is rebuilt and the
// entries that carried it are gone.
TEST(UITwoWay, PushBindingsSurviveAReload) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <TextField name="f" bind-value="v" push-focused="editing"
                   style="width: 200px; height: 30px"/></UI>)"));
    b.doc.SetFocus(b.el("f"));
    b.Type("one");
    ASSERT_EQ(b.src.GetString("v"), "one");

    // Re-collect the way a reload would.
    b.binder.Rebuild(b.doc, b.ctx, "t.uxml", &b.sheet);
    ASSERT_TRUE(b.binder.ok()) << b.binder.errors()[0];
    // Rebuild's force-apply pushes the model back INTO the field, which is the
    // right direction after a reload: the model is what survived.
    EXPECT_EQ(b.el("f")->textEdit()->value(), "one");

    b.doc.SetFocus(b.el("f"));
    b.Type("!");
    EXPECT_EQ(b.src.GetString("v"), "one!") << "the push binding did not survive";
}

// The structure epoch guard has to be on BOTH passes, and UpdateToSource is the
// one that runs immediately after handlers may have torn the tree apart.
TEST(UITwoWay, UpdateToSourceSurvivesARestructuredTree) {
    Bound b;
    ASSERT_TRUE(b.Load(R"(<UI data-source="s">
        <Element name="keep" push-hovered="a" style="width: 100px; height: 100px"/>
        <Element name="doomed" push-hovered="c" style="width: 100px; height: 100px"/>
      </UI>)"));
    ASSERT_EQ(b.binder.bindingCount(), 2u);

    b.doc.root().RemoveChild(b.el("doomed"));   // ownership returned and dropped
    b.binder.UpdateToSource();                  // must re-collect, not dereference
    EXPECT_EQ(b.binder.bindingCount(), 1u) << "a stale entry survived";
    SUCCEED();
}
