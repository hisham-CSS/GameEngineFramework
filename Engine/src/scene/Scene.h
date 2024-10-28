#pragma once
#include "../src/core/Core.h"
#include "entt/entt.hpp"
#include "glm/glm.hpp"

namespace MyCoreEngine {
    struct Transform {
        glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };
    };

    struct MeshComponent {
        // Add mesh data
    };

    class ENGINE_API Scene {
    public:
        Scene() = default;
        ~Scene() = default;

        entt::entity CreateEntity();
        void DestroyEntity(entt::entity entity);

        template<typename T>
        T& AddComponent(entt::entity entity) {
            return m_Registry.emplace<T>(entity);
        }

        entt::registry& GetRegistry() { return m_Registry; }

    private:
        entt::registry m_Registry;
    };
}