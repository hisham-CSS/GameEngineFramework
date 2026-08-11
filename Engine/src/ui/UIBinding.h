#pragma once
// The VIEW half of data binding: what the markup authored, and the runtime that
// applies it.
//
// Two halves with very different lifetimes, and keeping them apart is what makes
// hot reload safe:
//
//   UIBinding      authored, immutable, and OWNED BY THE ELEMENT. A rebuilt tree
//                  carries its own bindings and no runtime state at all, so a
//                  binding can never outlive the element it points at — it IS
//                  part of it.
//   UIBinder       a flat index over every binding in a document, rebuilt from
//                  scratch whenever the tree's structure changes. Holds raw
//                  UIElement*, which is exactly why it re-collects on a
//                  structure-epoch change instead of trusting them.
//
// There is deliberately NO expression language. A hole is a path, an optional
// converter chain, and an optional format spec. Arithmetic and comparison would
// need '<' and '&&' inside XML attribute values, which XML forbids — every
// comparison would have to be written backwards ("0.3 > health") forever.
// Derived values are named C++ converters instead: real functions you can
// breakpoint, and a typo answered with the list of names that exist.
#include "../core/Core.h"
#include "UIDataSource.h"
#include "UIEvent.h"
#include "UIStyleSheet.h"   // UIDeclaration::Prop

