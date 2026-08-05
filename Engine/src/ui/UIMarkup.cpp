#include "UIMarkup.h"

#include <cstdlib>   // strtof, for a <Slider>'s bounds

#include "UIElement.h"
#include "UIStyleSheet.h"
#include "../core/PathSandbox.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace MyCoreEngine::ui {

namespace {

    std::vector<std::string> splitClasses(const std::string& s) {
        std::vector<std::string> out;
        std::istringstream in(s);
        std::string t;
        while (in >> t) out.push_back(t);
        return out;
    }

    std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    std::string lower(std::string s) {
        for (char& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    }

    std::string describe(const pugi::xml_node& node) {
        std::string s = "<";
        s += node.name();
        if (const char* n = node.attribute("name").value(); n && *n) {
            s += " name='";
            s += n;
            s += "'";
        }
        return s + ">";
    }

    // Applies every attribute of `node` to `el`.
    //
    // ONE helper for both children and the root, deliberately. Root attributes
    // used to be handled in a separate block in LoadInto, which meant every new
    // attribute family had to be added in two places and the two silently
    // diverged the first time one was forgotten. Nothing here reaches outside
    // `el`, so it works equally for a freshly built child and for the
    // document's existing root.
    //
    // Returns false having appended a message; the caller then discards the
    // whole staged tree, so a partial parse never reaches the document.
    bool applyAttributes(const pugi::xml_node& node, UIElement& el,
                         std::vector<std::string>& errors, const std::string& origin) {
        const std::string loc = origin + ": " + describe(node) + " ";

        // Unknown attributes are ERRORS, not silence. Until now this loader
        // read the attributes it knew by name and never enumerated the rest, so
        // `nmae="healthFill"` loaded cleanly and produced an element that no
        // stylesheet rule and no binding could ever find.
        for (pugi::xml_attribute a : node.attributes()) {
            const std::string n = a.name();
            if (n == "name" || n == "class" || n == "style" || n == "text" ||
                n == "data-source" || n == "bind" || n == "if" ||
                n == "focusable" || n == "disabled" ||
                n == "value" || n == "maxlength" || n == "mask" ||
                n == "multiline" ||
                n == "focus-scope" ||
                n == "min" || n == "max" || n == "step" || n == "key-step" ||
                n == "vertical" ||
                // EXACT entries, not a "repeat-" prefix rule: the exact-match
                // allow-list is the only reason `repeat-cont=` is reported
                // rather than quietly ignored.
                n == "repeat" || n == "repeat-count" || n == "repeat-offset" ||
                n == "label" || n == "selected" || n == "bind-selected") {
                continue;
            }
            // The on-<event> and push-<state> families are validated in full
            // below; here they only have to survive the allow-list.
            if (n.rfind("on-", 0) == 0) continue;
            if (n.rfind("push-", 0) == 0) continue;
            if (n == "bind-value" || n == "classes") continue;
            errors.push_back(loc + "unknown attribute '" + n + "'");
            return false;
        }

        // `label` belongs to a <Tab>'s header and `selected` to a <TabView>;
        // on anything else they are a silent no-op, which is the one thing this
        // loader never allows.
        if (node.attribute("label") && el.type() != "Tab") {
            errors.push_back(loc + "label: 'label' is only valid on a <Tab>");
            return false;
        }
        if (node.attribute("selected") && el.type() != "TabView") {
            errors.push_back(loc + "selected: 'selected' is only valid on a <TabView>");
            return false;
        }
        if (node.attribute("bind-selected") && el.type() != "TabView") {
            errors.push_back(loc + "bind-selected: 'bind-selected' is only valid on a <TabView>");
            return false;
        }

        // The two modifiers mean nothing on their own, and an ignored
        // `repeat-count` reads exactly like a pool that refused to grow.
        for (const char* attr : { "repeat-count", "repeat-offset" }) {
            if (node.attribute(attr) && !node.attribute("repeat")) {
                errors.push_back(loc + attr + ": '" + attr +
                                 "' is only valid with 'repeat'");
                return false;
            }
        }

        if (const char* n = node.attribute("name").value(); n && *n) el.setName(n);

        // Replace, don't merge: reloading a file must not accumulate classes
        // from its previous version.
        el.ClearClasses();
        for (auto& c : splitClasses(node.attribute("class").value())) {
            el.AddClass(std::move(c));
        }

        el.setDataSourceName(node.attribute("data-source").value());

        // Both are reset unconditionally so a removed attribute takes effect on
        // a reload — the same reason text and inline style are cleared below.
        // A `Button` or `TextField` is focusable by default because that is
        // what those words mean; anything else has to ask.
        const std::string type = el.type();
        bool focusable = (type == "Button" || type == "TextField");
        bool disabled = false;
        // A FOCUS SCOPE confines navigation to its own subtree while it is
        // visible. Without it, opening a panel merely ADDS its controls to one
        // flat ring, so walking off the end of a settings page wanders back to
        // the main verbs -- which is not what any menu anywhere does.
        if (const pugi::xml_attribute a = node.attribute("focus-scope")) {
            const std::string v = lower(trim(a.value()));
            if (v.empty() || v == "true") el.setFocusScope(true);
            else if (v == "false")        el.setFocusScope(false);
            else {
                errors.push_back(loc + "focus-scope: expected true|false, got '" + v + "'");
                return false;
            }
        }
        for (const char* attr : { "focusable", "disabled" }) {
            const pugi::xml_attribute a = node.attribute(attr);
            if (!a) continue;
            const std::string v = lower(trim(a.value()));
            // Bare `disabled` with no value means true, as in HTML.
            if (v.empty() || v == "true") { (attr[0] == 'f' ? focusable : disabled) = true; }
            else if (v == "false")        { (attr[0] == 'f' ? focusable : disabled) = false; }
            else {
                errors.push_back(loc + attr + ": expected true|false, got '" + v + "'");
                return false;
            }
        }
        el.setFocusable(focusable);
        el.setEnabled(!disabled);

        const bool isField = (type == "TextField");
        const bool isSlider = (type == "Slider");

        // `text` is a TEMPLATE now: literal text with {holes} that read named
        // values. A constant template still compiles, so there is exactly one
        // code path and no "is this one bound?" mode flag anywhere.
        std::vector<UIBinding> bindings;
        // Cleared unconditionally, so removing a text= (or replacing a constant
        // with a binding) cannot leave the previous version's text behind. That
        // matters only for the ROOT, which is reused across loads rather than
        // rebuilt — and it is exactly the sort of asymmetry that used to make
        // the root behave differently from every other element.
        el.setText(std::string());
        if (const char* t = node.attribute("text").value(); t && *t) {
            UIBinding b;
            b.target.kind = UIBindTarget::Kind::Text;
            b.label = "text";
            std::string err;
            if (!UITextTemplate::Compile(t, b.tmpl, err)) {
                errors.push_back(loc + "text: " + err);
                return false;
            }
            if (b.tmpl.isConstant()) {
                // No holes: set it directly and store no binding, so the
                // binder's flat array holds only things that can change.
                el.setText(t);
            } else {
                bindings.push_back(std::move(b));
            }
        }

        // `bind="width: {health | percent}; background-color: {health|tint}"`
        // — CSS declarations whose VALUES carry holes. Split by the same
        // brace-aware splitter `style=` uses, so the two can never come to
        // disagree about where one declaration ends.
        if (const char* bindAttr = node.attribute("bind").value(); bindAttr && *bindAttr) {
            std::vector<std::pair<std::string, std::string>> decls;
            std::vector<std::string> splitErrors;
            UIStyleSheet::SplitDeclarations(bindAttr, decls, splitErrors);
            if (!splitErrors.empty()) {
                for (const auto& e : splitErrors) errors.push_back(loc + "bind: " + e);
                return false;
            }
            for (const auto& d : decls) {
                const std::string propName = lower(trim(d.first));
                UIDeclaration::Prop prop{};
                UIPropValueKind kind{};
                if (!UIDeclaration::PropFromName(propName, prop, kind)) {
                    errors.push_back(loc + "bind: unknown property '" + propName + "'");
                    return false;
                }
                // An asset path is authored, not computed. Binding one would
                // intern a new id on every apply, and the intern table is
                // monotonic by design — ids must never come to mean a different
                // file — so a path that changed each frame would grow it without
                // bound. Swap the whole element with `if=` instead, or toggle a
                // class whose rule names a different image.
                if (kind == UIPropValueKind::AssetPath) {
                    errors.push_back(loc + "bind: '" + propName +
                                     "' takes an authored path and cannot be bound - "
                                     "toggle a class whose rule names a different image, "
                                     "or swap the element with if=");
                    return false;
                }

                UIBinding b;
                b.target.kind = UIBindTarget::Kind::Style;
                b.target.styleProp = prop;
                b.target.valueKind = kind;
                b.label = "bind " + propName;
                std::string err;
                // TRIMMED, and that matters more than it looks: whitespace
                // around a CSS value is insignificant, but an untrimmed " {x}"
                // is not a single hole, and the single-hole case is what lets a
                // bound colour or length reach its field WITHOUT a round trip
                // through text. Without this, `background-color: {tint}`
                // silently quantises to 8 bits per channel.
                if (!UITextTemplate::Compile(trim(d.second), b.tmpl, err)) {
                    errors.push_back(loc + b.label + ": " + err);
                    return false;
                }
                // A bind with no hole is a silent duplicate of style=, and the
                // two would then disagree about which wins. Say so instead.
                if (b.tmpl.isConstant()) {
                    errors.push_back(loc + b.label +
                                     ": value has no {} - use style= for a constant");
                    return false;
                }
                bindings.push_back(std::move(b));
            }
        }

        // `if="alive"` / `if="!dead"` — visibility from a bool, written as
        // display. Deliberately its own attribute rather than
        // `bind="display: {alive}"`: showing and hiding is the single most
        // common dynamic-HUD requirement and deserves to read like one.
        if (const char* cond = node.attribute("if").value(); cond && *cond) {
            std::string path = trim(cond);
            UIBinding b;
            b.target.kind = UIBindTarget::Kind::Display;
            b.label = "if";
            if (!path.empty() && path[0] == '!') {
                b.negate = true;
                path = trim(path.substr(1));
            }
            if (path.empty()) {
                errors.push_back(loc + "if: empty");
                return false;
            }
            // Written as a bare path for readability, compiled as a one-hole
            // template so it shares every code path with every other binding.
            std::string err;
            if (!UITextTemplate::Compile("{" + path + "}", b.tmpl, err)) {
                errors.push_back(loc + "if: " + err);
                return false;
            }
            bindings.push_back(std::move(b));
        }

        // `classes="low-health: {isLow}; boosted: {hasBoost}"` — each entry
        // toggles ONE class from a bool.
        //
        // Shaped like `bind=` rather than a `class-<name>=` attribute family,
        // which would sit next to `class=` in an attribute list meaning
        // something quite different. Same brace-aware splitter, so `classes=`
        // and `bind=` can never come to disagree about where an entry ends.
        if (const char* classAttr = node.attribute("classes").value(); classAttr && *classAttr) {
            std::vector<std::pair<std::string, std::string>> decls;
            std::vector<std::string> splitErrors;
            UIStyleSheet::SplitDeclarations(classAttr, decls, splitErrors);
            if (!splitErrors.empty()) {
                for (const auto& e : splitErrors) errors.push_back(loc + "classes: " + e);
                return false;
            }
            for (const auto& d : decls) {
                const std::string cls = trim(d.first);
                if (cls.empty() || cls.find_first_of(" \t.#:") != std::string::npos) {
                    errors.push_back(loc + "classes: '" + cls +
                                     "' is not a plain class name");
                    return false;
                }
                UIBinding b;
                b.target.kind = UIBindTarget::Kind::Class;
                b.target.className = cls;
                b.label = "classes " + cls;
                std::string cond = trim(d.second);
                if (!cond.empty() && cond[0] == '!') {
                    b.negate = true;
                    cond = trim(cond.substr(1));
                }
                std::string err;
                if (!UITextTemplate::Compile(cond, b.tmpl, err)) {
                    errors.push_back(loc + b.label + ": " + err);
                    return false;
                }
                if (b.tmpl.isConstant()) {
                    errors.push_back(loc + b.label +
                                     ": value has no {} - use class= for a constant");
                    return false;
                }
                bindings.push_back(std::move(b));
            }
        }

        // ---- element -> source ----
        // `push-hovered="isOver"`, `push-pressed="firing"`, `push-focused="typing"`
        // and, on a text field, `bind-value="playerName"`.
        //
        // A BARE PATH, not a template: you cannot un-format a rendered string
        // back into a value, so anything with a converter chain or literal text
        // around it is one-directional by construction, and pretending
        // otherwise would produce a binding that silently only worked one way.
        auto splitPath = [&](const std::string& raw, UIBinding& b, const char* what) {
            const std::string path = trim(raw);
            if (path.empty()) { errors.push_back(loc + what + ": empty path"); return false; }
            const size_t dot = path.find('.');
            if (dot != std::string::npos) {
                if (path.find('.', dot + 1) != std::string::npos) {
                    errors.push_back(loc + what + ": path '" + path +
                                     "' has more than one '.' (write source.property)");
                    return false;
                }
                b.pushSource = trim(path.substr(0, dot));
                b.pushProp = trim(path.substr(dot + 1));
            } else {
                b.pushProp = path;
            }
            if (b.pushProp.empty()) {
                errors.push_back(loc + what + ": path '" + path + "' has no property");
                return false;
            }
            return true;
        };

        for (pugi::xml_attribute a : node.attributes()) {
            const std::string n = a.name();
            if (n.rfind("push-", 0) != 0) continue;
            const std::string what = lower(n.substr(5));
            UIBinding b;
            b.target.kind = UIBindTarget::Kind::State;
            b.label = n;
            if (what == "hovered")      b.target.state = UIBindTarget::StateProp::Hovered;
            else if (what == "pressed") b.target.state = UIBindTarget::StateProp::Pressed;
            else if (what == "focused") b.target.state = UIBindTarget::StateProp::Focused;
            else {
                errors.push_back(loc + "unknown state '" + what + "' in '" + n +
                                 "' (hovered|pressed|focused)");
                return false;
            }
            if (!splitPath(a.value(), b, n.c_str())) return false;
            bindings.push_back(std::move(b));
        }

        if (const pugi::xml_attribute a = node.attribute("bind-value")) {
            if (!isField && !isSlider) {
                errors.push_back(loc +
                    "'bind-value' is only valid on a <TextField> or <Slider>");
                return false;
            }
            UIBinding b;
            b.target.kind = UIBindTarget::Kind::Value;
            b.label = "bind-value";
            if (!splitPath(a.value(), b, "bind-value")) return false;
            bindings.push_back(std::move(b));
        }

        el.setBindings(std::move(bindings));

        // `on-click="addScore"` — a NAMED action the app registered. Not a
        // statement: a real C++ function you can breakpoint, and a typo is
        // answered with the actions that exist.
        std::vector<UIBoundAction> boundActions;
        for (pugi::xml_attribute a : node.attributes()) {
            const std::string n = a.name();
            if (n.rfind("on-", 0) != 0) continue;

            const std::string evName = lower(n.substr(3));
            UIEventType type{};
            if      (evName == "click")         type = UIEventType::Click;
            else if (evName == "pointer-down")  type = UIEventType::PointerDown;
            else if (evName == "pointer-up")    type = UIEventType::PointerUp;
            else if (evName == "pointer-enter") type = UIEventType::PointerEnter;
            else if (evName == "pointer-leave") type = UIEventType::PointerLeave;
            else if (evName == "pointer-move")  type = UIEventType::PointerMove;
            // Observes the wheel; it does NOT claim it. The built-in scroll
            // still runs afterwards, because making a bound action claim its
            // event would silently change what every on-click means too. A C++
            // handler calling StopPropagation is the way to suppress it.
            else if (evName == "wheel")         type = UIEventType::Wheel;
            // `on-back` belongs on a focus-scope root: it is what B / Escape
            // invokes while that scope is open, and its job is to set whatever
            // app state hides the panel again.
            else if (evName == "back")          type = UIEventType::Back;
            else {
                errors.push_back(loc + "unknown event '" + evName + "' in '" + n +
                                 "' (click|pointer-down|pointer-up|pointer-enter|"
                                 "pointer-leave|pointer-move|wheel|back)");
                return false;
            }

            UIBoundAction act;
            act.type = type;
            act.label = n;
            std::string target = trim(a.value());
            if (target.empty()) {
                errors.push_back(loc + n + ": empty action name");
                return false;
            }
            // Same one-dot rule as a binding path: source.action.
            const size_t dot = target.find('.');
            if (dot != std::string::npos) {
                if (target.find('.', dot + 1) != std::string::npos) {
                    errors.push_back(loc + n + ": action '" + target +
                                     "' has more than one '.' (write source.action)");
                    return false;
                }
                act.sourceName = trim(target.substr(0, dot));
                act.actionName = trim(target.substr(dot + 1));
            } else {
                act.actionName = target;
            }
            boundActions.push_back(std::move(act));
        }
        el.setBoundActions(std::move(boundActions));

        // ---- text fields ----
        // AFTER the text/bind handling above, which clears style().text
        // unconditionally so a removed attribute takes effect on a reload —
        // syncing the field's display text before that would just be wiped.
        //
        // `value`, `maxlength` and `mask` only mean anything on a TextField,
        // and `text` means the opposite of one: the VALUE decides what a field
        // shows. Silently ignoring either is exactly the class of no-op this
        // loader reports.
        for (const char* attr : { "maxlength", "mask", "multiline" }) {
            if (node.attribute(attr) && !isField) {
                errors.push_back(loc + "'" + attr + "' is only valid on a <TextField>");
                return false;
            }
        }
        // `value` is the one they SHARE: both own a value and both bind it
        // two-way through bind-value. Everything else is exclusive.
        if (node.attribute("value") && !isField && !isSlider) {
            errors.push_back(loc + "'value' is only valid on a <TextField> or <Slider>");
            return false;
        }
        for (const char* attr : { "min", "max", "step", "key-step", "vertical" }) {
            if (node.attribute(attr) && !isSlider) {
                errors.push_back(loc + "'" + attr + "' is only valid on a <Slider>");
                return false;
            }
        }
        if (isSlider) {
            if (node.attribute("text")) {
                errors.push_back(loc + "a <Slider> shows its 'value', not 'text'");
                return false;
            }
            UISliderState& sl = el.MakeSlider();
            // Parsed with the same strictness as everything else here: a
            // mistyped bound is a load error naming the attribute, not a
            // silent 0 that makes the control inert.
            const auto num = [&](const char* attr, float& out) {
                const pugi::xml_attribute a = node.attribute(attr);
                if (!a) return true;
                const std::string v = trim(a.value());
                char* end = nullptr;
                const float f = std::strtof(v.c_str(), &end);
                if (v.empty() || !end || *end != '\0') {
                    errors.push_back(loc + attr + ": expected a number, got '" + v + "'");
                    return false;
                }
                out = f;
                return true;
            };
            if (!num("min", sl.min) || !num("max", sl.max) ||
                !num("step", sl.step) || !num("key-step", sl.keyStep)) return false;
            if (sl.max == sl.min) {
                errors.push_back(loc + "min and max are both " + trim(std::to_string(sl.min)) +
                                       ": the slider would have no range");
                return false;
            }
            if (const pugi::xml_attribute a = node.attribute("vertical")) {
                const std::string v = lower(trim(a.value()));
                if (v.empty() || v == "true") sl.vertical = true;
                else if (v == "false")        sl.vertical = false;
                else {
                    errors.push_back(loc + "vertical: expected true|false, got '" + v + "'");
                    return false;
                }
            }
            // LAST, so min/max/step are in place to clamp and quantise it.
            if (const pugi::xml_attribute a = node.attribute("value")) {
                sl.SetValue(std::strtof(trim(a.value()).c_str(), nullptr));
            } else {
                sl.SetValue(sl.min);
            }
        }
        if (isField) {
            if (node.attribute("text")) {
                errors.push_back(loc + "a <TextField> shows its 'value', not 'text'");
                return false;
            }
            UITextEdit& edit = el.MakeTextField();
            if (const pugi::xml_attribute a = node.attribute("maxlength")) {
                const std::string v = trim(a.value());
                if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                    errors.push_back(loc + "maxlength: expected a byte count, got '" + v + "'");
                    return false;
                }
                edit.setMaxLength(std::size_t(std::strtoull(v.c_str(), nullptr, 10)));
            } else {
                edit.setMaxLength(0);
            }
            if (const pugi::xml_attribute a = node.attribute("multiline")) {
                const std::string v = lower(trim(a.value()));
                if (v.empty() || v == "true") edit.setMultiline(true);
                else if (v == "false")        edit.setMultiline(false);
                else {
                    errors.push_back(loc + "multiline: expected true|false, got '" + v + "'");
                    return false;
                }
            } else {
                edit.setMultiline(false);
            }
            edit.setMaskCharacter(node.attribute("mask").value());
            // Set LAST, so maxlength trims it and the mask is in place before
            // the display text is derived.
            edit.setValue(node.attribute("value").value());
            edit.MoveToEnd(false);
            el.SyncTextFromEdit();
        }

        if (const char* s = node.attribute("style").value(); s && *s) {
            std::vector<UIDeclaration> decls;
            std::vector<std::string> declErrors;
            if (!UIStyleSheet::ParseDeclarationList(s, decls, declErrors)) {
                for (const auto& e : declErrors) errors.push_back(loc + "style: " + e);
                return false;
            }
            el.setInlineStyle(std::move(decls));
        } else {
            el.setInlineStyle({});
        }
        return true;
    }

