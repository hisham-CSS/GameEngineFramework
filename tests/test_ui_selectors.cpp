// Descendant and child combinators: `.panel .btn` and `.panel > .btn`.
//
// Pure CPU. Matching runs RIGHT TO LEFT — start at the element being styled and
// satisfy each earlier compound against something above it — which is how every
// real CSS engine does it, because left-to-right would need backtracking over
// the whole subtree.
//
// The other half is specificity: a selector's weight SUMS across its compounds,
// so `.panel .btn` (two classes) beats a bare `.btn` no matter which came first
// in the file. That is the entire point of having contexts at all.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// panel > row > btn, plus a btn sitting outside the panel entirely.
const char* kTree = R"(<UI name="root">
  <Element name="panel" class="panel">
    <Element name="row" class="row">
      <Element name="btn" class="btn"/>
    </Element>
    <Element name="direct" class="btn"/>
  </Element>
  <Element name="loose" class="btn"/>
</UI>)";

struct Tree {
    UIDocument doc;
    UIStyleSheet sheet;
    std::vector<std::string> errors;

    bool Load(const std::string& css, const char* markup = kTree) {
        if (!sheet.ParseString(css, "t.uss")) return false;
        if (!UIMarkup::LoadInto(doc, markup, errors, "t.uxml")) return false;
        sheet.ApplyTo(doc.root());
        return true;
    }
    UIElement* el(const char* n) { return doc.root().Find(n); }
    float red(const char* n) { return el(n)->style().backgroundColor.r; }
};

int classCount(const UIStyleSheet& s, size_t rule) {
    int ids = 0, cls = 0, types = 0;
    s.rules()[rule].selectors[0].Specificity(ids, cls, types);
    return cls;
}

} // namespace

TEST(UISelectors, DescendantMatchesAnyAncestor) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel .btn { background-color: #ff0000; }"))
        << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);

    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f) << "a grandchild is still a descendant";
    EXPECT_NEAR(t.red("direct"), 1.0f, 0.01f) << "a direct child is a descendant too";
    EXPECT_NEAR(t.red("loose"), 0.0f, 0.01f) << "matched outside the panel";
}

TEST(UISelectors, ChildMatchesOnlyTheImmediateParent) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel > .btn { background-color: #ff0000; }"))
        << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);

    EXPECT_NEAR(t.red("direct"), 1.0f, 0.01f);
    EXPECT_NEAR(t.red("btn"), 0.0f, 0.01f) << "a grandchild matched a child combinator";
    EXPECT_NEAR(t.red("loose"), 0.0f, 0.01f);
}

TEST(UISelectors, ChainsOfThreeWork) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel .row .btn { background-color: #ff0000; }"))
        << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);
    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f);
    EXPECT_NEAR(t.red("direct"), 0.0f, 0.01f) << "no .row above it";
}

TEST(UISelectors, DescendantAndChildMix) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel > .row .btn { background-color: #ff0000; }"))
        << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);
    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f);

    Tree u;
    // `.row` is NOT a direct child of the root, so this must not match.
    ASSERT_TRUE(u.Load("#root > .row .btn { background-color: #ff0000; }"));
    EXPECT_NEAR(u.red("btn"), 0.0f, 0.01f);
}

// The whole point of contexts: a more specific one wins regardless of order.
TEST(UISelectors, SpecificitySumsAcrossCompounds) {
    UIStyleSheet s;
    ASSERT_TRUE(s.ParseString(
        ".btn { color: red; }\n"                 // 1 class
        ".panel .btn { color: red; }\n"          // 2 classes
        ".panel .row .btn:hover { color: red; }\n"  // 4 (3 classes + pseudo)
        "#root > Element.btn { color: red; }\n", // 1 id + 1 class + 1 type
        "t.uss")) << (s.errors().empty() ? "" : s.errors()[0]);
    EXPECT_EQ(classCount(s, 0), 1);
    EXPECT_EQ(classCount(s, 1), 2);
    EXPECT_EQ(classCount(s, 2), 4);

    int ids = 0, cls = 0, types = 0;
    s.rules()[3].selectors[0].Specificity(ids, cls, types);
    EXPECT_EQ(ids, 1);
    EXPECT_EQ(cls, 1);
    EXPECT_EQ(types, 1);
}

