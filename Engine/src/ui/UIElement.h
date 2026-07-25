#pragma once
// Retained UI element tree — the "DOM" half of the UI system.
//
// RETAINED, not immediate: you build the tree once and MUTATE it (set a style,
// change text, add/remove a child). That is the opposite of ImGui, which the
// editor uses, and it is the right model for game UI because layout is
// expensive, elements have identity (needed for events, animation and binding
// later), and most frames change nothing.
//
// Layout is flexbox, computed by yoga. No yoga type is visible here: each
// element owns an opaque handle, so the layout engine can be swapped without
// touching a line of authored UI.
#include "../core/Core.h"
#include "UIStyle.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {
    class Renderer2D;
    class Font;
}

namespace MyCoreEngine::ui {

    // Computed by Layout(): absolute position (top-left, screen pixels) and
    // size of this element's border box.
    struct ComputedLayout {
        glm::vec2 position{ 0.0f };
        glm::vec2 size{ 0.0f };
    };

    class ENGINE_API UIElement {
    public:
        explicit UIElement(std::string name = {});
        ~UIElement();
        UIElement(const UIElement&) = delete;
        UIElement& operator=(const UIElement&) = delete;

        // Takes ownership and returns a borrowed pointer for chaining. The
        // parent owns its children, so a subtree dies with its root.
        UIElement* AddChild(std::unique_ptr<UIElement> child);
        // Convenience: construct, adopt, and hand back the raw pointer.
        UIElement* AddChild(std::string name = {});
        // Detaches and returns ownership; null if not a child of this element.
        std::unique_ptr<UIElement> RemoveChild(UIElement* child);
        void ClearChildren();

        const std::vector<std::unique_ptr<UIElement>>& children() const { return children_; }
        UIElement* parent() const { return parent_; }

        const std::string& name() const { return name_; }
        void setName(std::string n) { name_ = std::move(n); }

        // Mutate freely; the next Layout() picks changes up. Style writes are
        // pushed into the layout engine on Layout, and yoga only re-solves
        // subtrees whose values actually changed, so setting a style to the
        // value it already has costs nothing.
        Style&       style()       { return style_; }
        const Style& style() const { return style_; }

        // Text is part of Style, but changing it must also invalidate the
        // element's MEASURED size — use this rather than writing style().text
        // directly, or a label keeps its old width until something else dirties
        // it.
        void setText(std::string t);

        const ComputedLayout& layout() const { return layout_; }

        // Depth-first search by name; null when absent. Linear — fine for the
        // handful of named elements a HUD has, and the hook a future
        // CSS-selector query would replace.
        UIElement* Find(const std::string& name);

    private:
        friend class UIDocument;

        void* yogaNode_ = nullptr;   // YGNodeRef, opaque here on purpose
        UIElement* parent_ = nullptr;
        std::vector<std::unique_ptr<UIElement>> children_;
        std::string name_;
        Style style_{};
        ComputedLayout layout_{};
    };

    // Owns a UI tree and drives layout + painting for it.
    class ENGINE_API UIDocument {
    public:
        UIDocument();
        ~UIDocument();
        UIDocument(const UIDocument&) = delete;
        UIDocument& operator=(const UIDocument&) = delete;

        UIElement& root() { return *root_; }
        const UIElement& root() const { return *root_; }

        // Solves flexbox for a viewport of the given size, filling every
        // element's ComputedLayout. `font` may be null, in which case text
        // elements measure as empty (they still lay out, just with no size of
        // their own) — a missing font must not collapse the whole HUD.
        void Layout(float viewportW, float viewportH, const Font* font = nullptr);

        // Walks the laid-out tree and emits draws. Parents paint before
        // children (painter's algorithm), and `overflowHidden` pushes a clip
        // rect for the subtree. Call between Renderer2D::BeginScreen/End.
        void Draw(Renderer2D& r2d, const Font* font = nullptr, int baseLayer = 0) const;

    private:
        // Recursion helpers. They live here, as members of the class that is
        // already UIElement's friend, so they can reach the layout node and
        // computed rect without UIElement having to expose either. None of
        // their signatures mentions a yoga type, which is what keeps the layout
        // engine swappable.
        static void pushStyles_(UIElement& el);
        static void readLayout_(UIElement& el, const glm::vec2& parentOrigin);
        static void draw_(const UIElement& el, Renderer2D& r2d,
                          const Font* font, int layer);

        std::unique_ptr<UIElement> root_;
    };

} // namespace MyCoreEngine::ui