    std::string describeEl(const UIElement& el) {
        std::string s = "<";
        s += el.type();
        if (!el.name().empty()) { s += " name='"; s += el.name(); s += "'"; }
        return s + ">";
    }

    // Splits `source.thing` against an inherited scope, the same one-dot rule
    // the hole grammar and every push path already use.
    bool splitScoped(const std::string& raw, const std::string& scope,
                     const std::string& what, const std::string& loc,
                     std::string& outSource, std::string& outName,
                     std::vector<std::string>& errors) {
        const std::string path = trim(raw);
        if (path.empty()) { errors.push_back(loc + what + ": empty"); return false; }
        const size_t dot = path.find('.');
        if (dot != std::string::npos) {
            if (path.find('.', dot + 1) != std::string::npos) {
                errors.push_back(loc + what + ": path '" + path +
                                 "' has more than one '.' (write source.list)");
                return false;
            }
            outSource = trim(path.substr(0, dot));
            outName = trim(path.substr(dot + 1));
        } else {
            outSource = scope;
            outName = path;
        }
        if (outName.empty()) {
            errors.push_back(loc + what + ": path '" + path + "' has no name");
            return false;
        }
        if (outSource.empty()) {
            errors.push_back(loc + what +
                             ": no data source in scope - add data-source= to an "
                             "ancestor, or write \"source." +
                             (what == "repeat" ? std::string("list") : std::string("property")) +
                             "\"");
            return false;
        }
        return true;
    }

