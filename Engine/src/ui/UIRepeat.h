#pragma once
// What the markup loader expanded for one `repeat=`.
//
// Says nothing about the element tree, deliberately: the pool that fills the
// slots never has to walk one, and this header stays free of UIDataSource and
// UIElement so UIMarkup.h can include it without dragging either in.
#include "../core/Core.h"

#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    struct ENGINE_API UIRepeatSpec {
        // Resolved at load: the scope's name for a bare `repeat="inventory"`,
        // or the qualified half of `repeat="hud.inventory"`.
        std::string listSource;
        std::string listName;
        // VERBATIM what the author wrote. Every diagnostic says this rather
        // than a generated slot-source name, which appears in no file they own.
        std::string label;

        // "" when repeat-offset was absent.
        std::string offsetSource;
        std::string offsetProp;

        int count = 0;

        // Every UNQUALIFIED hole property the expanded template reads, sorted
        // and unique, including any $-columns it names.
        //
        // This field is why the feature works at all. A slot the window never
        // reaches receives no row, and UIDataSource has no way to declare a
        // property other than writing one — so without seeding these on EVERY
        // slot at build time, an unreached slot's bindings never resolve.
        // UIBinder counts those as unresolved forever, which permanently arms
        // its pending-retry check and turns every frame in which any source
        // moves into a full document re-collect.
        std::vector<std::string> columns;

        // "repeat#3" — the per-document ordinal. Two repeats over the SAME
        // list would otherwise generate the same slot names and silently
        // overwrite each other's registrations.
        std::string namePrefix;

        // Written per frame only when the template actually reads them. An
        // absolute $index moves on every window step, and change detection is
        // per source, so publishing one nothing reads would re-apply every
        // binding in the pool on every scroll notch.
        bool readsIndex = false;
        bool readsCount = false;

        // The generated source name for slot i.
        std::string slotSourceName(int i) const {
            return namePrefix + "#" + std::to_string(i);
        }
    };

    // Columns the pool publishes on every slot, over and above the row's own.
    //
    // '$'-prefixed because UIDataSource resolves a property by NAME ALONE: a
    // row column called `index` and the pool's own write would be the same
    // property, alternating every frame, bumping the version every frame, and
    // pinning that slot's bindings hot forever. A '$' cannot collide, because a
    // game writing r.SetInt("index", 7) produces `index`. The hole grammar
    // treats '$' as an ordinary path character, so `{$index}` needs no parser
    // change.
    constexpr const char* kRepeatSlotProp    = "$slot";     // pool index, constant
    constexpr const char* kRepeatIndexProp   = "$index";    // absolute row index
    constexpr const char* kRepeatCountProp   = "$count";    // rows in the whole list
    constexpr const char* kRepeatPresentProp = "$present";  // this slot has a row

} // namespace MyCoreEngine::ui
