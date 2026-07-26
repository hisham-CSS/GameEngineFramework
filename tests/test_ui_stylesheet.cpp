// USS-like stylesheets: parsing, selector matching and the cascade.
//
// Pure CPU. The behaviour worth pinning is CSS behaviour — specificity order,
// shorthand expansion, colour formats — because the whole point of copying the
// CSS subset is that authors can predict it. The other half is DIAGNOSTICS: a
// stylesheet that silently ignores what it does not understand is the most
// frustrating possible failure mode, so unknown properties and bad values must
// be reported AND must not partially apply.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UIElement.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {
// Parses and asserts success, surfacing the parser's own diagnostics on failure
// so a broken test tells you WHY rather than just "false".
UIStyleSheet parseOk(const std::string& css) {
    UIStyleSheet s;
    const bool ok = s.ParseString(css);
    EXPECT_TRUE(ok);
    for (const auto& e : s.errors()) ADD_FAILURE() << "parse error: " << e;
    return s;
}
}

TEST(UIStyleSheet, TypeClassAndNameSelectors) {
    UIStyleSheet s = parseOk(
        "Button { flex-grow: 1; }"
        ".panel { padding: 10px; }"
        "#hp    { width: 50%; }");

    UIElement button("btn");
    button.setType("Button");
    UIElement panel("p");
    panel.AddClass("panel");
    UIElement hp("hp");

    s.ApplyToElement(button);
    s.ApplyToElement(panel);
    s.ApplyToElement(hp);

    EXPECT_FLOAT_EQ(button.style().flexGrow, 1.f);
    EXPECT_FLOAT_EQ(panel.style().padding.left, 10.f);
    EXPECT_EQ(hp.style().width.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(hp.style().width.value, 50.f);

    // A rule must not touch elements it does not select.
    EXPECT_FLOAT_EQ(panel.style().flexGrow, 0.f);
}

TEST(UIStyleSheet, CompoundSelectorsRequireEveryPart) {
    UIStyleSheet s = parseOk("Button.primary#ok { flex-grow: 5; }");

    UIElement all("ok");
    all.setType("Button");
    all.AddClass("primary");
    s.ApplyToElement(all);
    EXPECT_FLOAT_EQ(all.style().flexGrow, 5.f);

    UIElement missingClass("ok");
    missingClass.setType("Button");
    s.ApplyToElement(missingClass);
    EXPECT_FLOAT_EQ(missingClass.style().flexGrow, 0.f) << "compound matched with a part missing";
}

TEST(UIStyleSheet, SelectorListsAndUniversal) {
    UIStyleSheet s = parseOk(".a, .b { flex-grow: 2; } * { flex-shrink: 3; }");
    UIElement a("a"); a.AddClass("a");
    UIElement b("b"); b.AddClass("b");
    UIElement c("c");
    s.ApplyToElement(a); s.ApplyToElement(b); s.ApplyToElement(c);

    EXPECT_FLOAT_EQ(a.style().flexGrow, 2.f);
    EXPECT_FLOAT_EQ(b.style().flexGrow, 2.f);
    EXPECT_FLOAT_EQ(c.style().flexGrow, 0.f);
    EXPECT_FLOAT_EQ(c.style().flexShrink, 3.f) << "* did not match everything";
}

// THE cascade rule: #id beats .class beats type, regardless of source order.
TEST(UIStyleSheet, SpecificityBeatsSourceOrder) {
    // Deliberately written weakest-LAST, so source order alone would give the
    // wrong answer.
    UIStyleSheet s = parseOk(
        "#hp     { width: 300px; }"
        ".bar    { width: 200px; }"
        "Element { width: 100px; }");

    UIElement el("hp");
    el.setType("Element");
    el.AddClass("bar");
    s.ApplyToElement(el);
    EXPECT_FLOAT_EQ(el.style().width.value, 300.f) << "#id must win over .class and type";

    UIElement noName("other");
    noName.setType("Element");
    noName.AddClass("bar");
    s.ApplyToElement(noName);
    EXPECT_FLOAT_EQ(noName.style().width.value, 200.f) << ".class must win over type";
}

TEST(UIStyleSheet, EqualSpecificityIsWonByTheLaterRule) {
    UIStyleSheet s = parseOk(".x { width: 10px; } .x { width: 20px; }");
    UIElement el("e"); el.AddClass("x");
    s.ApplyToElement(el);
    EXPECT_FLOAT_EQ(el.style().width.value, 20.f);
}

TEST(UIStyleSheet, LengthUnits) {
    UIStyleSheet s = parseOk(
        "#a { width: auto; } #b { width: 42px; } #c { width: 25%; } #d { width: 7; }");
    for (auto [name, unit, value] : { std::tuple{"a", StyleLength::Unit::Auto, 0.f},
                                      { "b", StyleLength::Unit::Point, 42.f },
                                      { "c", StyleLength::Unit::Percent, 25.f },
                                      { "d", StyleLength::Unit::Point, 7.f } }) {
        UIElement el(name);
        s.ApplyToElement(el);
        EXPECT_EQ(el.style().width.unit, unit) << name;
        if (unit != StyleLength::Unit::Auto) EXPECT_FLOAT_EQ(el.style().width.value, value) << name;
    }
}

// CSS edge shorthand, including the 4-value clockwise order that is easy to get
// wrong (top right bottom left, NOT left top right bottom).
TEST(UIStyleSheet, EdgeShorthandsFollowCSSOrder) {
    UIStyleSheet s = parseOk(
        "#one  { padding: 5px; }"
        "#two  { padding: 4px 8px; }"
        "#four { padding: 1px 2px 3px 4px; }");

    UIElement one("one");   s.ApplyToElement(one);
    EXPECT_FLOAT_EQ(one.style().padding.top, 5.f);
    EXPECT_FLOAT_EQ(one.style().padding.left, 5.f);

    UIElement two("two");   s.ApplyToElement(two);
    EXPECT_FLOAT_EQ(two.style().padding.top, 4.f)  << "first value is VERTICAL";
    EXPECT_FLOAT_EQ(two.style().padding.bottom, 4.f);
    EXPECT_FLOAT_EQ(two.style().padding.left, 8.f) << "second value is HORIZONTAL";
    EXPECT_FLOAT_EQ(two.style().padding.right, 8.f);

    UIElement four("four"); s.ApplyToElement(four);
    EXPECT_FLOAT_EQ(four.style().padding.top, 1.f);
    EXPECT_FLOAT_EQ(four.style().padding.right, 2.f);
    EXPECT_FLOAT_EQ(four.style().padding.bottom, 3.f);
    EXPECT_FLOAT_EQ(four.style().padding.left, 4.f);
}

TEST(UIStyleSheet, ColourFormats) {
    UIStyleSheet s = parseOk(
        "#hex3 { color: #f00; }"
        "#hex6 { color: #00ff00; }"
        "#hex8 { color: #0000ff80; }"
        "#rgb  { color: rgb(255, 0, 0); }"
        "#rgba { color: rgba(0, 0, 0, 0.5); }"
        "#name { color: white; }"
        "#none { background-color: transparent; }");

    auto colorOf = [&](const char* n) {
        UIElement el(n);
        s.ApplyToElement(el);
        return el.style().textColor;
    };
    EXPECT_NEAR(colorOf("hex3").r, 1.f, 0.01f);
    EXPECT_NEAR(colorOf("hex6").g, 1.f, 0.01f);
    EXPECT_NEAR(colorOf("hex8").a, 0.5f, 0.01f);
    EXPECT_NEAR(colorOf("rgb").r, 1.f, 0.01f);
    EXPECT_NEAR(colorOf("rgba").a, 0.5f, 0.01f);
    EXPECT_NEAR(colorOf("name").b, 1.f, 0.01f);

    UIElement none("none");
    s.ApplyToElement(none);
    EXPECT_NEAR(none.style().backgroundColor.a, 0.f, 0.01f);
}

TEST(UIStyleSheet, EnumsAndFlags) {
    UIStyleSheet s = parseOk(
        "#r { flex-direction: row; justify-content: space-between; align-items: center; }"
        "#a { position: absolute; left: 5; top: 6; }"
        "#o { overflow: hidden; pointer-events: none; }");

    UIElement r("r"); s.ApplyToElement(r);
    EXPECT_EQ(r.style().direction, FlexDirection::Row);
    EXPECT_EQ(r.style().justify, Justify::SpaceBetween);
    EXPECT_EQ(r.style().alignItems, Align::Center);

    UIElement a("a"); s.ApplyToElement(a);
    EXPECT_EQ(a.style().position, PositionType::Absolute);
    EXPECT_FLOAT_EQ(a.style().inset.left, 5.f);
    EXPECT_FLOAT_EQ(a.style().inset.top, 6.f);

    UIElement o("o"); s.ApplyToElement(o);
    EXPECT_EQ(o.style().overflowX, Overflow::Hidden);
    EXPECT_EQ(o.style().overflowY, Overflow::Hidden);
    EXPECT_FALSE(o.style().pickable) << "pointer-events: none did not disable picking";
}

TEST(UIStyleSheet, CommentsAndWhitespaceAreIgnored) {
    UIStyleSheet s = parseOk(
        "/* a header comment\n   spanning lines */\n"
        ".x {\n  flex-grow : 2 ;  /* trailing */\n}\n");
    UIElement el("e"); el.AddClass("x");
    s.ApplyToElement(el);
    EXPECT_FLOAT_EQ(el.style().flexGrow, 2.f);
}

TEST(UIStyleSheet, AppliesToAWholeSubtree) {
    UIStyleSheet s = parseOk(".tinted { background-color: #ff0000; }");
    UIDocument doc;
    UIElement* mid = doc.root().AddChild("mid");
    UIElement* leaf = mid->AddChild("leaf");
    leaf->AddClass("tinted");

    s.ApplyTo(doc.root());
    EXPECT_NEAR(leaf->style().backgroundColor.r, 1.f, 0.01f) << "subtree apply missed a leaf";
    EXPECT_NEAR(mid->style().backgroundColor.a, 0.f, 0.01f);
}

// Diagnostics. Silence here is the worst outcome: an author would have no idea
// why their rule did nothing.
TEST(UIStyleSheet, UnknownPropertiesAndBadValuesAreReportedNotIgnored) {
    UIStyleSheet s;
    EXPECT_FALSE(s.ParseString(".x { colour: red; }"))
        << "a misspelled property must fail, not be silently dropped";
    ASSERT_FALSE(s.errors().empty());
    EXPECT_NE(s.errors()[0].find("unknown property"), std::string::npos);

    UIStyleSheet bad;
    EXPECT_FALSE(bad.ParseString(".x { width: banana; }"));
    ASSERT_FALSE(bad.errors().empty());
    EXPECT_NE(bad.errors()[0].find("bad length"), std::string::npos);

    UIStyleSheet badColor;
    EXPECT_FALSE(badColor.ParseString(".x { color: #zz; }"));
    EXPECT_FALSE(badColor.errors().empty());

    // Descendant and child combinators ARE supported now (see
    // test_ui_selectors); what is still refused is a malformed one, which would
    // otherwise mis-match silently.
    UIStyleSheet combinator;
    EXPECT_TRUE(combinator.ParseString(".panel .btn { flex-grow: 1; }"))
        << (combinator.errors().empty() ? "" : combinator.errors()[0]);

    UIStyleSheet badCombinator;
    EXPECT_FALSE(badCombinator.ParseString("> .btn { flex-grow: 1; }"));
    ASSERT_FALSE(badCombinator.errors().empty());
    EXPECT_NE(badCombinator.errors()[0].find("leading '>'"), std::string::npos)
        << badCombinator.errors()[0];

    UIStyleSheet unterminated;
    EXPECT_FALSE(unterminated.ParseString(".x { width: 10px;"));
    EXPECT_FALSE(unterminated.errors().empty());
}

// The hot-reload contract: a file that fails to parse must leave the PREVIOUS
// sheet intact. Half-typed files are the normal state while editing, and
// swapping in a partial parse would make the UI fall apart as you type.
TEST(UIStyleSheet, AFailedReparseKeepsTheWorkingSheet) {
    UIStyleSheet s = parseOk(".x { width: 10px; }");
    ASSERT_EQ(s.rules().size(), 1u);

    EXPECT_FALSE(s.ParseString(".x { width: 10px; nonsense"));
    ASSERT_EQ(s.rules().size(), 1u) << "a broken reparse destroyed the working sheet";

    UIElement el("e"); el.AddClass("x");
    s.ApplyToElement(el);
    EXPECT_FLOAT_EQ(el.style().width.value, 10.f) << "the old rules stopped applying";
}

TEST(UIStyleSheet, LoadFromFileAndMissingFile) {
    const char* path = "test_ui_sheet.uss";
    { std::ofstream o(path); o << ".x { flex-grow: 4; }"; }

    UIStyleSheet s;
    ASSERT_TRUE(s.LoadFromFile(path)) << "failed to load a valid sheet";
    UIElement el("e"); el.AddClass("x");
    s.ApplyToElement(el);
    EXPECT_FLOAT_EQ(el.style().flexGrow, 4.f);
    std::remove(path);

    UIStyleSheet missing;
    EXPECT_FALSE(missing.LoadFromFile("no_such_sheet_12345.uss"));
    EXPECT_FALSE(missing.errors().empty()) << "a missing file must say so";
}

// Styles set from code must survive properties the sheet does not mention, so
// the two authoring paths compose instead of fighting.
TEST(UIStyleSheet, UnmentionedPropertiesAreLeftAlone) {
    UIStyleSheet s = parseOk(".x { width: 10px; }");
    UIElement el("e");
    el.AddClass("x");
    el.style().flexGrow = 9.f;         // set in code
    el.style().height = StyleLength::Px(77.f);
    s.ApplyToElement(el);

    EXPECT_FLOAT_EQ(el.style().width.value, 10.f);
    EXPECT_FLOAT_EQ(el.style().flexGrow, 9.f) << "the sheet clobbered a code-set property";
    EXPECT_FLOAT_EQ(el.style().height.value, 77.f);
}