    // Walks a BUILT clone, tracking the effective data source, and records
    // every unqualified hole that lands on the row — the column set the pool
    // must seed on every slot, including the ones the window never reaches.
    //
    // Also rejects the write-back paths that would resolve against the row:
    // resolvePush_ CREATES a missing push target, which the next frame's row
    // copy would then overwrite, and a bare action name is looked up on a row
    // source that has no actions at all.
    bool scanRowUsage(const UIElement& el, const std::string& inherited,
                      const std::string& rowScope, std::vector<std::string>& cols,
                      std::vector<std::string>& errors, const std::string& origin) {
        const std::string scope =
            el.dataSourceName().empty() ? inherited : el.dataSourceName();
        const bool onRow = (scope == rowScope);
        const std::string loc = origin + ": " + describeEl(el) + " ";

        for (const UIBinding& b : el.bindings()) {
            if (onRow) {
                for (const UIHole& h : b.tmpl.holes()) {
                    if (h.sourceName.empty()) cols.push_back(h.propName);
                }
            }
            if (onRow && !b.pushProp.empty() && b.pushSource.empty()) {
                errors.push_back(loc + b.label +
                                 ": a bare path inside a repeat writes into the row, "
                                 "which the next frame's copy overwrites - write "
                                 "\"source.property\"");
                return false;
            }
        }
        for (const UIBoundAction& a : el.boundActions()) {
            if (a.sourceName.empty() && onRow) {
                errors.push_back(loc + a.label +
                                 ": a bare action name inside a repeat looks it up on "
                                 "the row, which has none - write \"source.action\"");
                return false;
            }
        }
        for (const auto& c : el.children()) {
            if (!scanRowUsage(*c, scope, rowScope, cols, errors, origin)) return false;
        }
        return true;
    }

