#include "UIStyleSheet.h"
#include "UIAssetPath.h"
#include "UIElement.h"
#include "../core/PathSandbox.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace MyCoreEngine::ui {

namespace {

    std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> out;
        std::string cur;
        std::istringstream in(s);
        while (std::getline(in, cur, sep)) {
            const std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
        }
        return out;
    }

    // Whitespace-separated tokens (for shorthand values like "8px 14px").
    std::vector<std::string> tokens(const std::string& s) {
        std::vector<std::string> out;
        std::istringstream in(s);
        std::string t;
        while (in >> t) out.push_back(t);
        return out;
    }

    bool parseNumber(const std::string& s, float& out) {
        if (s.empty()) return false;
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str()) return false;
        while (end && *end && std::isspace((unsigned char)*end)) ++end;
        if (end && *end != '\0') return false; // trailing junk
        // strtod is C99-mandated to accept "nan", "inf" and "infinity", so
        // `width: nan` used to parse successfully and hand YGNodeStyleSetWidth
        // a NaN, which makes the whole layout undefined with nothing logged
        // anywhere. It is a bad number, so it is rejected like any other.
        if (!std::isfinite(v)) return false;
        out = float(v);
        return true;
    }

    // auto | Npx | N% | N  (a bare number is treated as px, like most game UIs)
    bool parseLength(const std::string& raw, StyleLength& out) {
        const std::string s = lower(trim(raw));
        if (s.empty()) return false;
        if (s == "auto") { out = StyleLength::Auto(); return true; }
        if (s.back() == '%') {
            float v = 0.f;
            if (!parseNumber(s.substr(0, s.size() - 1), v)) return false;
            out = StyleLength::Pct(v);
            return true;
        }
        std::string num = s;
        if (s.size() > 2 && s.compare(s.size() - 2, 2, "px") == 0) {
            num = s.substr(0, s.size() - 2);
        }
        float v = 0.f;
        if (!parseNumber(num, v)) return false;
        out = StyleLength::Px(v);
        return true;
    }

    bool hexDigit(char c, int& v) {
        if (c >= '0' && c <= '9') { v = c - '0'; return true; }
        c = (char)std::tolower((unsigned char)c);
        if (c >= 'a' && c <= 'f') { v = 10 + (c - 'a'); return true; }
        return false;
    }

    // #rgb | #rrggbb | #rrggbbaa | rgb(r,g,b) | rgba(r,g,b,a) | a few names.
    bool parseColor(const std::string& raw, glm::vec4& out) {
        const std::string s = lower(trim(raw));
        if (s.empty()) return false;

        static const std::unordered_map<std::string, glm::vec4> kNamed = {
            { "transparent", { 0, 0, 0, 0 } }, { "black", { 0, 0, 0, 1 } },
            { "white", { 1, 1, 1, 1 } },       { "red",   { 1, 0, 0, 1 } },
            { "green", { 0, 1, 0, 1 } },       { "blue",  { 0, 0, 1, 1 } },
            { "yellow", { 1, 1, 0, 1 } },      { "cyan",  { 0, 1, 1, 1 } },
            { "magenta", { 1, 0, 1, 1 } },     { "gray",  { 0.5f, 0.5f, 0.5f, 1 } },
            { "grey", { 0.5f, 0.5f, 0.5f, 1 } },
        };
        if (auto it = kNamed.find(s); it != kNamed.end()) { out = it->second; return true; }

        if (s[0] == '#') {
            const std::string h = s.substr(1);
            auto nib = [&](size_t i, int& v) { return hexDigit(h[i], v); };
            int r = 0, g = 0, b = 0, a = 15;
            if (h.size() == 3) {
                if (!nib(0, r) || !nib(1, g) || !nib(2, b)) return false;
                out = { r / 15.0f, g / 15.0f, b / 15.0f, 1.0f };
                return true;
            }
            if (h.size() == 6 || h.size() == 8) {
                int v[8]{};
                for (size_t i = 0; i < h.size(); ++i) if (!nib(i, v[i])) return false;
                const float rr = float(v[0] * 16 + v[1]) / 255.0f;
                const float gg = float(v[2] * 16 + v[3]) / 255.0f;
                const float bb = float(v[4] * 16 + v[5]) / 255.0f;
                const float aa = (h.size() == 8) ? float(v[6] * 16 + v[7]) / 255.0f : 1.0f;
                out = { rr, gg, bb, aa };
                return true;
            }
            (void)a;
            return false;
        }

        const bool isRgba = s.rfind("rgba(", 0) == 0;
        const bool isRgb = s.rfind("rgb(", 0) == 0;
        if (isRgb || isRgba) {
            const size_t open = s.find('(');
            const size_t close = s.rfind(')');
            if (close == std::string::npos || close < open) return false;
            const auto parts = split(s.substr(open + 1, close - open - 1), ',');
            if (parts.size() != (isRgba ? 4u : 3u)) return false;
            float c[4] = { 0, 0, 0, 1 };
            for (size_t i = 0; i < parts.size(); ++i) {
                if (!parseNumber(parts[i], c[i])) return false;
            }
            // Channels are 0..255, alpha 0..1 — the CSS convention.
            out = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, isRgba ? c[3] : 1.0f };
            return true;
        }
        return false;
    }

    // CSS edge shorthand: 1 = all, 2 = vertical horizontal,
    // 3 = top horizontal bottom, 4 = top right bottom left (clockwise).
    bool parseEdges(const std::string& raw, Edges& out) {
        const auto t = tokens(trim(raw));
        auto px = [](const std::string& s, float& v) {
            StyleLength l;
            if (!parseLength(s, l) || l.unit != StyleLength::Unit::Point) return false;
            v = l.value;
            return true;
        };
        float a = 0, b = 0, c = 0, d = 0;
        switch (t.size()) {
        case 1: if (!px(t[0], a)) return false; out = Edges::All(a); return true;
        case 2: if (!px(t[0], a) || !px(t[1], b)) return false;
                out = { b, a, b, a }; return true;  // {l,t,r,b} from vert,horiz
        case 3: if (!px(t[0], a) || !px(t[1], b) || !px(t[2], c)) return false;
                out = { b, a, b, c }; return true;
        case 4: if (!px(t[0], a) || !px(t[1], b) || !px(t[2], c) || !px(t[3], d)) return false;
                out = { d, a, b, c }; return true;  // css top right bottom left
        default: return false;
        }
    }

    struct EnumTable { const char* name; int value; };

    bool parseEnum(const std::string& raw, const std::vector<EnumTable>& table, int& out) {
        const std::string s = lower(trim(raw));
        for (const auto& e : table) if (s == e.name) { out = e.value; return true; }
        return false;
    }

    const std::vector<EnumTable> kDirection = {
        { "row", (int)FlexDirection::Row }, { "column", (int)FlexDirection::Column },
        { "row-reverse", (int)FlexDirection::RowReverse },
        { "column-reverse", (int)FlexDirection::ColumnReverse },
    };
    const std::vector<EnumTable> kJustify = {
        { "flex-start", (int)Justify::FlexStart }, { "center", (int)Justify::Center },
        { "flex-end", (int)Justify::FlexEnd }, { "space-between", (int)Justify::SpaceBetween },
        { "space-around", (int)Justify::SpaceAround }, { "space-evenly", (int)Justify::SpaceEvenly },
    };
    const std::vector<EnumTable> kAlign = {
        { "auto", (int)Align::Auto }, { "flex-start", (int)Align::FlexStart },
        { "center", (int)Align::Center }, { "flex-end", (int)Align::FlexEnd },
        { "stretch", (int)Align::Stretch },
    };
    const std::vector<EnumTable> kPosition = {
        { "relative", (int)PositionType::Relative },
        { "absolute", (int)PositionType::Absolute },
    };
    const std::vector<EnumTable> kOverflow = {
        { "visible", (int)Overflow::Visible },
        { "hidden",  (int)Overflow::Hidden  },
        { "scroll",  (int)Overflow::Scroll  },
        // No "auto" — see the Overflow comment in UIStyle.h. It is reserved and
        // reported, not silently aliased onto `scroll`; an always-painted bar
        // is `scrollbar-visibility: always`, which is what CSS `scroll` adds
        // over `auto` and is a property of the bar rather than of overflow.
    };
    const std::vector<EnumTable> kScrollbarVis = {
        { "auto",   (int)ScrollbarVisibility::Auto },
        { "always", (int)ScrollbarVisibility::Always },
    };
    const std::vector<EnumTable> kScrollBehavior = {
        { "instant", (int)ScrollBehavior::Instant },
        { "smooth",  (int)ScrollBehavior::Smooth },
        // "auto" is CSS's spelling of instant. Accepted here because unlike the
        // overflow case it is not ambiguous with anything: there is no third
        // behaviour it could mean.
        { "auto",    (int)ScrollBehavior::Instant },
    };

    using Prop = UIDeclaration::Prop;

    // THE property vocabulary. One table, three consumers: the declaration
    // parser (.cstyle and style=), the binding parser (bind=), and the diagnostic
    // that reports a shadowed declaration.
    //
    // It used to be a 25-way if-chain inside the parser, which meant a binding
    // needed its own copy of the same knowledge — a fourth hand-maintained
    // list, with nothing to keep it honest. Adding a property here is now the
    // only edit, and the /we4062 build flag makes forgetting the matching
    // ApplyTo case a compile error.
    struct PropSpec {
        const char* name;
        Prop prop;
        UIPropValueKind kind;
        const std::vector<EnumTable>* enumTable;  // Enum only
        const char* trueWord;                     // Boolean only
        const char* falseWord;                    // Boolean only
        // The longhands a shorthand writes. Both equal to `prop` when the
        // property is not a shorthand, so PropsOverlap needs no special case.
        Prop expandsTo[2];
        // Length only: reject `auto`, percentages and negatives. A scrollbar
        // cannot be a percentage of anything meaningful, and Length otherwise
        // validates neither unit nor sign.
        bool nonNegativePx;
    };

    // Adding a property is ONE edit here. The two trailing fields are the
    // shorthand expansion (equal to the property itself when it is not a
    // shorthand) and whether a Length must be a non-negative pixel count.
    const std::vector<EnumTable> kBackgroundSize = {
        { "stretch", int(BackgroundSize::Stretch) },
        { "cover",   int(BackgroundSize::Cover)   },
    };

    #define P1(p) { Prop::p, Prop::p }
    const PropSpec kProps[] = {
        { "flex-direction",   Prop::FlexDirection,   UIPropValueKind::Enum,    &kDirection, nullptr, nullptr, P1(FlexDirection), false },
        { "justify-content",  Prop::JustifyContent,  UIPropValueKind::Enum,    &kJustify,   nullptr, nullptr, P1(JustifyContent), false },
        { "align-items",      Prop::AlignItems,      UIPropValueKind::Enum,    &kAlign,     nullptr, nullptr, P1(AlignItems), false },
        { "align-self",       Prop::AlignSelf,       UIPropValueKind::Enum,    &kAlign,     nullptr, nullptr, P1(AlignSelf), false },
        { "flex-grow",        Prop::FlexGrow,        UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(FlexGrow), false },
        { "flex-shrink",      Prop::FlexShrink,      UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(FlexShrink), false },
        { "width",            Prop::Width,           UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(Width), false },
        { "height",           Prop::Height,          UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(Height), false },
        { "min-width",        Prop::MinWidth,        UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(MinWidth), false },
        { "min-height",       Prop::MinHeight,       UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(MinHeight), false },
        { "max-width",        Prop::MaxWidth,        UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(MaxWidth), false },
        { "max-height",       Prop::MaxHeight,       UIPropValueKind::Length,  nullptr,     nullptr, nullptr, P1(MaxHeight), false },
        { "margin",           Prop::Margin,          UIPropValueKind::Edges,   nullptr,     nullptr, nullptr, P1(Margin), false },
        { "padding",          Prop::Padding,         UIPropValueKind::Edges,   nullptr,     nullptr, nullptr, P1(Padding), false },
        { "gap",              Prop::Gap,             UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(Gap), false },
        { "position",         Prop::Position,        UIPropValueKind::Enum,    &kPosition,  nullptr, nullptr, P1(Position), false },
        { "left",             Prop::Left,            UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(Left), false },
        { "top",              Prop::Top,             UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(Top), false },
        { "right",            Prop::Right,           UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(Right), false },
        { "bottom",           Prop::Bottom,          UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(Bottom), false },
        { "background-color", Prop::BackgroundColor, UIPropValueKind::Color,   nullptr,     nullptr, nullptr, P1(BackgroundColor), false },
        { "color",            Prop::Color,           UIPropValueKind::Color,   nullptr,     nullptr, nullptr, P1(Color), false },
        { "font-scale",       Prop::FontScale,       UIPropValueKind::Number,  nullptr,     nullptr, nullptr, P1(FontScale), false },
        // The shorthand writes BOTH axes; the longhands write one each. Source
        // order within a rule decides, which falls out for free — see
        // ApplyToElement, which replays a rule's declarations in parse order.
        { "overflow",         Prop::Overflow,        UIPropValueKind::Enum,    &kOverflow,  nullptr, nullptr, { Prop::OverflowX, Prop::OverflowY }, false },
        { "overflow-x",       Prop::OverflowX,       UIPropValueKind::Enum,    &kOverflow,  nullptr, nullptr, P1(OverflowX), false },
        { "overflow-y",       Prop::OverflowY,       UIPropValueKind::Enum,    &kOverflow,  nullptr, nullptr, P1(OverflowY), false },
        { "scrollbar-width",      Prop::ScrollbarWidth,      UIPropValueKind::Length, nullptr, nullptr, nullptr, P1(ScrollbarWidth), true },
        { "scrollbar-min-thumb",  Prop::ScrollbarMinThumb,   UIPropValueKind::Length, nullptr, nullptr, nullptr, P1(ScrollbarMinThumb), true },
        { "scrollbar-color",      Prop::ScrollbarColor,      UIPropValueKind::Color,  nullptr, nullptr, nullptr, P1(ScrollbarColor), false },
        // Paint-only: neither of these reaches yoga. See UIStyle.h.
        { "border-radius",    Prop::BorderRadius,    UIPropValueKind::Length, nullptr, nullptr, nullptr, P1(BorderRadius), true },
        { "border-width",     Prop::BorderWidth,     UIPropValueKind::Length, nullptr, nullptr, nullptr, P1(BorderWidth),  true },
        { "border-color",     Prop::BorderColor,     UIPropValueKind::Color,  nullptr, nullptr, nullptr, P1(BorderColor),  false },
        { "background-image", Prop::BackgroundImage, UIPropValueKind::AssetPath, nullptr, nullptr, nullptr, P1(BackgroundImage), false },
        { "background-size",  Prop::BackgroundSize,  UIPropValueKind::Enum,   &kBackgroundSize, nullptr, nullptr, P1(BackgroundSize), false },
        { "scrollbar-thumb-color",Prop::ScrollbarThumbColor, UIPropValueKind::Color,  nullptr, nullptr, nullptr, P1(ScrollbarThumbColor), false },
        { "scrollbar-visibility", Prop::ScrollbarVisibility, UIPropValueKind::Enum, &kScrollbarVis, nullptr, nullptr, P1(ScrollbarVisibility), false },
        { "scroll-behavior",      Prop::ScrollBehavior,      UIPropValueKind::Enum, &kScrollBehavior, nullptr, nullptr, P1(ScrollBehavior), false },
        { "pointer-events",   Prop::PointerEvents,   UIPropValueKind::Boolean, nullptr,     "auto",   "none", P1(PointerEvents), false },
        // Boolean rather than an enum because there are exactly two values and
        // `if=` needs to write it from a bool. `true` means VISIBLE, so the
        // spelling that maps to true is "flex".
        { "display",          Prop::Display,         UIPropValueKind::Boolean, nullptr,     "flex",   "none", P1(Display), false },
    };
    #undef P1


    const PropSpec* specFor(Prop p) {
        for (const PropSpec& s : kProps) {
            if (s.prop == p) return &s;
        }
        return nullptr;
    }

    // Parses one `name: value`. Returns false with `err` set so the caller can
    // report the line; an unknown property is an ERROR, not a silent skip.
    bool parseDeclaration(const std::string& name, const std::string& value,
                          UIDeclaration& out, std::string& err) {
        const std::string n = lower(trim(name));
        Prop p{};
        UIPropValueKind k{};
        if (!UIDeclaration::PropFromName(n, p, k)) {
            err = "unknown property '" + n + "'";
            return false;
        }
        return UIDeclaration::ParseValueFor(p, value, out, err);
    }

    // Splits on `sep` at BRACE DEPTH ZERO, so a ';' or ':' inside a {hole}
    // stays part of its value.
    void splitTopLevel(const std::string& s, char sep, std::vector<std::string>& out) {
        int depth = 0;
        std::string cur;
        for (char c : s) {
            if (c == '{') ++depth;
            else if (c == '}' && depth > 0) --depth;
            if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        out.push_back(cur);
    }

} // namespace

