// Keyboard focus, tab navigation, and the :focus / :disabled states.
//
// Pure CPU. The things worth pinning down:
//  - focus can never land somewhere the user cannot see or Tab out of
//    (hidden, disabled, not focusable, not in the tree);
//  - disabled is INHERITED, so a disabled panel takes its whole subtree out of
//    both hit-testing and the tab order — a disabled panel whose buttons still
//    worked would be a trap;
//  - Tab wraps, because a Tab that stops dead at the last field leaves a
//    keyboard user with no way back;
//  - a handler that consumes Tab keeps it, which is what a multi-line field
//    will need later.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/render2d/Font.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIInteractionStyler.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// Three focusable boxes stacked in a column, each 100x40.
struct Form {
    UIDocument doc;
    std::vector<std::string> errors;

    Form(const std::string& markup =
             R"(<UI>
                  <Element name="a" focusable="true" style="width: 100px; height: 40px"/>
                  <Element name="b" focusable="true" style="width: 100px; height: 40px"/>
                  <Element name="c" focusable="true" style="width: 100px; height: 40px"/>
                </UI>)") {
        UIMarkup::LoadInto(doc, markup, errors, "t.cxml");
        UIStyleSheet sheet;
        sheet.ApplyTo(doc.root());   // replays inline styles onto style()
        doc.Layout(400.f, 400.f);
    }
    UIElement* el(const char* n) { return doc.root().Find(n); }

    void PressKey(UIKey k, bool shift = false) {
        UIKeyboardState kb;
        UIKeyEvent e;
        e.key = k;
        e.shift = shift;
        kb.keys.push_back(e);
        doc.UpdateKeyboard(kb);
    }
    void Type(const std::string& s) {
        UIKeyboardState kb;
        kb.text = s;
        doc.UpdateKeyboard(kb);
    }
    void ClickAt(float x, float y) {
        UIPointerState p;
        p.inside = true;
        p.position = { x, y };
        p.buttonDown = true;  doc.UpdatePointer(p);
        p.buttonDown = false; doc.UpdatePointer(p);
    }
};

} // namespace

// -------------------------------------------------------------- focus basics

TEST(UIFocus, SetFocusFiresFocusOutThenFocusIn) {
    Form f;
    std::vector<std::string> order;
    f.el("a")->OnFocusIn([&](UIEvent&) { order.push_back("a-in"); });
    f.el("a")->OnFocusOut([&](UIEvent&) { order.push_back("a-out"); });
    f.el("b")->OnFocusIn([&](UIEvent&) { order.push_back("b-in"); });

    f.doc.SetFocus(f.el("a"));
    EXPECT_EQ(f.doc.focused(), f.el("a"));
    EXPECT_TRUE(f.el("a")->isFocused());

    f.doc.SetFocus(f.el("b"));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "a-in");
    EXPECT_EQ(order[1], "a-out") << "the old element must be told first";
    EXPECT_EQ(order[2], "b-in");
    EXPECT_FALSE(f.el("a")->isFocused());

    // Setting the same element again is a no-op, not a churn of out/in events.
    f.doc.SetFocus(f.el("b"));
    EXPECT_EQ(order.size(), 3u);
}

// Focus that lands somewhere unreachable can never be escaped with the
// keyboard, so SetFocus refuses rather than trusting the caller.
TEST(UIFocus, RefusesElementsTheUserCouldNotReach) {
    Form f;
    UIElement* plain = f.doc.root().AddChild("plain");   // not focusable
    f.doc.SetFocus(plain);
    EXPECT_EQ(f.doc.focused(), nullptr);

    f.el("a")->setEnabled(false);
    f.doc.SetFocus(f.el("a"));
    EXPECT_EQ(f.doc.focused(), nullptr) << "focused a disabled element";

    f.el("a")->setEnabled(true);
    f.el("a")->style().display = DisplayMode::None;
    f.doc.SetFocus(f.el("a"));
    EXPECT_EQ(f.doc.focused(), nullptr) << "focused a hidden element";

    // An element in a DIFFERENT document is not ours to focus.
    UIDocument other;
    UIElement* stranger = other.root().AddChild("stranger");
    stranger->setFocusable(true);
    f.doc.SetFocus(stranger);
    EXPECT_EQ(f.doc.focused(), nullptr);
}

