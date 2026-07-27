// Data binding: the data source, the {hole} template grammar, and the binder.
//
// Pure CPU — no GL context, which is deliberate and is why UIElement carries a
// textRevision counter. Proving "the binding wrote through setText and
// therefore invalidated the measurement" via the measured WIDTH would need a
// real font, and a real font needs a GL texture.
//
// What actually matters here, in rough order of how much it would hurt to get
// wrong:
//  - a hot reload must not lose a value, and must not need the app to re-push
//    anything (this is the entire reason binding exists in this system);
//  - the binder caches raw UIElement*, so a restructured tree must force a
//    re-collect rather than a dereference;
//  - a typo must be REPORTED with the names that do exist — without reflection,
//    a misspelt path is otherwise indistinguishable from a value that never
//    changes, which is the worst bug class this system can have;
//  - an idle frame must cost nothing.
#include <gtest/gtest.h>

#include "Engine.h"
#include "ui_shipped_hud.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UITextField.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

std::string compileError(const char* text) {
    UITextTemplate t;
    std::string err;
    EXPECT_FALSE(UITextTemplate::Compile(text, t, err)) << "expected '" << text << "' to fail";
    return err;
}

std::string render(const char* text, UIDataSource& src, UIBindingContext& ctx) {
    UIDocument doc;
    doc.root().setDataSourceName("s");
    UIElement* label = doc.root().AddChild("label");
    std::vector<std::string> errors;
    EXPECT_TRUE(UIMarkup::LoadInto(doc, std::string("<UI data-source=\"s\"><Label name=\"l\" text=\"") +
                                        text + "\"/></UI>", errors, "t.cxml"))
        << (errors.empty() ? "" : errors[0]);
    (void)label;
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    UIElement* l = doc.root().Find("l");
    return l ? l->style().text : std::string("<missing>");
}

// Press and release over the same point, which is what UIDocument requires for
// a Click (sliding off a button before letting go correctly cancels it).
void clickAt(UIDocument& doc, float x, float y) {
    UIPointerState p;
    p.inside = true;
    p.position = { x, y };
    p.buttonDown = true;  doc.UpdatePointer(p);
    p.buttonDown = false; doc.UpdatePointer(p);
}

void writeFileAt(const std::string& path, const std::string& text, int secondsFromNow) {
    { std::ofstream o(path, std::ios::binary); o << text; }
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type::clock::now() +
                  std::chrono::seconds(secondsFromNow));
}

} // namespace

// ------------------------------------------------------------- UIDataSource

TEST(UIDataSource, StoresAndReadsEveryKind) {
    UIDataSource s;
    s.SetInt("score", 1234);
    s.SetNumber("health", 0.37f);
    s.SetBool("alive", true);
    s.SetString("name", "PLAYER");

    EXPECT_EQ(s.GetInt("score"), 1234);
    EXPECT_FLOAT_EQ(s.GetNumber("health"), 0.37f);
    EXPECT_TRUE(s.GetBool("alive"));
    EXPECT_EQ(s.GetString("name"), "PLAYER");
    EXPECT_TRUE(s.Has("score"));
    EXPECT_FALSE(s.Has("scoer"));
    EXPECT_EQ(s.GetInt("nope", -1), -1) << "a missing property must yield the default";
}

// The equality gate is what makes "gameplay writes health every frame" free.
TEST(UIDataSource, WritingAnUnchangedValueBumpsNothing) {
    UIDataSource s;
    s.SetNumber("health", 1.0f);
    const auto v = s.version();

    s.SetNumber("health", 1.0f);
    EXPECT_EQ(s.version(), v) << "an unchanged write moved the version";

    s.SetNumber("health", 0.9f);
    EXPECT_NE(s.version(), v);
}

// A different KIND with the same numeric value is a real change: a binding
// coercing an int and a binding coercing a string behave differently.
TEST(UIDataSource, ChangingTheKindCountsAsAChange) {
    UIDataSource s;
    s.SetInt("x", 1);
    const auto v = s.version();
    s.SetString("x", "1");
    EXPECT_NE(s.version(), v);
}

TEST(UIDataSource, ObservedPropertiesReadThroughToTheApp) {
    UIDataSource s;
    int live = 5;
    s.Observe("ammo", [&live] { return UIValue::Int(live); });
    EXPECT_EQ(s.GetInt("ammo"), 5);
    live = 9;
    EXPECT_EQ(s.GetInt("ammo"), 9) << "the getter was not re-run";
    EXPECT_TRUE(s.hasPolled());

    // Observing a name again REPLACES it, so a bind callback that runs on every
    // hot reload cannot accumulate duplicates.
    s.Observe("ammo", [] { return UIValue::Int(99); });
    EXPECT_EQ(s.GetInt("ammo"), 99);
    EXPECT_EQ(s.propertyNames().size(), 1u);
}

TEST(UIDataSource, ReadOnlyObservedPropertyRefusesAWrite) {
    UIDataSource s;
    s.Observe("hp", [] { return UIValue::Int(3); });   // no setter
    const int i = s.IndexOf("hp");
    ASSERT_GE(i, 0);
    EXPECT_FALSE(s.IsWritableAt(i));
    EXPECT_FALSE(s.WriteAt(i, UIValue::Int(9)));

    int backing = 0;
    s.Observe("mp", [&backing] { return UIValue::Int(backing); },
              [&backing](const UIValue& v) { long long o = 0; v.AsInt(o); backing = int(o); });
    const int j = s.IndexOf("mp");
    ASSERT_GE(j, 0);
    EXPECT_TRUE(s.IsWritableAt(j));
    EXPECT_TRUE(s.WriteAt(j, UIValue::Int(7)));
    EXPECT_EQ(backing, 7);
}