    std::unique_ptr<UIElement> buildElement(const pugi::xml_node& node,
                                            std::vector<std::string>& errors,
                                            const std::string& origin,
                                            const std::string& scope,
                                            std::vector<UIRepeatSpec>* specs,
                                            std::vector<UITabSpec>* tabs,
                                            bool asTabPanel = false);

    // Expands `repeat=` into a FIXED pool of clones, once, at load.
    //
    // The template's pugi node is re-built from XML `repeat-count` times rather
    // than deep-copied: nodes are handles into the parsed document, which
    // outlives this call, so there is no need for a UIElement::Clone (and none
    // exists). Every clone is a real element from that point on — the tree
    // never changes shape again, which is the entire reason this is a load-time
    // expansion and not a runtime one.
    bool expandRepeat(const pugi::xml_node& node, UIElement& el,
                      std::vector<std::string>& errors, const std::string& origin,
                      const std::string& scope, std::vector<UIRepeatSpec>* specs,
                      std::vector<UITabSpec>* tabs) {
        const std::string loc = origin + ": " + describe(node) + " ";

        // `specs == nullptr` means we are already inside one. 64 x 64 elements
        // from two attributes, with no node-count guard anywhere to catch it.
        if (!specs) {
            errors.push_back(loc + "repeat: nested 'repeat' is not supported - "
                                   "a row cannot itself repeat");
            return false;
        }

        UIRepeatSpec spec;
        spec.label = trim(node.attribute("repeat").value());
        if (!splitScoped(spec.label, scope, "repeat", loc,
                         spec.listSource, spec.listName, errors)) {
            return false;
        }

        const pugi::xml_attribute countAttr = node.attribute("repeat-count");
        if (!countAttr) {
            errors.push_back(loc + "repeat: 'repeat-count' is required "
                                   "(the pool size is fixed at load)");
            return false;
        }
        const std::string countText = trim(countAttr.value());
        // 64 is not arbitrary: hidden slots still walk the style push and
        // layout read (neither has a display guard) and Layout runs up to three
        // times a frame, so the pool is a floor cost whether it is full or not.
        long count = 0;
        if (countText.empty() ||
            countText.find_first_not_of("0123456789") != std::string::npos ||
            (count = std::strtol(countText.c_str(), nullptr, 10)) < 1 || count > 64) {
            errors.push_back(loc + "repeat-count: expected a count 1..64, got '" +
                             countText + "'");
            return false;
        }
        spec.count = int(count);

        if (const pugi::xml_attribute a = node.attribute("repeat-offset")) {
            if (!splitScoped(a.value(), scope, "repeat-offset", loc,
                             spec.offsetSource, spec.offsetProp, errors)) {
                return false;
            }
        }

        pugi::xml_node templateNode;
        int childCount = 0;
        for (pugi::xml_node c : node.children()) {
            if (c.type() != pugi::node_element) continue;
            if (!childCount) templateNode = c;
            ++childCount;
        }
        if (childCount != 1) {
            errors.push_back(loc + "repeat: expected exactly one element child "
                                   "(the row template), found " +
                             (childCount ? std::to_string(childCount) : std::string("none")));
            return false;
        }

        // Rejected rather than skipped. An `if=` on the template root would be
        // overwritten by the row's own visibility binding — or, if we skipped
        // ours to keep theirs, the surplus slots would never be hidden at all
        // and would sit in layout, painted and hit-testable.
        for (const char* banned : { "if", "data-source", "repeat" }) {
            if (!templateNode.attribute(banned)) continue;
            errors.push_back(loc + "repeat: the row template " + describe(templateNode) +
                             " cannot carry its own '" + banned + "=' - " +
                             (std::string(banned) == "if"
                                  ? "the repeat decides whether a row is shown; move the "
                                    "condition to a child, or publish it as a row column"
                              : std::string(banned) == "data-source"
                                  ? "the repeat points it at the row"
                                  : "a row cannot itself repeat"));
            return false;
        }

        spec.namePrefix = "repeat#" + std::to_string(specs->size());

        std::vector<std::unique_ptr<UIElement>> clones;
        clones.reserve(size_t(spec.count));
        for (int i = 0; i < spec.count; ++i) {
            const std::string slotName = spec.slotSourceName(i);
            auto clone = buildElement(templateNode, errors, origin, slotName, nullptr, tabs);
            if (!clone) return false;
            // AFTER buildElement, both of them: applyAttributes writes
            // setDataSourceName unconditionally from the node and finalises
            // bindings in one shot, so anything set beforehand is discarded.
            clone->setDataSourceName(slotName);

            UIBinding present;
            present.target.kind = UIBindTarget::Kind::Display;
            // Its own label, so where_() and Describe() never present an
            // engine-injected binding as something the author wrote.
            present.label = "repeat present";
            std::string err;
            if (!UITextTemplate::Compile(std::string("{") + kRepeatPresentProp + "}",
                                         present.tmpl, err)) {
                errors.push_back(loc + "repeat: " + err);   // unreachable
                return false;
            }
            auto bs = clone->bindings();
            bs.push_back(std::move(present));
            clone->setBindings(std::move(bs));
            clones.push_back(std::move(clone));
        }

        // Clone 0 stands for all of them: they were built from the same node.
        if (!scanRowUsage(*clones[0], spec.slotSourceName(0), spec.slotSourceName(0),
                          spec.columns, errors, origin)) {
            return false;
        }
        std::sort(spec.columns.begin(), spec.columns.end());
        spec.columns.erase(std::unique(spec.columns.begin(), spec.columns.end()),
                           spec.columns.end());
        spec.readsIndex = std::binary_search(spec.columns.begin(), spec.columns.end(),
                                             std::string(kRepeatIndexProp));
        spec.readsCount = std::binary_search(spec.columns.begin(), spec.columns.end(),
                                             std::string(kRepeatCountProp));

        specs->push_back(std::move(spec));
        for (auto& c : clones) el.AddChild(std::move(c));
        return true;
    }

