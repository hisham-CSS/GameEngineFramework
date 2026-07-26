#include "DemoUIContent.h"

#include "UIWorld.h"

#include <algorithm>
#include <string>

namespace MyCoreEngine {

void InstallDemoUIContent(UIWorld& world) {
    ui::UIDataSource& src = world.shared();

    // Starting values for everything hud.uxml binds to. Seeded BEFORE the
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

    // A named action, so the markup can write on-click="addScore" instead of
    // the app attaching a handler that a hot reload would then have to
    // re-attach. Captures the source by reference: UIWorld owns it and both
    // outlive the scene.
    src.AddAction("addScore", [&src] {
        src.SetInt("score", src.GetInt("score") + 100);
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
