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
    int       lod = 0;                 // mesh LOD level chosen for this item
    entt::entity entity = entt::null;  // producer entity (for material overrides)
};

// Tag component: add to an entity to skip it from shadow maps.
struct NoShadow {};

namespace MyCoreEngine {

    // Environment lighting for a scene. PURE DATA — the renderer bakes GL
    // resources from it; nothing here is a texture id, so it serializes and
    // survives undo/restore like any other scene setting.
    struct EnvironmentSettings {
        enum class Source { ProceduralSky = 0, HDRi = 1 };

        // Defaults to the SHIPPED sky, so a brand-new scene is properly lit
        // without the author configuring anything. Safe to default to a file
        // because a missing/unreadable path falls back to the procedural sky
        // (see Renderer::ApplyEnvironment) — both paths are tested, so a
        // project that deletes the asset still gets an environment rather
        // than a black scene.
        //
        // The shipped default is a PURE SKY (no ground/terrain baked in): a
        // location-specific HDRi imposes someone else's horizon on every
        // scene, which reads as wrong as soon as your own geometry does not
        // line up with it.
        Source      source = Source::HDRi;
        std::string hdriPath = "Exported/Env/kloofendal_puresky_2k.hdr";

        // NOTE: how strongly the environment LIGHTS the scene lives in
        // Scene::SetIBLIntensity, which already existed and is already
        // serialized. Duplicating it here would give the Inspector two
        // sliders that disagree.
        float skyIntensity = 1.0f;        // scales the DRAWN sky only
        bool  drawSkybox = true;

        // Procedural sky. Defaults are a clear-ish daytime sky; the sun
        // direction is taken from the scene's sun so the environment and the
        // shadow-casting light cannot disagree about where it is.
        glm::vec3 zenith{ 0.10f, 0.22f, 0.45f };
        glm::vec3 horizon{ 0.62f, 0.72f, 0.86f };
        glm::vec3 ground{ 0.22f, 0.20f, 0.18f };
        float     sunIntensity = 3.0f;

        // Cheap value comparison so hosts can re-bake only on a real change:
        // baking is milliseconds, far too slow to do every frame.
        bool operator==(const EnvironmentSettings& o) const {
            return source == o.source && hdriPath == o.hdriPath &&
                   skyIntensity == o.skyIntensity &&
                   drawSkybox == o.drawSkybox && zenith == o.zenith &&
                   horizon == o.horizon && ground == o.ground &&
                   sunIntensity == o.sunIntensity;
        }
        bool operator!=(const EnvironmentSettings& o) const { return !(*this == o); }
    };

    struct RenderStats {
        unsigned draws = 0;           // non-instanced draw calls
        unsigned instancedDraws = 0;  // instanced draw calls
        unsigned instances = 0;       // total instances drawn via instancing
        unsigned vaoBinds = 0;
        unsigned textureBinds = 0;    // when a new texture bucket is bound
        unsigned culled = 0;          // entities rejected by frustum
        unsigned culledSmall = 0;     // entities rejected by projected-size cull
        unsigned submitted = 0;       // items submitted to GPU (draws + instances)
        unsigned itemsBuilt = 0;      // items_ after culling (meshes that passed)
        unsigned entitiesTotal = 0;   // 'total' (as you already increment)
        unsigned lodInstances[3] = { 0, 0, 0 }; // submitted instances per LOD level
        unsigned lightsActive = 0;    // punctual lights uploaded this frame
        unsigned lightsCulled = 0;    // in the scene but out of range / disabled
    };

    // One punctual light resolved to world space and ready to upload. Kept
    // separate from LightComponent so the selection step is pure data and can
    // be unit-tested without a GL context.
    struct PunctualLight {
        glm::vec3 position{ 0.f };
        float     range = 0.f;
        glm::vec3 color{ 1.f };
        float     intensity = 0.f;
        glm::vec3 spotDir{ 0.f, 0.f, -1.f }; // world-space aim (spot only)
        float     cosOuter = -1.f;
        float     cosInner = 1.f;
        int       type = 0;                  // matches LightType
    };

