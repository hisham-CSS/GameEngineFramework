#pragma once
// Owns the active audio backend and the entity <-> playing-sound mapping, and
// is the ONLY place the ECS meets audio. Mirrors PhysicsWorld / ScriptWorld:
// same lifecycle contract (Start populates, Clear tears down, both hosts drive
// it identically through AudioInstall).
#include "../core/Core.h"
#include "IAudioBackend.h"
#include "AudioComponents.h"

#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <unordered_map>

class Camera; // global (Engine/src/core/Camera.h), like Transform

namespace MyCoreEngine {

    class ENGINE_API AudioWorld {
    public:
        AudioWorld();
        ~AudioWorld();
        AudioWorld(const AudioWorld&) = delete;
        AudioWorld& operator=(const AudioWorld&) = delete;

        // Selects a backend by name ("Miniaudio", "Null"). If it fails to open a
        // device, falls back to Null so the world is always usable (if silent).
        bool SetBackend(const std::string& name, const AudioSettings& settings = {});
        const std::string& BackendName() const { return backendName_; }
        IAudioBackend* Backend() { return backend_.get(); }
        bool HasBackend() const { return backend_ != nullptr; }
        void SetMasterVolume(float v);

        // Start every play-on-start AudioSourceComponent. Idempotent-ish: calls
        // Clear() first so a re-Start doesn't stack duplicate voices.
        void Start(entt::registry& reg);
        // Stop all sounds and forget the map.
        void Clear();

        // Per-frame: set the listener (from an AudioListenerComponent entity if
        // present, else `fallbackListener`), move 3D sources to their world
        // transforms, and reap finished one-shots.
        void Update(entt::registry& reg, const Camera& fallbackListener);

        // Fire-and-forget a 3D one-shot at a world position (for gameplay/scripts).
        SoundId PlayOneShot(const std::string& clip, const glm::vec3& worldPos,
                            float volume = 1.0f);

    private:
        void playSource_(entt::entity e, const AudioSourceComponent& src,
                         const glm::vec3& worldPos);

        std::unique_ptr<IAudioBackend> backend_;
        std::string backendName_;
        std::unordered_map<entt::entity, SoundId> active_; // entity -> its voice
    };

} // namespace MyCoreEngine
