#pragma once
#include <entt/entt.hpp>

#include <string>

namespace MyCoreEngine { class AssetIndex; }

// Scene-level requests the panel can't perform itself (they touch undo
// history, async model requests, play-mode gating, CSM rebuilds, and
// status UI in the editor). The panel EMITS intents; the editor executes
// them — model loads resolve asynchronously through the AssetManager.
struct AssetBrowserActions {
    std::string loadScene;    // non-empty: load this scene file
    std::string setStartup;   // non-empty: make this the player startup scene
    std::string spawnModel;   // non-empty: spawn this model in front of the camera
    std::string assignModel;  // non-empty: assign this model to the selected entity
    bool assetClicked = false;      // an asset row was selected this frame
    bool validateRequested = false; // toolbar "Validate" pressed
};

// The Assets panel: a VIEW over the engine's AssetIndex (all disk walking
// lives there — the asset filesystem domain). Unity-style layout: folder
// tree on the left (collapse/expand, click to select), the selected
// folder's contents on the right, clickable breadcrumbs on top. Models
// drag into the viewport (payload kAssetPayload) or spawn/assign via
// context menu; scene files load or become the startup scene.
class AssetBrowserPanel {
public:
    // `index` is non-const only for forceRescan (the Refresh button); the
    // panel never mutates the tree itself. `loadingCount` = async model
    // loads in flight (toolbar indicator); `validating` disables the
    // Validate button while a cooker run is active.
    AssetBrowserActions Draw(entt::registry& reg, entt::entity selected,
                             MyCoreEngine::AssetIndex& index, bool playing,
                             int loadingCount, bool validating);

    static constexpr const char* kAssetPayload = "CSE_ASSET_MODEL"; // char[260] path

    // Single-clicked asset (relPath; empty = none) — the editor shows its
    // Inspector asset view for it. Cleared when an ENTITY gets selected.
    const std::string& selectedAsset() const { return selectedAsset_; }
    void clearAssetSelection() { selectedAsset_.clear(); }

private:
    void drawBreadcrumbs_();
    void drawFolderTree_(const void* node, bool isRoot); // AssetIndex::Node*
    AssetBrowserActions drawContents_(const void* node, entt::registry& reg,
                                      entt::entity selected, bool playing);
    void navigateTo_(const std::string& relPath);

    std::string selectedDir_;      // relPath of the folder shown on the right
    std::string selectedAsset_;    // relPath of the highlighted FILE (Inspector)
    bool revealSelection_ = false; // one-shot: open tree ancestors of selectedDir_
    float treeWidth_ = 150.f;      // splitter position
};