    // --- transform hierarchy helpers (P2-8) --------------------------------
    // True if `node` is `ancestor` itself or sits anywhere below it.
    ENGINE_API bool IsSameOrDescendantOf(entt::registry& reg, entt::entity node,
                                         entt::entity ancestor);
    // World matrix resolved through the Parent chain from local TRS values
    // (correct even when cached modelMatrix values are stale/dirty).
    ENGINE_API glm::mat4 ResolveWorldMatrix(entt::registry& reg, entt::entity e);
    // --- game camera helpers -----------------------------------------------
    // The camera entity the game renders from: highest-priority ENABLED
    // CameraComponent with a Transform (ties: lowest entity index);
    // entt::null when the scene has none (callers fall back to their own).
    // Stateless — for blending between cameras use a CameraDirector, which
    // runs this same selection internally.
    ENGINE_API entt::entity FindActiveCamera(entt::registry& reg);
    // Copies the entity's world pose + lens into `cam` (Position/Front/Up/
    // Right from the world matrix columns, Zoom from fovDeg, NearClip/
    // FarClip from the clip planes). Returns false if the entity is not a
    // valid camera (cam is left untouched).
    ENGINE_API bool SyncCameraFromEntity(entt::registry& reg, entt::entity e,
                                         Camera& cam);

    // Decompose an affine TRS matrix into position / YXZ euler degrees /
    // scale — EXACTLY the conventions Transform::localMatrix rebuilds, so a
    // decompose->rebuild round-trip is lossless for shear-free matrices.
    // (ImGuizmo's DecomposeMatrixToComponents uses a different euler order
    // and silently re-orients compound rotations — never use it to fill
    // Transform::rotation.)
    ENGINE_API void DecomposeTRS(const glm::mat4& m, glm::vec3& outPos,
                                 glm::vec3& outRotDeg, glm::vec3& outScale);
    // Reparent `child` under `newParent` (entt::null = make root) while
    // preserving its world transform: the local TRS is rewritten against the
    // new parent. Refuses cycles and entities without Transforms.
    ENGINE_API bool SetParentKeepWorld(entt::registry& reg, entt::entity child,
                                       entt::entity newParent);

    class ENGINE_API Scene {
    public:
        entt::registry registry;

        virtual ~Scene(); // frees instanceVBO_

        // Create a new entity and return the wrapper.
        Entity createEntity();

        // "New scene": destroys every entity and restores the scene-level
        // settings (lighting, material scalars, toggles) to their defaults.
        // GL resources (instance buffer) are kept. Callers own the follow-up
        // bookkeeping: stale entity handles (selection, undo history) and a
        // shadow rebuild (wholesale caster removal bypasses dirty tracking).
        void ResetToDefaults();

        void UpdateTransforms();
        // Renderer calls this; builds a draw list with frustum culling +
        // optional projected-size culling, sorts, then batches by texture key.
        // viewportHeightPx (pixels) drives the screen-size cull; 0 disables it
        // (e.g. callers with no framebuffer size handy).
        virtual void RenderScene(const Frustum& camFrustum, Shader& shader, Camera& camera,
                                 int viewportHeightPx = 0);

        // Depth-only shadow pass (directional)
        void RenderShadowDepth(Shader & shadowShader, const glm::mat4 & lightVP);

        // Toggle instancing at runtime
        void SetInstancingEnabled(bool enabled) { instancingEnabled_ = enabled; }
        bool GetInstancingEnabled() const { return instancingEnabled_; }

        // Punctual lights are a BOUNDED set: the shader carries a fixed array,
        // so when a scene has more lights than this the strongest ones win
        // rather than an arbitrary prefix. Must match MAX_PUNCTUAL_LIGHTS in
        // Exported/Shaders/frag.glsl.
        static constexpr size_t kMaxPunctualLights = 16;