    // A binding the ENGINE injects. Given its own label so where_() and
    // Describe() never present it as something the author wrote.
    bool injectFlagBinding(UIElement& el, UIBindTarget::Kind kind,
                           const std::string& holePath, const std::string& label,
                           const std::string& className,
                           std::vector<std::string>& errors, const std::string& loc) {
        UIBinding b;
        b.target.kind = kind;
        b.target.className = className;
        b.label = label;
        std::string err;
        if (!UITextTemplate::Compile("{" + holePath + "}", b.tmpl, err)) {
            errors.push_back(loc + label + ": " + err);   // unreachable
            return false;
        }
        // Read-modify-write: applyAttributes finalises bindings in one shot, so
        // this can only run AFTER buildElement returned.
        auto bs = el.bindings();
        bs.push_back(std::move(b));
        el.setBindings(std::move(bs));
        return true;
    }

    // Expands `<TabView>` into a generated header strip plus the authored
    // panels, once, at load. The tree never changes shape again — switching
    // tabs writes a bool, exactly like `if=`.
    bool expandTabView(const pugi::xml_node& node, UIElement& el,
                       std::vector<std::string>& errors, const std::string& origin,
                       const std::string& scope, std::vector<UIRepeatSpec>* specs,
                       std::vector<UITabSpec>* tabs) {
        const std::string loc = origin + ": " + describe(node) + " ";

        if (!tabs) {
            errors.push_back(loc + "tabview: a <TabView> cannot appear here");
            return false;
        }
        if (node.attribute("repeat")) {
            errors.push_back(loc + "tabview: 'repeat' is not supported on a <TabView> - "
                                   "the copies would share one name and one selection");
            return false;
        }

        UITabSpec spec;
        spec.name = trim(node.attribute("name").value());
        if (spec.name.empty()) {
            errors.push_back(loc + "tabview: 'name' is required on a <TabView> - "
                                   "it keys the tab state and the C++ lookup");
            return false;
        }
        for (const UITabSpec& other : *tabs) {
            if (other.name != spec.name) continue;
            errors.push_back(loc + "tabview: '" + spec.name +
                             "' duplicates the name of another <TabView> in this document");
            return false;
        }

        std::vector<pugi::xml_node> tabNodes;
        for (pugi::xml_node c : node.children()) {
            if (c.type() != pugi::node_element) continue;
            if (std::string(c.name()) != "Tab") {
                errors.push_back(loc + "tabview: a <TabView> may only contain <Tab> "
                                       "elements, found " + describe(c));
                return false;
            }
            tabNodes.push_back(c);
        }
        if (tabNodes.empty()) {
            errors.push_back(loc + "tabview: expected 1..32 <Tab> children, found none");
            return false;
        }
        if (int(tabNodes.size()) > kMaxTabs) {
            errors.push_back(loc + "tabview: expected 1..32 <Tab> children, found " +
                             std::to_string(tabNodes.size()));
            return false;
        }

        for (pugi::xml_node t : tabNodes) {
            const std::string label = trim(t.attribute("label").value());
            if (label.empty()) {
                errors.push_back(origin + ": " + describe(t) +
                                 " tab: 'label' is required on a <Tab>");
                return false;
            }
            // Rejected for the same reason a repeat template may not carry one:
            // the injected Display binding would silently overwrite it, and the
            // author would be left with a panel whose condition does nothing.
            if (t.attribute("if")) {
                errors.push_back(origin + ": " + describe(t) +
                                 " tab: the panel cannot carry its own 'if=' - the "
                                 "TabView decides whether a panel is shown; move the "
                                 "condition to a child");
                return false;
            }
            spec.labels.push_back(label);
        }

        if (const pugi::xml_attribute a = node.attribute("bind-selected")) {
            // A BARE PATH, like every other write-back binding, and for the same
            // reason: there is nothing to interpolate, and you cannot un-format
            // a rendered string back into an index.
            if (!splitScoped(a.value(), scope, "bind-selected", loc,
                             spec.bindSource, spec.bindProp, errors)) {
                return false;
            }
        }

        if (const pugi::xml_attribute a = node.attribute("selected")) {
            const std::string v = trim(a.value());
            long idx = -1;
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos ||
                (idx = std::strtol(v.c_str(), nullptr, 10)) < 0 ||
                idx >= long(tabNodes.size())) {
                errors.push_back(loc + "selected: expected an index 0.." +
                                 std::to_string(tabNodes.size() - 1) + ", got '" + v + "'");
                return false;
            }
            spec.initialSelected = int(idx);
        }