bool UIDeclaration::PropFromName(const std::string& name, Prop& p, UIPropValueKind& k) {
    for (const PropSpec& s : kProps) {
        if (name == s.name) { p = s.prop; k = s.kind; return true; }
    }
    return false;
}

bool UIDeclaration::PropsOverlap(Prop a, Prop b) {
    const PropSpec* sa = specFor(a);
    const PropSpec* sb = specFor(b);
    if (!sa || !sb) return a == b;
    for (Prop x : sa->expandsTo) {
        for (Prop y : sb->expandsTo) if (x == y) return true;
    }
    return false;
}

const char* UIDeclaration::NameOf(Prop p) {
    const PropSpec* s = specFor(p);
    return s ? s->name : "?";
}

bool UIDeclaration::ParseValueFor(Prop p, const std::string& value,
                                  UIDeclaration& out, std::string& err) {
    const PropSpec* s = specFor(p);
    if (!s) { err = "unknown property"; return false; }
    out.prop = p;
    switch (s->kind) {
    case UIPropValueKind::Length:
        if (!parseLength(value, out.length)) { err = "bad length '" + value + "'"; return false; }
        // A scrollbar cannot be a percentage of anything meaningful and cannot
        // be negative, and Length otherwise validates neither. Reported rather
        // than clamped: a silently-ignored `50%` is the no-op this codebase
        // reports errors to avoid.
        if (s->nonNegativePx &&
            (out.length.unit != StyleLength::Unit::Point || out.length.value < 0.0f)) {
            err = std::string(s->name) + " must be a non-negative pixel length (got '"
                  + trim(value) + "')";
            return false;
        }
        return true;
    case UIPropValueKind::Number:
        if (!parseNumber(trim(value), out.number)) { err = "bad number '" + value + "'"; return false; }
        return true;
    case UIPropValueKind::Color:
        if (!parseColor(value, out.color)) { err = "bad colour '" + value + "'"; return false; }
        return true;
    case UIPropValueKind::Edges:
        if (!parseEdges(value, out.edges)) { err = "bad edge shorthand '" + value + "'"; return false; }
        return true;
    case UIPropValueKind::Enum: {
        // Guarded: a table-less Enum row would be a null deref at PARSE time,
        // which is inside the hot-reload path — a message beats a crash.
        if (!s->enumTable) {
            err = std::string("internal: no keyword table for '") + s->name + "'";
            return false;
        }
        if (!parseEnum(value, *s->enumTable, out.enumValue)) {
            // Name the property AND the legal words. The old "bad value 'x'"
            // named neither, which only half-honours the reported-errors rule:
            // you learned that you were wrong, not what would be right.
            std::string legal;
            for (const auto& e : *s->enumTable) {
                if (!legal.empty()) legal += "|";
                legal += e.name;
            }
            err = std::string(s->name) + " must be one of " + legal +
                  " (got '" + trim(value) + "')";
            return false;
        }
        return true;
    }
    case UIPropValueKind::AssetPath: {
        std::string v = trim(value);
        // Quotes are REQUIRED, so a path is never mistaken for a keyword and a
        // path containing a space needs no escaping rule.
        if (v.size() < 2 || v.front() != '"' || v.back() != '"') {
            err = std::string(s->name) + " must be a quoted project-relative path "
                  "(got '" + v + "')";
            return false;
        }
        v = v.substr(1, v.size() - 2);
        if (v.empty()) {
            err = std::string(s->name) + ": empty path";
            return false;
        }
        // Checked HERE, at parse time, for the same reason markup paths are:
        // an authored path is untrusted content headed for a file open. Doing
        // it at paint time would mean re-checking every frame and reporting
        // from a function with no error channel.
        std::filesystem::path contained;
        if (!PathIsContained(/*baseDir=*/"", v, contained)) {
            err = std::string(s->name) + ": rejected path outside the project: '" + v + "'";
            return false;
        }
        out.assetId = InternAssetPath(v);
        return true;
    }
    case UIPropValueKind::Boolean: {
        const std::string v = lower(trim(value));
        if (v != s->trueWord && v != s->falseWord) {
            err = std::string(s->name) + " must be " + s->trueWord + "|" + s->falseWord;
            return false;
        }
        out.boolean = (v == s->trueWord);
        return true;
    }
    }
    err = "unknown property";
    return false;
}

