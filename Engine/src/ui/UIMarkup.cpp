#include "UIMarkup.h"

#include "UIElement.h"
#include "UIStyleSheet.h"
#include "../core/PathSandbox.h"

#include <pugixml.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
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
                n == "multiline") {
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
            if (!isField) {
                errors.push_back(loc + "'bind-value' is only valid on a <TextField>");
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
            else {
                errors.push_back(loc + "unknown event '" + evName + "' in '" + n +
                                 "' (click|pointer-down|pointer-up|pointer-enter|"
                                 "pointer-leave|pointer-move)");
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
        for (const char* attr : { "value", "maxlength", "mask", "multiline" }) {
            if (node.attribute(attr) && !isField) {
                errors.push_back(loc + "'" + attr + "' is only valid on a <TextField>");
                return false;
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

    // Builds one element (and its subtree). Returns null on error, having
    // appended a message — the caller discards the whole tree, so a partial
    // parse never reaches the document.
    std::unique_ptr<UIElement> buildElement(const pugi::xml_node& node,
                                            std::vector<std::string>& errors,
                                            const std::string& origin) {
        auto el = std::make_unique<UIElement>();
        el->setType(node.name());
        if (!applyAttributes(node, *el, errors, origin)) return nullptr;

        for (pugi::xml_node child : node.children()) {
            if (child.type() != pugi::node_element) continue; // text/comments
            auto builtChild = buildElement(child, errors, origin);
            if (!builtChild) return nullptr;
            el->AddChild(std::move(builtChild));
        }
        return el;
    }

} // namespace

bool UIMarkup::LoadInto(UIDocument& doc, const std::string& xml,
                        std::vector<std::string>& errors,
                        const std::string& originName) {
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

    // Build the whole replacement subtree BEFORE touching the document. The
    // root node maps onto the document's existing root (which the document
    // owns), so only its children and attributes are transferred.
    std::vector<std::unique_ptr<UIElement>> newChildren;
    for (pugi::xml_node child : rootNode.children()) {
        if (child.type() != pugi::node_element) continue;
        auto built = buildElement(child, errors, originName);
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
    root.setType(rootNode.name());
    applyAttributes(rootNode, root, errors, originName);
    for (auto& c : newChildren) root.AddChild(std::move(c));
    return true;
}

bool UIMarkup::LoadFileInto(UIDocument& doc, const std::string& path,
                            std::vector<std::string>& errors) {
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
    return LoadInto(doc, text, errors, path);
}

} // namespace MyCoreEngine::ui
