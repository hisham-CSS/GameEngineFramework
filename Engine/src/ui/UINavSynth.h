#pragma once
// Gamepad -> UINavState, in one place both hosts call.
//
// This is the ONLY file in the engine that knows a menu can be driven by a
// controller, and it still does not know what a controller IS: it reads named
// actions and axes from InputMap and produces intents. Swap the bindings and
// the same code drives a menu from a flight stick.
//
// Auto-repeat lives here rather than in the UI because it needs dt, and the UI
// has no clock -- UIDocument::UpdatePointer does not even take one.
#include "../core/InputMap.h"
#include "UINav.h"

namespace MyCoreEngine {

    // One per host. Holds the repeat clock, which is per-DEVICE state rather
    // than per-document: holding a stick through a page change should keep
    // repeating, not restart.
    struct UINavSynth {
        ui::UINavRepeater repeat;

        // `allowKeyboard` false silences the KEY bindings on these actions while
        // leaving the pad alone. That is the whole reason InputMap grew a source
        // filter: WASD navigating a menu and WASD typing a name into a field are
        // the same four keys, and a pad types nothing, so the two halves of one
        // action have to be switchable independently. Hosts pass
        // !uiWorld.wantsTextInput().
        ui::UINavState Poll(const InputMap& in, float dt, bool allowKeyboard = true) {
            ui::UINavState nav;

            const std::uint8_t src = allowKeyboard
                ? std::uint8_t(Src_Any) : std::uint8_t(Src_Pad);

            // The d-pad is discrete and edge-triggered already, so it needs no
            // repeat clock -- a held d-pad SHOULD repeat, but through the same
            // clock as the stick so the two feel identical. Both feed `held`.
            // WASD joins them here, so a held key repeats at the UI's rate
            // rather than the operating system's, and a keyboard and a pad
            // cannot drift on the feel of a menu.
            ui::UINavDir held = ui::UINavDir::None;
            if (in.isDown("UINavUp", src))         held = ui::UINavDir::Up;
            else if (in.isDown("UINavDown", src))  held = ui::UINavDir::Down;
            else if (in.isDown("UINavLeft", src))  held = ui::UINavDir::Left;
            else if (in.isDown("UINavRight", src)) held = ui::UINavDir::Right;
            // Which half produced it, BEFORE the stick is consulted: a key wins
            // the attribution only if a key is what is actually down.
            if (held != ui::UINavDir::None) {
                nav.device = in.isDown("UINavUp", Src_Pad) || in.isDown("UINavDown", Src_Pad) ||
                             in.isDown("UINavLeft", Src_Pad) || in.isDown("UINavRight", Src_Pad)
                             ? ui::UINavDevice::Gamepad : ui::UINavDevice::KeyboardMouse;
            }
            if (held == ui::UINavDir::None) {
                held = ui::NavDirFromAxes(in.axis("UINavX"), in.axis("UINavY"));
                if (held != ui::UINavDir::None) nav.device = ui::UINavDevice::Gamepad;
            }

            const int n = repeat.Tick(held, dt);
            for (int i = 0; i < n; ++i) nav.moves.push_back(held);

            // Raw, for whatever can use it continuously. The discrete moves
            // above and this are the SAME stick reported two ways: UpdateNav
            // picks one per control, and suppresses the other so a slider never
            // gets both.
            nav.axisX = in.axis("UINavX");
            nav.axisY = in.axis("UINavY");
            nav.dt = dt;

            // Confirm / back / page are pad-only bindings today, so no source
            // filter is needed -- but they are read through `src` anyway, so
            // that binding a key to one later cannot quietly reintroduce
            // "typing a name also pressed the button".
            nav.activate = in.wasPressed("UIConfirm", src);
            nav.back     = in.wasPressed("UIBack", src);
            nav.page     = (in.wasPressed("UIPageNext", src) ? 1 : 0) -
                           (in.wasPressed("UIPagePrev", src) ? 1 : 0);

            // The stick counts as pad activity even with no discrete move out of
            // it, so a slow analog nudge on a slider still flips the prompts.
            if (nav.device == ui::UINavDevice::None) {
                if (nav.activate || nav.back || nav.page != 0) {
                    nav.device = in.wasPressed("UIConfirm", Src_Pad) ||
                                 in.wasPressed("UIBack", Src_Pad) ||
                                 in.wasPressed("UIPageNext", Src_Pad) ||
                                 in.wasPressed("UIPagePrev", Src_Pad)
                                 ? ui::UINavDevice::Gamepad : ui::UINavDevice::KeyboardMouse;
                } else if (nav.axisX != 0.0f || nav.axisY != 0.0f) {
                    nav.device = ui::UINavDevice::Gamepad;
                }
            }
            return nav;
        }
    };

} // namespace MyCoreEngine
