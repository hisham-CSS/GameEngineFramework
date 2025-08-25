// Scene.h
#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>

#include "Entity.h"
#include "Components.h"
#include "Shader.h"
#include "Model.h"
#include "Scene.h"

//forward declaration of glad unit
typedef unsigned int GLuint;

// Batched draw item (opaque)
struct DrawItem {
    uint64_t texKey = 0;
    const Mesh* mesh = nullptr;
    glm::mat4 model{ 1.0f };
    float     depth = 0.0f;
    entt::entity entity = entt::null;  // producer entity (for material overrides)
};

// Tag component: add to an entity to skip it from shadow maps.
struct NoShadow {};

namespace MyCoreEngine {
    struct RenderStats {
        unsigned draws = 0;           // non-instanced draw calls
        unsigned instancedDraws = 0;  // instanced draw calls
        unsigned instances = 0;       // total instances drawn via instancing
        unsigned vaoBinds = 0;
        unsigned textureBinds = 0;    // when a new texture bucket is bound
        unsigned culled = 0;          // items rejected by frustum
        unsigned submitted = 0;       // items submitted to GPU (draws + instances)
        unsigned itemsBuilt = 0;      // items_ after culling (meshes that passed)
        unsigned entitiesTotal = 0;   // 'total' (as you already increment)
    };

    class ENGINE_API Scene {
    public:
        entt::registry registry;

        // Create a new entity and return the wrapper.
        Entity createEntity();

        void UpdateTransforms();
        // Renderer calls this; we keep signature identical.
        // Now builds a draw list with frustum culling, sorts, then batches by texture key.
        virtual void RenderScene(const Frustum& camFrustum, Shader& shader, Camera& camera);

        // Depth-only shadow pass (directional)
        void RenderShadowDepth(Shader & shadowShader, const glm::mat4 & lightVP);

        // Toggle instancing at runtime
        void SetInstancingEnabled(bool enabled) { instancingEnabled_ = enabled; }
        bool GetInstancingEnabled() const { return instancingEnabled_; }
        
        // Read-only stats for the last frame
        const RenderStats &GetRenderStats() const { return lastStats_; }
        bool GetNormalMapEnabled() const { return normalMapEnabled_; }
        void SetNormalMapEnabled(bool v) { normalMapEnabled_ = v; }
        
        bool  GetPBREnabled() const { return pbrEnabled_; }
        void  SetPBREnabled(bool v) { pbrEnabled_ = v; }
        float GetMetallic()  const { return metallic_; }
        float GetRoughness() const { return roughness_; }
        float GetAO()        const { return ao_; }
        void  SetMetallic(float v) { metallic_ = std::clamp(v, 0.f, 1.f); }
        void  SetRoughness(float v) { roughness_ = std::clamp(v, 0.f, 1.f); }
        void  SetAO(float v) { ao_ = std::clamp(v, 0.f, 1.f); }
        glm::vec3& LightDir() { return lightDir_; }     // editable
        glm::vec3& LightColor() { return lightColor_; }
        float& LightIntensity() { return lightIntensity_; }
        bool GetMetallicMapEnabled()  const { return metallicMapEnabled_; }
        bool GetRoughnessMapEnabled() const { return roughnessMapEnabled_; }
        bool GetAOMapEnabled()        const { return aoMapEnabled_; }
        void SetMetallicMapEnabled(bool v) { metallicMapEnabled_ = v; }
        void SetRoughnessMapEnabled(bool v) { roughnessMapEnabled_ = v; }
        void SetAOMapEnabled(bool v) { aoMapEnabled_ = v; }
        bool  GetIBLEnabled() const { return iblEnabled_; }
        void  SetIBLEnabled(bool v) { iblEnabled_ = v; }
        float GetIBLIntensity() const { return iblIntensity_; }
        void  SetIBLIntensity(float v) { iblIntensity_ = std::max(0.0f, v); }
        // add a forward-only method (public)
        virtual void RenderDepth(class Shader& depthProg, const glm::mat4& lightVP);
        virtual void RenderDepthCascade(Shader& prog, const glm::mat4& lightVP, float splitNear, float splitFar, const glm::mat4& camView);

     private:
         std::vector<DrawItem> items_;
         // private:
         GLuint instanceVBO_ = 0;
         void ensureInstanceBuffer_();
         void bindInstanceAttribs_() const; // sets attribs 3..6 + divisors on current VAO

         // Choose material (override -> shared) for an item and bind it for drawing
         bool Scene::aabbIntersectsLightFrustum(const glm::mat4& lightVP, const AABB& aabb, const glm::mat4& model);
         
         const Material * chooseMaterial_(entt::entity e, const Mesh & mesh) const;
         void bindMaterialForItem_(const DrawItem & di, Shader & shader) const;
         static uint64_t texKeyFromMaterial_(const Material & m);

         bool instancingEnabled_ = true;
         RenderStats lastStats_;
         bool normalMapEnabled_ = true;

         bool  pbrEnabled_ = true;
         float metallic_ = 0.0f;
         float roughness_ = 0.5f;
         float ao_ = 1.0f;
         glm::vec3 lightDir_ = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
         glm::vec3 lightColor_ = glm::vec3(1.0f);
         float     lightIntensity_ = 3.0f;
         bool metallicMapEnabled_ = true;
         bool roughnessMapEnabled_ = true;
         bool aoMapEnabled_ = true;
         bool  iblEnabled_ = true;
         float iblIntensity_ = 1.0f;
    };

} // namespace MyCoreEngine