void UIDeclaration::ApplyTo(Style& s) const {
    switch (prop) {
    case Prop::FlexDirection:  s.direction = (FlexDirection)enumValue; break;
    case Prop::JustifyContent: s.justify = (Justify)enumValue; break;
    case Prop::AlignItems:     s.alignItems = (Align)enumValue; break;
    case Prop::AlignSelf:      s.alignSelf = (Align)enumValue; break;
    case Prop::FlexGrow:       s.flexGrow = number; break;
    case Prop::FlexShrink:     s.flexShrink = number; break;
    case Prop::Width:          s.width = length; break;
    case Prop::Height:         s.height = length; break;
    case Prop::MinWidth:       s.minWidth = length; break;
    case Prop::MinHeight:      s.minHeight = length; break;
    case Prop::MaxWidth:       s.maxWidth = length; break;
    case Prop::MaxHeight:      s.maxHeight = length; break;
    case Prop::Margin:         s.margin = edges; break;
    case Prop::Padding:        s.padding = edges; break;
    case Prop::Gap:            s.gap = number; break;
    case Prop::Position:       s.position = (PositionType)enumValue; break;
    case Prop::Left:           s.inset.left = number; break;
    case Prop::Top:            s.inset.top = number; break;
    case Prop::Right:          s.inset.right = number; break;
    case Prop::Bottom:         s.inset.bottom = number; break;
    case Prop::BackgroundColor: s.backgroundColor = color; break;
    case Prop::Color:          s.textColor = color; break;
    case Prop::FontScale:      s.fontScale = number; break;
    case Prop::Overflow:       s.overflowX = s.overflowY = (Overflow)enumValue; break;
    case Prop::OverflowX:      s.overflowX = (Overflow)enumValue; break;
    case Prop::OverflowY:      s.overflowY = (Overflow)enumValue; break;
    case Prop::ScrollbarWidth:      s.scrollbarWidth = length.value; break;
    case Prop::ScrollbarMinThumb:   s.scrollbarMinThumb = length.value; break;
    case Prop::BorderRadius:    s.borderRadius = length.value; break;
    case Prop::BorderWidth:     s.borderWidth = length.value; break;
    case Prop::BorderColor:     s.borderColor = color; break;
    case Prop::BackgroundImage: s.backgroundImage = assetId; break;
    case Prop::BackgroundSize:  s.backgroundSize = (BackgroundSize)enumValue; break;
    case Prop::ScrollbarColor:      s.scrollbarColor = color; break;
    case Prop::ScrollbarThumbColor: s.scrollbarThumbColor = color; break;
    case Prop::ScrollbarVisibility: s.scrollbarVisibility = (ScrollbarVisibility)enumValue; break;
    case Prop::ScrollBehavior:      s.scrollBehavior = (ScrollBehavior)enumValue; break;
    case Prop::PointerEvents:  s.pickable = boolean; break;
    case Prop::Display:        s.display = boolean ? DisplayMode::Flex : DisplayMode::None; break;
    }
}