// A disabled or hidden ANCESTOR makes the whole subtree unreachable.
TEST(UIFocus, DisabledIsInherited) {
    Form f(R"(<UI>
                <Element name="panel" style="width: 200px; height: 200px">
                  <Element name="inner" focusable="true" style="width: 50px; height: 50px"/>
                </Element>
              </UI>)");
    ASSERT_NE(f.el("inner"), nullptr);
    f.doc.SetFocus(f.el("inner"));
    ASSERT_EQ(f.doc.focused(), f.el("inner"));

    f.el("panel")->setEnabled(false);
    // Already-held focus is dropped the next time the keyboard is driven,
    // because the element is no longer somewhere the user can act.
    f.PressKey(UIKey::Escape);
    EXPECT_EQ(f.doc.focused(), nullptr) << "focus stayed inside a disabled panel";

    // ...and it cannot be put back while the panel is disabled.
    f.doc.SetFocus(f.el("inner"));
    EXPECT_EQ(f.doc.focused(), nullptr);
    // Nor can the pointer reach it.
    EXPECT_NE(f.doc.HitTest({ 10.f, 10.f }), f.el("inner"));
}

// -------------------------------------------------------------- tab order

TEST(UIFocus, TabWalksDocumentOrderAndWraps) {
    Form f;
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("a")) << "the first Tab must focus the first element";
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("b"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("c"));
    // Wraps: stopping dead at the last field leaves a keyboard user stuck.
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("a"));
}

TEST(UIFocus, ShiftTabWalksBackwardsAndWraps) {
    Form f;
    f.PressKey(UIKey::Tab, /*shift=*/true);
    EXPECT_EQ(f.doc.focused(), f.el("c")) << "the first Shift+Tab must land on the last";
    f.PressKey(UIKey::Tab, true);
    EXPECT_EQ(f.doc.focused(), f.el("b"));
    f.PressKey(UIKey::Tab, true);
    EXPECT_EQ(f.doc.focused(), f.el("a"));
    f.PressKey(UIKey::Tab, true);
    EXPECT_EQ(f.doc.focused(), f.el("c"));
}

TEST(UIFocus, TabSkipsDisabledAndHiddenElements) {
    Form f;
    f.el("b")->setEnabled(false);
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("a"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("c")) << "Tab stopped on a disabled element";

    f.el("b")->setEnabled(true);
    f.el("c")->style().display = DisplayMode::None;
    f.doc.SetFocus(f.el("a"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("b"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("a")) << "Tab stopped on a hidden element";
}

TEST(UIFocus, ADocumentWithNothingFocusableIsNotAnError) {
    Form f(R"(<UI><Element name="x" style="width: 10px; height: 10px"/></UI>)");
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), nullptr);
    EXPECT_EQ(f.doc.FocusNext(), nullptr);
}

// Consuming Tab keeps it. That is what will let a multi-line field insert a
// literal tab, and it is why the test is StopPropagation rather than a
// hardcoded check on the element's type.
TEST(UIFocus, AHandlerThatConsumesTabKeepsIt) {
    Form f;
    f.doc.SetFocus(f.el("a"));
    int seen = 0;
    f.el("a")->OnKeyDown([&](UIEvent& e) {
        if (e.key != UIKey::Tab) return;
        ++seen;
        e.StopPropagation();
    });
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(seen, 1);
    EXPECT_EQ(f.doc.focused(), f.el("a")) << "a consumed Tab still navigated";
}

// -------------------------------------------------------------- click + keys

TEST(UIFocus, ClickingFocusesTheNearestFocusableAncestor) {
    Form f(R"(<UI>
                <Element name="field" focusable="true" style="width: 100px; height: 100px">
                  <Element name="label" style="width: 50px; height: 50px"/>
                </Element>
              </UI>)");
    // The click lands on the inner label, but the FIELD is what takes focus —
    // the same reasoning that makes events bubble.
    f.ClickAt(10.f, 10.f);
    EXPECT_EQ(f.doc.focused(), f.el("field"));

    // Clicking nothing focusable clears focus, which is what makes a text field
    // commit when you click away from it.
    f.ClickAt(300.f, 300.f);
    EXPECT_EQ(f.doc.focused(), nullptr);
}

TEST(UIFocus, KeysAndTextGoToTheFocusedElementAndBubble) {
    Form f(R"(<UI>
                <Element name="outer" style="width: 200px; height: 200px">
                  <Element name="inner" focusable="true" style="width: 50px; height: 50px"/>
                </Element>
              </UI>)");
    f.doc.SetFocus(f.el("inner"));

    std::vector<std::string> order;
    std::string typed;
    f.el("inner")->OnKeyDown([&](UIEvent&) { order.push_back("inner"); });
    f.el("outer")->OnKeyDown([&](UIEvent&) { order.push_back("outer"); });
    f.el("inner")->OnTextInput([&](UIEvent& e) { typed += e.text; });

    f.PressKey(UIKey::Enter);
    ASSERT_EQ(order.size(), 2u) << "KeyDown did not bubble";
    EXPECT_EQ(order[0], "inner");
    EXPECT_EQ(order[1], "outer");

    f.Type("hi");
    EXPECT_EQ(typed, "hi");
}

