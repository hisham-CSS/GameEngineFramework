#pragma once
// ECS components for audio. Pure authoring data -- the runtime sound handles
// live in AudioWorld's entity->SoundId map, so these serialize cleanly.
#include <string>

namespace MyCoreEngine {

    // Emits a sound from an entity. When `spatial`, it is positioned at the
    // entity's world transform and attenuates with distance from the listener.
    struct AudioSourceComponent {
        std::string clip;               // file path, relative to the working dir
        float volume      = 1.0f;       // 0..1
        float pitch       = 1.0f;       // 1 = normal
        bool  loop        = false;
        bool  spatial     = true;       // 3D positioned vs. 2D
        bool  playOnStart = true;       // begin on Play / Player boot
        float minDistance = 1.0f;       // full volume within this radius
        float maxDistance = 100.0f;     // attenuated out to here
    };

    // Marks the entity whose transform is the audio listener (the "ears") --
    // usually the camera. With none present, AudioWorld falls back to the
    // rendering camera. The first one found wins.
    struct AudioListenerComponent {};

} // namespace MyCoreEngine