        el.AddClass("tab-view");

        // The strip and its headers are GENERATED, so they carry no authored
        // attributes and cannot collide with anything in the file.
        auto strip = std::make_unique<UIElement>();
        strip->setType("TabStrip");
        strip->AddClass("tab-strip");
        for (std::size_t i = 0; i < spec.labels.size(); ++i) {
            auto head = std::make_unique<UIElement>();
            head->setType("TabHeader");
            head->AddClass("tab");
            head->setText(spec.labels[i]);
            // Explicitly: the type-word rule that makes a Button focusable only
            // covers Button and TextField, and a tab strip you cannot reach
            // with Tab is not a control.
            head->setFocusable(true);
            strip->AddChild(std::move(head));
        }
        el.AddChild(std::move(strip));

        // The panels are the AUTHORED <Tab> nodes, built normally — every other
        // attribute on a <Tab> belongs to the panel, including class, style,
        // data-source, on-* and repeat.
        for (std::size_t i = 0; i < tabNodes.size(); ++i) {
            // specs FORWARDED, not null: a repeat inside a tab must work, and
            // its spec must reach the caller. Passing null would fail it with
            // repeat's nesting message, naming a feature the author never used.
            auto panel = buildElement(tabNodes[i], errors, origin, scope, specs, tabs,
                                      /*asTabPanel=*/true);
            if (!panel) return false;
            panel->AddClass("tab-panel");
            const std::string flag = std::string(kTabSourceName) + "." + spec.flagProp(int(i));
            if (!injectFlagBinding(*panel, UIBindTarget::Kind::Display, flag,
                                   "tab panel", std::string(), errors, loc)) {
                return false;
            }
            el.AddChild(std::move(panel));
        }

