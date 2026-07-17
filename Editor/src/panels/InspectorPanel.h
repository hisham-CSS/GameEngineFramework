#pragma once
#include <entt/entt.hpp>

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
};
