#pragma once
// One-call audio install, shared by BOTH hosts (editor Play and the Player), so
// "it works in Play" and "it works in the shipped game" can't drift apart --
// same rationale as PhysicsInstall.h / ScriptInstall.h.
//
// Subscribes a per-frame update (Application::AddUpdate, a SUBSCRIBER, never the
// single primary slot) that drives the listener and 3D source positions. The
// host still calls world.Start() where gameplay begins and world.Clear() where
// it ends.
#include "../core/Application.h"
#include "../core/Scene.h"
#include "AudioBackendRegistry.h"
#include "AudioWorld.h"

namespace MyCoreEngine {

    // Registers the built-in backends, selects `backendName` (default when
    // empty; SetBackend falls back to Null if the device can't open), and
    // subscribes the per-frame update. Returns the update handle (lives for the
    // app's life, which is what both hosts want).
    inline Application::TickHandle InstallAudio(Application& app, Scene& scene,
                                                AudioWorld& world,
                                                const std::string& backendName = {},
                                                const AudioSettings& settings = {}) {
        RegisterBuiltinAudioBackends();
        const std::string wanted = backendName.empty() ? DefaultAudioBackendName()
                                                       : backendName;
        world.SetBackend(wanted, settings);

        // The listener falls back to the app camera; an AudioListenerComponent
        // in the scene overrides it. Cheap no-op until Start() plays voices.
        return app.AddUpdate([&scene, &world, &app](float /*dt*/) {
            world.Update(scene.registry, app.camera());
        });
    }

} // namespace MyCoreEngine
