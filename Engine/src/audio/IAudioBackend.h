#pragma once
// The audio seam: every audio library (miniaudio today, anything later)
// implements exactly this interface, and nothing outside a backend's own .cpp
// sees a library type. Mirrors IPhysicsBackend / IScriptBackend.
//
// Contract notes:
// - initialize() may fail (no audio device, e.g. headless CI); return false and
//   leave the object safely destructible. Callers fall back to the Null backend.
// - play() returns a SoundId that stays valid until the sound finishes (a
//   non-looping one-shot), stop() is called, or shutdown(). After it finishes,
//   update() reaps it and the id goes stale (isPlaying() returns false).
#include "AudioTypes.h"

#include <string>

namespace MyCoreEngine {

    class IAudioBackend {
    public:
        virtual ~IAudioBackend() = default;

        // Stable identifier for the registry / project settings / editor picker
        // (e.g. "Null", "Miniaudio").
        virtual const char* name() const = 0;

        // ---- lifecycle ----
        virtual bool initialize(const AudioSettings& settings) = 0;
        virtual void shutdown() = 0;

        // ---- playback ----
        // Start a sound from a file (path relative to the working directory).
        // Returns a control handle, or 0 on failure.
        virtual SoundId play(const std::string& path, const SoundParams& params) = 0;
        virtual void stop(SoundId id) = 0;
        virtual void stopAll() = 0;
        virtual bool isPlaying(SoundId id) const = 0;

        // ---- per-sound control ----
        virtual void setPosition(SoundId id, const glm::vec3& worldPos) = 0;
        virtual void setVolume(SoundId id, float volume) = 0;
        virtual void setPitch(SoundId id, float pitch) = 0;

        // ---- global ----
        // Listener pose for 3D attenuation/panning (typically the active camera).
        virtual void setListener(const glm::vec3& position,
                                 const glm::vec3& forward,
                                 const glm::vec3& up) = 0;
        virtual void setMasterVolume(float volume) = 0;

        // Called once per frame: reap finished one-shots and service the mixer.
        virtual void update() = 0;
    };

} // namespace MyCoreEngine
