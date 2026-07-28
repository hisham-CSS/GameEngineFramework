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
        // AUTHORABLE: {__tabs.demo} in a label is the read path, and it is what
        // stands in for the two-way binding U20 deliberately does not ship. A
        // '#' would parse, but nobody would guess it.
        std::string indexProp() const { return name; }
        std::string flagProp(int i) const { return name + "_" + std::to_string(i); }
    };

    // Reserved, and '__'-prefixed so an author cannot collide by accident.
    // Registered by UIAssetDocument, one per document.
    constexpr const char* kTabSourceName    = "__tabs";
    // Toggled on the selected header. A CLASS rather than a pseudo-class:
    // a real :selected would need a fifth bool on every UIElement, a parser
    // entry, a compound-matcher case, and a fifth slot in the interaction
    // styler's watch struct — to buy only that an author cannot remove it by
    // hand. AddClass/RemoveClass already re-cascade and already do not move the
    // structure epoch.
    constexpr const char* kTabSelectedClass = "selected";

    constexpr int kMaxTabs = 32;

} // namespace MyCoreEngine::ui
