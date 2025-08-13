// Scene.h
#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Core.h"
#include "Entity.h"
#include "Components.h"
#include "Shader.h"

//forward declaration of glad unit
typedef unsigned int GLuint;

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
        Entity createEntity();

        void UpdateTransforms();
        // Renderer calls this; we keep signature identical.
        // Now builds a draw list with frustum culling, sorts, then batches by texture key.
        void RenderScene(const Frustum& camFrustum, Shader& shader, unsigned int& display, unsigned int& total);
         
     private:
         std::vector<DrawItem> items_;
         // private:
         GLuint instanceVBO_ = 0;
         void ensureInstanceBuffer_();
         void bindInstanceAttribs_() const; // sets attribs 3..6 + divisors on current VAO
    };

} // namespace MyCoreEngine
