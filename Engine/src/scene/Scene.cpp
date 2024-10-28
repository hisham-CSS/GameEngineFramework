#include "Scene.h"

namespace MyCoreEngine {

    entt::entity Scene::CreateEntity() {
        // Creates a new entity in the registry and returns it
        entt::entity entity = m_Registry.create();
        // You could add default components here if needed, e.g., a Transform component
        m_Registry.emplace<Transform>(entity);
        m_Registry.emplace<MeshComponent>(entity);
        return entity;
    }

    void Scene::DestroyEntity(entt::entity entity) {
        // Destroys the specified entity, including all its components
        if (m_Registry.valid(entity)) {
            m_Registry.destroy(entity);
        }
    }

}