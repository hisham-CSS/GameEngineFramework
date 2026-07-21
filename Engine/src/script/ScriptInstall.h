#pragma once
// One-call scripting install, shared by BOTH hosts (editor and player) — so
// "it works in Play" and "it works in the shipped game" can never drift
// apart. Same rationale as PhysicsInstall.h.
//
// Uses Application::AddFixedUpdate (a SUBSCRIBER), never SetFixedUpdate: the
// single primary slot belongs to a game's own gameplay hook.
//
// NOTE ON ORDERING: install scripting AFTER physics so the script fixed-tick
// subscriber runs after the simulation step. A script's OnFixedUpdate then
// observes the poses physics just produced, rather than last tick's.

#include "../core/Application.h"
#include "../core/Scene.h"
#include "../physics/PhysicsWorld.h"
#include "ScriptBackendRegistry.h"
#include "ScriptWorld.h"

namespace MyCoreEngine {

    class InputMap;

    // Registers the built-in backends, selects `backendName` (falling back to
    // the default, then to "Null" so a bad name can never leave the app
    // without a working — if inert — scripting subsystem), wires the optional
    // physics/input capabilities, subscribes the fixed tick, and bridges
    // physics contacts into OnCollision.
    //
    // Subscribes BOTH the fixed tick and the per-frame update, so the host
    // only has to call Build/Start at the point where gameplay begins. Both
    // hosts therefore run scripts at identical points in the loop -- see
    // Application::AddUpdate for why that had to stop being per-host.
    //
    // Returns the FIXED-tick handle; the per-frame subscription lives for the
    // life of the application, which is what both hosts want.
    inline Application::TickHandle InstallScripting(Application& app, Scene& scene,
                                                    ScriptWorld& world,
                                                    PhysicsWorld* physics = nullptr,
                                                    InputMap* input = nullptr,
                                                    const std::string& backendName = {},
                                                    const ScriptSettings& settings = {}) {
        RegisterBuiltinScriptBackends();

        const std::string wanted = backendName.empty() ? DefaultScriptBackendName()
                                                       : backendName;
        if (!world.SetBackend(wanted, settings) &&
            !world.SetBackend(DefaultScriptBackendName(), settings)) {
            world.SetBackend("Null", settings);
        }

        world.SetPhysics(physics);
        world.SetInput(input);

        // Contacts reach scripts through the SAME listener mechanism gameplay
        // uses, which means they fire on the fixed tick after the step,
        // single-threaded — safe for a script to touch the registry even
        // though Jolt reports contacts from its job threads.
        if (physics) {
            physics->OnCollision([&world](const PhysicsWorld::CollisionEvent& e) {
                world.DispatchCollision(e.a, e.b,
                                        e.phase == ContactPhase::Begin ? "begin" : "end",
                                        e.isTrigger, e.point, e.normal, e.impulse);
            });
        }

        app.AddUpdate([&scene, &world](float dt) {
            world.Update(scene.registry, dt);
        });
        return app.AddFixedUpdate([&scene, &world](float fixedDt) {
            world.FixedUpdate(scene.registry, fixedDt);
        });
    }

} // namespace MyCoreEngine
