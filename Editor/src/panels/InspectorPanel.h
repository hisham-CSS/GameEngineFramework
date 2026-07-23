#pragma once
#include <entt/entt.hpp>

#include <cstdint>
#include <string>

namespace MyCoreEngine { class AssetManager; class ScriptWorld; class AudioWorld; }
class UndoHistory;

class InspectorPanel {
public:
    // assets may be null (model assignment UI is hidden then).
    // scripts may be null; when supplied, the Script section shows the
    // compile/runtime error for this entity right next to the field that
    // caused it, instead of only in the console.
    // audio may be null; when supplied, an Audio Source shows a Preview
    // button that auditions the clip through the always-on editor backend
    // (no Play press needed).
    // Returns true when the shadow-caster set changed without a transform
    // dirtying (model assign/replace/remove, Casts Shadows toggle) — the
    // editor must force a CSM rebuild or stale shadows stay baked while
    // the camera is stationary.
    // pOpen (optional) drives the window's close button; the editor gates the
    // whole Draw on the same bool so the tab X hides the Inspector.
    bool Draw(entt::registry& reg, entt::entity selected, UndoHistory& undo,
              MyCoreEngine::AssetManager* assets = nullptr,
              const MyCoreEngine::ScriptWorld* scripts = nullptr,
              MyCoreEngine::AudioWorld* audio = nullptr,
              bool* pOpen = nullptr);

    // Asset view (P4-3 phase 4): drawn INSTEAD of the entity view when an
    // asset is highlighted in the Assets panel (Unity-style: the last
    // click wins). `indexNode` is a const AssetIndex::Node* (void* keeps
    // this header engine-include-free, matching the browser panel).
    // Textures get an Import Settings section backed by the .import
    // sidecar; other kinds show file info only for now.
    void DrawAsset(const void* indexNode, bool* pOpen = nullptr);

private:
    // asset-view cache, refreshed when the highlighted path changes
    std::string assetPath_;
    std::uintmax_t assetSize_ = 0;
    int importMaxDim_ = 0;      // mirrors ImportSettings.maxDimension
    std::string assetStatus_;   // transient "saved"/"save failed" line

    // Audio preview: at most one auditioning voice at a time, so previewing a
    // second source (or reselecting) stops the first. previewEntity_ tracks
    // which entity owns it, so switching selection can stop a looping preview
    // whose Stop button is no longer on screen.
    std::uint32_t previewVoice_ = 0;            // SoundId; 0 = none
    entt::entity  previewEntity_ = entt::null;
};
