// UXML-like markup loading, and how it composes with stylesheets.
//
// Pure CPU. Three things matter beyond "it parses":
//  - the NON-DESTRUCTIVE contract: a bad file must leave the live UI alone,
//    because half-typed files are the normal state during hot reload;
//  - CASCADE ORDER: inline style beats every stylesheet rule, as in CSS, and
//    must survive a stylesheet being re-applied;
//  - the PATH SANDBOX: markup is authored content flowing into a parser, the
//    same class of input as models, scripts, clips and HDRis.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UIElement.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {
const char* kHud = R"(<UI class="root">
  <Element name="topBar" class="row bar">
    <Element name="healthTrack" class="track">
      <Element name="healthFill" class="fill"/>
    </Element>
    <Label name="scoreLabel" class="hud-text" text="SCORE 0"/>
  </Element>
  <Button name="scoreButton" class="btn" text="+100"/>
</UI>)";
}

TEST(UIMarkup, BuildsTheTreeWithTypesNamesClassesAndText) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, kHud, errors)) << (errors.empty() ? "" : errors[0]);
    EXPECT_TRUE(errors.empty());

    // Root maps onto the document's own root (which the document owns).
    EXPECT_EQ(doc.root().type(), "UI");
    EXPECT_TRUE(doc.root().HasClass("root"));
    ASSERT_EQ(doc.root().children().size(), 2u);

    UIElement* bar = doc.root().Find("topBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->type(), "Element");
    EXPECT_TRUE(bar->HasClass("row"));
    EXPECT_TRUE(bar->HasClass("bar"));
    EXPECT_FALSE(bar->HasClass("nope"));

    // The TAG becomes the type, so a stylesheet can select Button/Label with no
    // special knowledge in the loader.
    UIElement* button = doc.root().Find("scoreButton");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->type(), "Button");
    EXPECT_EQ(button->style().text, "+100");

    UIElement* label = doc.root().Find("scoreLabel");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->type(), "Label");
    EXPECT_EQ(label->style().text, "SCORE 0");

    // Nesting depth is preserved.
    UIElement* fill = doc.root().Find("healthFill");
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(fill->parent(), nullptr);
    EXPECT_EQ(fill->parent()->name(), "healthTrack");
    EXPECT_EQ(fill->parent()->parent(), bar);
}

TEST(UIMarkup, InlineStyleParsesAndApplies) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="x" style="width: 40px; background-color: #ff0000;"/></UI>)",
        errors)) << (errors.empty() ? "" : errors[0]);

    UIElement* x = doc.root().Find("x");
    ASSERT_NE(x, nullptr);
    ASSERT_EQ(x->inlineStyle().size(), 2u);

    // Inline declarations are STORED, not baked, so applying a sheet replays
    // them last. Applying an empty sheet is enough to realise them.
    UIStyleSheet empty;
    empty.ApplyTo(doc.root());
    EXPECT_FLOAT_EQ(x->style().width.value, 40.f);
    EXPECT_NEAR(x->style().backgroundColor.r, 1.f, 0.01f);
}

// CSS rule: an element's own style attribute outranks every selector rule,
// whatever its specificity — including an #id rule.
TEST(UIMarkup, InlineStyleBeatsEvenAnIdRule) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="x" class="c" style="width: 40px;"/></UI>)", errors));

    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString("#x { width: 999px; } .c { width: 500px; }"));
    sheet.ApplyTo(doc.root());

    UIElement* x = doc.root().Find("x");
    ASSERT_NE(x, nullptr);
    EXPECT_FLOAT_EQ(x->style().width.value, 40.f) << "a stylesheet rule overrode inline style";

    // ...and it survives re-applying the sheet, which is what hot reload does.
    sheet.ApplyTo(doc.root());
    EXPECT_FLOAT_EQ(x->style().width.value, 40.f) << "inline style was lost on re-apply";
}

TEST(UIMarkup, MarkupAndStylesheetComposeByTypeClassAndName) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, kHud, errors));

    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        ".row { flex-direction: row; justify-content: space-between; }"
        "#healthTrack { width: 220px; height: 18px; }"
        "Button { padding: 8px 14px; }"));
    sheet.ApplyTo(doc.root());

    EXPECT_EQ(doc.root().Find("topBar")->style().direction, FlexDirection::Row);
    EXPECT_FLOAT_EQ(doc.root().Find("healthTrack")->style().width.value, 220.f);
    EXPECT_FLOAT_EQ(doc.root().Find("scoreButton")->style().padding.top, 8.f);
    EXPECT_FLOAT_EQ(doc.root().Find("scoreButton")->style().padding.left, 14.f);
}