        // Gathers every enabled LightComponent with a Transform, resolves it to
        // world space, drops lights whose range cannot reach the camera's
        // neighbourhood, and keeps the kMaxPunctualLights most influential
        // (brightest per unit distance-squared). Pure function of the registry
        // — no GL, no renderer state — so it is directly testable.
        static void SelectPunctualLights(entt::registry& reg, const glm::vec3& camPos,
                                         std::vector<PunctualLight>& out,
                                         size_t maxLights = kMaxPunctualLights,
                                         unsigned* culledOut = nullptr);

        // Mesh LOD: level picked per entity from camera distance vs object size
        void  SetLODEnabled(bool v) { lodEnabled_ = v; }
        bool  GetLODEnabled() const { return lodEnabled_; }
        // >1 keeps high detail farther out; <1 switches down sooner (cheaper)
        void  SetLODDistanceScale(float s) { lodDistanceScale_ = std::clamp(s, 0.1f, 8.f); }
        float GetLODDistanceScale() const { return lodDistanceScale_; }

        // Projected-size cull: drop objects whose bounding sphere projects
        // smaller than N pixels tall from the forward pass. Adaptive with
        // camera distance/altitude — the lever that actually helps a
        // vertex/instance-bound wide view (shadows/PCF/fill measured free).
        // Needs the viewport pixel height passed to RenderScene.
        void  SetSmallCullEnabled(bool v) { smallCullEnabled_ = v; }
        bool  GetSmallCullEnabled() const { return smallCullEnabled_; }
        // pixel-height floor; higher culls more (and pops sooner). 0 disables.
        void  SetSmallCullPixels(float px) { smallCullPixels_ = std::clamp(px, 0.f, 64.f); }
        float GetSmallCullPixels() const { return smallCullPixels_; }

        // Depth prepass: lay depth down first (cheap shader), then shade color
        // with depth-equal so each pixel runs the PBR shader exactly once.
        void  SetDepthPrepassEnabled(bool v) { depthPrepassEnabled_ = v; }
        bool  GetDepthPrepassEnabled() const { return depthPrepassEnabled_; }
        // Non-owning; set each frame by the forward pass (same vertex shader as
        // the color pass so gl_Position matches bit-for-bit under GL_EQUAL).
        void  SetDepthPrepassShader(Shader* s) { depthPrepassShader_ = s; }

        // True if the SHADOW FOOTPRINT of any caster whose transform changed
        // this frame overlaps the camera view-depth range [zNear, zFar].
        // Fragments pick their cascade by view depth, so only cascades whose
        // slice range the shadow can reach ever sample it — a spinning object
        // near the camera must not re-render the (huge, expensive) far
        // cascades every frame.
        bool HasDynamicCasterInViewRange(const glm::vec3& camPos, const glm::vec3& camFwd,
                                         float zNear, float zFar,
                                         const glm::vec3& sunDir) const;
        
        // Read-only stats for the last frame
        const RenderStats &GetRenderStats() const { return lastStats_; }

        // Environment lighting. Scene-level (like the sun) rather than a
        // component: there is exactly one environment, and it is what the
        // single CSM-casting sun is lighting alongside.
        EnvironmentSettings&       Environment() { return environment_; }
        const EnvironmentSettings& Environment() const { return environment_; }
        void SetEnvironment(const EnvironmentSettings& e) { environment_ = e; }
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
        // Whether the environment maps actually EXIST this frame. Runtime
        // state set by the render pass, deliberately NOT serialized: it
        // describes GL resources, not authoring intent. IBL needs both this
        // and the user's iblEnabled_ toggle.
        void  SetIBLAvailable(bool v) { iblAvailable_ = v; }
        bool  GetIBLAvailable() const { return iblAvailable_; }
        float GetIBLIntensity() const { return iblIntensity_; }
        void  SetIBLIntensity(float v) { iblIntensity_ = std::max(0.0f, v); }
        // add a forward-only method (public)
        virtual void RenderDepth(class Shader& depthProg, const glm::mat4& lightVP);
        virtual void RenderDepthCascade(Shader& prog, const glm::mat4& lightVP, float splitNear, float splitFar, const glm::mat4& camView);

#include <functional>

