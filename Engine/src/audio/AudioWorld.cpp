#include "AudioWorld.h"

#include "AudioBackendRegistry.h"
#include "../core/Camera.h"
#include "../core/Components.h" // Transform (global)

#include <glm/gtc/matrix_access.hpp>

namespace MyCoreEngine {

    AudioWorld::AudioWorld() = default;
    AudioWorld::~AudioWorld() {
        Clear();
        if (backend_) backend_->shutdown();
    }

    bool AudioWorld::SetBackend(const std::string& name, const AudioSettings& settings) {
        Clear();
        if (backend_) { backend_->shutdown(); backend_.reset(); }

        backend_ = AudioBackendRegistry::Create(name);
        if (backend_ && backend_->initialize(settings)) {
            backendName_ = name;
            return true;
        }
        // Requested backend missing or its device failed to open: fall back to
        // Null so the world is always usable (silent). Never leave a half-state.
        backend_ = AudioBackendRegistry::Create("Null");
        if (backend_) { backend_->initialize(settings); backendName_ = "Null"; }
        else          { backendName_.clear(); }
        return false;
    }

    void AudioWorld::SetMasterVolume(float v) {
        if (backend_) backend_->setMasterVolume(v);
    }

    void AudioWorld::playSource_(entt::entity e, const AudioSourceComponent& src,
                                 const glm::vec3& worldPos) {
        if (!backend_ || src.clip.empty()) return;
        SoundParams p;
        p.spatial     = src.spatial;
        p.loop        = src.loop;
        p.volume      = src.volume;
        p.pitch       = src.pitch;
        p.position    = worldPos;
        p.minDistance = src.minDistance;
        p.maxDistance = src.maxDistance;
        const SoundId id = backend_->play(src.clip, p);
        if (id) active_[e] = id;
    }

    void AudioWorld::Start(entt::registry& reg) {
        Clear(); // don't stack duplicate voices on a re-Start
        if (!backend_) return;
        auto view = reg.view<Transform, AudioSourceComponent>();
        for (auto e : view) {
            const auto& src = view.get<AudioSourceComponent>(e);
            if (!src.playOnStart) continue;
            const glm::vec3 pos(view.get<Transform>(e).modelMatrix[3]);
            playSource_(e, src, pos);
        }
    }

    void AudioWorld::Clear() {
        if (backend_) backend_->stopAll();
        active_.clear();
    }

    void AudioWorld::Update(entt::registry& reg, const Camera& fallbackListener) {
        if (!backend_) return;

        // Listener: an explicit AudioListenerComponent entity wins; else the
        // rendering camera. Forward/up come from the transform's world axes.
        glm::vec3 lpos = fallbackListener.Position;
        glm::vec3 lfwd = fallbackListener.Front;
        glm::vec3 lup  = fallbackListener.Up;
        auto listeners = reg.view<Transform, AudioListenerComponent>();
        for (auto e : listeners) {
            const glm::mat4& m = listeners.get<Transform>(e).modelMatrix;
            lpos = glm::vec3(m[3]);
            lfwd = -glm::normalize(glm::vec3(m[2])); // -Z is "forward"
            lup  =  glm::normalize(glm::vec3(m[1]));
            break; // first listener wins
        }
        backend_->setListener(lpos, lfwd, lup);

        // Move 3D voices to their entities' current world positions.
        auto sources = reg.view<Transform, AudioSourceComponent>();
        for (auto e : sources) {
            auto it = active_.find(e);
            if (it == active_.end()) continue;
            const auto& src = sources.get<AudioSourceComponent>(e);
            if (src.spatial)
                backend_->setPosition(it->second, glm::vec3(sources.get<Transform>(e).modelMatrix[3]));
        }

        backend_->update(); // reap finished one-shots inside the backend

        // Drop entries whose voice finished (id no longer playing).
        for (auto it = active_.begin(); it != active_.end();) {
            if (!backend_->isPlaying(it->second)) it = active_.erase(it);
            else ++it;
        }
    }

    SoundId AudioWorld::PlayOneShot(const std::string& clip, const glm::vec3& worldPos,
                                    float volume) {
        if (!backend_ || clip.empty()) return 0;
        SoundParams p;
        p.spatial = true;
        p.volume  = volume;
        p.position = worldPos;
        return backend_->play(clip, p); // fire-and-forget; backend reaps it
    }

} // namespace MyCoreEngine
