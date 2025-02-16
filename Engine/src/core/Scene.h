// Scene.h
#pragma once

#include <entt/entt.hpp>
#include "Core.h"
#include "Entity.h"
#include "Components.h"

namespace MyCoreEngine {

    class ENGINE_API Scene {
    public:
        entt::registry registry;

        // Create a new entity and return the wrapper.
        Entity createEntity() {
            entt::entity handle = registry.create();
            return Entity(handle, &registry);
        }

        // Optionally, you can provide additional methods to run systems,
        // update transforms, or render the scene.
    };

} // namespace MyCoreEngine