TEST(UIDataSource, ActionsAreInvokedByIndexAndReplacedByName) {
    UIDataSource s;
    int calls = 0, which = 0;
    s.AddAction("go", [&] { ++calls; which = 1; });
    const int i = s.ActionIndexOf("go");
    ASSERT_GE(i, 0);
    EXPECT_TRUE(s.InvokeAction(i));
    EXPECT_EQ(calls, 1);

    // Replacement matters: a stale std::function may capture a dead `this`.
    s.AddAction("go", [&] { ++calls; which = 2; });
    EXPECT_TRUE(s.InvokeAction(s.ActionIndexOf("go")));
    EXPECT_EQ(which, 2);
    EXPECT_EQ(s.actionNames().size(), 1u);
    EXPECT_EQ(s.ActionIndexOf("nope"), -1);
}

// Adding a property is itself a change: a binding that reported "no property
// 'ammo'" has to resolve once the app supplies it.
TEST(UIDataSource, AddingAPropertyMovesTheVersion) {
    UIDataSource s;
    const auto v = s.version();
    s.SetInt("ammo", 1);
    EXPECT_NE(s.version(), v);
}

// -------------------------------------------------------------- converters

TEST(UIConverters, BuiltinsDoWhatTheirNamesSay) {
    const UIConverterTable& t = BuiltinUIConverters();
    UIValue out;
    std::string err;

    // The one every health bar needs: a 0..1 model value becomes a CSS
    // percentage, with the unit built in so markup has nothing to get wrong.
    ASSERT_TRUE((*t.Find("percent"))(UIValue::Number(0.37f), out, err)) << err;
    EXPECT_EQ(out.kind, UIValue::Kind::Length);
    EXPECT_EQ(out.length.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(out.length.value, 37.0f);

    ASSERT_TRUE((*t.Find("ratio"))(UIValue::Number(0.5f), out, err)) << err;
    EXPECT_FLOAT_EQ(out.number, 50.0f);

    ASSERT_TRUE((*t.Find("px"))(UIValue::Number(12.f), out, err)) << err;
    EXPECT_EQ(out.length.unit, StyleLength::Unit::Point);

    ASSERT_TRUE((*t.Find("not"))(UIValue::Bool(true), out, err)) << err;
    EXPECT_FALSE(out.boolean);

    ASSERT_TRUE((*t.Find("round"))(UIValue::Number(2.6f), out, err)) << err;
    EXPECT_FLOAT_EQ(out.number, 3.0f);
    ASSERT_TRUE((*t.Find("int"))(UIValue::Number(2.9f), out, err)) << err;
    EXPECT_EQ(out.kind, UIValue::Kind::Int);
    EXPECT_EQ(out.integer, 2);

    ASSERT_TRUE((*t.Find("upper"))(UIValue::Str("hi"), out, err)) << err;
    EXPECT_EQ(out.text, "HI");

    EXPECT_EQ(t.Find("definitely_not_a_converter"), nullptr);
}

TEST(UIConverters, AConverterThatCannotAcceptTheKindFailsAndSaysSo) {
    UIValue out;
    std::string err;
    EXPECT_FALSE((*BuiltinUIConverters().Find("percent"))(
        UIValue::Color4({ 1, 0, 0, 1 }), out, err));
    EXPECT_NE(err.find("colour"), std::string::npos) << err;
    EXPECT_NE(err.find("percent"), std::string::npos) << err;
}

TEST(UIConverters, AppTableShadowsABuiltinAndIsRemovable) {
    UIConverterTable t;
    t.Register("percent", [](const UIValue&, UIValue& o, std::string&) {
        o = UIValue::Str("shadowed");
        return true;
    });
    UIValue out;
    std::string err;
    ASSERT_TRUE((*t.Find("percent"))(UIValue::Number(1.f), out, err));
    EXPECT_EQ(out.text, "shadowed");
    t.Remove("percent");
    EXPECT_EQ(t.Find("percent"), nullptr);
}

// ---------------------------------------------------------- UITextTemplate

TEST(UITextTemplate, SplitsLiteralsAndHoles) {
    UITextTemplate t;
    std::string err;
    ASSERT_TRUE(UITextTemplate::Compile("SCORE {score} pts", t, err)) << err;
    EXPECT_FALSE(t.isConstant());
    EXPECT_FALSE(t.isSingleHole());
    ASSERT_EQ(t.holes().size(), 1u);
    ASSERT_EQ(t.literals().size(), 2u) << "literals must always be holes+1";
    EXPECT_EQ(t.literals()[0], "SCORE ");
    EXPECT_EQ(t.literals()[1], " pts");
    EXPECT_EQ(t.holes()[0].propName, "score");
    EXPECT_TRUE(t.holes()[0].sourceName.empty());
}

TEST(UITextTemplate, ConstantAndSingleHoleAreDistinguished) {
    UITextTemplate t;
    std::string err;
    ASSERT_TRUE(UITextTemplate::Compile("just text", t, err)) << err;
    EXPECT_TRUE(t.isConstant());

    // The single-hole case is what lets a bound colour or length skip
    // stringification entirely.
    ASSERT_TRUE(UITextTemplate::Compile("{health}", t, err)) << err;
    EXPECT_TRUE(t.isSingleHole());
    ASSERT_TRUE(UITextTemplate::Compile(" {health}", t, err)) << err;
    EXPECT_FALSE(t.isSingleHole());
}

TEST(UITextTemplate, ParsesConvertersAndFormatSpec) {
    UITextTemplate t;
    std::string err;
    ASSERT_TRUE(UITextTemplate::Compile("{ health | percent | round : 2 }", t, err)) << err;
    ASSERT_EQ(t.holes().size(), 1u);
    const UIHole& h = t.holes()[0];
    EXPECT_EQ(h.propName, "health");
    ASSERT_EQ(h.converters.size(), 2u);
    EXPECT_EQ(h.converters[0], "percent");
    EXPECT_EQ(h.converters[1], "round");
    EXPECT_EQ(h.decimals, 2);
}

TEST(UITextTemplate, QualifiedPathSplitsOnTheDot) {
    UITextTemplate t;
    std::string err;
    ASSERT_TRUE(UITextTemplate::Compile("{player.score}", t, err)) << err;
    EXPECT_EQ(t.holes()[0].sourceName, "player");
    EXPECT_EQ(t.holes()[0].propName, "score");
}

TEST(UITextTemplate, DoubledBracesAreLiterals) {
    UITextTemplate t;
    std::string err;
    ASSERT_TRUE(UITextTemplate::Compile("a {{literal}} brace", t, err)) << err;
    EXPECT_TRUE(t.isConstant());
    ASSERT_EQ(t.literals().size(), 1u);
    EXPECT_EQ(t.literals()[0], "a {literal} brace");
}

TEST(UITextTemplate, MalformedHolesAreReportedNotGuessed) {
    EXPECT_NE(compileError("SCORE {score").find("unterminated"), std::string::npos);
    EXPECT_NE(compileError("a } b").find("'}}'"), std::string::npos);
    EXPECT_NE(compileError("{}").find("empty"), std::string::npos);
    EXPECT_NE(compileError("{a.b.c}").find("more than one"), std::string::npos);
    EXPECT_NE(compileError("{score:x}").find("format spec"), std::string::npos);
    EXPECT_NE(compileError("{score||round}").find("empty converter"), std::string::npos);
}

// -------------------------------------------------------------- the binder

TEST(UIBinding, InterpolatesTextFromTheDataSource) {
    UIDataSource s;
    s.SetInt("score", 1234);
    s.SetNumber("health", 0.376f);
    UIBindingContext ctx;
    ctx.RegisterSource("s", &s);

    EXPECT_EQ(render("SCORE {score}", s, ctx), "SCORE 1234");
    EXPECT_EQ(render("{health | ratio : 0}%", s, ctx), "38%");
    EXPECT_EQ(render("{score} / {score}", s, ctx), "1234 / 1234");
    EXPECT_EQ(render("no holes here", s, ctx), "no holes here");
}

TEST(UIBinding, WritingTheSourceUpdatesTheElementOnTheNextPass) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="hud"><Label name="l" text="SCORE {score}"/></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource s;
    s.SetInt("score", 0);
    UIBindingContext ctx;
    ctx.RegisterSource("hud", &s);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    UIElement* l = doc.root().Find("l");
    ASSERT_NE(l, nullptr);
    // Rebuild force-applies, so the tree is correct BEFORE the first layout —
    // which is why a bound label never flashes its placeholder for one frame.
    EXPECT_EQ(l->style().text, "SCORE 0");

    s.SetInt("score", 100);
    EXPECT_EQ(binder.UpdateToTarget().applied, 1u);
    EXPECT_EQ(l->style().text, "SCORE 100");
}