TEST(UIFocus, NothingFocusedMeansNothingReceivesKeys) {
    Form f;
    int keys = 0;
    f.el("a")->OnKeyDown([&](UIEvent&) { ++keys; });
    f.PressKey(UIKey::Enter);
    EXPECT_EQ(keys, 0);
    // ...but Tab still works, because that is document-level navigation.
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("a"));
}

// The focused element may be removed by gameplay or by a handler between
// frames; dispatching into it would be a use-after-free.
TEST(UIFocus, FocusOnARemovedElementIsDroppedSafely) {
    Form f;
    f.doc.SetFocus(f.el("b"));
    ASSERT_NE(f.doc.focused(), nullptr);
    f.doc.root().RemoveChild(f.el("b"));   // ownership returned and dropped
    f.PressKey(UIKey::Enter);              // must not dereference it
    EXPECT_EQ(f.doc.focused(), nullptr);
    SUCCEED();
}

// -------------------------------------------------------- :focus / :disabled

TEST(UIFocusStyling, FocusAndDisabledAreStyleable) {
    UIDocument doc;
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        ".f { background-color: #202020; }\n"
        ".f:focus { background-color: #00ff00; }\n"
        ".f:disabled { background-color: #ff0000; }\n", "t.cstyle"))
        << (sheet.errors().empty() ? "" : sheet.errors()[0]);

    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="e" class="f" focusable="true"
                     style="width: 50px; height: 50px"/></UI>)", errors, "t.cxml"));
    sheet.ApplyTo(doc.root());
    UIInteractionStyler styler;
    styler.Rebuild(doc, sheet, nullptr);
    doc.Layout(200.f, 200.f);

    UIElement* e = doc.root().Find("e");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->style().backgroundColor.r, 0.125f, 0.01f);

    doc.SetFocus(e);
    ASSERT_TRUE(styler.Update());
    EXPECT_NEAR(e->style().backgroundColor.g, 1.0f, 0.01f) << ":focus did not apply";

    doc.ClearFocus();
    ASSERT_TRUE(styler.Update());
    EXPECT_NEAR(e->style().backgroundColor.r, 0.125f, 0.01f) << ":focus did not un-apply";

    e->setEnabled(false);
    ASSERT_TRUE(styler.Update());
    EXPECT_NEAR(e->style().backgroundColor.r, 1.0f, 0.01f) << ":disabled did not apply";
}

// :disabled matches through ancestors, so disabling a panel greys out
// everything inside it without the author repeating the rule.
TEST(UIFocusStyling, DisabledStylingIsInherited) {
    UIDocument doc;
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(
        ".btn { background-color: #202020; }\n"
        ".btn:disabled { background-color: #ff0000; }\n", "t.cstyle"));

    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI>
          <Element name="panel" style="width: 200px; height: 200px">
            <Element name="btn" class="btn" style="width: 50px; height: 50px"/>
          </Element></UI>)", errors, "t.cxml"));
    sheet.ApplyTo(doc.root());
    UIInteractionStyler styler;
    styler.Rebuild(doc, sheet, nullptr);

    UIElement* btn = doc.root().Find("btn");
    ASSERT_NE(btn, nullptr);
    doc.root().Find("panel")->setEnabled(false);
    ASSERT_TRUE(styler.Update());
    EXPECT_NEAR(btn->style().backgroundColor.r, 1.0f, 0.01f)
        << "a disabled ancestor did not grey out its child";
}

TEST(UIFocusStyling, MarkupCanDeclareFocusableAndDisabled) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI>
          <Button name="btn"/>
          <Element name="plain"/>
          <Element name="opt" focusable="true"/>
          <Element name="off" disabled="true"/>
          <Element name="bare" disabled=""/>
          <Button name="optout" focusable="false"/>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    // A Button is focusable because that is what the word means; a plain
    // Element is not, because a tab order full of panels is worse than none.
    EXPECT_TRUE(doc.root().Find("btn")->isFocusable());
    EXPECT_FALSE(doc.root().Find("plain")->isFocusable());
    EXPECT_TRUE(doc.root().Find("opt")->isFocusable());
    EXPECT_FALSE(doc.root().Find("optout")->isFocusable()) << "focusable=false must opt out";
    EXPECT_FALSE(doc.root().Find("off")->isEnabled());
    EXPECT_FALSE(doc.root().Find("bare")->isEnabled()) << "a bare disabled= means true";

    std::vector<std::string> bad;
    UIDocument other;
    EXPECT_FALSE(UIMarkup::LoadInto(other, R"(<UI><Element focusable="yes"/></UI>)",
                                    bad, "t.cxml"));
    ASSERT_FALSE(bad.empty());
    EXPECT_NE(bad[0].find("expected true|false"), std::string::npos) << bad[0];
}

