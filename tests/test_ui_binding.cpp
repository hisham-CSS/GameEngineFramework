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
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
                                        text + "\"/></UI>", errors, "t.uxml"))
        << (errors.empty() ? "" : errors[0]);
    (void)label;
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
    UIElement* l = doc.root().Find("l");
    return l ? l->style().text : std::string("<missing>");
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
        errors, "t.uxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource s;
    s.SetInt("score", 0);
    UIBindingContext ctx;
    ctx.RegisterSource("hud", &s);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
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
                                   errors, "t.uxml"));
    UIDataSource s;
    s.SetInt("n", 1);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");

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
                                   errors, "t.uxml"));
    UIDataSource s;
    s.SetInt("n", 1);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");

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
        </UI>)", errors, "t.uxml")) << (errors.empty() ? "" : errors[0]);

    UIDataSource a, b;
    a.SetString("v", "from-a");
    b.SetString("v", "from-b");
    UIBindingContext ctx;
    ctx.RegisterSource("a", &a);
    ctx.RegisterSource("b", &b);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
    EXPECT_TRUE(binder.ok()) << (binder.errors().empty() ? "" : binder.errors()[0]);
    EXPECT_EQ(doc.root().Find("deep")->style().text, "from-a");
    // A nested data-source overrides for its own subtree, like UI Toolkit.
    EXPECT_EQ(doc.root().Find("deep2")->style().text, "from-b");
}

TEST(UIBinding, QualifiedPathBeatsTheInheritedSource) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="a"><Label name="l" text="{b.v}"/></UI>)", errors, "t.uxml"));
    UIDataSource a, b;
    a.SetString("v", "wrong");
    b.SetString("v", "right");
    UIBindingContext ctx;
    ctx.RegisterSource("a", &a);
    ctx.RegisterSource("b", &b);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
    EXPECT_EQ(doc.root().Find("l")->style().text, "right");
}

// Without reflection this is the highest-value diagnostic in the system: a
// misspelt path is otherwise indistinguishable from a value that never changes.
TEST(UIBinding, AMisspeltPathIsReportedWithTheNamesThatExist) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(
        doc, R"(<UI data-source="hud"><Label name="scoreLabel" text="SCORE {scoer}"/></UI>)",
        errors, "t.uxml"));
    UIDataSource s;
    s.SetInt("score", 0);
    s.SetNumber("health", 1.f);
    UIBindingContext ctx;
    ctx.RegisterSource("hud", &s);

    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
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
                                       errors, "t.uxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.uxml");
        ASSERT_FALSE(b.errors().empty());
        EXPECT_NE(b.errors()[0].find("unknown data source 'missing'"), std::string::npos)
            << b.errors()[0];
        EXPECT_NE(b.errors()[0].find("hud"), std::string::npos) << "must list what IS registered";
    }
    {
        UIDocument doc;
        std::vector<std::string> errors;
        ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="hud"><Label name="l" text="{n|pct}"/></UI>)",
                                       errors, "t.uxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.uxml");
        ASSERT_FALSE(b.errors().empty());
        EXPECT_NE(b.errors()[0].find("unknown converter 'pct'"), std::string::npos) << b.errors()[0];
        EXPECT_NE(b.errors()[0].find("percent"), std::string::npos) << "must list the real ones";
    }
    {
        // No data-source anywhere: say what to DO about it, not just what failed.
        UIDocument doc;
        std::vector<std::string> errors;
        ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Label name="l" text="{n}"/></UI>)",
                                       errors, "t.uxml"));
        UIBinder b;
        b.Rebuild(doc, ctx, "t.uxml");
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
                                   errors, "t.uxml"));
    UIBindingContext ctx;
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
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
        </UI>)", errors, "t.uxml"));
    UIDataSource s;
    s.SetString("v", "x");
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");
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
                                   errors, "t.uxml"));
    UIDataSource s;
    s.SetInt("v", 7);
    UIBindingContext ctx;
    ctx.RegisterSource("h", &s);
    UIBinder binder;
    binder.Rebuild(doc, ctx, "t.uxml");

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
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI><Element name="good"/></UI>)", ok, "t.uxml"));

    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><Element nmae="healthFill"/></UI>)",
                                    errors, "t.uxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("unknown attribute 'nmae'"), std::string::npos) << errors[0];
    EXPECT_NE(doc.root().Find("good"), nullptr) << "a bad attribute destroyed the running UI";
}

// The root used to be handled by a SEPARATE block, so every new attribute
// family had to be added twice and the two silently diverged.
TEST(UIMarkupAttributes, TheRootIsValidatedExactlyLikeAChild) {
    UIDocument doc;
    std::vector<std::string> ok;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI name="hud"><Element name="good"/></UI>)", ok, "t.uxml"));

    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI data-sorce="hud"><Element name="x"/></UI>)",
                                    errors, "t.uxml"));
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
        doc, R"(<UI><Label name="scoreLabel" text="SCORE {score"/></UI>)", errors, "t.uxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("scoreLabel"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("unterminated"), std::string::npos) << errors[0];
}

TEST(UIMarkupAttributes, ReloadingReplacesDataSourceAndBindingsRatherThanAccumulating) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI data-source="a" text="{v}"/>)", errors, "t.uxml"));
    EXPECT_EQ(doc.root().dataSourceName(), "a");
    EXPECT_EQ(doc.root().bindings().size(), 1u);

    ASSERT_TRUE(UIMarkup::LoadInto(doc, R"(<UI/>)", errors, "t.uxml"));
    EXPECT_TRUE(doc.root().dataSourceName().empty()) << "a stale data-source survived a reload";
    EXPECT_TRUE(doc.root().bindings().empty()) << "a stale binding survived a reload";
    EXPECT_TRUE(doc.root().style().text.empty()) << "stale text survived a reload";
}

// ------------------------------------------------- the whole thing, on disk

// The property that justifies the entire design: a hot reload rebuilds every
// element, and the model is not in the tree, so no value is lost and the app
// re-pushes nothing.
TEST(UIBindingHotReload, ValuesSurviveAReloadWithNoRePush) {
    const std::string markup = "test_ui_binding.uxml";
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
    const std::string markup = "test_ui_binding_bad.uxml";
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

TEST(UIBindingHotReload, TheShippedHudBindsItsScore) {
    UIDataSource src;
    src.SetInt("score", 55);
    src.SetNumber("health", 1.0f);

    UIAssetDocument assets;
    assets.bindingContext().RegisterSource("hud", &src);
    ASSERT_TRUE(assets.Load("Exported/UI/hud.uxml", "Exported/UI/hud.uss"));
    EXPECT_TRUE(assets.binder().ok())
        << "shipped hud.uxml has an unresolved binding: "
        << (assets.binder().errors().empty() ? "" : assets.binder().errors()[0]);

    UIElement* label = assets.document().root().Find("scoreLabel");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->style().text, "SCORE 55");
}