TEST(UISelectors, AMoreSpecificContextWinsEvenWhenListedFirst) {
    Tree t;
    ASSERT_TRUE(t.Load(
        // Listed FIRST, so only specificity can make it win.
        ".panel .btn { background-color: #ff0000; }\n"
        ".btn { background-color: #000000; }\n"));
    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f) << "the context selector lost to a bare class";
    EXPECT_NEAR(t.red("loose"), 0.0f, 0.01f);
}

// Whitespace around a '>' must not each be read as a descendant combinator.
TEST(UISelectors, WhitespaceAroundAChildCombinatorIsNotItselfACombinator) {
    for (const char* css : { ".panel>.btn { background-color: #ff0000; }",
                             ".panel > .btn { background-color: #ff0000; }",
                             ".panel   >   .btn { background-color: #ff0000; }" }) {
        Tree t;
        ASSERT_TRUE(t.Load(css)) << css << " -> "
                                 << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);
        EXPECT_NEAR(t.red("direct"), 1.0f, 0.01f) << css;
        EXPECT_NEAR(t.red("btn"), 0.0f, 0.01f) << css;
    }
}

TEST(UISelectors, MalformedCombinatorsAreReported) {
    for (const char* css : { "> .btn { color: red; }",
                             ".panel > { color: red; }",
                             ".panel > > .btn { color: red; }" }) {
        UIStyleSheet s;
        EXPECT_FALSE(s.ParseString(css, "t.uss")) << css;
        ASSERT_FALSE(s.errors().empty()) << css;
        EXPECT_TRUE(s.rules().empty()) << css;
    }
}

// Comma lists still work, and each selector keeps its own chain.
TEST(UISelectors, CommaListsOfChains) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel .row .btn, .loose-never { background-color: #ff0000; }"))
        << (t.sheet.errors().empty() ? "" : t.sheet.errors()[0]);
    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f);
    EXPECT_NEAR(t.red("loose"), 0.0f, 0.01f);
}

// Single-compound selectors are the overwhelmingly common case and must not
// have regressed while gaining chains.
TEST(UISelectors, PlainSelectorsAreUnchanged) {
    Tree t;
    ASSERT_TRUE(t.Load("* { background-color: #000000; }\n"
                       "Element { background-color: #110000; }\n"
                       ".btn { background-color: #ff0000; }\n"
                       "#loose { background-color: #00ff00; }\n"));
    EXPECT_NEAR(t.red("btn"), 1.0f, 0.01f) << "class beat type";
    EXPECT_NEAR(t.el("loose")->style().backgroundColor.g, 1.0f, 0.01f) << "#name beat class";
    EXPECT_NEAR(t.red("panel"), 0.0666f, 0.01f) << "type beat universal";
}

// A descendant rule that would match only in a particular state still has to
// put the element on the styler's watch list.
TEST(UISelectors, HasStateRuleForLooksThroughTheWholeChain) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel .btn:hover { background-color: #ff0000; }"));
    EXPECT_TRUE(t.sheet.HasStateRuleFor(*t.el("btn")));
    EXPECT_TRUE(t.sheet.HasStateRuleFor(*t.el("direct")));
    EXPECT_FALSE(t.sheet.HasStateRuleFor(*t.el("loose")))
        << "an element outside the context can never be reached by that rule";
    EXPECT_FALSE(t.sheet.HasStateRuleFor(*t.el("panel")));
}

// Matching walks PARENTS, so an element detached from the tree simply fails its
// context rather than reading a dangling pointer.
TEST(UISelectors, ADetachedElementMatchesNoContext) {
    Tree t;
    ASSERT_TRUE(t.Load(".panel .btn { background-color: #ff0000; }"));
    std::unique_ptr<UIElement> taken = t.el("panel")->RemoveChild(t.el("direct"));
    ASSERT_NE(taken, nullptr);
    EXPECT_FALSE(t.sheet.rules()[0].selectors[0].Matches(*taken));
    // ...and re-applying to the detached element is safe and does nothing.
    t.sheet.ApplyToElement(*taken);
    SUCCEED();
}