bool UICompound::MatchesIgnoringState(const UIElement& el) const {
    if (!type.empty() && el.type() != type) return false;
    if (!name.empty() && el.name() != name) return false;
    for (const auto& c : classes) if (!el.HasClass(c)) return false;
    return true;
}

bool UICompound::Matches(const UIElement& el) const {
    if (!MatchesIgnoringState(el)) return false;
    if ((pseudo & std::uint8_t(UIPseudo::Hover)) && !el.isHovered()) return false;
    if ((pseudo & std::uint8_t(UIPseudo::Active)) && !el.isPressed()) return false;
    if ((pseudo & std::uint8_t(UIPseudo::Focus)) && !el.isFocused()) return false;
    if (pseudo & std::uint8_t(UIPseudo::Disabled)) {
        // Disabled is INHERITED: a control inside a disabled panel is disabled,
        // and it would be very odd for `:disabled` not to grey it out too.
        bool off = false;
        for (const UIElement* n = &el; n; n = n->parent()) {
            if (!n->isEnabled()) { off = true; break; }
        }
        if (!off) return false;
    }
    return true;
}

void UICompound::AddSpecificity(int& ids, int& cls, int& types) const {
    if (!name.empty()) ++ids;
    // A pseudo-class counts as a class, as in CSS, which is what makes
    // `.btn:hover` outrank `.btn` without anyone having to think about it.
    int pseudoCount = 0;
    for (std::uint8_t bit = 1; bit; bit = std::uint8_t(bit << 1)) {
        if (pseudo & bit) ++pseudoCount;
        if (bit == 0x80) break;
    }
    cls += int(classes.size()) + pseudoCount;
    if (!type.empty()) ++types;
}

