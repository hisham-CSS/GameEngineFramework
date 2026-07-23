#pragma once
// Public value types for the audio seam. No backend/library types leak here --
// callers speak only in these, exactly like the physics/scripting seams.
#include <glm/vec3.hpp>
#include <cstdint>

namespace MyCoreEngine {

    // Opaque handle to a playing sound. 0 is never valid (a failed play()).
    using SoundId = std::uint32_t;

    struct AudioSettings {
        float masterVolume = 1.0f; // 0..1, applied to the whole mix
    };

    // How one sound is played. A 2D sound (spatial=false) plays at a fixed
    // stereo balance; a 3D sound is positioned in the world and attenuates with
    // distance from the listener.
    struct SoundParams {
        bool  spatial     = false; // 3D positioned vs. plain 2D
        bool  loop        = false;
        float volume      = 1.0f;  // 0..1
        float pitch       = 1.0f;  // 1 = normal; also scales playback speed
        glm::vec3 position{ 0.0f };
        float minDistance = 1.0f;   // full volume within this radius (3D)
        float maxDistance = 100.0f; // attenuated out to here (3D)
    };

} // namespace MyCoreEngine
