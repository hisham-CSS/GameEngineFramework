#include "MiniaudioBackend.h"

#include <miniaudio.h> // declarations only; MINIAUDIO_IMPLEMENTATION is in miniaudio_impl.cpp

#include <iostream>
#include <unordered_map>

namespace MyCoreEngine {

    struct MiniaudioBackend::Impl {
        ma_engine engine{};
        bool      ready = false;
        SoundId   nextId = 1;
        std::unordered_map<SoundId, ma_sound*> sounds;
    };

    MiniaudioBackend::MiniaudioBackend() : impl_(std::make_unique<Impl>()) {}
    MiniaudioBackend::~MiniaudioBackend() { shutdown(); }

    bool MiniaudioBackend::initialize(const AudioSettings& s) {
        if (impl_->ready) return true;
        // ma_engine bundles a device + resource manager + node graph. Init can
        // fail with no audio device (headless CI) -- report and let the caller
        // fall back to the Null backend.
        if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS) {
            std::cerr << "[Audio] no audio device (miniaudio init failed) -- silent.\n";
            return false;
        }
        impl_->ready = true;
        ma_engine_set_volume(&impl_->engine, s.masterVolume);
        return true;
    }

    void MiniaudioBackend::shutdown() {
        if (!impl_->ready) return;
        stopAll();
        ma_engine_uninit(&impl_->engine);
        impl_->ready = false;
    }

    SoundId MiniaudioBackend::play(const std::string& path, const SoundParams& p) {
        if (!impl_->ready) return 0;
        auto* snd = new ma_sound();
        // DECODE up front (fine for SFX; music would stream) to avoid per-frame
        // disk hitches during playback.
        if (ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                    MA_SOUND_FLAG_DECODE, nullptr, nullptr, snd) != MA_SUCCESS) {
            delete snd;
            std::cerr << "[Audio] failed to load '" << path << "'\n";
            return 0;
        }
        ma_sound_set_volume(snd, p.volume);
        ma_sound_set_pitch(snd, p.pitch);
        ma_sound_set_looping(snd, p.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(snd, p.spatial ? MA_TRUE : MA_FALSE);
        if (p.spatial) {
            ma_sound_set_position(snd, p.position.x, p.position.y, p.position.z);
            ma_sound_set_min_distance(snd, p.minDistance);
            ma_sound_set_max_distance(snd, p.maxDistance);
        }
        ma_sound_start(snd);
        const SoundId id = impl_->nextId++;
        impl_->sounds[id] = snd;
        return id;
    }

    void MiniaudioBackend::stop(SoundId id) {
        auto it = impl_->sounds.find(id);
        if (it == impl_->sounds.end()) return;
        ma_sound_stop(it->second);
        ma_sound_uninit(it->second);
        delete it->second;
        impl_->sounds.erase(it);
    }

    void MiniaudioBackend::stopAll() {
        for (auto& [id, snd] : impl_->sounds) {
            ma_sound_stop(snd);
            ma_sound_uninit(snd);
            delete snd;
        }
        impl_->sounds.clear();
    }

    bool MiniaudioBackend::isPlaying(SoundId id) const {
        auto it = impl_->sounds.find(id);
        return it != impl_->sounds.end() && ma_sound_is_playing(it->second);
    }

    void MiniaudioBackend::setPosition(SoundId id, const glm::vec3& p) {
        auto it = impl_->sounds.find(id);
        if (it != impl_->sounds.end()) ma_sound_set_position(it->second, p.x, p.y, p.z);
    }
    void MiniaudioBackend::setVolume(SoundId id, float v) {
        auto it = impl_->sounds.find(id);
        if (it != impl_->sounds.end()) ma_sound_set_volume(it->second, v);
    }
    void MiniaudioBackend::setPitch(SoundId id, float v) {
        auto it = impl_->sounds.find(id);
        if (it != impl_->sounds.end()) ma_sound_set_pitch(it->second, v);
    }

    void MiniaudioBackend::setListener(const glm::vec3& pos, const glm::vec3& fwd,
                                       const glm::vec3& up) {
        if (!impl_->ready) return;
        ma_engine_listener_set_position(&impl_->engine, 0, pos.x, pos.y, pos.z);
        ma_engine_listener_set_direction(&impl_->engine, 0, fwd.x, fwd.y, fwd.z);
        ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
    }
    void MiniaudioBackend::setMasterVolume(float v) {
        if (impl_->ready) ma_engine_set_volume(&impl_->engine, v);
    }

    void MiniaudioBackend::update() {
        // Reap finished one-shots (looping sounds stay until stopped).
        for (auto it = impl_->sounds.begin(); it != impl_->sounds.end();) {
            if (!ma_sound_is_looping(it->second) && ma_sound_at_end(it->second)) {
                ma_sound_uninit(it->second);
                delete it->second;
                it = impl_->sounds.erase(it);
            } else {
                ++it;
            }
        }
    }

} // namespace MyCoreEngine
