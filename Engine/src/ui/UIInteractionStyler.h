#pragma once
// Applies `:hover` and `:active` styling as interaction state changes.
//
// THE PROBLEM THIS SOLVES: the cascade has no undo. Applying a rule copies its
// declarations into the element's Style and records nothing about what they
// overwrote, so when the pointer leaves there is no "remove the hover rule"
// operation to perform. The only correct answer is to reset the element to
// defaults and run the whole cascade again for its CURRENT state — which is
// exactly what this does, and why it is a separate pass rather than a flag on a
// selector.
//
// TWO CONSEQUENCES, both deliberate:
//
//  1. Only elements some pseudo rule could ever reach are watched, decided once
//     at load. An element no `:hover` rule targets is never touched, so the
//     feature costs nothing on a HUD that does not use it.
//
//  2. On a WATCHED element, a raw `style()` write from C++ does not survive a
//     state change — the reset discards it. Text is carried across explicitly
//     (it is not a cascadable property), and bindings are re-applied straight
//     afterwards, so those two keep working. Anything else should be authored
//     as a class, a binding, or a stylesheet rule. This is the whole reason
//     watching is opt-in by selector: the caveat only applies where an author
//     asked for state-dependent styling.
#include "../core/Core.h"
#include "UIBinding.h"
#include "UIElement.h"
#include "UIStyleSheet.h"

#include <cstdint>
#include <vector>

namespace MyCoreEngine::ui {

    class ENGINE_API UIInteractionStyler {
    public:
        // Scans the tree for elements a pseudo-class rule could match and
        // remembers them with their current state. Call AFTER the cascade, on a
        // freshly loaded tree. `binder` may be null; when given, an element's
        // bindings are re-applied after each re-cascade, because the reset
        // discards what they wrote.
        void Rebuild(UIDocument& doc, const UIStyleSheet& sheet, UIBinder* binder);
        // Drops the watch list. Must run BEFORE the tree is freed: every entry
        // holds a raw UIElement*.
        void Clear();

        // Call once per frame AFTER UpdatePointer, which is where hover and
        // press state is decided. Returns true if anything was restyled, in
        // which case the caller should lay out again — a state rule may change
        // padding or size, not just colour.
        bool Update();

        std::size_t watchedCount() const { return watched_.size(); }
        // Diagnostics: how many elements were restyled by the last Update.
        std::size_t lastRestyled() const { return lastRestyled_; }

    private:
        struct Watched {
            UIElement* el = nullptr;
            bool hovered = false;
            bool pressed = false;
        };

        void collect_(UIElement& el);
        void restyle_(UIElement& el) const;

        std::vector<Watched> watched_;
        UIDocument*         doc_ = nullptr;
        const UIStyleSheet* sheet_ = nullptr;
        UIBinder*           binder_ = nullptr;
        std::uint32_t       seenEpoch_ = 0;
        std::size_t         lastRestyled_ = 0;
    };

} // namespace MyCoreEngine::ui