// Behaviour is attached in C++ by looking elements up by name — the separation
// the format exists for. This is the whole authoring loop in one test.
TEST(UIMarkup, HandlersAttachByNameAndTheUIWorks) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, kHud, errors));
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        "#scoreButton { position: absolute; left: 0; top: 0; width: 100px; height: 40px; }"));
    sheet.ApplyTo(doc.root());

    int clicks = 0;
    UIElement* button = doc.root().Find("scoreButton");
    ASSERT_NE(button, nullptr);
    button->OnClick([&](UIEvent&) { ++clicks; });

    doc.Layout(800.f, 600.f);
    UIPointerState p;
    p.inside = true;
    p.position = { 50.f, 20.f };
    p.buttonDown = true;
    doc.UpdatePointer(p);
    p.buttonDown = false;
    doc.UpdatePointer(p);

    EXPECT_EQ(clicks, 1) << "a markup-authored button did not receive its click";
}

// THE hot-reload contract.
TEST(UIMarkup, AFailedLoadLeavesTheLiveTreeIntact) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, kHud, errors));
    ASSERT_NE(doc.root().Find("scoreButton"), nullptr);
    const size_t before = doc.root().children().size();

    // Malformed XML (unclosed tag).
    std::vector<std::string> e2;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, "<UI><Element name=\"a\"></UI>", e2));
    EXPECT_FALSE(e2.empty()) << "a malformed document must say why";
    EXPECT_EQ(doc.root().children().size(), before) << "malformed markup destroyed the live tree";
    EXPECT_NE(doc.root().Find("scoreButton"), nullptr);

    // Well-formed XML but a bad inline style: the tree must ALSO survive, since
    // the failure is discovered part-way through building it.
    std::vector<std::string> e3;
    EXPECT_FALSE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="ok"/><Element name="bad" style="width: banana;"/></UI>)", e3));
    EXPECT_FALSE(e3.empty());
    EXPECT_EQ(doc.root().children().size(), before)
        << "a partially built tree reached the document";
    EXPECT_NE(doc.root().Find("scoreButton"), nullptr);
    EXPECT_EQ(doc.root().Find("ok"), nullptr) << "elements from a failed load leaked in";
}

TEST(UIMarkup, EmptyAndCommentOnlyDocuments) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, "", errors)) << "an empty document must fail";
    EXPECT_FALSE(errors.empty());

    std::vector<std::string> e2;
    EXPECT_TRUE(UIMarkup::LoadInto(doc, "<!-- just a comment --><UI/>", e2))
        << (e2.empty() ? "" : e2[0]);
    EXPECT_EQ(doc.root().children().size(), 0u);

    // Comments and text nodes between elements are skipped, not turned into
    // elements.
    std::vector<std::string> e3;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        "<UI>\n  <!-- c -->\n  <Element name=\"a\"/>\n  text\n</UI>", e3));
    EXPECT_EQ(doc.root().children().size(), 1u);
}

TEST(UIMarkup, ReloadReplacesRatherThanAccumulates) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI class="a"><Element name="one"/></UI>)", errors));
    ASSERT_EQ(doc.root().children().size(), 1u);

    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI class="b"><Element name="two"/></UI>)", errors));
    EXPECT_EQ(doc.root().children().size(), 1u) << "reload appended instead of replacing";
    EXPECT_EQ(doc.root().Find("one"), nullptr);
    EXPECT_NE(doc.root().Find("two"), nullptr);
    EXPECT_FALSE(doc.root().HasClass("a")) << "root classes accumulated across reloads";
    EXPECT_TRUE(doc.root().HasClass("b"));
}

TEST(UIMarkup, LoadsFromFileAndReportsAMissingOne) {
    const char* path = "test_ui_markup.uxml";
    { std::ofstream o(path); o << kHud; }

    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadFileInto(doc, path, errors)) << (errors.empty() ? "" : errors[0]);
    EXPECT_NE(doc.root().Find("scoreButton"), nullptr);
    std::remove(path);

    std::vector<std::string> e2;
    EXPECT_FALSE(UIMarkup::LoadFileInto(doc, "no_such_markup_12345.uxml", e2));
    EXPECT_FALSE(e2.empty());
}

// Same containment gate as every other authored path. Markup feeds a parser, so
// "../../evil.xml" or an absolute path must be refused BEFORE it is opened.
TEST(UIMarkup, RejectsTraversalAndAbsolutePaths) {
    UIDocument doc;
    for (const char* evil : { "../../evil.uxml",
                              R"(..\..\evil.uxml)",
                              "Exported/UI/../../../../evil.uxml",
                              "C:/Windows/System32/evil.uxml",
                              "/etc/passwd" }) {
        std::vector<std::string> errors;
        EXPECT_FALSE(UIMarkup::LoadFileInto(doc, evil, errors)) << evil;
        ASSERT_FALSE(errors.empty()) << evil;
        EXPECT_NE(errors[0].find("outside the project"), std::string::npos)
            << "expected a containment rejection for " << evil << ", got: " << errors[0];
    }
}
