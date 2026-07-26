#include "UIStyleSheet.h"
#include "UIElement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

    using Prop = UIDeclaration::Prop;

    // THE property vocabulary. One table, three consumers: the declaration
    // parser (.uss and style=), the binding parser (bind=), and the diagnostic
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
    };

    const PropSpec kProps[] = {
        { "flex-direction",   Prop::FlexDirection,   UIPropValueKind::Enum,    &kDirection, nullptr, nullptr },
        { "justify-content",  Prop::JustifyContent,  UIPropValueKind::Enum,    &kJustify,   nullptr, nullptr },
        { "align-items",      Prop::AlignItems,      UIPropValueKind::Enum,    &kAlign,     nullptr, nullptr },
        { "align-self",       Prop::AlignSelf,       UIPropValueKind::Enum,    &kAlign,     nullptr, nullptr },
        { "flex-grow",        Prop::FlexGrow,        UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "flex-shrink",      Prop::FlexShrink,      UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "width",            Prop::Width,           UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "height",           Prop::Height,          UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "min-width",        Prop::MinWidth,        UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "min-height",       Prop::MinHeight,       UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "max-width",        Prop::MaxWidth,        UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "max-height",       Prop::MaxHeight,       UIPropValueKind::Length,  nullptr,     nullptr, nullptr },
        { "margin",           Prop::Margin,          UIPropValueKind::Edges,   nullptr,     nullptr, nullptr },
        { "padding",          Prop::Padding,         UIPropValueKind::Edges,   nullptr,     nullptr, nullptr },
        { "gap",              Prop::Gap,             UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "position",         Prop::Position,        UIPropValueKind::Enum,    &kPosition,  nullptr, nullptr },
        { "left",             Prop::Left,            UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "top",              Prop::Top,             UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "right",            Prop::Right,           UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "bottom",           Prop::Bottom,          UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "background-color", Prop::BackgroundColor, UIPropValueKind::Color,   nullptr,     nullptr, nullptr },
        { "color",            Prop::Color,           UIPropValueKind::Color,   nullptr,     nullptr, nullptr },
        { "font-scale",       Prop::FontScale,       UIPropValueKind::Number,  nullptr,     nullptr, nullptr },
        { "overflow",         Prop::Overflow,        UIPropValueKind::Boolean, nullptr,     "hidden", "visible" },
        { "pointer-events",   Prop::PointerEvents,   UIPropValueKind::Boolean, nullptr,     "auto",   "none" },
    };

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
    case UIPropValueKind::Enum:
        if (!parseEnum(value, *s->enumTable, out.enumValue)) {
            err = "bad value '" + value + "'";
            return false;
        }
        return true;
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
    case Prop::Overflow:       s.overflowHidden = boolean; break;
    case Prop::PointerEvents:  s.pickable = boolean; break;
    }
}

bool UISelector::Matches(const UIElement& el) const {
    if (!type.empty() && el.type() != type) return false;
    if (!name.empty() && el.name() != name) return false;
    for (const auto& c : classes) if (!el.HasClass(c)) return false;
    return true;
}

void UISelector::Specificity(int& ids, int& cls, int& types) const {
    ids = name.empty() ? 0 : 1;
    cls = int(classes.size());
    types = type.empty() ? 0 : 1;
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
            std::string cur;
            char mode = 't'; // t=type, c=class, n=name
            auto flush = [&] {
                if (cur.empty()) return;
                if (mode == 'c') sel.classes.push_back(cur);
                else if (mode == 'n') sel.name = cur;
                else if (cur != "*") sel.type = cur;   // '*' = any
                cur.clear();
            };
            bool bad = false;
            for (char ch : selText) {
                if (std::isspace((unsigned char)ch)) {
                    // A space would be a descendant combinator, which is not
                    // supported — flag it rather than silently mis-matching.
                    flush();
                    bad = true;
                    break;
                }
                if (ch == '.') { flush(); mode = 'c'; continue; }
                if (ch == '#') { flush(); mode = 'n'; continue; }
                cur.push_back(ch);
            }
            if (bad) {
                errs.push_back(originName + ":" + std::to_string(lineOf(selStart)) +
                               ": combinators are not supported in '" + trim(selText) + "'");
                continue;
            }
            flush();
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
    for (const auto& r : rules_) {
        for (const auto& s : r.selectors) {
            if (!s.Matches(el)) continue;
            Hit h{};
            s.Specificity(h.ids, h.cls, h.types);
            h.order = r.order;
            h.rule = &r;
            hits.push_back(h);
            break; // one match per rule is enough
        }
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

void UIStyleSheet::DeclaredPropsFor(const UIElement& el,
                                    std::vector<UIDeclaration::Prop>& out) const {
    // Matching only — no specificity sort needed, because the QUESTION is "does
    // anything set this property", not "which rule wins".
    for (const auto& r : rules_) {
        for (const auto& s : r.selectors) {
            if (!s.Matches(el)) continue;
            for (const auto& d : r.declarations) out.push_back(d.prop);
            break;
        }
    }
    for (const auto& d : el.inlineStyle()) out.push_back(d.prop);
}

} // namespace MyCoreEngine::ui