        tabs->push_back(std::move(spec));
        return true;
    }

    // Builds one element (and its subtree). Returns null on error, having
    // appended a message — the caller discards the whole tree, so a partial
    // parse never reaches the document.
    std::unique_ptr<UIElement> buildElement(const pugi::xml_node& node,
                                            std::vector<std::string>& errors,
                                            const std::string& origin,
                                            const std::string& scope,
                                            std::vector<UIRepeatSpec>* specs,
                                            std::vector<UITabSpec>* tabs,
                                            bool asTabPanel) {
        auto el = std::make_unique<UIElement>();
        el->setType(node.name());
        // A <Tab> reaching here WITHOUT the panel flag is loose in the tree:
        // expandTabView is the only caller that passes it, so this is the only
        // way a stray one arrives.
        if (el->type() == "Tab" && !asTabPanel) {
            errors.push_back(origin + ": " + describe(node) +
                             " tab: <Tab> is only valid as a direct child of a <TabView>");
            return nullptr;
        }
        if (!applyAttributes(node, *el, errors, origin)) return nullptr;

        // An element's own data-source= covers it and everything beneath it,
        // which is the same rule UIBinder::collect_ applies at resolve time.
        const std::string childScope =
            el->dataSourceName().empty() ? scope : el->dataSourceName();

        if (el->type() == "TabView") {
            // Replaces the ordinary child walk: expandTabView builds the strip
            // and the panels itself.
            if (!expandTabView(node, *el, errors, origin, childScope, specs, tabs)) {
                return nullptr;
            }
            return el;
        }

        if (node.attribute("repeat")) {
            // Replaces the ordinary child walk: the one element child is the
            // template, and expandRepeat adds the clones itself.
            if (!expandRepeat(node, *el, errors, origin, childScope, specs, tabs)) return nullptr;
            return el;
        }

        for (pugi::xml_node child : node.children()) {
            if (child.type() != pugi::node_element) continue; // text/comments
            auto builtChild = buildElement(child, errors, origin, childScope, specs, tabs);
            if (!builtChild) return nullptr;
            el->AddChild(std::move(builtChild));
        }
        return el;
    }

} // namespace

