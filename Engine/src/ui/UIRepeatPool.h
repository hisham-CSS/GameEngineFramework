#pragma once
// The runtime half of `repeat=`: a FIXED pool of row sources whose contents
// slide over a game-owned list.
//
// The tree is expanded once, at load, and never changes shape again. That is
// the whole design, and it is forced by UIElement::structureEpoch() being
// PROCESS-WIDE: a list that grew and shrank its children would make every
// binder in every document re-collect and re-resolve on the frames it moved.
//
// So the pool inverts it. The elements stand still and the DATA moves: slot i
// is written with row (offset + i) each frame. Every UIDataSource setter is
// equality-gated, so a slot whose row did not change bumps no version and
// re-applies no binding — which means sliding a window costs exactly the slots
// that actually changed, and an idle list costs one integer compare.
//
// An ABSENT slot (one the window has run past the end of) is written with an
// EMPTY value in every column, deliberately, so a stale row cannot linger. The
// BINDER knows to skip a slot whose $present is false rather than trying to
// type an empty value — see UIBinder::slotAbsent_ — so a row template may
// carry a class toggle, an `if=` or a converter without caring how long the
// list is.
#include "../core/Core.h"
#include "UIDataSource.h"
#include "UIRepeat.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class ENGINE_API UIRepeatPool {
    public:
        UIRepeatPool() = default;
        UIRepeatPool(const UIRepeatPool&) = delete;
        UIRepeatPool& operator=(const UIRepeatPool&) = delete;
        // Frees the slot sources. It deliberately does NOT unregister them: it
        // holds no context handle, because UIAssetDocument declares its pools
        // before ctx_ and a self-unregistering destructor would be reaching
        // into an object that is already gone. Teardown is the only
        // unregistration path, and it is explicit.
        ~UIRepeatPool() = default;

        // Creates spec.count slot sources at stable addresses, seeds EVERY
        // column in spec.columns plus the four $-properties on each of them,
        // and registers them as internal sources.
        //
        // Call after the markup load and BEFORE UIBinder::Rebuild: registering
        // afterwards moves the context revision and buys a full re-collect on
        // the next frame for nothing.
        //
        // Reports an unknown source or a missing list into `errors`, once. It
        // does not fail: the list may be registered later, and a pool with no
        // list simply hides every slot.
        void Build(const UIRepeatSpec& spec, UIBindingContext& ctx,
                   std::vector<std::string>& errors, const std::string& origin);

        // Unregisters every name Build registered — from registeredNames_,
        // never from a spec, a count delta or a list name, because all three
        // are gone the moment an author DELETES the repeat — and only then
        // frees the slot storage. Getting that order wrong does not produce a
        // stale read: it leaves a freed UIDataSource* registered, which
        // UIBindingContext::sourceVersionSum dereferences every single frame.
        void Teardown(UIBindingContext& ctx);

        // Slides the window. Call once per frame BEFORE the binder reads.
        //
        // Re-Finds the list source BY NAME every call: caching that pointer
        // across frames is exactly how a re-pointed or removed source ends up
        // being read through a stale one.
        //
        // Has NO error channel and must never grow one. It runs twice per
        // document per frame, and a per-frame diagnostic is a per-frame
        // allocation for a condition that cannot be fixed without a reload.
        void Refresh(UIBindingContext& ctx);

        std::size_t size() const { return slots_.size(); }
        const UIRepeatSpec& spec() const { return spec_; }
        // Null when i is out of range. Tests and tooling.
        const UIDataSource* slotSource(std::size_t i) const {
            return i < slots_.size() ? slots_[i].src.get() : nullptr;
        }

    private:
        struct Slot {
            // unique_ptr rather than by value: UIDataSource is non-copyable AND
            // non-movable on purpose, so a std::vector<UIDataSource> does not
            // compile — and could not be allowed to, since the binder caches a
            // raw pointer to each one.
            std::unique_ptr<UIDataSource> src;
            std::vector<int> colIndex;   // parallel to spec_.columns
            int slotIdx = -1, indexIdx = -1, countIdx = -1, presentIdx = -1;
        };

        UIRepeatSpec             spec_;
        std::vector<Slot>        slots_;
        std::vector<std::string> registeredNames_;

        // COMPARED, never dereferenced: it exists only to notice that the name
        // now resolves to a different object.
        const void*   lastSrc_ = nullptr;
        std::uint32_t lastListVersion_ = 0;
        long long     lastOffset_ = 0;
        bool          primed_ = false;
    };

} // namespace MyCoreEngine::ui
