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
#include "UIScale.h"
#include "UITextureCache.h"
#include "UIStyle.h"
#include "UIEvent.h"
#include "UIStyleSheet.h"   // UIDeclaration (for inline styles)
#include "UIBinding.h"      // UIBinding / UIBoundAction (authored, owned here)
#include "UINav.h"          // UINavState (directional navigation)
#include "UISlider.h"       // UISliderState (owned by slider elements)
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
        // This element's painted rect UNIONED with every visible descendant's,
        // filled by readLayout_ on the way back up. draw_ culls a subtree that
        // cannot intersect the clip it is under.
        //
        // The UNION is what makes culling safe: an absolutely positioned
        // descendant may legitimately paint outside its parent's own rect, and
        // a long text leaf paints outside its box too (the measure callback
        // clamps to the parent's offer, but DrawText does not). Testing the
        // element's own rect would cull glyphs that are on screen. It is a
        // deliberate SUPERSET of what is painted — conservative costs a few
        // wasted quads, optimistic costs correctness.
        glm::vec2 subtreeMin{ 0.0f }, subtreeMax{ 0.0f };
    };

    // Scroll state for one `overflow: scroll` element.
    //
    // Held behind a pointer on UIElement for the same reason UITextEdit is:
    // it is state ONE kind of element has, and inlining it would cost every
    // label in the tree the same bytes.
    //
    // It lives OUTSIDE Style deliberately. UIStyleSheet::Recascade assigns a
    // fresh `Style{}` before re-applying rules, and the interaction styler
    // drives that on every :hover edge — a scroll offset stored in Style would
    // snap a list back to the top the moment you moved the mouse over a row.
    struct UIScrollState {
        // Unrounded, so a sub-pixel wheel or drag still integrates. readLayout_
        // rounds it on use: UIWorld already rounds the document origin so every
        // glyph lands on a texel, and a fractional scroll would undo that for
        // the whole subtree.
        glm::vec2 offset{ 0.0f };
        // Where the offset is heading. Equal to `offset` unless the element
        // carries `scroll-behavior: smooth`, in which case AdvanceTime walks
        // one toward the other.
        glm::vec2 target{ 0.0f };

        // The content's extent in the scroller's own space. `contentMin` is
        // normally zero but goes NEGATIVE when a justify/align rule centres or
        // end-aligns overflowing content — flexbox puts the overflow on the
        // leading side, and clamping to [0, ...] would make it unreachable
        // forever. Measured, not assumed.
        glm::vec2 contentMin{ 0.0f }, contentMax{ 0.0f };

        // The reachable offset range, recomputed EVERY layout so a resize or a
        // binding that shrinks the content can never strand the offset.
        glm::vec2 minOffset{ 0.0f }, maxOffset{ 0.0f };

        // Absolute rects for the overlay bars, filled during layout. A
        // zero-size thumb means "this axis does not scroll", which is the one
        // signal both the painter and the hit test read.
        ComputedLayout trackX, thumbX, trackY, thumbY;

        // A DECLARED extent, overriding the measured one. Zero/unset means
        // "measure the children", which is the normal case.
        glm::vec2 declaredExtent{ 0.0f };
        bool hasDeclaredExtent = false;

        bool scrollsX() const { return maxOffset.x > minOffset.x; }
        bool scrollsY() const { return maxOffset.y > minOffset.y; }
    };

    // The four bar rects for one scroller, in absolute pixels. A zero-size thumb
    // means that axis paints no bar — the one signal the painter and the hit
    // test share.
    struct ScrollBarRects {
        ComputedLayout trackX, thumbX, trackY, thumbY;
    };

    // Pure geometry, exposed so it can be asserted directly: the UI suite has no
    // GL and no font, and a scrollbar is the one part of this system whose
    // correctness is entirely arithmetic.
    // `cornerInsetPx` pulls both tracks in from the ends of the box, so a bar
    // on a ROUNDED scroller cannot paint its square end outside the silhouette.
    // Clipping cannot save it: clipping is the axis-aligned scissor, and the
    // corner it needs to cut is not.
    ENGINE_API ScrollBarRects ComputeScrollBars(
        const glm::vec2& boxPos, const glm::vec2& boxSize,
        const glm::vec2& offset, const glm::vec2& minOffset, const glm::vec2& maxOffset,
        float barWidth, float minThumb, bool alwaysVisible,
        float cornerInsetPx = 0.0f);

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

        // Selector identity, mirroring CSS: `type` is the element kind
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

        // ---- scrolling ----
        // Non-null only on an element that is (or has been) `overflow: scroll`.
        // Created by the layout pass and never destroyed by a style change: a
        // class that toggles on hover must not reset where the user scrolled
        // to. Its offset IS zeroed while the element is not a scroller, so a
        // de-scrolled element can never displace its children into space that
        // nothing clips.
        const UIScrollState* scrollState() const { return scroll_.get(); }
        glm::vec2 scrollOffset()  const { return scroll_ ? scroll_->offset    : glm::vec2(0.0f); }
        glm::vec2 minScroll()     const { return scroll_ ? scroll_->minOffset : glm::vec2(0.0f); }
        glm::vec2 maxScroll()     const { return scroll_ ? scroll_->maxOffset : glm::vec2(0.0f); }
        // Content extent in the scroller's own space; zero on a non-scroller.
        glm::vec2 contentSize() const {
            return scroll_ ? scroll_->contentMax - scroll_->contentMin : glm::vec2(0.0f);
        }

        // Clamped against the last computed range; takes visible effect at the
        // next Layout. No-op on an element that has never been a scroller.
        // Returns true if the offset actually moved.
        bool SetScrollOffset(glm::vec2 px);

        // Moves the offset the least amount that brings an ABSOLUTE rect inside
        // this scroller's box. The primitive behind focus-follow, and what app
        // code calls to pin a log to its tail:
        //   log->ScrollIntoView(last->layout().position, last->layout().size);
        bool ScrollIntoView(const glm::vec2& posAbs, const glm::vec2& sizePx);

        // Declare how big the content REALLY is, overriding what the laid-out
        // children measure. This is the one piece a hand-rolled virtual list
        // cannot do without.
        //
        // The extent is otherwise derived purely from the children that exist,
        // so keeping six live rows out of a thousand would honestly report six
        // rows of content: the range would collapse, the thumb would vanish and
        // the wheel would walk past. Declaring the full height lets you keep a
        // window of live rows and position them yourself from scrollOffset().
        //
        // The engine does NOT virtualise for you — see the manual. This is the
        // hook that makes doing it possible.
        void SetContentExtent(const glm::vec2& px);
        void ClearContentExtent();

        // Depth-first search by name; null when absent. Linear — fine for the
        // handful of named elements a HUD has, and the hook a future
        // CSS-selector query would replace.
        UIElement* Find(const std::string& name);

        // ---- events ----
        // Multiple handlers per type are allowed and run in registration
        // order, so a widget's own behaviour and a caller's extra listener can
        // coexist without one clobbering the other.
        void AddEventListener(UIEventType type, UIEventHandler handler);
        // Attach on behalf of `owner`, which must be a stable non-null address.
        // Only RemoveOwnedListeners(owner) takes these back out, so a subsystem
        // that re-runs can replace its own handlers without disturbing the
        // app's — or duplicating its own.
        void AddOwnedListener(const void* owner, UIEventType type, UIEventHandler handler);
        // Removes only what was attached with this exact owner. A null owner is
        // a no-op: null means "the app added this by hand", and only the app
        // may take one of those back out.
        void RemoveOwnedListeners(const void* owner);
        void OnClick(UIEventHandler h)        { AddEventListener(UIEventType::Click, std::move(h)); }
        void OnPointerDown(UIEventHandler h)  { AddEventListener(UIEventType::PointerDown, std::move(h)); }
        void OnPointerUp(UIEventHandler h)    { AddEventListener(UIEventType::PointerUp, std::move(h)); }
        void OnPointerEnter(UIEventHandler h) { AddEventListener(UIEventType::PointerEnter, std::move(h)); }
        void OnPointerLeave(UIEventHandler h) { AddEventListener(UIEventType::PointerLeave, std::move(h)); }
        void OnPointerMove(UIEventHandler h)  { AddEventListener(UIEventType::PointerMove, std::move(h)); }
        // Observes the wheel; it does NOT claim it. The built-in scroll still
        // runs unless the handler calls StopPropagation.
        void OnWheel(UIEventHandler h)        { AddEventListener(UIEventType::Wheel, std::move(h)); }
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
        // A scope root CONFINES navigation to its subtree while it is visible
        // (focus-scope="true"). See UIDocument::SyncFocusScopes.
        bool isFocusScope() const { return focusScope_; }
        void setFocusScope(bool on) { focusScope_ = on; }

        UITextEdit*       textEdit()       { return edit_.get(); }
        const UITextEdit* textEdit() const { return edit_.get(); }
        // Held the same way and for the same reason: a <Slider> owns a number,
        // and everything else about it is authored.
        UISliderState*       slider()       { return slider_.get(); }
        const UISliderState* slider() const { return slider_.get(); }
        // Turns this element into a text field (idempotent). Done by markup for
        // `<TextField>`, and available to code building a tree by hand.
        UITextEdit& MakeTextField();
        UISliderState& MakeSlider();
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

        // Returns whether any listener actually RAN, which is how Back tells
        // "a scope closed itself" from "nobody wanted it".
        bool dispatchLocal_(UIEvent& e);

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
        std::unique_ptr<UISliderState> slider_;  // <Slider> only
        bool focusScope_ = false;               // focus-scope="true"
        std::unique_ptr<UIScrollState> scroll_; // `overflow: scroll` only
        std::uint32_t textRevision_ = 0;
        // Last fontScale handed to the layout engine, so pushStyles_ can tell
        // when a text element needs re-measuring. See there for why yoga cannot
        // work this out for itself.
        float         pushedFontScale_ = 1.0f;

        // `owner` is null for a handler the APP attached and non-null for one
        // attached on behalf of a subsystem — today only UIBinder, which passes
        // its own address. That is the whole reason the field exists: the binder
        // re-collects whenever the structure epoch moves, and without a way to
        // remove exactly its own handlers it appended a duplicate every time.
        struct Listener {
            const void*     owner = nullptr;
            UIEventType     type{};
            UIEventHandler  fn;
        };
        std::vector<Listener> listeners_;
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

        // ---- scaling ----
        // Authored lengths in a .cstyle are in REFERENCE-RESOLUTION pixels;
        // everything this class computes and reports — ComputedLayout, scroll
        // offsets, ScrollIntoView, the origin — is in REAL surface pixels.
        // scale() is the conversion, and it is recomputed at the top of every
        // Layout(). See UIScale.h for the rule in full.
        void SetScaleSettings(const UIScaleSettings& s) { scaleSettings_ = s; }
        const UIScaleSettings& scaleSettings() const { return scaleSettings_; }

        // The WHOLE UI surface, in real pixels, that the scale is computed
        // against. Zero (the default) means "whatever viewport Layout was
        // given". A document occupying a fraction of the screen must be told
        // the whole screen, or a quarter-width sidebar would scale ITSELF down
        // on exactly the large display this feature exists to serve.
        void SetSurfaceSize(const glm::vec2& px) { surfaceSize_ = px; }
        const glm::vec2& surfaceSize() const { return surfaceSize_; }

        // In effect as of the last Layout(). Exactly 1.0f in Constant mode.
        float scale() const { return scale_; }

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
        // `textures` resolves background-image ids. Null simply paints no
        // images — a document drawn without one still lays out and paints
        // everything else, the same graceful degradation a missing font gets.
        void Draw(Renderer2D& r2d, const Font* font = nullptr, int baseLayer = 0,
                  UITextureCache* textures = nullptr) const;

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
        // `font` is optional, and only used to keep a text field's caret inside
        // its box after an edit. Without it the editing still works; the view
        // just does not follow the caret past the field's width.
        void UpdateKeyboard(const UIKeyboardState& keyboard, const Font* font = nullptr);

        // The system clipboard, supplied by the host — GLFW and ImGui both have
        // one and neither belongs in an engine header. Without these, Ctrl+C/X/V
        // simply do nothing rather than half-working against a private buffer
        // the rest of the machine cannot see.
        void SetClipboardHandlers(std::function<void(const std::string&)> write,
                                  std::function<std::string()> read) {
            clipboardWrite_ = std::move(write);
            clipboardRead_ = std::move(read);
        }

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

        // Activate the focused element: synthesize a Click on it, through the
        // ordinary bubble path a real click takes.
        //
        // This is what makes the UI operable without a mouse, and it is one
        // function rather than a feature because UIBinding attaches every
        // authored on-* as a plain Click listener -- so this lights up every
        // on-click in every document with no markup change anywhere. It is
        // equally the gamepad's A button: a pad needs no separate path, only a
        // host that calls this.
        //
        // Returns false when there is nothing to activate, INCLUDING when the
        // focused element is a text edit -- a field owns its own Enter (a
        // multi-line one inserts a newline), and a single-line field leaves it
        // for a container that may want to mean "submit". Neither should be a
        // click on the field itself.
        bool ActivateFocused();
        // How long a synthesized press stays lit. Long enough to register as a
        // press at 60Hz, short enough not to lag the action it confirms.
        static constexpr float kActivateFlashSeconds = 0.08f;

        // Where navigation is currently confined, or the document root when no
        // scope is open. Everything that walks the focus order goes through
        // this, which is what makes "the menu takes navigation" one rule rather
        // than a special case in each caller.
        UIElement& focusRoot();

        // Reconcile the scope STACK against what is currently visible.
        //
        // Visibility is the single source of truth, deliberately: a panel is
        // shown by an `if=` bound to app state, and making the stack follow
        // that -- rather than having a second, parallel notion of "open" --
        // means the two cannot disagree. Pushing remembers what was focused and
        // moves focus into the new scope; popping puts it back.
        //
        // Called once per frame from UpdateNav, and idempotent when nothing
        // changed, so a host that calls it twice pays a walk and nothing else.
        void SyncFocusScopes();

    private:
        UIElement* scopeMemoryFor_(UIElement* scope);
        void rememberScopeFocus_(UIElement* scope);
        void focusIntoScope_(UIElement* scope);
    public:

        // Move focus in a DIRECTION.
        //
        // GEOMETRIC, over the laid-out rects, within the current focus scope.
        //
        // Tab order is wrong for a 2D layout and it shows: three chips in a row
        // share a y, so a linear walk made Down step through LOW, MEDIUM and
        // HIGH one press at a time before reaching the next setting. Down now
        // means "the nearest thing actually below", so a row costs one press to
        // pass and Left/Right move within it.
        //
        // The rules, all four of which are the tuning:
        //   - a candidate must CLEAR the current element along the axis, not
        //     merely differ from it, or same-row siblings count as below;
        //   - off-axis distance is the GAP BETWEEN RECTS, zero when they
        //     overlap, so a wide row below a narrow item wins over something
        //     nearer but off to the side;
        //   - off-axis is penalised harder than along-axis, so directly-ahead
        //     beats diagonal;
        //   - NO WRAP. Down at the bottom does nothing. Tab still wraps.
        //
        // Falls back to the linear walk when nothing has geometry yet, rather
        // than trapping focus: a document can be navigated before its first
        // Layout, and a degenerate rect must never be a dead end.
        //
        // With nothing focused, ANY direction focuses the first focusable —
        // otherwise the first press of a d-pad on a freshly opened menu would
        // do nothing, which reads as a dead controller.
        UIElement* FocusMove(UINavDir dir);

        // The whole gamepad path. Returns true if anything was consumed, so a
        // host can give an unhandled `back` its own meaning (close the menu,
        // quit) rather than guessing.
        bool UpdateNav(const UINavState& nav);

        // "Back out of what I am in", and the one implementation of it: the
        // pad's B and the keyboard's Escape both land here, so the two cannot
        // drift on what backing out means.
        //
        // Returns whether anything HANDLED it. False means the document had
        // nothing to close and no focus to drop -- see backWentUnhandled().
        bool Back();

        // True if a Back since the last call found no taker. Consuming clears
        // it, so the host reads it exactly once per frame and a press cannot be
        // acted on twice. This is the seam that lets a game say what back means
        // at its ROOT menu, where the UI itself has no answer.
        bool ConsumeBackUnhandled() {
            const bool b = backUnhandled_;
            backUnhandled_ = false;
            return b;
        }

    private:
        // Cursor -> value for a captured slider. Private because the capture is
        // the only caller: a slider's value moves from a drag, from the two-way
        // binding, or from a key -- never from an arbitrary poke.
        void applySliderFromCursor_(UIElement& el, const glm::vec2& cursor);
    public:

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
        // `parentScrolls` decides whether this element's flex-shrink is forced
        // to 0 — see pushStyle for why that, and not yoga's own overflow, is
        // what makes a scroller's content exist.
        static void pushStyles_(UIElement& el, bool parentScrolls);
        // Measures each scroller's content and clamps its offset. Runs BETWEEN
        // the flexbox solve and readLayout_: it reads yoga's raw
        // parent-relative rects, because measuring from layout_ — which
        // readLayout_ has already displaced by the offset — would make the
        // extent shrink as you scroll and turn the clamp into a fixed point
        // that pins the content at half its length.
        // A MEMBER, not static: it re-arms the document's animation flag, which
        // is what lets a smooth scroll survive a writer that did not know to.
        void measureScroll_(UIElement& el);
        static void readLayout_(UIElement& el, const glm::vec2& parentOrigin);
        // Fills the four bar rects from the freshly-computed absolute box.
        static void updateScrollBars_(UIElement& el);
        // `focused` and `caretVisible` are threaded through rather than read
        // from a member because draw_ is static and recursive; only the one
        // focused element in the whole walk cares. `clip` is the resolved
        // clipping ancestor's rect, or null when nothing clips.
        static void draw_(const UIElement& el, Renderer2D& r2d, const Font* font,
                          int layer, const UIElement* focused, bool caretVisible,
                          const ComputedLayout* clip);
        static UIElement* hitTest_(UIElement& el, const glm::vec2& pos);
        // The scrollbar is painted from draw_ and is invisible to hitTest_, so
        // a press on it is resolved separately and ahead of everything else.
        static UIElement* hitScrollBar_(UIElement& el, const glm::vec2& pos,
                                        int& axisOut, bool& onThumbOut);
        // Shared by the track click and PageUp/PageDown, so they agree.
        static float pageAmount_(const UIElement& el, int axis);
        // Folds the one-wheel-two-axes problem into one place. See the .cpp.
        static glm::vec2 axisLockedDelta_(const UIElement& el, glm::vec2 raw);
        // Scrolls the nearest ancestor of `from` that can take this key.
        bool keyboardScroll_(UIElement* from, UIKey key);
        // Scrolls a field's own text so the caret stays inside its box.
        void followCaret_(UIElement& el, const Font* font);
        // Brings the focused element inside its clipping ancestors. Runs at the
        // END of Layout, where the absolute rects it needs are finally correct.
        void revealFocus_();
        // One easing step for every animating scroller in the subtree.
        void advanceScroll_(UIElement& el, float dt);
        // Bubbles `e` from `target` up through its ancestors, honouring
        // StopPropagation.
        // Returns whether any listener ran ANYWHERE along the chain.
        static bool bubble_(UIElement* target, UIEvent& e);
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
        // A synthesized press, held briefly so it can be SEEN.
        //
        // pressed_ is otherwise written only on the pointer-down edge, so
        // `:active` -- the only press state a stylesheet can express -- was
        // unreachable from a gamepad or from Enter. Confirming anything on the
        // input this menu exists for changed no pixels at all.
        //
        // A deadline on the document clock rather than a countdown, so a long
        // frame cannot stretch the flash and a stall cannot swallow it.
        UIElement* activated_ = nullptr;
        float      activateUntil_ = 0.0f;
        // The open focus scopes, innermost LAST.
        std::vector<UIElement*> scopeStack_;
        // Set by Back() when nothing took it; drained by ConsumeBackUnhandled.
        bool backUnhandled_ = false;
        // What was last focused INSIDE each scope, so re-entering one puts you
        // back where you were rather than at the top of its list.
        //
        // Keyed on the scope rather than stored alongside the stack, because a
        // scope's memory has to outlive its own closing: opening SETTINGS hides
        // the verb column, which POPS it, and the whole point is that closing
        // SETTINGS returns to the SETTINGS verb rather than to NEW GAME.
        //
        // A vector rather than a map: a document has a handful of scopes, and
        // this is walked only when one opens or closes.
        std::vector<std::pair<UIElement*, UIElement*>> scopeMemory_;
        bool  hadPointer_ = false;       // pointer was inside last frame
        bool  wasDown_ = false;
        glm::vec2 lastPos_{ 0.0f };
        // Reset to 0 on every edit and caret move, so the caret is SOLID the
        // instant you type. A caret that keeps blinking on its own schedule
        // while you type reads as dropped input.
        float caretClock_ = 0.0f;
        glm::vec2 origin_{ 0.0f };
        // The surface Layout was last solved for. Draw seeds its cull rect with
        // it, so culling works outside a scroller too — otherwise a HUD whose
        // only clipping element is one list would cull nothing anywhere else.
        glm::vec2 viewport_{ 0.0f };
        UIScaleSettings scaleSettings_{};
        glm::vec2       surfaceSize_{ 0.0f };
        float           scale_ = 1.0f;
        // Thumb being dragged. Validated with isInTree_ like hovered_/pressed_:
        // a hot reload frees the whole tree, and a stylesheet-only save triggers
        // one, so a pointer held across frames without that check is a
        // use-after-free during the advertised edit-and-save loop.
        // WHAT OWNS THE POINTER, if anything.
        //
        // A press on a scrollbar thumb, a scrollbar track or a slider takes the
        // pointer for as long as it is HELD, and everything underneath stops
        // seeing hover, move and click until it is released. That is what makes
        // dragging a thumb past the end of its track keep working — the gesture
        // every user makes at 0% and 100% — instead of the control going dead
        // the moment the cursor leaves its box.
        //
        // This began as three scrollbar-specific members. It is one mechanism
        // with a tag because a slider drag IS a thumb drag: same press, same
        // grab offset, same clamp, different value on the other end.
        enum class Capture : std::uint8_t { None, ScrollThumb, ScrollTrack, Slider };
        Capture    captureKind_ = Capture::None;
        UIElement* capture_ = nullptr;
        // Held while a press sits on the TRACK, so the row underneath receives
        // no move, no hover and no Click on release.
        // The element a wheel GESTURE latched onto. Chaining is only safe with
        // this: it is what stops a list reaching its end mid-gesture from
        // handing the rest of the flick to whatever contains it.
        UIElement* wheelLatch_ = nullptr;
        float lastWheelTime_ = -1000.0f;   // in docClock_ seconds
        float docClock_ = 0.0f;            // accumulated by AdvanceTime
        int   captureAxis_ = 0;      // 0 = x, 1 = y
        float captureGrab_ = 0.0f;   // cursor offset within the thumb at press
        bool  scrollDirty_ = false;
        bool  pendingFocusReveal_ = false;
        // Set by measureScroll_ when the tree holds any scroll state at all, so
        // AdvanceTime can skip its walk entirely on a document with none.
        bool  hasScrollers_ = false;
        // ~55ms to close the gap. Fast enough to feel immediate, slow enough to
        // read as motion rather than a jump.
        static constexpr float kSmoothScrollRate = 18.0f;
        std::function<void(const std::string&)> clipboardWrite_;
        std::function<std::string()>            clipboardRead_;

    public:
        // True (and cleared) when input moved a scroll offset since the last
        // Layout. The host ORs this into its relayout decision so a wheel notch
        // or a thumb drag is applied in the SAME frame — otherwise the thumb
        // trails the cursor for the whole drag.
        bool ConsumeScrollDirty() { const bool d = scrollDirty_; scrollDirty_ = false; return d; }
    };

} // namespace MyCoreEngine::ui