// The reason UIElement has a textRevision at all: proving the write went
// through setText (and therefore invalidated the measurement) rather than
// straight into style().text, without needing a font and therefore a GL context.
TEST(UIBinding, WritesThroughSetTextSoTheLabelReMeasures) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="h"><Label name="l" text="{n}"/></UI>)",
                                   errors, "t.cxml"));
    UIDataSource s;
    s.SetInt("n", 1);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");

    UIElement* l = doc.root().Find("l");
    ASSERT_NE(l, nullptr);
    const std::uint32_t before = l->textRevision();

    s.SetInt("n", 2);
    binder.UpdateToTarget();
    EXPECT_GT(l->textRevision(), before) << "the binding bypassed setText";

    // ...and an unchanged value must not churn the measurement.
    const std::uint32_t after = l->textRevision();
    binder.UpdateToTarget();
    EXPECT_EQ(l->textRevision(), after);
}

// An idle frame must cost nothing: one integer compare per source.
TEST(UIBinding, AnUnchangedSourceAppliesNothing) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="h"><Label name="l" text="{n}"/></UI>)",
                                   errors, "t.cxml"));
    UIDataSource s;
    s.SetInt("n", 1);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");

    EXPECT_EQ(binder.UpdateToTarget().applied, 0u);
    EXPECT_EQ(binder.UpdateToTarget().applied, 0u);
    s.SetInt("n", 2);
    EXPECT_EQ(binder.UpdateToTarget().applied, 1u);
    EXPECT_EQ(binder.UpdateToTarget().applied, 0u);
}

TEST(UIBinding, DataSourceIsInheritedByTheWholeSubtree) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="a">
          <Element name="mid">
            <Label name="deep" text="{v}"/>
          </Element>
          <Element name="other" data-source="b">
            <Label name="deep2" text="{v}"/>
          </Element>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource a, b;
    a.SetString("v", "from-a");
    b.SetString("v", "from-b");
    UIBindingContext ctx;
    ctx.RegisterSource("a", &a);
    ctx.RegisterSource("b", &b);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    EXPECT_TRUE(binder.ok()) << (binder.errors().empty() ? "" : binder.errors()[0]);
    EXPECT_EQ(doc.root().Find("deep")->style().text, "from-a");
    // A nested data-source overrides for its own subtree, like UI Toolkit.
    EXPECT_EQ(doc.root().Find("deep2")->style().text, "from-b");
}

TEST(UIBinding, QualifiedPathBeatsTheInheritedSource) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="a"><Label name="l" text="{b.v}"/></UI>)", errors, "t.cxml"));
    UIDataSource a, b;
    a.SetString("v", "wrong");
    b.SetString("v", "right");
    UIBindingContext ctx;
    ctx.RegisterSource("a", &a);
    ctx.RegisterSource("b", &b);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    EXPECT_EQ(doc.root().Find("l")->style().text, "right");
}

// Without reflection this is the highest-value diagnostic in the system: a
// misspelt path is otherwise indistinguishable from a value that never changes.
TEST(UIBinding, AMisspeltPathIsReportedWithTheNamesThatExist) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="hud"><Label name="scoreLabel" text="SCORE {scoer}"/></UI>)",
        errors, "t.cxml"));
    UIDataSource s;
    s.SetInt("score", 0);
    s.SetNumber("health", 1.f);
    UIBindingContext ctx;
    ctx.RegisterSource("hud", &s);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    EXPECT_FALSE(binder.ok());
    ASSERT_FALSE(binder.errors().empty());
    const std::string& e = binder.errors()[0];
    EXPECT_NE(e.find("scoer"), std::string::npos) << e;
    EXPECT_NE(e.find("score"), std::string::npos) << e;
    EXPECT_NE(e.find("health"), std::string::npos) << e;
    EXPECT_NE(e.find("scoreLabel"), std::string::npos) << e << " (must name the element)";
    EXPECT_EQ(binder.unresolvedCount(), 1u);
}

