#pragma once
#include <entt/entt.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace MyCoreEngine { class AssetManager; }
class UndoHistory;

// Scene-level requests the panel can't perform itself (they touch undo
// history, play-mode gating, CSM rebuilds, and status UI in the editor).
struct AssetBrowserActions {
    std::string loadScene;    // non-empty: load this scene file
    std::string setStartup;   // non-empty: make this the player startup scene
    std::string spawnModel;   // non-empty: spawn this model in front of the camera
    // The shadow-caster set changed without a transform dirtying (model
    // assigned to an existing entity): the editor must force a CSM rebuild
    // or the old model's shadow stays baked with a stationary camera.
    bool shadowsDirty = false;
};

// Filesystem view of the runtime asset root (Exported/ next to the exe —
// the same files the engine actually loads). Models drag into the viewport
// (payload kAssetPayload) or spawn/assign via context menu; scene files
// load or become the startup scene.
class AssetBrowserPanel {
public:
    AssetBrowserActions Draw(entt::registry& reg, entt::entity selected,
                             UndoHistory& undo, MyCoreEngine::AssetManager* assets,
                             bool playing);

    static constexpr const char* kAssetPayload = "CSE_ASSET_MODEL"; // char[260] path

private:
    enum class Kind { Directory, Model, SceneJson, Texture, Shader, Other };
    struct Entry {
        std::string name;    // filename for display
        std::string relPath; // engine-style path ("Exported/Model/foo.obj")
        Kind kind = Kind::Other;
    };

    void rescan_();

    std::filesystem::path cwd_{ "Exported" };
    std::vector<Entry> entries_;
    int framesSinceScan_ = 1 << 20; // force a scan on first draw
};
