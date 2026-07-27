// UIValue: the transport type between a game's data and its UI.
//
// Pure CPU. The behaviour worth pinning down is almost entirely about REFUSAL:
// a binding system with no reflection cannot recover from a value that
// converted to something plausible but wrong, because the symptom is a UI that
// simply looks wrong with nothing logged. So every coercion that is not
// obviously correct must return false and let the caller name both kinds.
//
// The other half is non-finite values. %g prints NaN as "nan" and strtod
// accepts "nan", so a NaN can round-trip through a string and arrive at the
// layout engine as a perfectly valid-looking length. That round trip is closed
// here and in the stylesheet's own number parser.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIValue.h"
#include "../Engine/src/ui/UIStyleSheet.h"

#include <cmath>
#include <limits>
#include <string>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

TEST(UIValue, CarriesEachKindAndNamesIt) {
    EXPECT_EQ(std::string(UIValue::Bool(true).KindName()), "bool");
    EXPECT_EQ(std::string(UIValue::Int(7).KindName()), "int");
    EXPECT_EQ(std::string(UIValue::Number(1.5f).KindName()), "number");
    EXPECT_EQ(std::string(UIValue::Len(StyleLength::Pct(50.f)).KindName()), "length");
    EXPECT_EQ(std::string(UIValue::Color4({ 1, 0, 0, 1 }).KindName()), "colour");
    EXPECT_EQ(std::string(UIValue::Str("hi").KindName()), "string");
    EXPECT_EQ(std::string(UIValue{}.KindName()), "nothing");
}

TEST(UIValue, NumericCoercionsThatAreObviouslyCorrect) {
    float f = 0.f;
    long long i = 0;

    EXPECT_TRUE(UIValue::Int(42).AsNumber(f));   EXPECT_FLOAT_EQ(f, 42.f);
    EXPECT_TRUE(UIValue::Number(1.9f).AsInt(i)); EXPECT_EQ(i, 1) << "must truncate like a cast";
    EXPECT_TRUE(UIValue::Bool(true).AsNumber(f)); EXPECT_FLOAT_EQ(f, 1.f);

    bool b = true;
    EXPECT_TRUE(UIValue::Number(0.f).AsBool(b)); EXPECT_FALSE(b);
    EXPECT_TRUE(UIValue::Int(3).AsBool(b));      EXPECT_TRUE(b);
}

// A string is NOT a bool. "false" and "0" are truthy under the obvious rule and
// falsy under the obvious-to-someone-else rule, and a visibility toggle that
// silently picks the wrong one is worse than a message naming the kind.
TEST(UIValue, StringIsNeverABool) {
    bool b = false;
    EXPECT_FALSE(UIValue::Str("true").AsBool(b));
    EXPECT_FALSE(UIValue::Str("false").AsBool(b));
    EXPECT_FALSE(UIValue::Str("0").AsBool(b));
}

TEST(UIValue, ColourAndLengthNeverCoerceToEachOther) {
    float f = 0.f;
    glm::vec4 c{ 0.f };
    StyleLength l;
    EXPECT_FALSE(UIValue::Color4({ 1, 0, 0, 1 }).AsNumber(f));
    EXPECT_FALSE(UIValue::Color4({ 1, 0, 0, 1 }).AsLength(l));
    EXPECT_FALSE(UIValue::Number(5.f).AsColor(c));
    EXPECT_FALSE(UIValue::Len(StyleLength::Px(5.f)).AsColor(c));
}

// A percentage has no numeric value without the parent it is a percentage of,
// and "auto" has none at all — so neither reads as a bare number.
TEST(UIValue, OnlyAPointLengthReadsAsANumber) {
    float f = -1.f;
    EXPECT_TRUE(UIValue::Len(StyleLength::Px(12.f)).AsNumber(f));
    EXPECT_FLOAT_EQ(f, 12.f);
    EXPECT_FALSE(UIValue::Len(StyleLength::Pct(50.f)).AsNumber(f));
    EXPECT_FALSE(UIValue::Len(StyleLength::Auto()).AsNumber(f));
}

// Strings route through the STYLESHEET's parsers, so authored text means the
// same thing in a bound value as it does in a .cstyle declaration. Two parsers
// that agree today drift the first time one learns a new unit.
TEST(UIValue, StringsParseWithTheStylesheetGrammar) {
    StyleLength l;
    ASSERT_TRUE(UIValue::Str("50%").AsLength(l));
    EXPECT_EQ(l.unit, StyleLength::Unit::Percent);
    EXPECT_FLOAT_EQ(l.value, 50.f);

    ASSERT_TRUE(UIValue::Str("auto").AsLength(l));
    EXPECT_EQ(l.unit, StyleLength::Unit::Auto);

    ASSERT_TRUE(UIValue::Str("18px").AsLength(l));
    EXPECT_EQ(l.unit, StyleLength::Unit::Point);
    EXPECT_FLOAT_EQ(l.value, 18.f);

    glm::vec4 c{ 0.f };
    ASSERT_TRUE(UIValue::Str("#d93a3d").AsColor(c));
    EXPECT_NEAR(c.r, 0.851f, 0.01f);
    ASSERT_TRUE(UIValue::Str("rgba(0, 0, 0, 0.45)").AsColor(c));
    EXPECT_FLOAT_EQ(c.a, 0.45f);

    EXPECT_FALSE(UIValue::Str("potato").AsLength(l));
    EXPECT_FALSE(UIValue::Str("potato").AsColor(c));
}

// A bare number is pixels, exactly as it is in a stylesheet.
TEST(UIValue, BareNumberBecomesPixels) {
    StyleLength l;
    ASSERT_TRUE(UIValue::Number(220.f).AsLength(l));
    EXPECT_EQ(l.unit, StyleLength::Unit::Point);
    EXPECT_FLOAT_EQ(l.value, 220.f);
}

