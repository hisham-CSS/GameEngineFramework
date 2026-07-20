#pragma once
// One-call physics install, shared by BOTH hosts (editor and player) exactly
// like InstallDemoGameplay — so "it works in Play" and "it works in the
// shipped game" can never drift apart.
//
// Uses Application::AddFixedUpdate (a SUBSCRIBER), never SetFixedUpdate:
// the single primary slot already belongs to the game's gameplay hook, and
// taking it would silently delete the game. Subscribers run after the
// primary slot, so gameplay applies forces on a tick and the simulation
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

        return app.AddFixedUpdate([&scene, &world](float fixedDt) {
            world.Step(scene.registry, fixedDt);
        });
    }

} // namespace MyCoreEngine