namespace {
    // Walks the ancestor chain right-to-left, which is how every real CSS
    // engine matches: start at the element being styled and try to satisfy each
    // earlier compound against something above it. Left-to-right would need
    // backtracking over the whole subtree.
    //
    // `stateAware` selects Matches vs MatchesIgnoringState uniformly across the
    // chain, so "could this rule ever apply here" asks the same shape of
    // question as "does it apply right now".
    // The element immediately before `el` among its parent's children, or null
    // when it is the first (or has no parent).
    const UIElement* previousSibling(const UIElement& el) {
        const UIElement* p = el.parent();
        if (!p) return nullptr;
        const UIElement* prev = nullptr;
        for (const auto& c : p->children()) {
            if (c.get() == &el) return prev;
            prev = c.get();
        }
        return nullptr;
    }

    bool matchChain(const std::vector<UISelector::Part>& parts, const UIElement& el,
                    bool stateAware) {
        auto hit = [stateAware](const UICompound& c, const UIElement& e) {
            return stateAware ? c.Matches(e) : c.MatchesIgnoringState(e);
        };
        if (!hit(parts.back().compound, el)) return false;
        if (parts.size() == 1) return true;

        // Descendant combinators need backtracking in general (`.a .b .c` may
        // have several candidate ancestors), but this grammar has no sibling
        // combinators and selectors are short, so a greedy walk with one level
        // of retry per part is both correct and simple: for Descendant we scan
        // upward until something matches; for Child there is exactly one
        // candidate.
        //
        // Greedy is not fully general for pathological chains, and that is a
        // deliberate trade: the alternative is a backtracking matcher run on
        // every element of every load, for selectors nobody writes.
        // `cur` is where the NEXT compound to the left is looked for. An
        // ancestor combinator moves it up the tree; a sibling combinator moves
        // it sideways, which is why it is tracked as a position rather than
        // just "the parent".
        const UIElement* self = &el;
        for (int i = int(parts.size()) - 2; i >= 0; --i) {
            const UICompound& want = parts[size_t(i)].compound;
            // The combinator recorded on a part describes its link to the part
            // BEFORE it, so the one that governs this hop is on the part to our
            // right.
            switch (parts[size_t(i) + 1].combinator) {
            case UICombinator::Child: {
                const UIElement* p = self->parent();
                if (!p || !hit(want, *p)) return false;
                self = p;
                break;
            }
            case UICombinator::AdjacentSibling: {
                const UIElement* prev = previousSibling(*self);
                if (!prev || !hit(want, *prev)) return false;
                self = prev;
                break;
            }
            case UICombinator::GeneralSibling: {
                const UIElement* found = nullptr;
                for (const UIElement* s = previousSibling(*self); s;
                     s = previousSibling(*s)) {
                    if (hit(want, *s)) { found = s; break; }
                }
                if (!found) return false;
                self = found;
                break;
            }
            case UICombinator::Descendant:
            default: {
                const UIElement* found = nullptr;
                for (const UIElement* n = self->parent(); n; n = n->parent()) {
                    if (hit(want, *n)) { found = n; break; }
                }
                if (!found) return false;
                self = found;
                break;
            }
            }
        }
        return true;
    }
} // namespace

