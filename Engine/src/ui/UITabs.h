#pragma once
// What the markup loader expanded for one `<TabView>`.
//
// Free of UIElement and UIDataSource, like UIRepeat.h, so UIMarkup.h can
// include it without dragging the tree in.
#include "../core/Core.h"

#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    struct ENGINE_API UITabSpec {
        // The author's name=, verbatim. It keys the state properties, the C++
        // lookup, and every diagnostic — which is why it is required rather
        // than generated: a generated key would appear in error messages and in
        // {holes} the author has to type, and match nothing in their file.
        std::string name;
        int initialSelected = 0;
        std::vector<std::string> labels;

        // Property names on the reserved source. `demo` is the current index;
        // `demo_2` is "tab 2 is selected".
        //
        // An underscore rather than a '#' because these are meant to be
        // AUTHORABLE: {__tabs.demo} in a label reads the current index without
        // any wiring at all. A '#' would parse, but nobody would guess it.
        std::string indexProp() const { return name; }
        std::string flagProp(int i) const { return name + "_" + std::to_string(i); }

        // `bind-selected="hud.activeTab"` — the TWO-WAY link, resolved and
        // driven by UITabView rather than by UIBinder.
        //
        // It lives there because that is where the selection already lives: the
        // view owns the index, holds a UIDataSource&, and already runs once per
        // frame. Routing it through the binder instead would need a whole new
        // binding Kind with its own write path — and the tempting shortcut,
        // reusing Kind::Value, resolves cleanly, reports ok(), and then never
        // writes anything, because that path reads a UITextEdit and bails when
        // there is not one.
        std::string bindSource;   // "" => no two-way link
        std::string bindProp;
    };

    // Reserved, and '__'-prefixed so an author cannot collide by accident.
    // Registered by UIAssetDocument, one per document.
    constexpr const char* kTabSourceName    = "__tabs";
    // Toggled on the selected header. A CLASS rather than a pseudo-class:
    // a real :selected would need a fifth bool on every UIElement, a parser
    // entry, a compound-matcher case, and a fifth slot in the interaction
    // styler's watch struct — to buy only that an author cannot remove it by
    // hand. AddClass/RemoveClass do not move the structure epoch, which is what
    // matters here; they do NOT re-cascade on their own, so UITabView asks the
    // binder to do it on the two headers whose class actually changed.
    constexpr const char* kTabSelectedClass = "selected";

    constexpr int kMaxTabs = 32;

} // namespace MyCoreEngine::ui
