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

    // Builds one element (and its subtree). Returns null on error, having
    // appended a message — the caller discards the whole tree, so a partial
    // parse never reaches the document.
    std::unique_ptr<UIElement> buildElement(const pugi::xml_node& node,
                                            std::vector<std::string>& errors,
                                            const std::string& origin) {
        auto el = std::make_unique<UIElement>();
        el->setType(node.name());

        if (const char* n = node.attribute("name").value(); n && *n) el->setName(n);
        for (auto& c : splitClasses(node.attribute("class").value())) {
            el->AddClass(std::move(c));
        }
        if (const char* t = node.attribute("text").value(); t && *t) el->setText(t);

        if (const char* s = node.attribute("style").value(); s && *s) {
            std::vector<UIDeclaration> decls;
            std::vector<std::string> declErrors;
            if (!UIStyleSheet::ParseDeclarationList(s, decls, declErrors)) {
                for (const auto& e : declErrors) {
                    errors.push_back(origin + ": <" + node.name() + "> style: " + e);
                }
                return nullptr;
            }
            el->setInlineStyle(std::move(decls));
        }

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

    std::vector<UIDeclaration> rootInline;
    if (const char* s = rootNode.attribute("style").value(); s && *s) {
        std::vector<std::string> declErrors;
        if (!UIStyleSheet::ParseDeclarationList(s, rootInline, declErrors)) {
            for (const auto& e : declErrors) errors.push_back(originName + ": root style: " + e);
            return false;
        }
    }

    // Commit.
    UIElement& root = doc.root();
    root.ClearChildren();
    root.setType(rootNode.name());
    if (const char* n = rootNode.attribute("name").value(); n && *n) root.setName(n);
    // Replace, don't merge: a reload must not accumulate classes from the
    // previous version of the file.
    root.ClearClasses();
    for (auto& c : splitClasses(rootNode.attribute("class").value())) {
        root.AddClass(std::move(c));
    }
    if (const char* t = rootNode.attribute("text").value(); t && *t) root.setText(t);
    root.setInlineStyle(std::move(rootInline));
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