TEST(UIBinding, AnUnknownSourceOrConverterIsReported) {
    UIDataSource s;
    s.SetInt("n", 1);
    UIBindingContext ctx;
    ctx.RegisterSource("hud", &s);

    {
        UIDocument doc;
        std::vector<std::string> errors;
        ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Label name="l" text="{missing.n}"/></UI>)",
                                       errors, "t.cxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.cxml");
        ASSERT_FALSE(b.errors().empty());
        EXPECT_NE(b.errors()[0].find("unknown data source 'missing'"), std::string::npos)
            << b.errors()[0];
        EXPECT_NE(b.errors()[0].find("hud"), std::string::npos) << "must list what IS registered";
    }
    {
        UIDocument doc;
        std::vector<std::string> errors;
        ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="hud"><Label name="l" text="{n|pct}"/></UI>)",
                                       errors, "t.cxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.cxml");
        ASSERT_FALSE(b.errors().empty());
        EXPECT_NE(b.errors()[0].find("unknown converter 'pct'"), std::string::npos) << b.errors()[0];
        EXPECT_NE(b.errors()[0].find("percent"), std::string::npos) << "must list the real ones";
    }
    {
        // No data-source anywhere: say what to DO about it, not just what failed.
        UIDocument doc;
        std::vector<std::string> errors;
        ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Label name="l" text="{n}"/></UI>)",
                                       errors, "t.cxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.cxml");
        ASSERT_FALSE(b.errors().empty());
        EXPECT_NE(b.errors()[0].find("no data source in scope"), std::string::npos) << b.errors()[0];
        EXPECT_NE(b.errors()[0].find("data-source="), std::string::npos) << b.errors()[0];
    }
}

// Load order is a diagnostic, not a failure: an app may register a source after
// the UI loads, and those bindings must come alive without a reload.
TEST(UIBinding, ABindingResolvesWhenItsSourceIsRegisteredLater) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="late"><Label name="l" text="{v}"/></UI>)",
                                   errors, "t.cxml"));
    UIBindingContext ctx;
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    EXPECT_EQ(binder.unresolvedCount(), 1u);
    EXPECT_FALSE(binder.ok());

    UIDataSource s;
    s.SetString("v", "arrived");
    ctx.RegisterSource("late", &s);
    binder.UpdateToTarget();          // notices the context revision moved
    EXPECT_EQ(binder.unresolvedCount(), 0u);
    EXPECT_TRUE(binder.ok());
    EXPECT_EQ(doc.root().Find("l")->style().text, "arrived");
}

// The binder caches raw UIElement*, and UIDocument explicitly allows handlers to
// restructure the tree. One integer compare buys the whole safety property.
TEST(UIBinding, RestructuringTheTreeForcesAReCollectInsteadOfADereference) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="h">
          <Label name="keep" text="{v}"/>
          <Label name="doomed" text="{v}"/>
        </UI>)", errors, "t.cxml"));
    UIDataSource s;
    s.SetString("v", "x");
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");
    EXPECT_EQ(binder.bindingCount(), 2u);

    // Destroy one element the binder is indexing.
    UIElement* doomed = doc.root().Find("doomed");
    ASSERT_NE(doomed, nullptr);
    doc.root().RemoveChild(doomed);   // returns ownership; dropped here

    s.SetString("v", "y");
    binder.UpdateToTarget();          // must NOT touch the freed element
    EXPECT_EQ(binder.bindingCount(), 1u) << "the stale entry survived a structural change";
    EXPECT_EQ(doc.root().Find("keep")->style().text, "y");
}

TEST(UIBinding, DescribeNamesEveryLiveBinding) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="h"><Label name="l" text="{v}"/></UI>)",
                                   errors, "t.cxml"));
    UIDataSource s;
    s.SetInt("v", 7);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");

    const auto lines = binder.Describe();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("name='l'"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("text"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("v=7"), std::string::npos) << lines[0];
}

// ------------------------------------------------------- markup attributes

// Until now this loader read the attributes it knew and never enumerated the
// rest, so a typo produced an element that no rule and no binding could find.
TEST(UIMarkupAttributes, AnUnknownAttributeIsAnErrorAndLeavesTheTreeAlone) {
    UIDocument doc;
    std::vector<std::string> ok;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Element name="good"/></UI>)", ok, "t.cxml"));

    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element nmae="healthFill"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown attribute 'nmae'"), std::string::npos) << errors[0];
    EXPECT_NE(doc.root().Find("good"), nullptr) << "a bad attribute destroyed the running UI";
}

// The root used to be handled by a SEPARATE block, so every new attribute
// family had to be added twice and the two silently diverged.
TEST(UIMarkupAttributes, TheRootIsValidatedExactlyLikeAChild) {
    UIDocument doc;
    std::vector<std::string> ok;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI name="hud"><Element name="good"/></UI>)", ok, "t.cxml"));

    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI data-sorce="hud"><Element name="x"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown attribute 'data-sorce'"), std::string::npos) << errors[0];
    // ...and the document is untouched, root included.
    EXPECT_EQ(doc.root().name(), "hud");
    EXPECT_NE(doc.root().Find("good"), nullptr);
}