bool UIMarkup::LoadInto(UIDocument& doc, const std::string& xml,
                        std::vector<std::string>& errors,
                        const std::string& originName,
                        std::vector<UIRepeatSpec>* outRepeats,
                        std::vector<UITabSpec>* outTabs) {
    pugi::xml_document xdoc;
    const pugi::xml_parse_result res = xdoc.load_string(xml.c_str());
    if (!res) {
        errors.push_back(originName + ":" + std::to_string(res.offset) + ": " +
                         res.description());
        return false;
    }

    // First element node is the root; anything before it (declaration,
    // comments) is skipped.
    pugi::xml_node rootNode;
    for (pugi::xml_node n : xdoc.children()) {
        if (n.type() == pugi::node_element) { rootNode = n; break; }
    }
    if (!rootNode) {
        errors.push_back(originName + ": document has no root element");
        return false;
    }

    // The root never passes through buildElement, so a `repeat=` on it would
    // clear the allow-list and then do exactly nothing.
    if (rootNode.attribute("repeat")) {
        errors.push_back(originName + ": " + describe(rootNode) +
                         " repeat: is not valid on the document root - "
                         "put it on a child element");
        return false;
    }
    // The root never passes through buildElement, so a <TabView> there would
    // expand into nothing at all.
    if (std::string(rootNode.name()) == "TabView") {
        errors.push_back(originName + ": " + describe(rootNode) +
                         " tabview: a <TabView> cannot be the document root");
        return false;
    }

    // Build the whole replacement subtree BEFORE touching the document. The
    // root node maps onto the document's existing root (which the document
    // owns), so only its children and attributes are transferred.
    //
    // Specs are collected HERE and not in applyAttributes, which runs twice on
    // the root (probe, then commit) and would record every spec twice.
    std::vector<UIRepeatSpec> repeats;
    std::vector<UITabSpec> tabs;
    const std::string rootScope = trim(rootNode.attribute("data-source").value());
    std::vector<std::unique_ptr<UIElement>> newChildren;
    for (pugi::xml_node child : rootNode.children()) {
        if (child.type() != pugi::node_element) continue;
        auto built = buildElement(child, errors, originName, rootScope, &repeats, &tabs);
        if (!built) return false; // doc untouched
        newChildren.push_back(std::move(built));
    }

    // The root's own attributes are validated on a SCRATCH element first. A
    // child subtree that fails to parse is simply discarded, but the root is
    // owned by the document and cannot be — so this is how it earns the same
    // "nothing is touched until everything parsed" guarantee.
    {
        UIElement probe;
        probe.setType(rootNode.name());
        if (!applyAttributes(rootNode, probe, errors, originName)) return false;
    }

    // Commit. applyAttributes cannot fail here: the probe above just ran it on
    // the identical node and succeeded.
    UIElement& root = doc.root();
    root.ClearChildren();
    // The root is REUSED across a reload while every child is destroyed and
    // rebuilt, so without this it is the one element whose handlers survive —
    // and the documented place to attach them, UIAssetDocument's bind callback,
    // runs after every successful load. A handler on the root accumulated one
    // copy per reload while the identical handler on a child did not, which is
    // exactly the root/child asymmetry this loader works to avoid elsewhere.
    root.ClearEventListeners();
    root.setType(rootNode.name());
    applyAttributes(rootNode, root, errors, originName);
    for (auto& c : newChildren) root.AddChild(std::move(c));
    // Assigned at COMMIT and nowhere earlier, so a load that failed anywhere
    // above leaves the caller's vector untouched and no pool is ever built for
    // a tree that never reached the document.
    if (outRepeats) *outRepeats = std::move(repeats);
    if (outTabs) *outTabs = std::move(tabs);
    return true;
}

bool UIMarkup::LoadFileInto(UIDocument& doc, const std::string& path,
                            std::vector<std::string>& errors,
                            std::vector<UIRepeatSpec>* outRepeats,
                            std::vector<UITabSpec>* outTabs) {
    // Containment BEFORE opening: authored markup is untrusted content headed
    // for a parser, exactly like model/script/clip/HDRi paths.
    std::filesystem::path contained;
    if (!PathIsContained(/*baseDir=*/"", path, contained)) {
        errors.push_back("rejected UI markup path outside the project: '" + path + "'");
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors.push_back("cannot open '" + path + "'");
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LoadInto(doc, text, errors, path, outRepeats, outTabs);
}

} // namespace MyCoreEngine::ui