bool UISelector::Matches(const UIElement& el) const {
    return !parts.empty() && matchChain(parts, el, /*stateAware=*/true);
}

bool UISelector::MatchesIgnoringState(const UIElement& el) const {
    return !parts.empty() && matchChain(parts, el, /*stateAware=*/false);
}

void UISelector::Specificity(int& ids, int& cls, int& types) const {
    ids = cls = types = 0;
    // SUMMED across every compound: `.panel .btn` is two classes, which is what
    // makes a more specific context win.
    for (const Part& p : parts) p.compound.AddSpecificity(ids, cls, types);
}

void UIStyleSheet::SplitDeclarations(const std::string& text,
                                     std::vector<std::pair<std::string, std::string>>& out,
                                     std::vector<std::string>& errors) {
    std::vector<std::string> chunks;
    splitTopLevel(text, ';', chunks);
    for (const std::string& raw : chunks) {
        const std::string declText = trim(raw);
        if (declText.empty()) continue;   // trailing ';' and blank entries
        std::vector<std::string> halves;
        splitTopLevel(declText, ':', halves);
        if (halves.size() < 2) {
            errors.push_back("declaration '" + declText + "' has no ':'");
            continue;
        }
        // Only the FIRST top-level colon separates; anything after it belongs
        // to the value (a format spec lives inside braces, but a future value
        // grammar may not).
        std::string value = halves[1];
        for (size_t i = 2; i < halves.size(); ++i) value += ":" + halves[i];
        out.emplace_back(halves[0], std::move(value));
    }
}

bool UIStyleSheet::ParseDeclarationList(const std::string& text,
                                        std::vector<UIDeclaration>& out,
                                        std::vector<std::string>& errors) {
    std::vector<std::pair<std::string, std::string>> decls;
    const size_t errsBefore = errors.size();
    SplitDeclarations(text, decls, errors);
    bool ok = errors.size() == errsBefore;
    for (const auto& d : decls) {
        UIDeclaration parsed;
        std::string err;
        if (!parseDeclaration(d.first, d.second, parsed, err)) {
            errors.push_back(err);
            ok = false;
            continue;
        }
        out.push_back(parsed);
    }
    return ok;
}

bool UIStyleSheet::ParseLengthValue(const std::string& s, StyleLength& out) {
    return parseLength(s, out);
}
bool UIStyleSheet::ParseColorValue(const std::string& s, glm::vec4& out) {
    return parseColor(s, out);
}
bool UIStyleSheet::ParseNumberValue(const std::string& s, float& out) {
    return parseNumber(trim(s), out);
}

void UIStyleSheet::Clear() { rules_.clear(); errors_.clear(); }