TEST(UIFocusStyling, UnknownPseudoClassListsAllFour) {
    UIStyleSheet s;
    EXPECT_FALSE(s.ParseString(".x:checked { color: red; }", "t.cstyle"));
    ASSERT_FALSE(s.errors().empty());
    EXPECT_NE(s.errors()[0].find("hover|active|focus|disabled"), std::string::npos)
        << s.errors()[0];
}

// ------------------------------------------------------------------- UTF-8

// Hosts receive typed input as CODEPOINTS (GLFW's char callback, ImGui's input
// queue) and must hand the UI UTF-8, so the encoder is on the input path of
// every text field.
TEST(UIFocus, Utf8RoundTripsThroughTheEncoder) {
    for (std::uint32_t cp : { 0x41u, 0xE9u, 0x20ACu, 0x1F600u }) {
        std::string s;
        Font::AppendUTF8(s, cp);
        const auto back = Font::DecodeUTF8(s);
        ASSERT_EQ(back.size(), 1u) << std::hex << cp;
        EXPECT_EQ(back[0], cp) << std::hex << cp;
    }
    // A surrogate or an out-of-range value is not a character; substituting
    // keeps a corrupt string out of the atlas and out of any saved file.
    for (std::uint32_t bad : { 0xD800u, 0xDFFFu, 0x110000u }) {
        std::string s;
        Font::AppendUTF8(s, bad);
        const auto back = Font::DecodeUTF8(s);
        ASSERT_EQ(back.size(), 1u);
        EXPECT_EQ(back[0], 0xFFFDu) << std::hex << bad;
    }
}

// `focusable="false"` must win on a Slider and a TextField too.
//
// applyAttributes parsed the attribute early and applied it immediately, but
// MakeSlider/MakeTextField ran AFTERWARDS in the same function and both stamp
// focusable_ = true on first creation (a field you cannot focus is a label).
// buildElement always hands them a FRESH element, so those guards never fired
// and the authored value was silently overwritten -- a decorative read-only
// slider still took a stop in the Tab ring and could still be driven with the
// arrow keys.
TEST(UIFocus, AuthoredFocusableFalseSurvivesSliderAndFieldConstruction) {
    Form f(R"(<UI>
                <Element name="a" focusable="true" style="width: 100px; height: 40px"/>
                <Slider name="deco" focusable="false" min="0" max="1"
                        style="width: 100px; height: 20px"/>
                <TextField name="readonly" focusable="false"
                           style="width: 100px; height: 20px"/>
                <Element name="c" focusable="true" style="width: 100px; height: 40px"/>
              </UI>)");

    ASSERT_NE(f.el("deco"), nullptr);
    ASSERT_NE(f.el("readonly"), nullptr);
    EXPECT_FALSE(f.el("deco")->isFocusable())
        << "focusable=\"false\" on a <Slider> was overwritten by MakeSlider";
    EXPECT_FALSE(f.el("readonly")->isFocusable())
        << "focusable=\"false\" on a <TextField> was overwritten by MakeTextField";

    // ...and it really is out of the ring: Tab from a goes straight to c.
    f.doc.SetFocus(f.el("a"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("c"))
        << "Tab stopped on an element the author declared unfocusable";
}

// The other half: with no attribute at all, a Slider IS focusable, because that
// is what the type word means. Seeding the default in applyAttributes rather
// than leaning on MakeSlider\'s side effect must not have lost that.
TEST(UIFocus, ASliderIsFocusableByDefault) {
    Form f(R"(<UI>
                <Element name="a" focusable="true" style="width: 100px; height: 40px"/>
                <Slider name="vol" min="0" max="1" style="width: 100px; height: 20px"/>
              </UI>)");

    ASSERT_NE(f.el("vol"), nullptr);
    EXPECT_TRUE(f.el("vol")->isFocusable())
        << "a plain <Slider> is no longer reachable by keyboard at all";

    f.doc.SetFocus(f.el("a"));
    f.PressKey(UIKey::Tab);
    EXPECT_EQ(f.doc.focused(), f.el("vol"));
}
