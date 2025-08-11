#pragma once
#include <entt/entt.hpp>

class InspectorPanel {
public:
    void Draw(entt::registry& reg, entt::entity selected);
};
