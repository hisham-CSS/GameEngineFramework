#include "DemoUIContent.h"

#include "UIWorld.h"

#include <algorithm>
#include <string>

namespace MyCoreEngine {

void InstallDemoUIContent(UIWorld& world) {
    ui::UIDataSource& src = world.shared();

    // Starting values for everything hud.cxml binds to. Seeded BEFORE the
    // scene loads, so the very first binding pass has real values and nothing
    // is reported unresolved.
    src.SetNumber("health", 1.0f);
    src.SetInt("score", 0);
    src.SetBool("lowHealth", false);
    // Owned by the text field from the first keystroke onward — this is only
    // what it starts with.
    src.SetString("playerName", "player one");
    // The multi-line field's starting text. A '\n' here is a line break in the
    // field, because the value is the one source of truth for both.
    src.SetString("notes", "multi-line field.\nEnter, arrows, Ctrl+Z, Ctrl+V.");

    // A COLLECTION, for `repeat=`. Twelve rows behind a pool of five, so the
    // window has somewhere to go — which is the whole point: the tree is
    // expanded once at load and never changes shape, and scrolling moves the
    // DATA through those five elements rather than creating and destroying
    // them.
    //
    // A list is not a property. No {hole} can read it, and it has its own
    // version, so writing it wakes the repeat and nothing else on this source.
    {
        static const struct { const char* name; long long count; } kItems[] = {
            { "Potion of Haste", 3 }, { "Iron Key",        1 },
            { "Rope (50ft)",     1 }, { "Torch",           9 },
            { "Rations",         6 }, { "Lockpick",        4 },
            { "Silver Ring",     1 }, { "Map Fragment",    3 },
            { "Antidote",        2 }, { "Whetstone",       1 },
            { "Lantern Oil",     5 }, { "Spare Boots",     1 },
        };
        ui::UIList inv;
        for (const auto& it : kItems) {
            ui::UIRecord& row = inv.Add();
            row.SetString("name", it.name);
            row.SetInt("count", it.count);
        }
        src.SetList("inventory", std::move(inv));
    }
    // The window start. Ordinary game data: the repeat reads it every frame and
    // clamps, so nothing here has to know how big the pool is.
    src.SetInt("invScroll", 0);

    // A named action, so the markup can write on-click="addScore" instead of
    // the app attaching a handler that a hot reload would then have to
    // re-attach. Captures the source by reference: UIWorld owns it and both
    // outlive the scene.
    src.AddAction("addScore", [&src] {
        src.SetInt("score", src.GetInt("score") + 100);
    });

    // Clamped to rows-1 rather than to rows-poolSize: the pool size lives in
    // the markup, and gameplay has no business knowing it. Scrolling to the
    // last row therefore shows one item and four empty slots, which is exactly
    // what $present is for.
    src.AddAction("invUp", [&src] {
        src.SetInt("invScroll", std::max<long long>(0, src.GetInt("invScroll") - 1));
    });
    src.AddAction("invDown", [&src] {
        const int li = src.ListIndexOf("inventory");
        const long long last = (long long)src.ListRowCount(li) - 1;
        src.SetInt("invScroll", std::clamp<long long>(src.GetInt("invScroll") + 1, 0, last));
    });

    // Registered on the WORLD, so every document in the scene can use it. A
    // drain ramp is a LOOK, which is why it lives next to the UI rather than
    // inside gameplay — but it is a function, and a function cannot be
    // authored in a stylesheet.
    world.converters().Register(
        "healthTint", [](const ui::UIValue& in, ui::UIValue& out, std::string& err) {
            float h = 0.0f;
            if (!in.AsNumber(h)) {
                err = std::string("healthTint needs a number, got ") + in.KindName();
                return false;
            }
            h = std::clamp(h, 0.0f, 1.0f);
            out = ui::UIValue::Color4({ 0.85f, 0.22f + (1.0f - h) * 0.45f, 0.24f, 1.0f });
            return true;
        });
}

} // namespace MyCoreEngine
