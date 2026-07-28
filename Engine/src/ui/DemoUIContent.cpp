#include "DemoUIContent.h"

#include "UIWorld.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace MyCoreEngine {

namespace {

    const struct { const char* name; long long count; } kDemoItems[] = {
        { "Potion of Haste", 3 }, { "Iron Key",        1 },
        { "Rope (50ft)",     1 }, { "Torch",           9 },
        { "Rations",         6 }, { "Lockpick",        4 },
        { "Silver Ring",     1 }, { "Map Fragment",    3 },
        { "Antidote",        2 }, { "Whetstone",       1 },
        { "Lantern Oil",     5 }, { "Spare Boots",     1 },
    };

    // Rebuilt whenever the cursor moves, because the SELECTED flag lives on the
    // row. That is deliberate rather than lazy: markup has no comparison
    // operator by design, so "is this the chosen row" has to arrive as data.
    //
    // Rebuilding costs nothing on a frame where nothing moved — SetList is
    // equality-gated like every other setter, so an identical list bumps no
    // version and wakes no binding.
    void RebuildDemoInventory(ui::UIDataSource& src) {
        const long long sel = src.GetInt("invSelected");
        ui::UIList inv;
        long long i = 0;
        for (const auto& it : kDemoItems) {
            ui::UIRecord& row = inv.Add();
            row.SetString("name", it.name);
            row.SetInt("count", it.count);
            row.SetBool("selected", i == sel);
            ++i;
        }
        src.SetList("inventory", std::move(inv));
    }

    void MoveDemoCursor(ui::UIDataSource& src, long long by) {
        const long long last = (long long)std::size(kDemoItems) - 1;
        src.SetInt("invSelected",
                   std::clamp<long long>(src.GetInt("invSelected") + by, 0, last));
        RebuildDemoInventory(src);
    }

} // namespace

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
    // expanded once at load and never changes shape, and moving through the
    // list moves the DATA through those five elements rather than creating and
    // destroying them.
    //
    // A list is not a property. No {hole} can read it, and it has its own
    // version, so writing it wakes the repeat and nothing else on this source.
    //
    // `invSelected` is a CURSOR, not a scroll position, and it is fed straight
    // to repeat-offset. That works because the repeat clamps the window to
    // [0, rows - poolSize]: the cursor walks all twelve rows while the window
    // stops at the last full page, so the selection is always on screen and the
    // panel never shrinks to a partial window at either end.
    src.SetInt("invSelected", 0);
    RebuildDemoInventory(src);

    // A named action, so the markup can write on-click="addScore" instead of
    // the app attaching a handler that a hot reload would then have to
    // re-attach. Captures the source by reference: UIWorld owns it and both
    // outlive the scene.
    src.AddAction("addScore", [&src] {
        src.SetInt("score", src.GetInt("score") + 100);
    });

    // Cursor movement, clamped to the LIST rather than to the window. Gameplay
    // has no business knowing the pool size — that lives in the markup — and it
    // does not need to: the repeat clamps the window itself, so feeding it a
    // cursor gives a list view for free.
    src.AddAction("invPrev", [&src] { MoveDemoCursor(src, -1); });
    src.AddAction("invNext", [&src] { MoveDemoCursor(src, +1); });

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
