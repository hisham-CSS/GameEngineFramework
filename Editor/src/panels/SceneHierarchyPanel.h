#pragma once
#include <entt/entt.hpp>

class UndoHistory;

class SceneHierarchyPanel {
public:
    // returns true if selection changed; selected will be updated
    bool Draw(entt::registry& reg, entt::entity& selected, UndoHistory& undo);
};