TEST(UIMarkupAttributes, ABadTemplateFailsTheLoadAndReportsWhere) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(
        doc, R"(<UI><Label name="scoreLabel" text="SCORE {score"/></UI>)", errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("scoreLabel"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("unterminated"), std::string::npos) << errors[0];
}

TEST(UIMarkupAttributes, ReloadingReplacesDataSourceAndBindingsRatherThanAccumulating) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="a" text="{v}"/>)", errors, "t.cxml"));
    EXPECT_EQ(doc.root().dataSourceName(), "a");
    EXPECT_EQ(doc.root().bindings().size(), 1u);

    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI/>)", errors, "t.cxml"));
    EXPECT_TRUE(doc.root().dataSourceName().empty()) << "a stale data-source survived a reload";
    EXPECT_TRUE(doc.root().bindings().empty()) << "a stale binding survived a reload";
    EXPECT_TRUE(doc.root().style().text.empty()) << "stale text survived a reload";
}

// -------------------------------------------------------- bind= (U4b)

namespace {

// Builds a one-element document with the given bind= attribute, resolved
// against a source carrying `health` (0.4), `w` (120), `tint` (a colour) and
// `label` ("auto").
struct BindFixture {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIBinder binder;
    std::vector<std::string> errors;

    bool Load(const std::string& bindAttr) {
        src.SetNumber("health", 0.4f);
        src.SetNumber("w", 120.f);
        src.SetColor("tint", { 0.1f, 0.2f, 0.3f, 1.0f });
        src.SetString("label", "auto");
        ctx.RegisterSource("s", &src);
        const std::string xml = "<UI data-source=\"s\"><Element name=\"e\" bind=\"" +
                                bindAttr + "\"/></UI>";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        binder.Rebuild(doc, ctx, "t.cxml");
        return true;
    }
    UIElement* el() { return doc.root().Find("e"); }
};

} // namespace

TEST(UIBindStyle, BindsALengthAColourAndANumber) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {health | percent}; background-color: {tint}; flex-grow: {w}"))
        << (f.errors.empty() ? "" : f.errors[0]);
    ASSERT_TRUE(f.binder.ok()) << (f.binder.errors().empty() ? "" : f.binder.errors()[0]);

    UIElement* e = f.el();
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->style().width.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(e->style().width.value, 40.f);
    EXPECT_FLOAT_EQ(e->style().backgroundColor.b, 0.3f);
    EXPECT_FLOAT_EQ(e->style().flexGrow, 120.f);

    f.src.SetNumber("health", 0.75f);
    f.binder.UpdateToTarget();
    EXPECT_FLOAT_EQ(e->style().width.value, 75.f);
}

// A bare number is pixels, exactly as in a declaration.
TEST(UIBindStyle, BareNumberBecomesPixels) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {w}"));
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_EQ(f.el()->style().width.unit, StyleLength::Unit::Point);
    EXPECT_FLOAT_EQ(f.el()->style().width.value, 120.f);
}

// Literals around the hole route through the ordinary declaration parser, so
// interpolated text means exactly what the same text means in a .cstyle file.
TEST(UIBindStyle, LiteralsAroundAHoleGoThroughTheDeclarationGrammar) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {health | ratio}%"));
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_EQ(f.el()->style().width.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(f.el()->style().width.value, 40.f);
}

// A string reaches an enum or a keyword the only way it can.
TEST(UIBindStyle, AStringCanCarryAKeywordOrAnEnum) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {label}"));
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_EQ(f.el()->style().width.unit, StyleLength::Unit::Auto);

    BindFixture g;
    g.src.SetString("dir", "row");
    ASSERT_TRUE(g.Load("flex-direction: {dir}"));
    ASSERT_TRUE(g.binder.ok()) << g.binder.errors()[0];
    EXPECT_EQ(g.el()->style().direction, FlexDirection::Row);
}

// The whole point of the brace-aware splitter: a ';' and a ':' inside a hole
// belong to that hole, not to the declaration list.
TEST(UIBindStyle, SplittingIsBraceAware) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {health | percent : 1}; flex-grow: {w}"))
        << (f.errors.empty() ? "" : f.errors[0]);
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_FLOAT_EQ(f.el()->style().width.value, 40.f);
    EXPECT_FLOAT_EQ(f.el()->style().flexGrow, 120.f);
}

// A bind with no hole would be a silent duplicate of style=, and the two would
// then disagree about which wins.
TEST(UIBindStyle, AConstantBindIsRefusedWithAdvice) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element name="e" bind="width: 100%"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("use style="), std::string::npos) << errors[0];
}

TEST(UIBindStyle, AnUnknownBoundPropertyFailsTheLoad) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element name="e" bind="widht: {x}"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown property 'widht'"), std::string::npos) << errors[0];
}

TEST(UIBindStyle, AValueThatCannotConvertIsReportedNamingBothKinds) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {tint}"));   // a colour is not a length
    EXPECT_FALSE(f.binder.ok());
    ASSERT_FALSE(f.binder.errors().empty());
    const std::string& e = f.binder.errors()[0];
    EXPECT_NE(e.find("colour"), std::string::npos) << e;
    EXPECT_NE(e.find("width"), std::string::npos) << e;
}

// parseLength accepts "-100%" quite happily, so without this a negative model
// value silently collapses the layout.
TEST(UIBindStyle, ANegativeSizeIsRejected) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {health | percent}"));
    ASSERT_TRUE(f.binder.ok());
    const float good = f.el()->style().width.value;

    f.src.SetNumber("health", -0.5f);
    f.binder.UpdateToTarget();
    EXPECT_FLOAT_EQ(f.el()->style().width.value, good) << "a negative width was applied";
    ASSERT_FALSE(f.binder.errors().empty());
    EXPECT_NE(f.binder.errors()[0].find("cannot be negative"), std::string::npos)
        << f.binder.errors()[0];
}

TEST(UIBindStyle, ANonFiniteValueNeverReachesTheLayout) {
    BindFixture f;
    ASSERT_TRUE(f.Load("width: {health | percent}"));
    ASSERT_TRUE(f.binder.ok());
    const float good = f.el()->style().width.value;

    f.src.SetNumber("health", std::numeric_limits<float>::quiet_NaN());
    f.binder.UpdateToTarget();
    EXPECT_FLOAT_EQ(f.el()->style().width.value, good) << "a NaN reached the layout";
    ASSERT_FALSE(f.binder.errors().empty());
    EXPECT_NE(f.binder.errors()[0].find("not finite"), std::string::npos)
        << f.binder.errors()[0];
}

