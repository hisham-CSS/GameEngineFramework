#pragma once
// The runtime half of `<TabView>`: which panel is showing, and the input that
// changes it.
//
// A peer of UIRepeatPool, and built the same way for the same reason — the
// whole thing is expanded ONCE at load, so switching tabs never moves the
// structure epoch and never makes another document's binder re-collect.
//
// Panel visibility is an injected Display BINDING, never a style().display
// write. UIStyleSheet::Recascade resets an element's Style wholesale and then
// re-applies only its BINDINGS, so a raw write is erased the first time any
// :hover or :focus rule touches the panel — and the hidden panel reappears.
// This is exactly the mechanism `repeat=` uses for $present, reused verbatim.
#include "../core/Core.h"
#include "UIDataSource.h"
#include "UITabs.h"

#include <cstddef>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;
    class UIDocument;

    class ENGINE_API UITabView {
    public:
        UITabView() = default;
        UITabView(const UITabView&) = delete;
        UITabView& operator=(const UITabView&) = delete;

        // Walks the expanded TabView once, caches the header and panel
        // pointers, seeds every state property on `src`, and attaches one owned
        // Click listener per header plus one owned KeyDown listener on the
        // strip.
        //
        // Call after the markup load and BEFORE UIBinder::Rebuild, so the
        // force-apply at the end of Rebuild already sees the right panel
        // visible — otherwise every panel is up for frame one.
        // `ctx` is used only to resolve a `bind-selected` path; a spec
        // without one never touches it.
        void Build(const UITabSpec& spec, UIDocument& doc, UIDataSource& src,
                   UIBindingContext& ctx,
                   std::vector<std::string>& errors, const std::string& origin);

        // Re-publishes the index and the N flags. Equality-gated by
        // UIDataSource, so an idle TabView costs N integer compares and wakes
        // nothing. Select() writes through immediately, so this is for the load
        // path and for a Select() made from game code between frames.
        //
        // It also services the two-way `bind-selected` link in both directions.
        // The conflict rule, stated because there has to be one: the SOURCE
        // wins when it moved since this last looked, and the ELEMENT wins
        // otherwise. Gameplay writing the property between frames therefore
        // opens that tab, while a click made after this ran is published on the
        // next pass — the same shape bind-value on a TextField already has, and
        // it means the most recent intent survives.
        void Refresh();

        int selected() const { return selected_; }
        std::size_t count() const { return headers_.size(); }
        const std::string& name() const { return spec_.name; }
        const UITabSpec& spec() const { return spec_; }
        // Null when i is out of range. Tests and tooling.
        UIElement* header(std::size_t i) const;
        UIElement* panel(std::size_t i) const;

        // Clamped to [0, count). Writes the source IMMEDIATELY, so a click is
        // visible in the frame of the click: UIWorld's post-input
        // UpdateToTarget picks the Display write up and reports wroteLayout, and
        // the conditional second Layout lands the swap in the same frame.
        //
        // Also moves focus to the newly selected header when focus was inside
        // the panel being hidden. The document's own eviction is lazy — it runs
        // in UpdateKeyboard, which UIWorld calls only for the KEYBOARD-target
        // document — so a background or non-interactive document would keep
        // :focus lit inside a hidden panel indefinitely. And even on the
        // keyboard document it drops focus to NOTHING, which sends the next Tab
        // back to the top of the whole HUD rather than to the tab you just
        // opened.
        void Select(int i);

    private:
        void publish_();
        // True when `el` is `root` or anywhere beneath it.
        static bool isWithin_(const UIElement* el, const UIElement* root);

        UITabSpec     spec_;
        UIDocument*   doc_ = nullptr;
        UIDataSource* src_ = nullptr;
        UIElement*    strip_ = nullptr;
        std::vector<UIElement*> headers_, panels_;
        int selected_ = 0;
        int indexIdx_ = -1;
        std::vector<int> flagIdx_;

        // The `bind-selected` link. Null means there is none.
        UIDataSource* boundSrc_ = nullptr;
        int  boundIdx_ = -1;
        // What the app source held last time this looked. The compare is
        // against THIS rather than against a copy of our own selection, so a
        // write by anything else at all is noticed.
        int  lastBoundValue_ = 0;
    };

} // namespace MyCoreEngine::ui
