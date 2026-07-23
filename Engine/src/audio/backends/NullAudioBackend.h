#pragma once
// No-op audio backend: the fallback when no device is available (headless CI,
// audio disabled) so the rest of the engine runs unchanged and silent.
#include "../IAudioBackend.h"

namespace MyCoreEngine {

    class NullAudioBackend : public IAudioBackend {
    public:
        const char* name() const override { return "Null"; }
        bool initialize(const AudioSettings&) override { return true; }
        void shutdown() override {}
        SoundId play(const std::string&, const SoundParams&) override { return 0; }
        void stop(SoundId) override {}
        void stopAll() override {}
        bool isPlaying(SoundId) const override { return false; }
        void setPosition(SoundId, const glm::vec3&) override {}
        void setVolume(SoundId, float) override {}
        void setPitch(SoundId, float) override {}
        void setListener(const glm::vec3&, const glm::vec3&, const glm::vec3&) override {}
        void setMasterVolume(float) override {}
        void update() override {}
    };

} // namespace MyCoreEngine
