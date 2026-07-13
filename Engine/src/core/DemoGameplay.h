// Engine/src/core/DemoGameplay.h
#pragma once
#include "Application.h"
#include "Scene.h"

namespace MyCoreEngine {

    // Demo gameplay shared by the editor's play mode and the standalone
    // player: spin every entity named "Hero" on the fixed tick. This is the
    // placeholder for real game systems/scripts (roadmap P3-9) — it lives in
    // the engine so "what Play does" is identical everywhere. The Application
    // gameplay gate decides WHEN it ticks: always in the player, only between
    // Play and Stop in the editor.
    inline void InstallDemoGameplay(Application& app, Scene& scene) {
        app.SetFixedUpdate([&scene](float fixedDt) {
            auto view = scene.registry.view<Name, Transform>();
            for (auto e : view) {
                if (view.get<Name>(e).value != "Hero") continue;
                auto& t = view.get<Transform>(e);
                t.rotation.y += 45.f * fixedDt; // deg/sec, framerate-independent
                if (t.rotation.y >= 360.f) t.rotation.y -= 360.f;
                t.dirty = true;
            }
        });
    }

} // namespace MyCoreEngine