#include <cstdint>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;
    class UIDocument;

    // One {hole}: `{path | converter | converter : decimals}`.
    struct UIHole {
        std::string sourceName;   // "" => inherit from the nearest data-source ancestor
        std::string propName;
        std::vector<std::string> converters;   // applied left to right
        int decimals = -1;        // -1 = "%g"
    };

    // Literal chunks interleaved with holes, compiled once at markup load.
    // literals.size() == holes.size() + 1, always — so rendering is a simple
    // alternating walk with no special cases at either end.
    class ENGINE_API UITextTemplate {
    public:
        // "SCORE {score}", "{health | percent : 1}", "a {{literal}} brace".
        static bool Compile(const std::string& text, UITextTemplate& out, std::string& err);

        bool isConstant() const { return holes_.empty(); }
        // Exactly one hole with no literal text on either side. That case skips
        // stringification entirely, so a bound colour or length never has to
        // survive a round trip through text.
        bool isSingleHole() const {
            return holes_.size() == 1 && literals_.size() == 2 &&
                   literals_[0].empty() && literals_[1].empty();
        }
        const std::vector<UIHole>& holes() const { return holes_; }
        const std::vector<std::string>& literals() const { return literals_; }

    private:
        std::vector<std::string> literals_;
        std::vector<UIHole>      holes_;
    };

    struct UIBindTarget {
        enum class Kind : std::uint8_t { Text, Style, Display, Class, State, Value };
        // Element state that can be pushed BACK to the source.
        enum class StateProp : std::uint8_t { Hovered, Pressed, Focused };

        Kind kind = Kind::Text;
        UIDeclaration::Prop styleProp{};  // Kind::Style
        UIPropValueKind     valueKind{};  // Kind::Style
        std::string         className;    // Kind::Class
        StateProp           state{};      // Kind::State
    };

    // One authored binding. Immutable after load.
    struct ENGINE_API UIBinding {
        UIBindTarget   target{};
        UITextTemplate tmpl;
        bool           negate = false;   // Display/Class: a leading '!'
        // Kind::State and Kind::Value: the source property WRITTEN to. These
        // are the only bindings that flow element -> source, and they carry a
        // bare path rather than a template because there is nothing to
        // interpolate — you cannot un-format a rendered string back into a
        // value, so a to-source binding is one path and nothing else.
        std::string    pushSource;       // "" => inherit
        std::string    pushProp;
        // "text", "bind width", "if", "push-hovered" — used verbatim in
        // messages, so a diagnostic always names what the author wrote.
        std::string    label;
    };

    // An authored `on-<event>="actionName"`.
    struct ENGINE_API UIBoundAction {
        UIEventType type = UIEventType::Click;
        std::string sourceName;   // "" => inherit
        std::string actionName;
        std::string label;        // "on-click"
    };

    struct UIBindTick {
        std::size_t applied = 0;
        // True only when a write can change a BOX. A colour-only write never
        // triggers a re-layout, which is what keeps the post-input pass free on
        // the frames that do not need it.
        bool wroteLayout = false;
    };

    class ENGINE_API UIBinder {
    public:
        // Walks the tree once, resolves data-source inheritance, collects every
        // authored binding into a flat array, resolves sources/properties/
        // converters, attaches action listeners, and ends by applying every
        // binding UNCONDITIONALLY.
        //
        // That final force-apply is the point: it is what replaces the app's
        // old "re-push everything you cached" step after a reload, and it is why
        // a label never flashes its authored placeholder for one frame as you
        // save the file.
        // `sheet` is optional and used only for diagnostics: it lets Rebuild
        // NOTE that a stylesheet declaration is shadowed by a binding, which
        // would otherwise be a silent no-op the first time someone edits the
        // .cstyle and nothing happens.
        void Rebuild(UIDocument& doc, UIBindingContext& ctx, std::string originName,
                     const UIStyleSheet* sheet = nullptr);
        // Drops the index. Must be called BEFORE the tree is freed: the
        // invariant is that the binder never holds a pointer into a tree that
        // is being rebuilt, not even for the length of one function.
        void Clear();

        // Source -> element. Call BEFORE Layout, so a changed label is measured
        // at its new width on the frame it changes.
        UIBindTick UpdateToTarget();

        // Element -> source. Call AFTER UpdatePointer and UpdateKeyboard, which
        // is where the state and the values it pushes are decided.
        //
        // Only `push-*` and a TextField's `value` flow this way. There is no
        // general two-way binding, deliberately: you cannot un-format a
        // rendered string back into a value, so anything with a converter chain
        // or literal text around it is one-directional by construction.
        UIBindTick UpdateToSource();

        // Re-applies every binding on ONE element, unconditionally.
        //
        // For callers that reset an element's Style and re-run the cascade —
        // :hover restyling does exactly that. The version-compare in
        // UpdateToTarget would skip these, because the SOURCE has not changed;
        // what changed is that the value written into the element was thrown
        // away. Returns the number applied.
        std::size_t ReapplyFor(const UIElement* el);
        // ReapplyFor over a whole subtree. Needed wherever the cascade was
        // re-run for a REGION rather than a single element — a contextual
        // sheet's class toggle or state change — because every element the
        // re-cascade reset has lost whatever its bindings had written.
        std::size_t ReapplyForSubtree(UIElement* root);

    // A class was added or removed on `el` by something OTHER than a class
    // binding -- a widget managing its own state, such as UITabView's selected
    // header. AddClass/RemoveClass only record the class; they do NOT re-run
    // the cascade, and the cascade has no undo, so without this the new rules
    // never match and the old ones never stop matching.
    //
    // Same operation the Kind::Class branch performs, and re-entrant-safe for
    // the same reason: the re-cascade re-applies this element's bindings, and a
    // class binding among them must not re-enter itself.
    void RecascadeAfterClassChange(UIElement* el);

        const std::vector<std::string>& errors() const { return errors_; }
        const std::vector<std::string>& notes()  const { return notes_;  }

        // True once, if a Rebuild has happened since the last call. A re-collect
        // triggered MID-RUN — by the structure epoch, or by a source appearing
        // that a pending binding was waiting for — recomputes errors_ and notes_
        // into vectors that only the load path ever read, so a binding that
        // broke while the game was running reported itself to nobody. The owner
        // drains on this.
        bool consumeRecollected() { const bool r = recollected_; recollected_ = false; return r; }
        bool ok() const { return errors_.empty(); }
        std::size_t bindingCount() const { return entries_.size(); }
        std::size_t unresolvedCount() const;

        // One line per live binding:
        //   "<Label name='scoreLabel'> text <- hud.score (int) v=7"
        // "It is not in this list" is a one-call diagnosis of the frozen-HUD
        // case, which is otherwise indistinguishable from a value that never
        // changes.
        std::vector<std::string> Describe() const;

    private:
        // Trivially copyable, no std::string and no std::function, so the
        // per-frame scan walks contiguous PODs. Converter functions live in
        // convPool_ as pointers into registry-owned std::functions — never
        // copies, which would double every capture.
        // One resolved hole: which source it reads, which property index, and
        // the version last consumed FROM THAT SOURCE.
        //
        // Per hole rather than per entry because a template may read several
        // sources — `text="{x} / {b.y}"` — and hanging change detection off the
        // first hole alone left the rest stale. Caching the index is the other
        // half: render_ used to call ctx_->Find and IndexOf on EVERY apply,
        // which is a map lookup and a linear strcmp scan per hole per frame,
        // flatly contradicting UIDataSource's "searched once per load".
        struct HoleRef {
            UIDataSource* src = nullptr;      // null => this hole is pending
            std::int32_t  propIndex = -1;
            std::uint32_t lastVersion = 0;
        };

        struct Entry {
            UIElement*       el = nullptr;
            const UIBinding* binding = nullptr;  // into el->bindings_; alive while el is
            UIDataSource*    src = nullptr;      // null => pending resolution
            std::uint32_t    lastSourceVersion = 0;
            std::int32_t     propIndex = -1;
            // 32-bit, not 16: resolve_ assigns pool sizes into these without a
            // range check, and the bounds checks downstream catch an index that
            // is OUT of range, not one that WRAPPED — a wrapped index is in
            // range and silently reads somebody else's hole. A `repeat=` of 64
            // slots multiplies every template in its subtree by 64, which is
            // how a document gets within reach of 65536 in the first place.
            std::uint32_t    convFirst = 0, convCount = 0;
            // Range into holePool_, parallel to convFirst/convCount.
            std::uint32_t    holeFirst = 0, holeCount = 0;
            bool             layoutAffecting = false;
            bool             reported = false;   // latch, re-armed on a kind change
            std::uint8_t     reportedKind = 0;
            // Kind::State / Kind::Value only: where to write back to. No cache
            // of the last value written — UpdateToSource compares against what
            // the SOURCE currently holds, which is both simpler and immune to a
            // cache going stale when something else writes the same property.
            std::int32_t     pushIndex = -1;
            UIDataSource*    pushSrc = nullptr;
            // THE ABSENT-SLOT GATE. Set only when this element's own scope is a
            // repeat SLOT source: where to read that slot's `$present`. While
            // it reads false the binding is skipped entirely -- not applied,
            // and not reported.
            //
            // A pool writes an EMPTY value into every column of an absent slot,
            // deliberately, so a stale row cannot linger when the window slides
            // onto a shorter list. But an empty value is Kind::None, and any
            // binding that needs a TYPED one -- a bool class toggle, an `if=`,
            // a converter -- was calling that a document error. A four-slot log
            // that starts empty reported four errors at boot for a state that
            // is completely normal.
            //
            // Null on everything that is not in a pool, which is almost
            // everything, so the check costs one pointer compare.
            UIDataSource*    gateSrc = nullptr;
            std::int32_t     gateIndex = -1;
        };
        struct ActionEntry {
            UIElement*    el = nullptr;
            UIDataSource* src = nullptr;
            int           actionIndex = -1;
        };

        void collect_(UIElement& el, const std::string& inheritedSource);
        bool holesMoved_(const Entry& e) const;
        void stampHoles_(Entry& e);
        bool resolve_(Entry& e, const std::string& inheritedSource);
        // Resolves the write-back target of a State/Value binding, reporting a
        // read-only property rather than dropping the write in silence.
        void resolvePush_(Entry& e, const std::string& inheritedSource);
        // Fills gateSrc/gateIndex when this element sits in a repeat slot.
        void resolveSlotGate_(Entry& e, const std::string& scope);
        // Is that slot currently holding no row?
        static bool slotAbsent_(const Entry& e);
        bool apply_(Entry& e);
        // Renders `tmpl` into scratch_; false with an error already reported if
        // a hole could not be read or converted.
        bool render_(Entry& e, const UIHole* holes, std::size_t holeCount);
        // Reads + converts a single hole, without stringifying. The fast path
        // for a style binding, and the only way a bound colour or length avoids
        // a round trip through text.
        bool evalSingle_(Entry& e, UIValue& out);
        bool applyStyle_(Entry& e);
        void reportOnce_(Entry& e, const UIValue& v, const std::string& what);
        void noteShadowedDeclarations_(const UIElement& el, const UIStyleSheet& sheet);
        std::string where_(const UIElement& el, const UIBinding& b) const;

        std::vector<Entry>              entries_;
        std::vector<ActionEntry>        actions_;
        std::vector<const UIConvertFn*> convPool_;
        std::vector<HoleRef> holePool_;
        bool recollected_ = false;
        std::vector<std::string>        errors_, notes_;
        // Capacity survives across frames, so the text path allocates nothing
        // on a steady frame.
        std::string  scratch_;

        UIDocument*         doc_ = nullptr;
        UIBindingContext*   ctx_ = nullptr;
        const UIStyleSheet* sheet_ = nullptr;   // diagnostics only
        std::string         origin_;
        std::uint32_t     seenCtxRev_ = 0;
        std::uint32_t     seenEpoch_ = 0;
        std::uint32_t     seenSourceSum_ = 0;
        // Set while a class binding is re-cascading its element, so ReapplyFor
        // cannot re-enter the binding that triggered it.
        bool              inRecascade_ = false;
        // Re-cascade the region a class toggle on `el` can affect, and re-apply
        // the bindings inside it. Which region depends on whether the sheet has
        // contextual rules at all — see the definition.
        void recascadeFor_(UIElement* el);
    };

} // namespace MyCoreEngine::ui