bool UIStyleSheet::ParseString(const std::string& text, const std::string& originName) {
    std::vector<UIRule> parsed;
    std::vector<std::string> errs;

    // Strip C-style comments first, preserving newlines so reported line
    // numbers still point at the right place.
    std::string src;
    src.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                if (text[i] == '\n') src.push_back('\n');
                ++i;
            }
            i = (i + 1 < text.size()) ? i + 2 : text.size();
        } else {
            src.push_back(text[i++]);
        }
    }

    auto lineOf = [&src](size_t pos) {
        return 1 + int(std::count(src.begin(), src.begin() + std::min(pos, src.size()), '\n'));
    };

    size_t i = 0;
    int order = 0;
    while (i < src.size()) {
        while (i < src.size() && std::isspace((unsigned char)src[i])) ++i;
        if (i >= src.size()) break;

        const size_t selStart = i;
        const size_t brace = src.find('{', i);
        if (brace == std::string::npos) {
            errs.push_back(originName + ":" + std::to_string(lineOf(selStart)) +
                           ": rule has no '{'");
            break;
        }
        const size_t close = src.find('}', brace);
        if (close == std::string::npos) {
            errs.push_back(originName + ":" + std::to_string(lineOf(brace)) +
                           ": unterminated rule (no '}')");
            break;
        }

        UIRule rule;
        rule.order = order++;

        // --- selector list ---
        for (const std::string& selText : split(src.substr(selStart, brace - selStart), ',')) {
            UISelector sel;
            UICompound compound;
            UICombinator pendingComb = UICombinator::Descendant;
            // Whether ANY token has gone into the compound being read. Not the
            // same as "the compound is non-empty": `*` is a real selector that
            // sets no fields, and treating it as nothing would reject it.
            bool compoundHasToken = false;
            // A '>' seen since the last completed compound, so `a > > b` is
            // caught rather than silently collapsing to `a > b`.
            bool sawCombinator = false;
            std::string cur;
            char mode = 't'; // t=type, c=class, n=name, p=pseudo
            std::string pseudoErr;
            std::string structErr;

            auto flush = [&] {
                // An EMPTY pseudo name is an error, not nothing. `.btn: { }`
                // would otherwise be pushed as a plain `.btn` rule that applies
                // all the time — the state selector silently deleted — and a
                // bare `:` would become the universal selector.
                if (cur.empty()) {
                    if (mode == 'p' && pseudoErr.empty()) pseudoErr = "<empty>";
                    return;
                }
                if (mode == 'c') compound.classes.push_back(cur);
                else if (mode == 'n') compound.name = cur;
                else if (mode == 'p') {
                    const std::string p = lower(cur);
                    if (p == "hover")         compound.pseudo |= std::uint8_t(UIPseudo::Hover);
                    else if (p == "active")   compound.pseudo |= std::uint8_t(UIPseudo::Active);
                    else if (p == "focus")    compound.pseudo |= std::uint8_t(UIPseudo::Focus);
                    else if (p == "disabled") compound.pseudo |= std::uint8_t(UIPseudo::Disabled);
                    // Reported rather than ignored: a silently-dropped
                    // pseudo-class turns `.btn:focus` into a plain `.btn` rule
                    // that applies ALL the time, which looks like the styling
                    // is simply broken.
                    else if (pseudoErr.empty()) pseudoErr = cur;
                }
                else if (cur != "*") compound.type = cur;   // '*' = any
                compoundHasToken = true;
                cur.clear();
                mode = 't';
            };
            // Ends the compound being read and starts the next one, recording
            // how the two are joined.
            auto endPart = [&] {
                flush();
                if (!compoundHasToken) {
                    // A trailing combinator, or `.a > > .b`.
                    if (structErr.empty()) structErr = "an empty compound";
                    return;
                }
                UISelector::Part part;
                part.compound = std::move(compound);
                part.combinator = pendingComb;
                compound = UICompound{};
                compoundHasToken = false;
                sel.parts.push_back(std::move(part));
                pendingComb = UICombinator::Descendant;
                sawCombinator = false;
            };

            for (size_t ci = 0; ci < selText.size(); ++ci) {
                const char ch = selText[ci];
                if (std::isspace((unsigned char)ch)) {
                    // Whitespace ends a compound, but it is only a DESCENDANT
                    // combinator if a compound follows. `.a > .b` has spaces
                    // around the '>' that must not each mean "descendant", so
                    // whitespace with nothing read is simply skipped.
                    if (compoundHasToken || !cur.empty()) endPart();
                    continue;
                }
                if (ch == '>' || ch == '+' || ch == '~') {
                    if (compoundHasToken || !cur.empty()) endPart();
                    if (sel.parts.empty()) {
                        if (structErr.empty()) {
                            structErr = std::string("a leading '") + ch + "'";
                        }
                    } else if (sawCombinator && structErr.empty()) {
                        structErr = "consecutive combinators";
                    }
                    pendingComb = ch == '>'   ? UICombinator::Child
                                : ch == '+'   ? UICombinator::AdjacentSibling
                                              : UICombinator::GeneralSibling;
                    sawCombinator = true;
                    continue;
                }
                if (ch == '.') { flush(); mode = 'c'; continue; }
                if (ch == '#') { flush(); mode = 'n'; continue; }
                if (ch == ':') { flush(); mode = 'p'; continue; }
                cur.push_back(ch);
            }
            endPart();

            if (!pseudoErr.empty()) {
                const std::string what =
                    pseudoErr == "<empty>"
                        ? std::string("a ':' with no pseudo-class after it")
                        : std::string("unknown pseudo-class ':") + pseudoErr + "'";
                errs.push_back(originName + ":" + std::to_string(lineOf(selStart)) +
                               ": " + what + " in '" + trim(selText) +
                               "' (hover|active|focus|disabled)");
                continue;
            }
            if (!structErr.empty() || sel.parts.empty()) {
                errs.push_back(originName + ":" + std::to_string(lineOf(selStart)) +
                               ": " + (structErr.empty() ? "empty selector" : structErr) +
                               " in '" + trim(selText) + "'");
                continue;
            }
            rule.selectors.push_back(std::move(sel));
        }

        // --- declarations ---
        // Through the SAME splitter as `style=` and `bind=`, so the three can
        // never come to disagree about where one declaration ends.
        {
            const std::string prefix = originName + ":" + std::to_string(lineOf(brace)) + ": ";
            std::vector<std::pair<std::string, std::string>> decls;
            std::vector<std::string> splitErrs;
            SplitDeclarations(src.substr(brace + 1, close - brace - 1), decls, splitErrs);
            for (const auto& e : splitErrs) errs.push_back(prefix + e);
            for (const auto& d : decls) {
                UIDeclaration parsed;
                std::string err;
                if (!parseDeclaration(d.first, d.second, parsed, err)) {
                    errs.push_back(prefix + err);
                    continue;
                }
                rule.declarations.push_back(parsed);
            }
        }

        if (!rule.selectors.empty()) parsed.push_back(std::move(rule));
        i = close + 1;
    }

    errors_ = std::move(errs);
    if (!errors_.empty()) {
        // Leave the previous sheet intact. During hot-reload a half-typed file
        // is the NORMAL state, and swapping in a partial parse would make the
        // UI flicker apart as you type.
        return false;
    }
    rules_ = std::move(parsed);
    return true;
}