TEST(UIValue, DisplayStringReadsLikeATemplateNotLikeCpp) {
    // The single most common bound value in any HUD.
    EXPECT_EQ(UIValue::Int(1234).ToDisplayString(), "1234");
    // Not "100.000000", which is most of the difference between a template and
    // a debug print.
    EXPECT_EQ(UIValue::Number(100.f).ToDisplayString(), "100");
    EXPECT_EQ(UIValue::Number(0.5f).ToDisplayString(), "0.5");
    EXPECT_EQ(UIValue::Number(87.6543f).ToDisplayString(0), "88");
    EXPECT_EQ(UIValue::Number(87.6543f).ToDisplayString(2), "87.65");
    EXPECT_EQ(UIValue::Bool(true).ToDisplayString(), "true");
    EXPECT_EQ(UIValue::Str("SCORE").ToDisplayString(), "SCORE");
    EXPECT_EQ(UIValue::Len(StyleLength::Pct(37.f)).ToDisplayString(), "37%");
    EXPECT_EQ(UIValue::Len(StyleLength::Px(18.f)).ToDisplayString(), "18px");
    EXPECT_EQ(UIValue::Len(StyleLength::Auto()).ToDisplayString(), "auto");
}

// A length printed by one half of the system must be readable by the other, or
// a value that survives a round trip through text quietly changes meaning.
TEST(UIValue, LengthAndColourRoundTripThroughTheirDisplayString) {
    for (StyleLength in : { StyleLength::Px(18.f), StyleLength::Pct(37.5f),
                            StyleLength::Auto() }) {
        StyleLength out;
        ASSERT_TRUE(UIValue::Str(UIValue::Len(in).ToDisplayString()).AsLength(out))
            << UIValue::Len(in).ToDisplayString();
        EXPECT_EQ(out.unit, in.unit);
        EXPECT_FLOAT_EQ(out.value, in.value);
    }

    const glm::vec4 c{ 0.2f, 0.4f, 0.6f, 0.45f };
    glm::vec4 back{ 0.f };
    ASSERT_TRUE(UIValue::Str(UIValue::Color4(c).ToDisplayString()).AsColor(back));
    EXPECT_NEAR(back.r, c.r, 0.005f);
    EXPECT_NEAR(back.g, c.g, 0.005f);
    EXPECT_NEAR(back.b, c.b, 0.005f);
    EXPECT_FLOAT_EQ(back.a, c.a);
}

// The important one. A NaN that is not caught BEFORE stringification prints as
// "nan", parses back as a valid number, and reaches yoga — where it makes the
// entire layout undefined with nothing logged anywhere.
TEST(UIValue, RejectsNonFiniteValues) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::string why;

    EXPECT_FALSE(UIValue::Number(nan).IsFinite(why));
    EXPECT_NE(why.find("nan"), std::string::npos) << why;
    EXPECT_FALSE(UIValue::Number(inf).IsFinite(why));
    EXPECT_FALSE(UIValue::Len(StyleLength::Px(nan)).IsFinite(why));
    EXPECT_FALSE(UIValue::Color4({ 1.f, nan, 0.f, 1.f }).IsFinite(why));

    EXPECT_TRUE(UIValue::Number(0.f).IsFinite(why));
    EXPECT_TRUE(UIValue::Int(0).IsFinite(why));
    EXPECT_TRUE(UIValue::Str("nan").IsFinite(why)) << "a string is just a string here";
}

// The other end of the same hole, and a latent bug in shipped code: strtod is
// C99-mandated to accept "nan" and "inf", so `width: nan` in a .cstyle used to
// parse successfully.
TEST(UIValue, StylesheetNumberParserRejectsNanAndInf) {
    float f = 0.f;
    StyleLength l;
    EXPECT_FALSE(UIStyleSheet::ParseNumberValue("nan", f));
    EXPECT_FALSE(UIStyleSheet::ParseNumberValue("inf", f));
    EXPECT_FALSE(UIStyleSheet::ParseNumberValue("-infinity", f));
    EXPECT_FALSE(UIStyleSheet::ParseLengthValue("nan", l));
    EXPECT_FALSE(UIStyleSheet::ParseLengthValue("infpx", l));
    EXPECT_FALSE(UIStyleSheet::ParseLengthValue("nan%", l));

    // Still parses everything it always did.
    EXPECT_TRUE(UIStyleSheet::ParseNumberValue("-1.5", f));
    EXPECT_FLOAT_EQ(f, -1.5f);
}

TEST(UIValue, StylesheetRefusesANonFiniteLengthEndToEnd) {
    UIStyleSheet sheet;
    EXPECT_FALSE(sheet.ParseString(".x { width: nan; }", "t.cstyle"));
    ASSERT_FALSE(sheet.errors().empty());
    EXPECT_NE(sheet.errors()[0].find("bad length"), std::string::npos) << sheet.errors()[0];
}

// Equality drives change detection, so a kind change must never compare equal
// to the value it replaced — otherwise a genuine type change goes unnoticed.
TEST(UIValue, EqualityIsKindSensitive) {
    EXPECT_EQ(UIValue::Int(1), UIValue::Int(1));
    EXPECT_NE(UIValue::Int(1), UIValue::Number(1.0f));
    EXPECT_NE(UIValue::Int(1), UIValue::Bool(true));
    EXPECT_NE(UIValue::Str("1"), UIValue::Int(1));
    EXPECT_EQ(UIValue::Str("a"), UIValue::Str("a"));
    EXPECT_NE(UIValue::Str("a"), UIValue::Str("b"));
    EXPECT_EQ(UIValue{}, UIValue{});
}
