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

        ui::UINavState Poll(const InputMap& in, float dt) {
            ui::UINavState nav;

            // The d-pad is discrete and edge-triggered already, so it needs no
            // repeat clock -- a held d-pad SHOULD repeat, but through the same
            // clock as the stick so the two feel identical. Both feed `held`.
            ui::UINavDir held = ui::UINavDir::None;
            if (in.isDown("UINavUp"))         held = ui::UINavDir::Up;
            else if (in.isDown("UINavDown"))  held = ui::UINavDir::Down;
            else if (in.isDown("UINavLeft"))  held = ui::UINavDir::Left;
            else if (in.isDown("UINavRight")) held = ui::UINavDir::Right;
            if (held == ui::UINavDir::None) {
                held = ui::NavDirFromAxes(in.axis("UINavX"), in.axis("UINavY"));
            }

            const int n = repeat.Tick(held, dt);
            for (int i = 0; i < n; ++i) nav.moves.push_back(held);

            nav.activate = in.wasPressed("UIConfirm");
            nav.back     = in.wasPressed("UIBack");
            nav.page     = (in.wasPressed("UIPageNext") ? 1 : 0) -
                           (in.wasPressed("UIPagePrev") ? 1 : 0);
            return nav;
        }
    };

} // namespace MyCoreEngine