        struct CascadeParam {
            glm::mat4 lightVP;
            float splitNear;
            float splitFar;
            glm::mat4 viewMatrix; // camera view matrix to check splitZ
        };
        // Culls and buckets entities for all cascades in one go.
        // preDrawCallback(index) is called before drawing the bucket for cascade 'index', allowing FBO/Viewport switches.
        virtual void RenderShadowsCombined(Shader& shadowShader, const std::vector<CascadeParam>& cascades, std::function<void(int)> preDrawCallback = nullptr);

     private:
         std::vector<DrawItem> items_;
         // Reusable storage for shadow items per cascade (up to 4)
         std::vector<DrawItem> shadowCascadeItems_[4];
         // private:
         GLuint instanceVBO_ = 0;
         std::size_t instanceVBOCapacity_ = 0; // high-water mark, never shrinks
         void ensureInstanceBuffer_();
         // Orphan at capacity + subdata: the store must NEVER shrink, because
         // mesh VAOs keep instanced-attrib pointers baked at large offsets and
         // a smaller store would make later fetches out-of-bounds (GL UB).
         void uploadInstanceMats_();
         // sets attribs 8..11 + divisors on the current VAO, reading from
         // instanceVBO_ starting at byteOffset
         void bindInstanceAttribs_(std::size_t byteOffset) const;

         // Choose material (override -> shared) for an item and bind it for drawing
         static bool aabbIntersectsLightFrustum(const glm::mat4& lightVP, const AABB& aabb, const glm::mat4& model);
         
         const Material * chooseMaterial_(entt::entity e, const Mesh & mesh) const;
         void bindMaterialForItem_(const DrawItem & di, Shader & shader) const;
         static uint64_t texKeyFromMaterial_(const Material & m);

         bool instancingEnabled_ = true;
         bool lodEnabled_ = true;
         float lodDistanceScale_ = 1.0f;
         // Off by default: it changes what's visible (distant objects pop out),
         // so it's opt-in. Editor exposes an enable + pixel-floor slider; the
         // player can turn it on per project. 3px is a safe "sub-visible" floor.
         bool  smallCullEnabled_ = false;
         float smallCullPixels_ = 3.0f;
         // Off by default: early-Z already rejects most occluded fragments in
         // front-to-back-ish scenes, so on this content the extra geometry
         // submission costs more than the shading it saves. Enable for scenes
         // with heavy fragment cost + bad depth ordering.
         bool depthPrepassEnabled_ = false;
         Shader* depthPrepassShader_ = nullptr; // non-owning (forward pass owns it)

         // Per-frame scratch: instanced-run table + gathered instance matrices
         // (single buffer upload per frame; per-run map/unmap cycles were the
         // top frame cost at high instance counts)
         struct DrawRun {
             uint64_t texKey;
             const Mesh* mesh;
             int lod;
             std::size_t first;     // index into items_
             std::size_t count;
             std::size_t matOffset; // index into instanceMats_
         };
         std::vector<DrawRun> runs_;
         std::vector<glm::mat4> instanceMats_;
         // per-frame scratch for the selected punctual lights (reused so the
         // light upload does not allocate every frame)
         std::vector<PunctualLight> punctualScratch_;

         // world-space bounding spheres of casters whose transforms changed
         // this frame
         struct DirtyCaster { glm::vec3 center; float radius; };
         std::vector<DirtyCaster> dirtyCasters_;

         RenderStats lastStats_;
         EnvironmentSettings environment_{};
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
         bool  iblAvailable_ = false; // runtime; set by the render pass
         float iblIntensity_ = 1.0f;
    };

} // namespace MyCoreEngine
