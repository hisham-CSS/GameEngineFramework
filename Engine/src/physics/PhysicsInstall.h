#pragma once
// One-call physics install, shared by BOTH hosts (editor and player) — so
// "it works in Play" and "it works in the shipped game" can never drift
// apart.
//
// Uses Application::AddFixedUpdate (a SUBSCRIBER), never SetFixedUpdate:
// the single primary slot is reserved for a game's own gameplay hook, and
// taking it would silently replace that game's logic. Subscribers run after
// the primary slot, so gameplay applies forces on a tick and the simulation
// integrates them in the same tick.

#include "../core/Application.h"
#include "../core/Scene.h"
#include "PhysicsBackendRegistry.h"
#include "PhysicsWorld.h"

namespace MyCoreEngine {

    // Registers the built-in backends, selects `backendName` (falling back to
    // the default, then to "Simple" so a bad name can never leave the app
    // without physics), and subscribes the fixed-tick step.
    // The returned handle can be passed to Application::RemoveFixedUpdate.
    inline Application::TickHandle InstallPhysics(Application& app, Scene& scene,
                                                 PhysicsWorld& world,
                                                 const std::string& backendName = {},
                                                 const PhysicsSettings& settings = {}) {
        RegisterBuiltinPhysicsBackends();

        const std::string wanted = backendName.empty() ? DefaultPhysicsBackendName()
                                                       : backendName;
        if (!world.SetBackend(wanted, settings) &&
            !world.SetBackend(DefaultPhysicsBackendName(), settings)) {
            world.SetBackend("Simple", settings);
        }

        // Drop what this derives from the registry when the scene is replaced.
        // Subscribed at INSTALL time so it cannot be forgotten: this used to
        // live in one editor private method, which is why a game could not
        // switch scenes without leaking it.
        //
        // will-unload, not did-load: the outgoing entities must still be alive.
        //
        // ...and then BUILD the incoming ones, because a cleared world stays
        // cleared. Without the second hook a game that changed scene arrived
        // with no bodies at all: nothing fell, nothing collided, and the only
        // clue was that it looked like physics had been switched off.
        //
        // Gated on gameplayEnabled(), which already means "gameplay is live":
        // always true in the shipped player, true in the editor only between
        // Play and Stop. Edit mode deliberately has no bodies -- startPlay_
        // builds them -- and this must not give it any.
        if (SceneLoader* loader = app.sceneLoader()) {
            loader->AddObserver(
                [&world](Scene&) { world.Clear(); },
                [&app, &world](Scene& s) {
                    if (app.gameplayEnabled()) world.Rebuild(s.registry);
                });
        }

        return app.AddFixedUpdate([&scene, &world](float fixedDt) {
            world.Step(scene.registry, fixedDt);
        });
    }

} // namespace MyCoreEngine
