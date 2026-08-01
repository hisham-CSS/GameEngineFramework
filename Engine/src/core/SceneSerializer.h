#pragma once
#include "Core.h"

#include <string>
#include <vector>

namespace MyCoreEngine {

    class Scene;
    class AssetManager;

    // JSON scene (de)serialization.
    // Per entity: Name, Transform, model asset path, material-override scalars,
    // NoShadow tag. Plus the scene-level lighting/shading settings.
    // AABBs are derived data and are regenerated from the model on load.
    // What a load actually PRODUCED.
    //
    // Load()'s bool answers "did the file parse". This answers "is the scene
    // complete", and the two are genuinely different: a scene whose every model
    // is missing loads successfully and renders nothing, while its colliders are
    // still real and its triggers still fire. Silence there is the worst kind —
    // the game runs, and the world is invisible.
    struct ENGINE_API SceneLoadReport {
        int entitiesCreated = 0;
        int modelsRequested = 0;                  // deduped
        std::vector<std::string> failedModels;    // imported to an empty Model
        std::vector<std::string> rejectedModels;  // refused by the path sandbox
        bool complete() const {
            return failedModels.empty() && rejectedModels.empty();
        }
    };

    class ENGINE_API SceneSerializer {
    public:
        SceneSerializer(Scene& scene, AssetManager& assets)
            : scene_(scene), assets_(assets) {}

        // Writes the scene to 'path'. Returns false on I/O failure.
        bool Save(const std::string& path) const;

        // Replaces the scene contents with the file's. Parses and validates the
        // whole file BEFORE touching the registry, so a bad file leaves the
        // current scene intact and returns false.
        //
        // `report`, when given, receives what the load produced — see
        // SceneLoadReport for why the bool alone is not enough.
        bool Load(const std::string& path, SceneLoadReport* report = nullptr);

        // Would Load succeed? Runs exactly the probe Load runs, against a
        // throwaway Scene, and touches nothing.
        //
        // Exposed because a DEFERRED swap has to answer this at request time:
        // by the time the swap runs, the outgoing scene's subsystems have
        // already been torn down, so "the file was bad" has to be caught before
        // anyone commits to the swap. Cheap — a JSON walk, no asset I/O.
        static bool Validate(const std::string& path, AssetManager& assets,
                             std::string* whyNot = nullptr);

        static constexpr int kVersion = 1;

    private:
        Scene& scene_;
        AssetManager& assets_;
        // Set only on the internal probe Load runs against a throwaway Scene to
        // validate the file before the real one touches anything. It performs
        // every JSON read (so a wrong-typed field still throws) but skips model
        // loading, which is the only expensive/observable side effect.
        bool dryRun_ = false;
        // Filled during a non-probe load; null when the caller did not ask.
        SceneLoadReport* report_ = nullptr;
    };

} // namespace MyCoreEngine
