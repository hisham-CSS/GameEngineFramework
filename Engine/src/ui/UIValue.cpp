#include "UIValue.h"

#include "UIStyleSheet.h"

#include <cmath>
#include <cstdio>

namespace MyCoreEngine::ui {

UIValue UIValue::Bool(bool v)   { UIValue x; x.kind = Kind::Bool;   x.boolean = v; return x; }
UIValue UIValue::Int(long long v) { UIValue x; x.kind = Kind::Int;  x.integer = v; return x; }
UIValue UIValue::Number(float v){ UIValue x; x.kind = Kind::Number; x.number = v;  return x; }
UIValue UIValue::Len(const StyleLength& v) { UIValue x; x.kind = Kind::Length; x.length = v; return x; }
UIValue UIValue::Color4(const glm::vec4& v){ UIValue x; x.kind = Kind::Color;  x.color = v;  return x; }
UIValue UIValue::Str(std::string v) { UIValue x; x.kind = Kind::String; x.text = std::move(v); return x; }

const char* UIValue::KindName() const {
    switch (kind) {
    case Kind::None:   return "nothing";
    case Kind::Bool:   return "bool";
    case Kind::Int:    return "int";
    case Kind::Number: return "number";
    case Kind::Length: return "length";
    case Kind::Color:  return "colour";
    case Kind::String: return "string";
    }
    return "?";
}

// A number is true when it is non-zero, matching every scripting language a
// game author is likely to have used. A STRING is deliberately not accepted:
// "false" and "0" are both truthy under the obvious rule and falsy under the
// obvious-to-someone-else rule, and a visibility toggle that silently picks the
// wrong one is worse than an error naming the kind.
bool UIValue::AsBool(bool& out) const {
    switch (kind) {
    case Kind::Bool:   out = boolean; return true;
    case Kind::Int:    out = integer != 0; return true;
    case Kind::Number: out = number != 0.0f; return true;
    case Kind::None:
    case Kind::Length:
    case Kind::Color:
    case Kind::String: return false;
    }
    return false;
}

bool UIValue::AsNumber(float& out) const {
    switch (kind) {
    case Kind::Number: out = number; return true;
    case Kind::Int:    out = float(integer); return true;
    case Kind::Bool:   out = boolean ? 1.0f : 0.0f; return true;
    // A length only reads as a plain number when it carries one: "auto" has no
    // numeric value at all, and 50% means nothing without the parent it is a
    // percentage of.
    case Kind::Length: if (length.unit != StyleLength::Unit::Point) return false;
                       out = length.value; return true;
    case Kind::String: return UIStyleSheet::ParseNumberValue(text, out);
    case Kind::None:
    case Kind::Color:  return false;
    }
    return false;
}

bool UIValue::AsInt(long long& out) const {
    switch (kind) {
    case Kind::Int:  out = integer; return true;
    case Kind::Bool: out = boolean ? 1 : 0; return true;
    // Truncates toward zero, like a C++ cast. Rejecting a fractional number
    // here would make {health | percent | int} — the obvious way to write a
    // whole-number percentage — an error.
    case Kind::Number: if (!std::isfinite(number)) return false;
                       out = (long long)number; return true;
    case Kind::Length: if (length.unit != StyleLength::Unit::Point ||
                           !std::isfinite(length.value)) return false;
                       out = (long long)length.value; return true;
    case Kind::String: { float f = 0.0f;
                         if (!UIStyleSheet::ParseNumberValue(text, f)) return false;
                         out = (long long)f; return true; }
    case Kind::None:
    case Kind::Color:  return false;
    }
    return false;
}

bool UIValue::AsLength(StyleLength& out) const {
    switch (kind) {
    case Kind::Length: out = length; return true;
    // A bare number is pixels, matching the stylesheet's own rule for an
    // unsuffixed value.
    case Kind::Number: if (!std::isfinite(number)) return false;
                       out = StyleLength::Px(number); return true;
    case Kind::Int:    out = StyleLength::Px(float(integer)); return true;
    case Kind::String: return UIStyleSheet::ParseLengthValue(text, out);
    case Kind::None:
    case Kind::Bool:
    case Kind::Color:  return false;
    }
    return false;
}

bool UIValue::AsColor(glm::vec4& out) const {
    switch (kind) {
    case Kind::Color:  out = color; return true;
    case Kind::String: return UIStyleSheet::ParseColorValue(text, out);
    case Kind::None:
    case Kind::Bool:
    case Kind::Int:
    case Kind::Number:
    case Kind::Length: return false;
    }
    return false;
}

namespace {
    // %g, but never in scientific notation for values a UI actually shows, and
    // never with a trailing ".000000". snprintf into a stack buffer rather than
    // std::to_string, which allocates and always prints six decimals.
    void formatNumber(char* buf, size_t n, double v, int decimals) {
        if (decimals >= 0) {
            if (decimals > 9) decimals = 9;
            std::snprintf(buf, n, "%.*f", decimals, v);
        } else {
            std::snprintf(buf, n, "%g", v);
        }
    }
} // namespace

std::string UIValue::ToDisplayString(int decimals) const {
    char buf[64];
    switch (kind) {
    case Kind::None:   return {};
    case Kind::Bool:   return boolean ? "true" : "false";
    case Kind::Int:
        if (decimals >= 0) { formatNumber(buf, sizeof(buf), double(integer), decimals); return buf; }
        std::snprintf(buf, sizeof(buf), "%lld", integer);
        return buf;
    case Kind::Number:
        formatNumber(buf, sizeof(buf), double(number), decimals);
        return buf;
    case Kind::Length:
        switch (length.unit) {
        case StyleLength::Unit::Auto:    return "auto";
        case StyleLength::Unit::Percent: formatNumber(buf, sizeof(buf), double(length.value), decimals);
                                         return std::string(buf) + "%";
        case StyleLength::Unit::Point:   formatNumber(buf, sizeof(buf), double(length.value), decimals);
                                         return std::string(buf) + "px";
        }
        return {};
    case Kind::Color:
        // The .cstyle spelling, so a colour that appears in a label and a colour
        // that appears in a stylesheet read identically. Channels are 0-255 and
        // alpha is 0-1, matching parseColor.
        std::snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %g)",
                      int(color.r * 255.0f + 0.5f), int(color.g * 255.0f + 0.5f),
                      int(color.b * 255.0f + 0.5f), double(color.a));
        return buf;
    case Kind::String: return text;
    }
    return {};
}

bool UIValue::IsFinite(std::string& why) const {
    auto bad = [&why](float v, const char* what) {
        if (std::isfinite(v)) return false;
        why = std::string("value is not finite (") + (std::isnan(v) ? "nan" : "inf") +
              ") in " + what;
        return true;
    };
    switch (kind) {
    case Kind::Number: return !bad(number, "a number");
    case Kind::Length: return !bad(length.value, "a length");
    case Kind::Color:  return !(bad(color.r, "a colour") || bad(color.g, "a colour") ||
                                bad(color.b, "a colour") || bad(color.a, "a colour"));
    case Kind::None:
    case Kind::Bool:
    case Kind::Int:
    case Kind::String: return true;
    }
    return true;
}

bool UIValue::operator==(const UIValue& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
    case Kind::None:   return true;
    case Kind::Bool:   return boolean == o.boolean;
    case Kind::Int:    return integer == o.integer;
    case Kind::Number: return number == o.number;
    case Kind::Length: return length == o.length;
    case Kind::Color:  return color == o.color;
    case Kind::String: return text == o.text;
    }
    return false;
}

} // namespace MyCoreEngine::ui