// A colour-only write cannot change a box, so it must not cost a re-layout.
TEST(UIBindStyle, AColourWriteDoesNotForceALayout) {
    BindFixture f;
    ASSERT_TRUE(f.Load("background-color: {tint}"));
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    f.src.SetColor("tint", { 0.9f, 0.1f, 0.1f, 1.f });
    const UIBindTick tick = f.binder.UpdateToTarget();
    EXPECT_EQ(tick.applied, 1u);
    EXPECT_FALSE(tick.wroteLayout) << "a colour change asked for a re-layout";

    BindFixture g;
    ASSERT_TRUE(g.Load("width: {health | percent}"));
    g.src.SetNumber("health", 0.9f);
    EXPECT_TRUE(g.binder.UpdateToTarget().wroteLayout) << "a width change must re-layout";
}

// ------------------------------------------- if= and named actions (U4c)

namespace {

struct IfFixture {
    UIDocument doc;
    UIDataSource src;
    UIBindingContext ctx;
    UIStyleSheet sheet;
    UIBinder binder;
    std::vector<std::string> errors;

    bool Load(const std::string& attrs) {
        src.SetBool("alive", true);
        src.SetBool("dead", false);
        ctx.RegisterSource("s", &src);
        const std::string xml =
            "<UI data-source=\"s\"><Element name=\"e\" " + attrs +
            " style=\"width: 50px; height: 50px\"/></UI>";
        if (!UIMarkup::LoadInto(doc, xml, errors, "t.cxml")) return false;
        // Markup only STORES an inline style; the cascade is what replays it
        // onto style() (that ordering is how inline outranks every selector).
        // Same order UIAssetDocument uses: cascade, then bind.
        sheet.ApplyTo(doc.root());
        binder.Rebuild(doc, ctx, "t.cxml", &sheet);
        return true;
    }
    UIElement* el() { return doc.root().Find("e"); }
};

} // namespace

TEST(UIBindDisplay, IfShowsAndHides) {
    IfFixture f;
    ASSERT_TRUE(f.Load(R"(if="alive")")) << (f.errors.empty() ? "" : f.errors[0]);
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_EQ(f.el()->style().display, DisplayMode::Flex);

    f.src.SetBool("alive", false);
    EXPECT_TRUE(f.binder.UpdateToTarget().wroteLayout) << "show/hide must re-layout";
    EXPECT_EQ(f.el()->style().display, DisplayMode::None);
}

TEST(UIBindDisplay, LeadingBangNegates) {
    IfFixture f;
    ASSERT_TRUE(f.Load(R"(if="!dead")"));
    ASSERT_TRUE(f.binder.ok()) << f.binder.errors()[0];
    EXPECT_EQ(f.el()->style().display, DisplayMode::Flex);
    f.src.SetBool("dead", true);
    f.binder.UpdateToTarget();
    EXPECT_EQ(f.el()->style().display, DisplayMode::None);
}

// The element keeps its identity: hiding is a style write, not tree surgery,
// so a cached pointer and its handlers survive a hide/show cycle.
TEST(UIBindDisplay, HidingKeepsTheElementAndItsHandlers) {
    IfFixture f;
    ASSERT_TRUE(f.Load(R"(if="alive")"));
    UIElement* e = f.el();
    ASSERT_NE(e, nullptr);
    int clicks = 0;
    e->OnClick([&](UIEvent&) { ++clicks; });

    f.src.SetBool("alive", false);
    f.binder.UpdateToTarget();
    EXPECT_EQ(f.el(), e) << "hiding removed the element from the tree";

    f.src.SetBool("alive", true);
    f.binder.UpdateToTarget();
    f.doc.Layout(200.f, 200.f);
    clickAt(f.doc, 10.f, 10.f);
    EXPECT_EQ(clicks, 1) << "the handler did not survive a hide/show cycle";
}

// Hidden means hidden: zero layout, no paint, and NOT clickable.
TEST(UIBindDisplay, AHiddenElementHasNoBoxAndRefusesAHit) {
    IfFixture f;
    ASSERT_TRUE(f.Load(R"(if="alive")"));
    f.doc.Layout(200.f, 200.f);
    ASSERT_GT(f.el()->layout().size.x, 0.f);
    EXPECT_EQ(f.doc.HitTest({ 10.f, 10.f }), f.el());

    f.src.SetBool("alive", false);
    f.binder.UpdateToTarget();
    f.doc.Layout(200.f, 200.f);
    EXPECT_FLOAT_EQ(f.el()->layout().size.x, 0.f) << "a hidden element still took space";
    EXPECT_NE(f.doc.HitTest({ 10.f, 10.f }), f.el()) << "a hidden element was still clickable";
}

TEST(UIBindDisplay, DisplayIsAlsoAPlainStylesheetProperty) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.ParseString(".hidden { display: none; }", "t.cstyle"))
        << (sheet.errors().empty() ? "" : sheet.errors()[0]);
    UIDocument doc;
    UIElement* e = doc.root().AddChild("e");
    e->AddClass("hidden");
    sheet.ApplyTo(doc.root());
    EXPECT_EQ(e->style().display, DisplayMode::None);

    UIStyleSheet bad;
    EXPECT_FALSE(bad.ParseString(".x { display: sideways; }", "t.cstyle"));
    ASSERT_FALSE(bad.errors().empty());
    EXPECT_NE(bad.errors()[0].find("display must be flex|none"), std::string::npos)
        << bad.errors()[0];
}

