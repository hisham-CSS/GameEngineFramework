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
#include "UIEvent.h"
#include "UIStyleSheet.h"   // UIDeclaration (for inline styles)
#include "UIBinding.h"      // UIBinding / UIBoundAction (authored, owned here)
#include "UITextField.h"    // UITextEdit (owned by text-field elements)

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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

        // Selector identity, mirroring CSS/USS: `type` is the element kind
        // ("Element", "Label", "Button" — matched by a bare type selector),
        // `name` is the #id, and classes are the .class list. All three exist
        // so a stylesheet can target elements without the code knowing about
        // the stylesheet.
        const std::string& type() const { return type_; }
        void setType(std::string t) { type_ = std::move(t); }

        const std::vector<std::string>& classes() const { return classes_; }
        bool HasClass(const std::string& c) const;
        void AddClass(std::string c);      // no-op if already present
        void RemoveClass(const std::string& c);
        void ClearClasses() { classes_.clear(); }

        // Declarations from a markup `style="..."` attribute. Stored rather
        // than baked into style() because, exactly as in CSS, inline styles
        // outrank EVERY selector rule: UIStyleSheet::ApplyToElement replays
        // these last, so re-applying a sheet (hot reload) can never lose them.
        const std::vector<UIDeclaration>& inlineStyle() const { return inlineStyle_; }
        void setInlineStyle(std::vector<UIDeclaration> decls) { inlineStyle_ = std::move(decls); }

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

        // Bumped by setText, right next to the measurement invalidation. A raw
        // `style().text = x` does NOT bump it, which is what makes "the write
        // went through setText and therefore re-measured" assertable without a
        // font — and a font needs a GL context (Font::IsValid() is a texture id).
        std::uint32_t textRevision() const { return textRevision_; }

        // ---- data binding ----
        // Authored bindings and actions for this element, parsed from markup.
        // OWNED HERE on purpose: they die with the element, so a hot reload
        // cannot leave a live binding pointing at freed memory. UIBinder's flat
        // index is only a cache over these.
        const std::vector<UIBinding>& bindings() const { return bindings_; }
        void setBindings(std::vector<UIBinding> b);
        const std::vector<UIBoundAction>& boundActions() const { return actions_; }
        void setBoundActions(std::vector<UIBoundAction> a);

        // The data source name this element declares for itself and its whole
        // subtree ("" = inherit from an ancestor), from `data-source=`.
        const std::string& dataSourceName() const { return dataSource_; }
        void setDataSourceName(std::string s);

        // Bumped by anything that changes the SHAPE of a tree: AddChild,
        // RemoveChild, ClearChildren, setBindings, setBoundActions, and
        // destruction.
        //
        // UIBinder caches raw UIElement* and would dereference a freed element
        // if gameplay (or an event handler — UpdatePointer explicitly allows
        // it) restructured the tree between frames. Comparing one integer at
        // the top of every binding pass forces a re-collect instead. It is
        // process-wide and unsynchronised, which is safe for the same reason
        // the layout font scope is: the UI runs inside the render pass, on one
        // thread. A false positive from another document costs one extra tree
        // walk, never correctness.
        static std::uint32_t structureEpoch();

        const ComputedLayout& layout() const { return layout_; }

        // Depth-first search by name; null when absent. Linear — fine for the
        // handful of named elements a HUD has, and the hook a future
        // CSS-selector query would replace.
        UIElement* Find(const std::string& name);

        // ---- events ----
        // Multiple handlers per type are allowed and run in registration
        // order, so a widget's own behaviour and a caller's extra listener can
        // coexist without one clobbering the other.
        void AddEventListener(UIEventType type, UIEventHandler handler);
        void OnClick(UIEventHandler h)        { AddEventListener(UIEventType::Click, std::move(h)); }
        void OnPointerDown(UIEventHandler h)  { AddEventListener(UIEventType::PointerDown, std::move(h)); }
        void OnPointerUp(UIEventHandler h)    { AddEventListener(UIEventType::PointerUp, std::move(h)); }
        void OnPointerEnter(UIEventHandler h) { AddEventListener(UIEventType::PointerEnter, std::move(h)); }
        void OnPointerLeave(UIEventHandler h) { AddEventListener(UIEventType::PointerLeave, std::move(h)); }
        void OnPointerMove(UIEventHandler h)  { AddEventListener(UIEventType::PointerMove, std::move(h)); }
        void OnFocusIn(UIEventHandler h)      { AddEventListener(UIEventType::FocusIn, std::move(h)); }
        void OnFocusOut(UIEventHandler h)     { AddEventListener(UIEventType::FocusOut, std::move(h)); }
        void OnKeyDown(UIEventHandler h)      { AddEventListener(UIEventType::KeyDown, std::move(h)); }
        void OnTextInput(UIEventHandler h)    { AddEventListener(UIEventType::TextInput, std::move(h)); }
        void OnValueChanged(UIEventHandler h) { AddEventListener(UIEventType::ValueChanged, std::move(h)); }
        void ClearEventListeners();

        // ---- focus and enablement ----
        // Focusable elements are what Tab walks and what a click focuses. Off
        // by default: a HUD is mostly decoration, and a Tab order full of
        // panels and labels is worse than none.
        bool isFocusable() const { return focusable_; }
        void setFocusable(bool f) { focusable_ = f; }

        // A disabled element takes no pointer hit, is skipped by Tab, and
        // matches `:disabled`. Its SUBTREE is disabled too — a disabled panel
        // whose buttons still worked would be a trap.
        bool isEnabled() const { return enabled_; }
        void setEnabled(bool e) { enabled_ = e; }

        // Maintained by UIDocument::SetFocus. Only ONE element in a document
        // has it, unlike hover, which runs up the ancestor chain.
        bool isFocused() const { return focused_; }

        // ---- text entry ----
        // Non-null only on a text field. Held behind a pointer rather than
        // inline because a caret, a selection and a length limit are state ONE
        // kind of element has, and putting them on UIElement would cost every
        // label in the tree.
        UITextEdit*       textEdit()       { return edit_.get(); }
        const UITextEdit* textEdit() const { return edit_.get(); }
        // Turns this element into a text field (idempotent). Done by markup for
        // `<TextField>`, and available to code building a tree by hand.
        UITextEdit& MakeTextField();
        // Pushes the edit buffer's DISPLAY text into style().text through
        // setText, so measurement, layout and painting need to know nothing
        // about text fields. Called after every edit.
        void SyncTextFromEdit();

        // Interaction state, maintained by UIDocument::UpdatePointer.
        // `hovered` is true for the whole ancestor chain under the pointer
        // (CSS :hover semantics), so a button and the panel containing it are
        // both hovered. `pressed` is true only for the element the press landed
        // on.
        //
        // These are what UIInteractionStyler reads to apply `:hover` and
        // `:active` rules, so a stylesheet is usually the better place to
        // express what interaction LOOKS like; these accessors are for
        // behaviour that styling cannot express.
        bool isHovered() const { return hovered_; }
        bool isPressed() const { return pressed_; }

    private:
        friend class UIDocument;

        void dispatchLocal_(UIEvent& e);

        void* yogaNode_ = nullptr;   // YGNodeRef, opaque here on purpose
        UIElement* parent_ = nullptr;
        std::vector<std::unique_ptr<UIElement>> children_;
        std::string name_;
        std::string type_ = "Element";
        std::vector<std::string> classes_;
        std::vector<UIDeclaration> inlineStyle_;
        Style style_{};
        ComputedLayout layout_{};

        std::vector<UIBinding>     bindings_;
        std::vector<UIBoundAction> actions_;
        std::string   dataSource_;
        std::unique_ptr<UITextEdit> edit_;   // text fields only
        std::uint32_t textRevision_ = 0;
        // Last fontScale handed to the layout engine, so pushStyles_ can tell
        // when a text element needs re-measuring. See there for why yoga cannot
        // work this out for itself.
        float         pushedFontScale_ = 1.0f;

        std::vector<std::pair<UIEventType, UIEventHandler>> listeners_;
        bool hovered_ = false;
        bool pressed_ = false;
        bool focused_ = false;
        bool focusable_ = false;
        bool enabled_ = true;
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

        // Where this document's root sits on the UI surface, in surface pixels.
        // Layout produces ABSOLUTE rects, so moving the origin moves everything
        // the document does — painting, hit-testing and clipping alike — with
        // no other change anywhere. That is what lets a document occupy part of
        // the screen instead of all of it.
        void SetOrigin(const glm::vec2& px) { origin_ = px; }
        const glm::vec2& origin() const { return origin_; }

        // Walks the laid-out tree and emits draws. Parents paint before
        // children (painter's algorithm), and `overflowHidden` pushes a clip
        // rect for the subtree. Call between Renderer2D::BeginScreen/End.
        void Draw(Renderer2D& r2d, const Font* font = nullptr, int baseLayer = 0) const;

        // ---- input ----
        // Feed once per frame AFTER Layout (hit-testing needs computed rects)
        // and before Draw. Runs the hover/press state machine and dispatches
        // events; handlers may safely mutate styles, and even the tree.
        //
        // `font` is optional and only used to place a text field's caret from a
        // click. Without it a click still focuses the field, it just leaves the
        // caret where it was — the same graceful degradation as everywhere else
        // a missing font appears.
        void UpdatePointer(const UIPointerState& pointer, const Font* font = nullptr);

        // ---- keyboard ----
        // Feed once per frame AFTER UpdatePointer, so clicking a field and
        // typing into it works within a single frame.
        //
        // Tab and Shift+Tab are handled here and move focus; everything else is
        // dispatched to the focused element and BUBBLES, so a form can handle
        // Enter in one place. A handler that consumes a key calls
        // StopPropagation, which is also what stops Tab being treated as
        // navigation — that is how a future multi-line field would keep its
        // literal tabs.
        void UpdateKeyboard(const UIKeyboardState& keyboard);

        // Advances the document's clock. Only the caret blink uses it today;
        // transitions and animation will. Call once per frame with the frame
        // delta, before Draw.
        void AdvanceTime(float dt);

        // ---- focus ----
        // Null clears it. Refuses elements that are not focusable, not enabled,
        // hidden, or not in this tree — so a caller cannot strand focus
        // somewhere the user can never Tab out of. Fires FocusOut then FocusIn.
        void SetFocus(UIElement* el);
        void ClearFocus() { SetFocus(nullptr); }
        UIElement* focused() const { return focused_; }

        // Moves focus to the next (or previous) focusable element in DOCUMENT
        // order, wrapping. Returns the newly focused element, or null when the
        // document has nothing focusable at all.
        UIElement* FocusNext(bool backwards = false);

        // Deepest PICKABLE element containing `pos`, or null. Topmost wins:
        // children are tested in reverse paint order, and an `overflowHidden`
        // parent that does not contain the point rejects its whole subtree.
        UIElement* HitTest(const glm::vec2& pos);

        UIElement* hovered() const { return hovered_; }
        UIElement* pressed() const { return pressed_; }

    private:
        // Recursion helpers. They live here, as members of the class that is
        // already UIElement's friend, so they can reach the layout node and
        // computed rect without UIElement having to expose either. None of
        // their signatures mentions a yoga type, which is what keeps the layout
        // engine swappable.
        static void pushStyles_(UIElement& el);
        static void readLayout_(UIElement& el, const glm::vec2& parentOrigin);
        // `focused` and `caretVisible` are threaded through rather than read
        // from a member because draw_ is static and recursive; only the one
        // focused element in the whole walk cares.
        static void draw_(const UIElement& el, Renderer2D& r2d, const Font* font,
                          int layer, const UIElement* focused, bool caretVisible);
        static UIElement* hitTest_(UIElement& el, const glm::vec2& pos);
        // Bubbles `e` from `target` up through its ancestors, honouring
        // StopPropagation.
        static void bubble_(UIElement* target, UIEvent& e);
        // True if `el` is still reachable from the root. Cached hover/press
        // pointers must be validated this way because a handler (or gameplay)
        // may have removed the element between frames; comparing addresses
        // during a top-down walk never dereferences a dangling pointer, which
        // walking parent_ upwards from a destroyed element would.
        bool isInTree_(const UIElement* el) const;

        // Depth-first walk collecting every element eligible for keyboard
        // focus. Document order IS the tab order: it is what the author already
        // sees in the markup, and it needs no tabindex to get out of sync with.
        static void collectFocusables_(UIElement& el, std::vector<UIElement*>& out);
        // Disabled or display:none anywhere up the chain makes an element
        // unreachable, so both have to be checked against ANCESTORS, not just
        // the element itself.
        static bool isInteractable_(const UIElement& el);

        std::unique_ptr<UIElement> root_;
        UIElement* hovered_ = nullptr;   // deepest element under the pointer
        UIElement* pressed_ = nullptr;   // element that received PointerDown
        UIElement* focused_ = nullptr;   // keyboard focus; at most one
        bool  hadPointer_ = false;       // pointer was inside last frame
        bool  wasDown_ = false;
        glm::vec2 lastPos_{ 0.0f };
        // Reset to 0 on every edit and caret move, so the caret is SOLID the
        // instant you type. A caret that keeps blinking on its own schedule
        // while you type reads as dropped input.
        float caretClock_ = 0.0f;
        glm::vec2 origin_{ 0.0f };
    };

} // namespace MyCoreEngine::ui
