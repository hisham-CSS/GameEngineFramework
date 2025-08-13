// Scene.h
#pragma once

#include <entt/entt.hpp>
#include "Core.h"
#include "Entity.h"
#include "Components.h"
#include "Shader.h"

namespace MyCoreEngine {

    class ENGINE_API Scene {
    public:
        entt::registry registry;

        // Create a new entity and return the wrapper.
        Entity createEntity() {
            entt::entity handle = registry.create();
            return Entity(handle, &registry);
        }

        void UpdateTransforms()
        {
            auto RegView = registry.view<Transform>();
            for (auto entity : RegView) {
                auto& t = RegView.get<Transform>(entity);
                if (t.dirty) {
                    t.updateMatrix();
                }
            }
        }

        void RenderScene(const Frustum& camFrustum, Shader& shader, unsigned int& display, unsigned int& total)
        {
            auto renderView = registry.view<ModelComponent, Transform, AABB>();
            for (auto entity : renderView) {
                auto& mc = renderView.get<ModelComponent>(entity);
                auto& t = renderView.get<Transform>(entity);
                auto& bounds = renderView.get<AABB>(entity);

                if (bounds.isOnFrustum(camFrustum, t))
                {
                    shader.setMat4("model", t.modelMatrix);
                    mc.model->Draw(shader);
                    display++;
                }
                total++;
            }
        }
    };

} // namespace MyCoreEngine
