#include "UIMarkup.h"

#include "UIElement.h"
#include "UIStyleSheet.h"
#include "../core/PathSandbox.h"

#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace MyCoreEngine::ui {

namespace {

    std::vector<std::string> splitClasses(const std::string& s) {
        std::vector<std::string> out;
        std::istringstream in(s);
        std::string t;
        while (in >> t) out.push_back(t);
        return out;
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
                n == "data-source") {
                continue;
            }
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
        el.setBindings(std::move(bindings));

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
