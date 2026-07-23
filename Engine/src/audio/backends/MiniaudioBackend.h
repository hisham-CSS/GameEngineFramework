#pragma once
// The real audio backend, on miniaudio (single-header, cross-platform, dlopens
// the OS audio backend at runtime -- no link/system deps). miniaudio types are
// pimpl'd so <miniaudio.h> never leaks out of the .cpp, like the physics SDKs.
#include "../IAudioBackend.h"

#include <memory>

namespace MyCoreEngine {

    class MiniaudioBackend : public IAudioBackend {
    public:
        MiniaudioBackend();
        ~MiniaudioBackend() override;

        const char* name() const override { return "Miniaudio"; }
        bool initialize(const AudioSettings& settings) override;
        void shutdown() override;

        SoundId play(const std::string& path, const SoundParams& params) override;
        void stop(SoundId id) override;
        void stopAll() override;
        bool isPlaying(SoundId id) const override;

        void setPosition(SoundId id, const glm::vec3& worldPos) override;
        void setVolume(SoundId id, float volume) override;
        void setPitch(SoundId id, float pitch) override;

        void setListener(const glm::vec3& position, const glm::vec3& forward,
                         const glm::vec3& up) override;
        void setMasterVolume(float volume) override;
        void update() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace MyCoreEngine
