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
                                                const AudioSettings& settings = {},
                                                const Camera* listenerCamera = nullptr) {
        RegisterBuiltinAudioBackends();
        const std::string wanted = backendName.empty() ? DefaultAudioBackendName()
                                                       : backendName;
        world.SetBackend(wanted, settings);

        // Listener priority: an AudioListenerComponent in the scene, else this
        // fallback camera.
        //
        // `listenerCamera` exists because app.camera() is the WRONG fallback in
        // the editor. In the Player it is the game camera (setRenderFromSceneCamera
        // makes the director write the scene camera's pose into it), but the
        // editor never calls that, so app.camera() is the Scene-view FLY camera:
        // mixing distances while playing gave the answer for wherever the author
        // had flown to, not for what the shipped game hears. Hosts that render
        // through a separate camera pass it here. The pointer must outlive the
        // app (both hosts pass a member).
        const Camera* fallback = listenerCamera;
        // Drop what this derives from the registry when the scene is replaced.
        // Subscribed at INSTALL time so it cannot be forgotten: this used to
        // live in one editor private method, which is why a game could not
        // switch scenes without leaking it.
        //
        // will-unload, not did-load: a voice from the departing scene must
        // be stopped before the entity that owns it is gone.
        if (SceneLoader* loader = app.sceneLoader()) {
            loader->AddObserver([&world](Scene&) { world.Clear(); });
        }

        return app.AddUpdate([&scene, &world, &app, fallback](float /*dt*/) {
            world.Update(scene.registry, fallback ? *fallback : app.camera());
        });
    }

} // namespace MyCoreEngine