TEST(UIBindDisplay, AnEmptyOrNonBoolConditionIsReported) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element name="e" if=" ! "/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("if: empty"), std::string::npos) << errors[0];

    IfFixture f;
    ASSERT_TRUE(f.Load(R"(if="alive")"));
    ASSERT_TRUE(f.binder.ok());
    // A string is never a bool — "false" is truthy under one obvious rule and
    // falsy under another, so a visibility toggle must refuse rather than pick.
    f.src.SetString("alive", "yes");
    f.binder.UpdateToTarget();
    EXPECT_FALSE(f.binder.ok());
    ASSERT_FALSE(f.binder.errors().empty());
    EXPECT_NE(f.binder.errors()[0].find("needs a bool"), std::string::npos)
        << f.binder.errors()[0];
}

TEST(UIBindActions, OnClickInvokesTheNamedActionAndBubbles) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="s">
          <Element name="outer" style="width: 100px; height: 100px" on-click="outerHit">
            <Element name="inner" style="width: 40px; height: 40px" on-click="innerHit"/>
          </Element>
        </UI>)", errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource s;
    std::vector<std::string> order;
    s.AddAction("outerHit", [&] { order.push_back("outer"); });
    s.AddAction("innerHit", [&] { order.push_back("inner"); });
    UIBindingContext ctx;
    ctx.RegisterSource("s", &s);
    UIStyleSheet sheet;
    sheet.ApplyTo(doc.root());   // replays the inline styles that give the boxes size
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml", &sheet);
    ASSERT_TRUE(binder.ok()) << binder.errors()[0];

    doc.Layout(200.f, 200.f);
    clickAt(doc, 10.f, 10.f);
    // Through the ordinary listener path, so a bound action bubbles exactly
    // like a hand-written handler.
    ASSERT_EQ(order.size(), 2u) << "a bound action did not bubble";
    EXPECT_EQ(order[0], "inner");
    EXPECT_EQ(order[1], "outer");
}

TEST(UIBindActions, AMisspeltActionIsReportedWithTheOnesThatExist) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="s"><Button name="b" on-click="addScre"/></UI>)",
        errors, "t.cxml"));
    UIDataSource s;
    s.AddAction("addScore", [] {});
    UIBindingContext ctx;
    ctx.RegisterSource("s", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.cxml");

    EXPECT_FALSE(binder.ok());
    ASSERT_FALSE(binder.errors().empty());
    EXPECT_NE(binder.errors()[0].find("unknown action 'addScre'"), std::string::npos)
        << binder.errors()[0];
    EXPECT_NE(binder.errors()[0].find("addScore"), std::string::npos) << binder.errors()[0];
}

TEST(UIBindActions, AnUnknownEventNameFailsTheLoad) {
    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Button name="b" on-hover="x"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown event 'hover'"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("pointer-enter"), std::string::npos)
        << "must list the events that do exist";
}

// A reload rebuilds the tree and its listeners, so a bound action must come
// back by itself — that is the whole reason to prefer it over an OnClick the
// app re-attaches.
TEST(UIBindActions, ABoundActionSurvivesAReloadWithNoReAttach) {
    const std::string markup = "test_ui_action.cxml";
    writeFileAt(markup, R"(<UI data-source="s">
        <Element name="b" style="width: 50px; height: 50px" on-click="go"/></UI>)", 0);

    UIDataSource s;
    int calls = 0;
    s.AddAction("go", [&] { ++calls; });

    UIAssetDocument assets;
    assets.bindingContext().RegisterSource("s", &s);
    ASSERT_TRUE(assets.Load(markup, ""));
    ASSERT_TRUE(assets.binder().ok()) << assets.binder().errors()[0];

    assets.document().Layout(200.f, 200.f);
    clickAt(assets.document(), 10.f, 10.f);
    ASSERT_EQ(calls, 1);

    assets.SetPollInterval(0.0f);
    writeFileAt(markup, R"(<UI data-source="s">
        <Element name="b" style="width: 60px; height: 60px" on-click="go"/></UI>)", 2);
    ASSERT_TRUE(assets.Update(0.0f));

    assets.document().Layout(200.f, 200.f);
    clickAt(assets.document(), 10.f, 10.f);
    EXPECT_EQ(calls, 2) << "the bound action did not survive the reload";
    std::remove(markup.c_str());
}

// ------------------------------------------------- the whole thing, on disk

// The property that justifies the entire design: a hot reload rebuilds every
// element, and the model is not in the tree, so no value is lost and the app
// re-pushes nothing.
TEST(UIBindingHotReload, ValuesSurviveAReloadWithNoRePush) {
    const std::string markup = "test_ui_binding.cxml";
    writeFileAt(markup, R"(<UI data-source="hud"><Label name="l" text="SCORE {score}"/></UI>)", 0);

    UIDataSource src;
    src.SetInt("score", 4242);

    UIAssetDocument assets;
    assets.bindingContext().RegisterSource("hud", &src);
    int binds = 0;
    ASSERT_TRUE(assets.Load(markup, "", [&](UIDocument&) { ++binds; }));
    EXPECT_TRUE(assets.binder().ok()) << (assets.errors().empty() ? "" : assets.errors()[0]);
    ASSERT_NE(assets.document().root().Find("l"), nullptr);
    EXPECT_EQ(assets.document().root().Find("l")->style().text, "SCORE 4242");

    // Edit the file: different literal, same binding. Nothing in C++ changes.
    assets.SetPollInterval(0.0f);
    writeFileAt(markup, R"(<UI data-source="hud"><Label name="l" text="PTS {score}"/></UI>)", 2);
    ASSERT_TRUE(assets.Update(0.0f));

    UIElement* l = assets.document().root().Find("l");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->style().text, "PTS 4242")
        << "the reload lost the value, or the app had to re-push it";
    EXPECT_EQ(binds, 2) << "the bind callback should still run, it just has less to do";

    // And the rebuilt tree is live, not a snapshot.
    src.SetInt("score", 1);
    assets.binder().UpdateToTarget();
    EXPECT_EQ(l->style().text, "PTS 1");
    std::remove(markup.c_str());
}

