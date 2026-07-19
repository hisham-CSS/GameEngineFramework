#pragma once
#include <entt/entt.hpp>

#include <cstdint>
#include <string>

namespace MyCoreEngine { class AssetManager; }
class UndoHistory;

class InspectorPanel {
public:
    // assets may be null (model assignment UI is hidden then).
    // Returns true when the shadow-caster set changed without a transform
    // dirtying (model assign/replace/remove, Casts Shadows toggle) — the
    // editor must force a CSM rebuild or stale shadows stay baked while
    // the camera is stationary.
    bool Draw(entt::registry& reg, entt::entity selected, UndoHistory& undo,
              MyCoreEngine::AssetManager* assets = nullptr);

    // Asset view (P4-3 phase 4): drawn INSTEAD of the entity view when an
    // asset is highlighted in the Assets panel (Unity-style: the last
    // click wins). `indexNode` is a const AssetIndex::Node* (void* keeps
    // this header engine-include-free, matching the browser panel).
    // Textures get an Import Settings section backed by the .import
    // sidecar; other kinds show file info only for now.
    void DrawAsset(const void* indexNode);

private:
    // asset-view cache, refreshed when the highlighted path changes
    std::string assetPath_;
    std::uintmax_t assetSize_ = 0;
    int importMaxDim_ = 0;      // mirrors ImportSettings.maxDimension
    std::string assetStatus_;   // transient "saved"/"save failed" line
};
