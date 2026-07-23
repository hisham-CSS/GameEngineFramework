// Engine/src/core/ProjectSettings.h
#pragma once
#include "Core.h"

#include <string>

namespace MyCoreEngine {

    // Build/launch settings shared between the editor and the player.
    // Stored as JSON next to the assets (Exported/project.json) so it ships
    // inside the packaged game bundle. The editor writes it ("Build
    // Settings"); the player reads it at boot to pick the startup scene.
    struct ENGINE_API ProjectSettings {
        std::string startupScene = "Exported/scene.json";
        float masterVolume = 1.0f; // 0..1, scales the whole audio mix; the
                                   // editor writes it, the player boots at it

        static const char* DefaultPath() { return "Exported/project.json"; }

        // Missing file is not an error: defaults stand. Malformed JSON logs
        // and returns false, keeping defaults.
        bool Load(const std::string& path = DefaultPath());
        bool Save(const std::string& path = DefaultPath()) const;
    };

} // namespace MyCoreEngine
