#pragma once
#include <entt/entt.hpp>

class SceneHierarchyPanel {
public:
    // returns true if selection changed; selected will be updated
    bool Draw(entt::registry& reg, entt::entity& selected);
};
