#pragma once
#include <entt/entt.hpp>

namespace MyCoreEngine { class AssetManager; }
class UndoHistory;

class InspectorPanel {
public:
    // assets may be null (model assignment UI is hidden then)
    void Draw(entt::registry& reg, entt::entity selected, UndoHistory& undo,
              MyCoreEngine::AssetManager* assets = nullptr);
};
