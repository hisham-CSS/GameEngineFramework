// Scene.h
#pragma once

#include <glad/glad.h>
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <GLFW/glfw3.h>
#include "Core.h"
#include "Entity.h"
#include "Components.h"
#include "Shader.h"


// Batched draw item (opaque)
struct DrawItem {
    uint64_t texKey;
    const MyCoreEngine::Mesh* mesh;
    glm::mat4 model;
    float depth;
};


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

        // Renderer calls this; we keep signature identical.
        // Now builds a draw list with frustum culling, sorts, then batches by texture key.
        void RenderScene(const Frustum & camFrustum, Shader & shader, unsigned int& display, unsigned int& total)
        {
            display = 0; total = 0;
            items_.clear();
            items_.reserve(1024);
             
            // 1) Build draw list with your existing frustum test
            auto view = registry.view<ModelComponent, Transform, AABB>();
            for (auto entity : view) {
                auto & mc = view.get<ModelComponent>(entity);
                auto & t = view.get<Transform>(entity);
                auto & bounds = view.get<AABB>(entity);
                total++;
                if (!mc.model) continue;
                if (!bounds.isOnFrustum(camFrustum, t)) continue;
                 
                // Push one DrawItem per mesh in the model
                // Depth is optional (kept 0.0 here). If you later expose camera pos/dir via Frustum,
                // you can compute front-to-back ordering.
                for (const auto& mesh : mc.model->Meshes()) {
                    DrawItem di;
                    di.texKey = mesh.TextureSignature();
                    di.mesh = &mesh;
                    di.model = t.modelMatrix;
                    di.depth = 0.0f; // TODO: set from camera if available
                    items_.push_back(di);
                }
            }
            // 2) Sort: primarily by texKey to minimize texture/VAO rebinds
            // (If you later add camera depth, sort by {depth asc, texKey asc})
            std::sort(items_.begin(), items_.end(),
                [](const DrawItem& a, const DrawItem& b) {
            if (a.texKey != b.texKey) return a.texKey < b.texKey;
            return (uintptr_t)a.mesh < (uintptr_t)b.mesh; // stable tiebreaker
            });
             
             // 3) Batched draw: bind once per texture signature, then issue draws
            uint64_t currentKey = ~0ull;
            const MyCoreEngine::Mesh* currentMesh = nullptr;

            for (const DrawItem& it : items_) {
                if (it.texKey != currentKey) {
                    // New texture bucket: bind textures + VAO from this first mesh in the bucket
                    it.mesh->BindForDraw(shader);        // binds textures AND the mesh VAO
                    currentKey = it.texKey;
                    currentMesh = it.mesh;               // VAO now matches this mesh
                }
                else if (it.mesh != currentMesh) {
                    // Same textures, different mesh → must switch VAO
                    glBindVertexArray(it.mesh->VAO());
                    currentMesh = it.mesh;
                }

                shader.setMat4("model", it.model);
                it.mesh->IssueDraw();
                display++;
            }
            // tidy
            glBindVertexArray(0);
            glActiveTexture(GL_TEXTURE0);    
        }
         
     private:
         std::vector<DrawItem> items_;
         
    };

} // namespace MyCoreEngine