// A half-typed file must not silently UNBIND a working UI any more than it may
// blank one.
TEST(UIBindingHotReload, ABrokenEditKeepsTheLastGoodTreeStillBound) {
    const std::string markup = "test_ui_binding_bad.cxml";
    writeFileAt(markup, R"(<UI data-source="hud"><Label name="l" text="{score}"/></UI>)", 0);

    UIDataSource src;
    src.SetInt("score", 1);
    UIAssetDocument assets;
    assets.bindingContext().RegisterSource("hud", &src);
    ASSERT_TRUE(assets.Load(markup, ""));
    assets.SetPollInterval(0.0f);

    writeFileAt(markup, R"(<UI><Label name="l" text="{score)", 2);   // unterminated
    ASSERT_FALSE(assets.Update(0.0f));

    // Still there, and STILL BOUND.
    UIElement* l = assets.document().root().Find("l");
    ASSERT_NE(l, nullptr) << "a broken edit destroyed the running UI";
    src.SetInt("score", 99);
    assets.binder().UpdateToTarget();
    EXPECT_EQ(l->style().text, "99") << "a broken edit silently unbound a working UI";
    std::remove(markup.c_str());
}

// Through the SHIPPED path — a scene entity and a UIWorld — not a hand-built
// document. That is what makes this catch markup naming a converter or a
// property the shipped C++ forgot to register.
TEST(UIBindingHotReload, TheShippedHudResolvesEveryBinding) {
    ShippedHud hud;
    hud.data().SetInt("score", 55);
    hud.data().SetNumber("health", 0.4f);
    hud.Frame();

    ASSERT_NE(hud.assets(), nullptr) << "the shipped HUD did not load";
    ASSERT_TRUE(hud.assets()->binder().ok())
        << "shipped HUD has an unresolved binding: "
        << (hud.assets()->binder().errors().empty() ? ""
                                                    : hud.assets()->binder().errors()[0]);

    UIElement* label = hud.find("scoreLabel");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->style().text, "SCORE 55");

    // Both halves of the health bar are authored: the unit comes from the
    // `percent` builtin and the colour from the demo's healthTint converter,
    // registered on the WORLD so every document in the scene can use it.
    UIElement* fill = hud.find("healthFill");
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(fill->style().width.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(fill->style().width.value, 40.0f);
    EXPECT_NEAR(fill->style().backgroundColor.g, 0.22f + 0.6f * 0.45f, 0.001f);
}

// The showcase's multi-line field. A two-way binding writes the field with
// `setValue`, which CLEARS the undo history — so if the round trip ever wrote
// back a value the user already has, undo would quietly die on every keystroke
// in the shipped HUD. This is the test that would catch that.
TEST(UIBindingHotReload, TheShippedMultilineFieldEditsAndUndoes) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr);

    UIElement* notes = hud.find("notes");
    ASSERT_NE(notes, nullptr) << "the showcase lost its multi-line field";
    UITextEdit* ed = notes->textEdit();
    ASSERT_NE(ed, nullptr);
    EXPECT_TRUE(ed->multiline());
    EXPECT_NE(ed->value().find('\n'), std::string::npos)
        << "the seeded notes should arrive with their line break intact";

    hud.doc().SetFocus(notes);
    ed->MoveToEnd(false);
    UIKeyboardState kb;
    kb.text = "abc";
    hud.doc().UpdateKeyboard(kb);
    hud.Frame();
    ASSERT_NE(ed->value().find("abc"), std::string::npos);
    EXPECT_NE(hud.data().GetString("notes").find("abc"), std::string::npos)
        << "the field never reached the source";

    // Several frames of the round trip settling, then undo must still be there.
    hud.Frame(); hud.Frame();
    ASSERT_TRUE(ed->canUndo()) << "the source -> field half wiped the undo history";
    EXPECT_TRUE(ed->Undo());
    EXPECT_EQ(ed->value().find("abc"), std::string::npos);
    hud.Frame();
    EXPECT_EQ(hud.data().GetString("notes"), ed->value())
        << "the undone value never reached the source";
}

// Binding a property makes its stylesheet rule dead. That is a legitimate
// pattern — the rule is the pre-bind default — but finding out by editing the
// .cstyle and watching nothing happen is the silent no-op this codebase reports
// errors to avoid.
TEST(UIBindingHotReload, AShadowedStylesheetDeclarationIsNoted) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr);

    bool sawWidth = false;
    for (const auto& n : hud.assets()->binder().notes()) {
        if (n.find("'width'") != std::string::npos &&
            n.find("healthFill") != std::string::npos) sawWidth = true;
    }
    EXPECT_TRUE(sawWidth) << "hud.cstyle declares .fill { width } and hud.cxml binds width; "
                             "that has to be reported, not silent";
}

// A bound action must fire ONCE per click, however many times the binder has
// re-collected. The binder attaches its action lambdas through the ordinary
// listener path and Rebuild never removes them, so every re-collect used to
// append another copy — and the structure epoch is process-wide, so ANY tree
// mutation anywhere in the process (another document's hot reload, gameplay
// adding a child) triggers one. Ten saves during an iteration session turned
// the sample's +100 button into +1100.
TEST(UIBindActions, ARecollectDoesNotDuplicateTheHandler) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr);
    hud.data().SetInt("score", 0);
    hud.Frame();

    UIElement* btn = hud.find("scoreButton");
    ASSERT_NE(btn, nullptr);
    const glm::vec2 c = btn->layout().position + btn->layout().size * 0.5f;

    hud.ClickAt(c.x, c.y);
    ASSERT_EQ(hud.data().GetInt("score"), 100) << "the action never fired";

    // Something else in the process restructures its own tree. That is all it
    // takes: the epoch is global, so this document re-collects.
    for (int i = 0; i < 3; ++i) {
        UIDocument churn;
        churn.root().AddChild("x");
        churn.root().ClearChildren();
        hud.Frame();
    }

    hud.ClickAt(c.x, c.y);
    EXPECT_EQ(hud.data().GetInt("score"), 200)
        << "the bound action fired more than once — the binder duplicated its listener";
}
