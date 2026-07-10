#pragma once
#include "Core.h"
#include <string>

namespace MyCoreEngine {

    class Scene;
    class AssetManager;

    // JSON scene (de)serialization.
    // Per entity: Name, Transform, model asset path, material-override scalars,
    // NoShadow tag. Plus the scene-level lighting/shading settings.
    // AABBs are derived data and are regenerated from the model on load.
    class ENGINE_API SceneSerializer {
    public:
        SceneSerializer(Scene& scene, AssetManager& assets)
            : scene_(scene), assets_(assets) {}

        // Writes the scene to 'path'. Returns false on I/O failure.
        bool Save(const std::string& path) const;

        // Replaces the scene contents with the file's. Parses and validates the
        // whole file BEFORE touching the registry, so a bad file leaves the
        // current scene intact and returns false.
        bool Load(const std::string& path);

        static constexpr int kVersion = 1;

    private:
        Scene& scene_;
        AssetManager& assets_;
    };

} // namespace MyCoreEngine