bool UIStyleSheet::LoadFromFile(const std::string& path) {
    // Containment BEFORE opening, exactly as UIMarkup::LoadFileInto does. A
    // stylesheet is authored content headed for a parser — the same class of
    // input as markup, models, scripts and clips — and it now also names IMAGE
    // paths, so an unsandboxed sheet path was a way to reach outside the
    // project entirely. That this was missing while markup was gated is a gap,
    // not a decision.
    std::filesystem::path contained;
    if (!PathIsContained(/*baseDir=*/"", path, contained)) {
        errors_ = { "rejected stylesheet path outside the project: '" + path + "'" };
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors_ = { "cannot open '" + path + "'" };
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseString(text, path);
}

void UIStyleSheet::ApplyToElement(UIElement& el) const {
    // Gather matches with their specificity, then apply weakest-first so the
    // strongest rule wins — the CSS cascade, and the reason a #name rule beats
    // a .class rule no matter which came first in the file.
    struct Hit { int ids, cls, types, order; const UIRule* rule; };
    std::vector<Hit> hits;
    // In CSS each selector in a comma list carries its OWN specificity, so a
    // rule matched through several of them weighs as much as its strongest
    // match. Taking the first match instead would make `.btn, .btn:hover { }`
    // weigh as a bare class and lose to a plain `.btn` rule later in the file —
    // an ordering bug that only shows up once pseudo-classes exist to make two
    // selectors in one list differ in strength.
    auto stronger = [](const Hit& a, const Hit& b) {
        if (a.ids != b.ids) return a.ids > b.ids;
        if (a.cls != b.cls) return a.cls > b.cls;
        return a.types > b.types;
    };
    for (const auto& r : rules_) {
        Hit best{};
        bool matched = false;
        for (const auto& s : r.selectors) {
            if (!s.Matches(el)) continue;
            Hit h{};
            s.Specificity(h.ids, h.cls, h.types);
            if (!matched || stronger(h, best)) { best = h; matched = true; }
        }
        if (!matched) continue;
        best.order = r.order;
        best.rule = &r;
        hits.push_back(best);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.ids != b.ids) return a.ids < b.ids;
        if (a.cls != b.cls) return a.cls < b.cls;
        if (a.types != b.types) return a.types < b.types;
        return a.order < b.order;
    });
    for (const Hit& h : hits) {
        for (const auto& d : h.rule->declarations) d.ApplyTo(el.style());
    }
    // Inline last: in CSS an element's own style attribute outranks every
    // selector rule regardless of specificity. Replaying it here (rather than
    // baking it in at load) means a stylesheet hot-reload cannot silently drop
    // the inline values authored alongside the markup.
    for (const auto& d : el.inlineStyle()) d.ApplyTo(el.style());
}

void UIStyleSheet::ApplyTo(UIElement& root) const {
    ApplyToElement(root);
    for (const auto& c : root.children()) ApplyTo(*c);
}

void UIStyleSheet::Recascade(UIElement& el) const {
    std::string keepText = std::move(el.style().text);
    el.style() = Style{};
    ApplyToElement(el);   // rules for the current state + classes, then inline
    el.style().text = std::move(keepText);
}

bool UIStyleSheet::HasStateRuleFor(const UIElement& el) const {
    for (const auto& r : rules_) {
        for (const auto& s : r.selectors) {
            if (s.pseudo() && s.MatchesIgnoringState(el)) return true;
        }
    }
    return false;
}

void UIStyleSheet::DeclaredPropsFor(const UIElement& el,
                                    std::vector<UIDeclaration::Prop>& out) const {
    // Matching only — no specificity sort needed, because the QUESTION is "does
    // anything set this property", not "which rule wins". State is ignored for
    // the same reason: a `:hover` rule still shadows a bound property, and the
    // element is not hovered while the file is being loaded.
    for (const auto& r : rules_) {
        for (const auto& s : r.selectors) {
            if (!s.MatchesIgnoringState(el)) continue;
            for (const auto& d : r.declarations) out.push_back(d.prop);
            break;
        }
    }
    for (const auto& d : el.inlineStyle()) out.push_back(d.prop);
}

} // namespace MyCoreEngine::ui
