#include <glad/glad.h>
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp> // extractEulerAngleYXZ (matches localMatrix's Y*X*Z)
#include "Scene.h"
#include "CameraDirector.h" // FindActiveCamera delegates to its selection


// --- game camera helpers ---------------------------------------------------

entt::entity MyCoreEngine::FindActiveCamera(entt::registry& reg)
{
    return CameraDirector::SelectCamera(reg);
}

bool MyCoreEngine::SyncCameraFromEntity(entt::registry& reg, entt::entity e,
                                        Camera& cam)
{
    if (!reg.valid(e) || !reg.all_of<CameraComponent, Transform>(e)) return false;
    const auto& cc = reg.get<CameraComponent>(e);
    const glm::mat4& m = reg.get<Transform>(e).modelMatrix; // WORLD (hierarchy applied)

    auto column = [&](int c, glm::vec3 fallback) {
        const glm::vec3 v(m[c]);
        const float len = glm::length(v);
        return (len > 1e-6f) ? v / len : fallback;
    };
    cam.Position = glm::vec3(m[3]);
    cam.Right = column(0, { 1.f, 0.f, 0.f });
    cam.Up = column(1, { 0.f, 1.f, 0.f });
    cam.Front = -column(2, { 0.f, 0.f, 1.f }); // identity looks down -Z
    // euler look state must match the vectors: the fly cam rebuilds Front
    // FROM Yaw/Pitch on the first look input, so leaving them stale snaps
    // the view when a caller later falls back to free-fly controls
    cam.Yaw = glm::degrees(std::atan2(cam.Front.z, cam.Front.x));
    cam.Pitch = glm::degrees(std::asin(glm::clamp(cam.Front.y, -1.f, 1.f)));
    // clamp: fov outside (0,180) degenerates tan(fov/2) in every projection;
    // near must stay positive and far strictly past near or the projection
    // divides by zero (relative separation — see MinFarClipFor)
    cam.Zoom = glm::clamp(cc.fovDeg, 1.f, 179.f);
    cam.NearClip = std::max(cc.nearClip, 1e-3f);
    cam.FarClip = std::max(cc.farClip, MinFarClipFor(cam.NearClip));
    return true;
}

// --- transform hierarchy helpers (P2-8) ------------------------------------

bool MyCoreEngine::IsSameOrDescendantOf(entt::registry& reg, entt::entity node,
                                        entt::entity ancestor)
{
    // hop cap guards corrupt Parent cycles
    int hops = 0;
    for (entt::entity cur = node; cur != entt::null && hops < 100000; ++hops) {
        if (cur == ancestor) return true;
        const auto* p = reg.valid(cur) ? reg.try_get<Parent>(cur) : nullptr;
        cur = (p && reg.valid(p->value)) ? p->value : entt::null;
    }
    return false;
}

glm::mat4 MyCoreEngine::ResolveWorldMatrix(entt::registry& reg, entt::entity e)
{
    glm::mat4 world(1.f);
    int hops = 0;
    for (entt::entity cur = e; cur != entt::null && hops < 100000; ++hops) {
        const auto* t = reg.valid(cur) ? reg.try_get<Transform>(cur) : nullptr;
        if (!t) break;
        world = t->localMatrix() * world;
        const auto* p = reg.try_get<Parent>(cur);
        cur = (p && reg.valid(p->value)) ? p->value : entt::null;
    }
    return world;
}

void MyCoreEngine::DecomposeTRS(const glm::mat4& m, glm::vec3& outPos,
                                glm::vec3& outRotDeg, glm::vec3& outScale)
{
    // assumes no shear/negative scale
    outPos = glm::vec3(m[3]);
    outScale = { glm::length(glm::vec3(m[0])),
                 glm::length(glm::vec3(m[1])),
                 glm::length(glm::vec3(m[2])) };
    glm::mat4 rot(1.f);
    for (int c = 0; c < 3; ++c) {
        const float len = glm::length(glm::vec3(m[c]));
        rot[c] = (len > 1e-8f) ? glm::vec4(glm::vec3(m[c]) / len, 0.f)
                               : glm::mat4(1.f)[c];
    }
    float ry = 0.f, rx = 0.f, rz = 0.f;
    glm::extractEulerAngleYXZ(rot, ry, rx, rz); // localMatrix builds Y*X*Z
    outRotDeg = glm::degrees(glm::vec3(rx, ry, rz));
}

bool MyCoreEngine::SetParentKeepWorld(entt::registry& reg, entt::entity child,
                                      entt::entity newParent)
{
    if (!reg.valid(child) || !reg.all_of<Transform>(child)) return false;
    if (newParent != entt::null) {
        if (!reg.valid(newParent) || !reg.all_of<Transform>(newParent)) return false;
        // no cycles: the new parent must not be the child or below it
        if (IsSameOrDescendantOf(reg, newParent, child)) return false;
    }

    // world transform resolved from local TRS chains, NOT cached matrices —
    // cached ones can be stale mid-frame (e.g. right after a gizmo drag)
    const glm::mat4 childWorld = ResolveWorldMatrix(reg, child);
    glm::mat4 newLocal = childWorld;
    if (newParent != entt::null) {
        newLocal = glm::inverse(ResolveWorldMatrix(reg, newParent)) * childWorld;
    }

    auto& t = reg.get<Transform>(child);
    DecomposeTRS(newLocal, t.position, t.rotation, t.scale);
    t.dirty = true;

    if (newParent == entt::null) reg.remove<Parent>(child);
    else reg.emplace_or_replace<Parent>(child, Parent{ newParent });
    return true;
}

// Simple FNV-1a hash of the material’s bound texture ids
static uint64_t fnv1a64_(uint64_t h, uint32_t v) { h ^= v; return h * 1099511628211ull; }
uint64_t Scene::texKeyFromMaterial_(const Material & m) {
    uint64_t h = 1469598103934665603ull;
    h = fnv1a64_(h, m.albedoTex);
    h = fnv1a64_(h, m.normalTex);
    h = fnv1a64_(h, m.metallicTex);
    h = fnv1a64_(h, m.roughnessTex);
    h = fnv1a64_(h, m.aoTex);
    return h;    
}

Scene::~Scene() {
    if (instanceVBO_ && glfwGetCurrentContext()) {
        glDeleteBuffers(1, &instanceVBO_);
    }
}

const Material * Scene::chooseMaterial_(entt::entity e, const Mesh & mesh) const {
    if (registry.any_of<MaterialOverrides>(e)) {
        const auto & ov = registry.get<MaterialOverrides>(e);
        const size_t idx = mesh.MaterialIndex();
        if (auto it = ov.byIndex.find(idx); it != ov.byIndex.end() && it->second) {
            return it->second.get();
        }
    }
    return mesh.GetMaterial().get(); // shared fallback (may be null)
}

void Scene::bindMaterialForItem_(const DrawItem & di, Shader & shader) const {
    const Material * mat = chooseMaterial_(di.entity, *di.mesh);
    if (mat) {
        di.mesh->BindForDrawWith(shader, *mat);
    }
    else {
        di.mesh->BindForDraw(shader); // legacy path (no material)
    }
}

Entity Scene::createEntity() {
    entt::entity handle = registry.create();
    return Entity(handle, &registry);
}

void Scene::ResetToDefaults() {
    registry.clear();
    dirtyCasters_.clear();
    lastStats_ = RenderStats{};

    // scene-level settings: mirror the in-class initializers in Scene.h
    // (keep the two in sync when adding settings)
    instancingEnabled_ = true;
    lodEnabled_ = true;
    lodDistanceScale_ = 1.0f;
    smallCullEnabled_ = false;
    smallCullPixels_ = 3.0f;
    depthPrepassEnabled_ = false;
    normalMapEnabled_ = true;
    pbrEnabled_ = true;
    metallic_ = 0.0f;
    roughness_ = 0.5f;
    ao_ = 1.0f;
    lightDir_ = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
    lightColor_ = glm::vec3(1.0f);
    lightIntensity_ = 3.0f;
    metallicMapEnabled_ = true;
    roughnessMapEnabled_ = true;
    aoMapEnabled_ = true;
    aaEnabled_ = true;
    iblEnabled_ = true;
    iblIntensity_ = 1.0f;
}

void Scene::UpdateTransforms()
{
    auto worldSphere = [](const glm::mat4& m, const AABB& b) -> DirtyCaster {
        const glm::vec3 localC = (b.min + b.max) * 0.5f;
        const glm::vec3 worldC = glm::vec3(m * glm::vec4(localC, 1.f));
        const float maxScale = std::max({ glm::length(glm::vec3(m[0])),
                                          glm::length(glm::vec3(m[1])),
                                          glm::length(glm::vec3(m[2])) });
        const float radius = std::max(0.01f, glm::length(b.max - b.min) * 0.5f * maxScale);
        return { worldC, radius };
    };

    dirtyCasters_.clear();

    // Hierarchy pass (P2-8): world = parentWorld * localTRS, resolved
    // root-down. A node recomputes when its own local TRS is dirty OR any
    // ancestor recomputed, so children follow a moving parent. Entities in
    // a Parent cycle are unreachable from any root and simply freeze (the
    // editor refuses to create cycles; this guards corrupt data).
    auto view = registry.view<Transform>();

    // derived children adjacency; rebuilt per call (scene sizes are small)
    std::unordered_map<entt::entity, std::vector<entt::entity>> children;
    std::vector<entt::entity> roots;
    for (auto e : view) {
        const auto* p = registry.try_get<Parent>(e);
        if (p && p->value != entt::null && registry.valid(p->value) &&
            registry.all_of<Transform>(p->value)) {
            children[p->value].push_back(e);
        }
        else {
            roots.push_back(e);
        }
    }

    struct Item { entt::entity e; entt::entity parent; bool parentMoved; };
    std::vector<Item> stack;
    stack.reserve(roots.size());
    for (auto r : roots) stack.push_back({ r, entt::null, false });

    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        auto& t = view.get<Transform>(it.e);

        const bool needs = t.dirty || it.parentMoved;
        if (needs) {
            // Remember moved/rotated shadow casters so the CSM pass can
            // refresh cascades whose region their shadow can touch. BOTH the
            // departure and arrival positions matter: recording only the new
            // sphere leaves a baked "ghost" shadow behind teleports and fast
            // per-frame moves. The model must actually be LOADED — an empty
            // ModelComponent renders and casts nothing (every render path
            // null-checks it; this predicate must too).
            const auto* mcPtr = registry.try_get<ModelComponent>(it.e);
            const bool casts = !registry.any_of<NoShadow>(it.e) &&
                mcPtr && mcPtr->model && registry.all_of<AABB>(it.e);
            if (casts) {
                dirtyCasters_.push_back(worldSphere(t.modelMatrix, registry.get<AABB>(it.e)));
            }
            if (it.parent != entt::null) {
                // parent processed first: its modelMatrix is current
                t.modelMatrix = view.get<Transform>(it.parent).modelMatrix * t.localMatrix();
                t.dirty = false;
            }
            else {
                t.updateMatrix();
            }
            if (casts) {
                dirtyCasters_.push_back(worldSphere(t.modelMatrix, registry.get<AABB>(it.e)));
            }
        }

        if (auto itc = children.find(it.e); itc != children.end()) {
            for (auto c : itc->second) stack.push_back({ c, it.e, needs });
        }
    }
}

bool Scene::HasDynamicCasterInViewRange(const glm::vec3& camPos, const glm::vec3& camFwd,
                                        float zNear, float zFar,
                                        const glm::vec3& sunDir) const
{
    // Shadows lengthen as the sun drops: scale the swept footprint by the
    // sun's horizontal-over-vertical slope. Still an approximation for very
    // tall casters high above their receivers.
    const float sunSlope = glm::length(glm::vec2(sunDir.x, sunDir.z)) /
        std::max(std::abs(sunDir.y), 0.2f);

    for (const auto& dc : dirtyCasters_) {
        // Approximate the caster's shadow footprint as a sphere around the
        // caster swept along the light direction.
        const float shadowLen = dc.radius * (4.f + 4.f * sunSlope);
        const glm::vec3 sweptCenter = dc.center + sunDir * (shadowLen * 0.5f);
        const float sweptRadius = dc.radius + shadowLen * 0.5f;

        const float z = glm::dot(sweptCenter - camPos, camFwd);
        if (z + sweptRadius >= zNear && z - sweptRadius <= zFar) {
            return true;
        }
    }
    return false;
}

void Scene::SelectPunctualLights(entt::registry& reg, const glm::vec3& camPos,
                                 std::vector<PunctualLight>& out, size_t maxLights,
                                 unsigned* culledOut)
{
    out.clear();
    unsigned culled = 0;
    if (maxLights == 0) {
        if (culledOut) *culledOut = 0;
        return;
    }

    // score = influence at the camera. Brightness falls off with the square of
    // distance, so this ranks "how much can this light possibly matter to what
    // I am looking at" — a dim lamp at your feet outranks a floodlight a
    // kilometre away, which is the behaviour you want when the array overflows.
    struct Scored { PunctualLight light; float score; };
    std::vector<Scored> scored;

    auto view = reg.view<LightComponent, Transform>();
    for (auto e : view) {
        const auto& lc = view.get<LightComponent>(e);
        const auto& t = view.get<Transform>(e);
        if (!lc.enabled || lc.intensity <= 0.f || lc.range <= 0.f) { ++culled; continue; }

        // modelMatrix is a CACHE filled by UpdateTransforms; right after a
        // scene load it is still identity, so resolve dirty entities properly
        // (same trap that put physics bodies at the origin).
        const glm::mat4 world = t.dirty ? ResolveWorldMatrix(reg, e) : t.modelMatrix;

        PunctualLight p;
        p.position = glm::vec3(world[3]);
        p.range = lc.range;
        p.color = lc.color;
        p.intensity = lc.intensity;
        p.type = static_cast<int>(lc.type);

        if (lc.type == LightType::Spot) {
            // aim down the entity's -Z, matching the camera convention
            const glm::vec3 fwd = -glm::vec3(world[2]);
            const float len = glm::length(fwd);
            p.spotDir = (len > 1e-6f) ? fwd / len : glm::vec3(0.f, 0.f, -1.f);
            // clamp so outer >= inner; an inverted cone would make smoothstep
            // run backwards and light everything OUTSIDE the cone instead
            const float inner = glm::clamp(lc.innerAngleDeg, 0.f, 89.9f);
            const float outer = glm::clamp(std::max(lc.outerAngleDeg, inner), 0.f, 89.9f);
            p.cosInner = std::cos(glm::radians(inner));
            p.cosOuter = std::cos(glm::radians(outer));
        }

        // Range cull: a light whose sphere of influence cannot reach anywhere
        // near the camera cannot affect a visible fragment.
        const float d = glm::length(p.position - camPos);
        const float reach = d - p.range;

        Scored s;
        s.light = p;
        s.score = lc.intensity / (std::max(reach, 1.f) * std::max(reach, 1.f));
        scored.push_back(s);
    }

    if (scored.size() > maxLights) {
        std::partial_sort(scored.begin(), scored.begin() + maxLights, scored.end(),
            [](const Scored& a, const Scored& b) { return a.score > b.score; });
        culled += static_cast<unsigned>(scored.size() - maxLights);
        scored.resize(maxLights);
    }

    out.reserve(scored.size());
    for (const auto& s : scored) out.push_back(s.light);
    if (culledOut) *culledOut = culled;
}

// Renderer calls this; we keep signature identical.
// Now builds a draw list with frustum culling, sorts, then batches by texture key.
void Scene::RenderScene(const Frustum& camFrustum, Shader& shader, Camera& camera,
                        int viewportHeightPx)
{
    shader.setVec3("uCamPos", camera.Position);
    RenderStats stats{}; // local accumulator for this frame
    items_.clear();
    items_.reserve(1024);
    transparentItems_.clear(); // rebuilt below; consumed by RenderTransparent

    // Projected-size cull needs the vertical-FOV factor and a pixel height.
    // Object pixel height ~= viewportH * (2*radius) / (2*dist*tan(fovY/2))
    //                      = viewportH * radius / (dist * tanHalfFov).
    // Zoom is vertical FOV in DEGREES (mirror ForwardOpaquePass's frustum).
    const float tanHalfFov = std::tan(glm::radians(camera.Zoom) * 0.5f);
    const bool  screenCull = smallCullEnabled_ && viewportHeightPx > 0 &&
                             tanHalfFov > 1e-4f && smallCullPixels_ > 0.f;

    // 1) Build draw list with frustum test
    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto entity : view) {
        auto& mc = view.get<ModelComponent>(entity);
        auto& t = view.get<Transform>(entity);
        auto& bounds = view.get<AABB>(entity);
        stats.entitiesTotal++;

        if (!mc.model) continue;
        if (!bounds.isOnFrustum(camFrustum, t)) { stats.culled++; continue; }

        // World-space bounding sphere (center + radius) — needed by BOTH the
        // LOD level and the screen-size cull, so it's computed unconditionally
        // (was inside the lodEnabled_ block).
        const glm::vec3 localC = (bounds.min + bounds.max) * 0.5f;
        const glm::vec3 worldC = glm::vec3(t.modelMatrix * glm::vec4(localC, 1.f));
        const float maxScale = std::max({ glm::length(glm::vec3(t.modelMatrix[0])),
                                          glm::length(glm::vec3(t.modelMatrix[1])),
                                          glm::length(glm::vec3(t.modelMatrix[2])) });
        const float radius = std::max(0.01f, glm::length(bounds.max - bounds.min) * 0.5f * maxScale);
        const float dist = glm::length(worldC - camera.Position);

        // Screen-space size cull: skip objects whose projected sphere is
        // smaller than the threshold in pixels. Adaptive — flying high/wide
        // shrinks distant objects below the floor and bounds the frame, which
        // is the only lever that helps a vertex/instance-bound wide view
        // (shadows/PCF/fill were measured free). The object is dropped from
        // the FORWARD pass only; its (equally sub-pixel) shadow is left to the
        // shadow pass, so no caster-set change and no CSM ghosting.
        if (screenCull && dist > 1e-3f) {
            const float pixelH = (float)viewportHeightPx * radius / (dist * tanHalfFov);
            if (pixelH < smallCullPixels_) { stats.culledSmall++; continue; }
        }

        // LOD by camera distance relative to the object's world-space size
        int lod = 0;
        if (lodEnabled_) {
            const float ratio = dist / (radius * lodDistanceScale_);
            lod = (ratio > 60.f) ? 2 : (ratio > 25.f) ? 1 : 0;
        }

        // Push one DrawItem per mesh in the model
        for (const auto& mesh : mc.model->Meshes()) {
            DrawItem di;
            di.entity = entity;
            di.mesh = &mesh;
            di.model = t.modelMatrix;
            di.lod = lod;
            di.depth = glm::dot(glm::vec3(di.model[3]) - camera.Position, camera.Front);
            // Batch key is derived from the material actually used by this entity
            if (const Material* m = chooseMaterial_(entity, mesh)) {
                di.texKey = texKeyFromMaterial_(*m);
                di.alphaMode = static_cast<int>(m->alphaMode);
                di.doubleSided = m->doubleSided;
                di.shadingModel = static_cast<int>(m->shadingModel);
            }
            else {
            // Fallback: keep old key if no material (rare)
                di.texKey = mesh.TextureSignature();
            }
            // Blend geometry is drawn separately (sorted, composited, no depth
            // write); everything else joins the opaque list. When no material
            // is transparent -- the overwhelmingly common case -- this is a
            // straight push to items_ exactly as before.
            if (di.alphaMode == static_cast<int>(AlphaMode::Blend))
                transparentItems_.push_back(di);
            else
                items_.push_back(di);
        }
    }
    stats.itemsBuilt = static_cast<unsigned>(items_.size());

    // 2) Sort: Opaque (0) before Mask (1) so the opaque runs form a contiguous
    // prefix (the depth prepass and GL_EQUAL color path cover only them), then
    // by texKey to minimize texture/VAO rebinds. When every item is Opaque --
    // the usual case -- alphaMode is 0 for all and this reduces EXACTLY to the
    // previous {texKey, mesh, lod, depth} order, so opaque scenes are byte-for-
    // byte unchanged.
    std::sort(items_.begin(), items_.end(),
        [](const DrawItem& a, const DrawItem& b) {
            if (a.alphaMode != b.alphaMode) return a.alphaMode < b.alphaMode;  // opaque prefix, masked suffix
            // Single-sided before double-sided so the prepass-covered runs
            // (opaque + single-sided) form a contiguous prefix. All-false for a
            // conventional opaque scene, so the order is unchanged there.
            if (a.doubleSided != b.doubleSided) return !a.doubleSided;         // false (0) sorts first
            if (a.shadingModel != b.shadingModel) return a.shadingModel < b.shadingModel; // PBR/Toon split into separate runs
            if (a.texKey != b.texKey) return a.texKey < b.texKey;              // bucket by textures
            if (a.mesh != b.mesh)   return (uintptr_t)a.mesh < (uintptr_t)b.mesh; // group identical VAOs
            if (a.lod != b.lod)     return a.lod < b.lod;                       // instanced runs share a LOD
            return a.depth < b.depth;                                           // front-to-back within a mesh
        });
    ensureInstanceBuffer_();

    // Discover instanced runs once and upload EVERY instance matrix in one
    // buffer update; draws then point the instanced attribs at per-run byte
    // offsets. (The old per-run glBufferData+glMapBuffer cycle was a driver
    // sync per run — the top frame cost at high instance counts.)
    runs_.clear();
    instanceMats_.clear();
    for (std::size_t i = 0; i < items_.size(); ) {
        const auto key = items_[i].texKey;
        const Mesh* mesh = items_[i].mesh;
        const int lod = items_[i].lod;
        const int amode = items_[i].alphaMode;
        const bool ds = items_[i].doubleSided;
        const int sm = items_[i].shadingModel;
        std::size_t runEnd = i + 1;
        while (runEnd < items_.size() &&
            items_[runEnd].texKey == key &&
            items_[runEnd].mesh == mesh &&
            items_[runEnd].lod == lod &&
            items_[runEnd].alphaMode == amode &&
            items_[runEnd].doubleSided == ds &&
            items_[runEnd].shadingModel == sm) { // homogeneous in mode, cull state AND shading
            ++runEnd;
        }
        const std::size_t count = runEnd - i;
        runs_.push_back({ key, mesh, lod, i, count, instanceMats_.size(), amode });
        if (instancingEnabled_ && count >= 2) {
            for (std::size_t k = 0; k < count; ++k)
                instanceMats_.push_back(items_[i + k].model);
        }
        i = runEnd;
    }
    uploadInstanceMats_();

    // --- depth prepass: same runs/LODs/matrices as the color pass with a
    // no-op fragment shader, so the expensive PBR/PCF shading below runs at
    // most once per pixel (GL_EQUAL rejects occluded fragments early).
    const bool prepass = depthPrepassEnabled_ && depthPrepassShader_ && depthPrepassShader_->isValid();
    if (prepass) {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        Shader& d = *depthPrepassShader_;
        d.use();
        d.setInt("uUseInstancing", 0);

        const Mesh* prevMesh = nullptr;
        for (const auto& r : runs_) {
            // Only opaque, single-sided runs belong in the prepass:
            //  - Masked runs would write depth for fragments the color pass
            //    discards (the prepass shader has no alpha test), punching
            //    solid holes into the depth buffer.
            //  - Double-sided runs are drawn front-only here (cull is on in the
            //    prepass), so their back-face depth would be missing and the
            //    color pass's back faces would fail GL_EQUAL and vanish.
            // Both depth-test and write normally in the color pass instead.
            const bool inPrepass = (r.alphaMode == static_cast<int>(AlphaMode::Opaque))
                                   && !items_[r.first].doubleSided;
            if (!inPrepass) continue;
            if (r.mesh != prevMesh) {
                glBindVertexArray(r.mesh->VAO());
                prevMesh = r.mesh;
            }
            if (instancingEnabled_ && r.count >= 2) {
                bindInstanceAttribs_(r.matOffset * sizeof(glm::mat4));
                d.setInt("uUseInstancing", 1);
                r.mesh->IssueDrawInstanced(static_cast<GLsizei>(r.count), r.lod);
                d.setInt("uUseInstancing", 0);
            }
            else {
                for (std::size_t k = 0; k < r.count; ++k) {
                    d.setMat4("model", items_[r.first + k].model);
                    r.mesh->IssueDraw(r.lod);
                }
            }
        }

        // color pass: shade only the surviving depth, don't write depth
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
    }

    shader.use();
    shader.setInt("uUseInstancing", 0);
    uploadGlobalShadingUniforms_(shader, camera, stats);

    uint64_t currentKey = ~0ull;
    const Mesh* currentMesh = nullptr;
    int  boundAlphaMode = -1;      // alpha mode of the last-bound material
    int  boundShadingModel = -1;   // shading model of the last-bound material
    bool cullOff = false;
    bool depthSwitchedToNormal = false;

    for (const auto& r : runs_) {
        // The prepass covered only opaque + single-sided runs and left the
        // color pass in GL_EQUAL + depthMask FALSE. Everything else (masked,
        // or double-sided opaque) skipped the prepass and must depth-test and
        // write normally. Runs are sorted so all prepass-covered runs form the
        // prefix, so this one-way switch fires at most once. No-op when the
        // prepass is off (that state is already active).
        const bool inPrepass = (r.alphaMode == static_cast<int>(AlphaMode::Opaque))
                               && !items_[r.first].doubleSided;
        if (prepass && !inPrepass && !depthSwitchedToNormal) {
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            depthSwitchedToNormal = true;
        }

        // Double-sided materials draw their back faces too (foliage seen edge
        // on, glass interiors). Toggled per run; single-sided is the norm, so
        // this rarely fires on the opaque hot path.
        const bool wantCullOff = items_[r.first].doubleSided;
        if (wantCullOff != cullOff) {
            if (wantCullOff) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            cullOff = wantCullOff;
        }

        // bind textures+VAO bucket. Also re-bind when the alpha mode changes
        // even if the textures match: an Opaque and a Mask material with the
        // SAME texture set hash to the same texKey (texKeyFromMaterial_ hashes
        // textures only), and the opaque run sorts first -- without this the
        // masked run would skip the bind, keep the opaque run's uAlphaMode=0,
        // and silently render with no cutout.
        // Re-bind on shadingModel change too: a Toon run and a PBR run can share
        // the same textures + alpha mode (same texKey), so without this the
        // second would skip the bind and inherit the first's uShadingModel.
        if (r.texKey != currentKey || r.alphaMode != boundAlphaMode ||
            items_[r.first].shadingModel != boundShadingModel) {
            bindMaterialForItem_(items_[r.first], shader);
            currentKey = r.texKey;
            boundAlphaMode = r.alphaMode;
            boundShadingModel = items_[r.first].shadingModel;
            currentMesh = r.mesh;
            stats.textureBinds++;
            stats.vaoBinds++; // BindForDraw set VAO
        }
        else if (r.mesh != currentMesh) {
            glBindVertexArray(r.mesh->VAO());
            currentMesh = r.mesh;
            stats.vaoBinds++;
        }

        if (instancingEnabled_ && r.count >= 2) {
            bindInstanceAttribs_(r.matOffset * sizeof(glm::mat4));
            shader.setInt("uUseInstancing", 1);
            r.mesh->IssueDrawInstanced(static_cast<GLsizei>(r.count), r.lod);
            shader.setInt("uUseInstancing", 0);

            stats.instancedDraws++;
            stats.instances += static_cast<unsigned>(r.count);
            stats.submitted += static_cast<unsigned>(r.count);
            stats.lodInstances[glm::clamp(r.lod, 0, 2)] += static_cast<unsigned>(r.count);
        }
        else {
            for (std::size_t k = 0; k < r.count; ++k) {
                // per-item material bind: entities in the same texKey bucket can
                // still carry different override instances (same textures,
                // different scalars)
                bindMaterialForItem_(items_[r.first + k], shader);
                shader.setMat4("model", items_[r.first + k].model);
                r.mesh->IssueDraw(r.lod);
                stats.draws++;
                stats.submitted++;
                stats.lodInstances[glm::clamp(r.lod, 0, 2)]++;
            }
        }
    }

    // tidy
    if (prepass) {
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }
    if (cullOff) glEnable(GL_CULL_FACE); // a double-sided run left culling off
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);

    lastStats_ = stats; // publish render stats for the last frame
}

// Uploads the per-frame lighting/material-fallback uniforms shared by the
// opaque and transparent passes. Extracted so a transparent draw shades with
// EXACTLY the same sun, punctual lights, and IBL as the opaque geometry --
// otherwise glass would be lit by a different (or empty) light set and read as
// obviously wrong.
void Scene::uploadGlobalShadingUniforms_(Shader& shader, Camera& camera, RenderStats& stats) {
    shader.setInt("uUsePBR", pbrEnabled_ ? 1 : 0);
    shader.setFloat("uMetallic", metallic_);
    shader.setFloat("uRoughness", roughness_);
    shader.setFloat("uAO", ao_);
    shader.setVec3("uLightDir", lightDir_);
    shader.setVec3("uLightColor", lightColor_);
    shader.setFloat("uLightIntensity", lightIntensity_);

    // Punctual lights: selection is a pure function (testable headlessly),
    // this is just the upload. The array is bounded, so a scene with 500
    // lamps uploads the 16 that matter most to this camera.
    SelectPunctualLights(registry, camera.Position, punctualScratch_,
                         kMaxPunctualLights, &stats.lightsCulled);
    stats.lightsActive = static_cast<unsigned>(punctualScratch_.size());
    shader.setInt("uNumLights", static_cast<int>(punctualScratch_.size()));
    for (size_t i = 0; i < punctualScratch_.size(); ++i) {
        const PunctualLight& L = punctualScratch_[i];
        char name[40];
        std::snprintf(name, sizeof(name), "uLightPosRange[%zu]", i);
        shader.setVec4(name, glm::vec4(L.position, L.range));
        std::snprintf(name, sizeof(name), "uLightColorInt[%zu]", i);
        shader.setVec4(name, glm::vec4(L.color, L.intensity));
        std::snprintf(name, sizeof(name), "uLightSpotDir[%zu]", i);
        shader.setVec4(name, glm::vec4(L.spotDir, L.cosOuter));
        std::snprintf(name, sizeof(name), "uLightSpotMisc[%zu]", i);
        shader.setVec4(name, glm::vec4(L.cosInner, float(L.type), 0.f, 0.f));
    }
    shader.setInt("uUseMetallicMap", metallicMapEnabled_ ? 1 : 0);
    shader.setInt("uUseRoughnessMap", roughnessMapEnabled_ ? 1 : 0);
    shader.setInt("uUseAOMap", aoMapEnabled_ ? 1 : 0);
    shader.setInt("uUseIBL", (iblEnabled_ && iblAvailable_) ? 1 : 0);
    shader.setFloat("uIBLIntensity", iblIntensity_);
    shader.setInt("uNormalMapEnabled", normalMapEnabled_ ? 1 : 0);
    // Default alpha mode = Opaque. Per-material binds override it; this covers
    // the no-material (legacy BindForDraw) item, which would otherwise inherit
    // a stale uAlphaMode (e.g. Blend from the previous frame's transparent
    // pass) and wrongly discard or blend.
    shader.setInt("uAlphaMode", 0);
}

// Blend-mode geometry, drawn after the skybox so it composites over the whole
// scene. Sorted back-to-front (the order alpha blending REQUIRES: a near
// surface must blend over what is already behind it), depth-tested but not
// depth-writing (so two transparent surfaces do not occlude each other by
// depth), and never instanced (the sort order is per-item, not per-batch).
void Scene::RenderTransparent(Shader& shader, Camera& camera) {
    if (transparentItems_.empty()) return;

    // Back-to-front by view-space depth. depth was computed in RenderScene as
    // dot(centre - camPos, camFront), so larger = farther; draw farthest first.
    std::sort(transparentItems_.begin(), transparentItems_.end(),
        [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });

    shader.use();
    shader.setVec3("uCamPos", camera.Position);
    shader.setInt("uUseInstancing", 0);
    RenderStats scratch{}; // transparent lights fold into the opaque stats; not published
    uploadGlobalShadingUniforms_(shader, camera, scratch);

    // Establish a KNOWN cull state rather than trusting the incoming one.
    // (Belt-and-braces with SkyboxPass restoring it -- this pass must not
    // depend on another pass's cleanup.)
    glEnable(GL_CULL_FACE);

    // --- depth pre-pass ---------------------------------------------------
    // Blended geometry does not write depth, so within a single CONCAVE mesh
    // (a backpack, a bottle) the far interior faces blend over the near ones
    // in draw order and the object reads as turned inside-out. Prime the depth
    // buffer with the FRONTMOST transparent surface per pixel first (colour
    // off, depth write on, GL_LESS), then let the blend pass draw only that
    // surface (GL_LEQUAL). Cost is one extra colour-less draw per item.
    //
    // Tradeoff: where two transparent objects overlap you see only the nearer
    // one, not through it. That is the price of cheap sorted transparency;
    // true multi-layer needs order-independent transparency (tracked
    // separately). No material bind here -- the depth pre-pass only needs the
    // geometry, and the main shader is already current with view/proj set.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    {
        bool cullOff = false;
        for (const auto& di : transparentItems_) {
            const bool wantCullOff = di.doubleSided;
            if (wantCullOff != cullOff) {
                if (wantCullOff) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
                cullOff = wantCullOff;
            }
            glBindVertexArray(di.mesh->VAO());
            shader.setMat4("model", di.model);
            di.mesh->IssueDraw(di.lod);
        }
        if (cullOff) glEnable(GL_CULL_FACE);
    }

    // --- blend pass -------------------------------------------------------
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Blend happens in LINEAR HDR here, before tonemapping -- a 50%-opacity
    // surface is a true 50% radiance mix, tonemapped once with the frame.
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL); // == the frontmost surface primed above

    bool cullOff = false;
    const Mesh* currentMesh = nullptr;
    for (const auto& di : transparentItems_) {
        const bool wantCullOff = di.doubleSided;
        if (wantCullOff != cullOff) {
            if (wantCullOff) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            cullOff = wantCullOff;
        }
        bindMaterialForItem_(di, shader); // sets uAlphaMode=Blend, uOpacity, VAO
        if (di.mesh != currentMesh) currentMesh = di.mesh; // BindForDraw set the VAO
        shader.setMat4("model", di.model);
        di.mesh->IssueDraw(di.lod);
    }

    // Restore the state the rest of the pipeline assumes.
    if (cullOff) glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Scene::ensureInstanceBuffer_() {
    if (!instanceVBO_) {
        glGenBuffers(1, &instanceVBO_);
    }
}

void Scene::uploadInstanceMats_() {
    const std::size_t bytes = instanceMats_.size() * sizeof(glm::mat4);
    if (bytes == 0) return;
    if (bytes > instanceVBOCapacity_) instanceVBOCapacity_ = bytes;
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    // orphan at the high-water capacity (no GPU sync), then fill
    glBufferData(GL_ARRAY_BUFFER, instanceVBOCapacity_, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instanceMats_.data());
}

void Scene::bindInstanceAttribs_(std::size_t byteOffset) const {
    // current mesh VAO must already be bound
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    const GLsizei stride = sizeof(glm::mat4);
    const std::size_t vec4sz = sizeof(glm::vec4);

    for (int i = 0; i < 4; ++i) {
        GLuint loc = 8 + i; // 8,9,10,11
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<void*>(byteOffset + i * vec4sz));
        glVertexAttribDivisor(loc, 1);
    }
}

// Depth-only traversal for directional shadow map
void Scene::RenderShadowDepth(Shader & shadowShader, const glm::mat4 & lightVP)
{
    // ... (keeps legacy single-pass logic if needed, or we could redirect to Combined with 1 cascade, 
    // but the single render-depth is usually for non-CSM shadows or simple spots. keeping as is.)
    shadowShader.use();
    shadowShader.setMat4("uLightVP", lightVP);
    
    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto entity : view) {
        auto& mc = view.get<ModelComponent>(entity);
        auto& t = view.get<Transform>(entity);
        if (!mc.model) continue;
        if (registry.any_of<NoShadow>(entity)) continue;
            for (const auto& mesh : mc.model->Meshes()) {
                shadowShader.setMat4("model", t.modelMatrix);
                // No material/texture binds; just draw geometry
                mesh.IssueDraw();
            }
    }
}

void Scene::RenderShadowsCombined(Shader& shadowShader, const std::vector<CascadeParam>& cascades, std::function<void(int)> preDrawCallback)
{
    size_t numCascades = cascades.size();
    if (numCascades > 4) numCascades = 4;
    
    // 1. Clear buckets
    for (size_t i = 0; i < numCascades; ++i) {
        shadowCascadeItems_[i].clear();
        // pre-allocate reasonable size if empty
        if (shadowCascadeItems_[i].capacity() < 512) shadowCascadeItems_[i].reserve(1024);
    }

    // 2. Iterate entities once
    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto e : view) {
        const auto& mc = view.get<ModelComponent>(e);
        if (!mc.model) continue;
        if (registry.any_of<NoShadow>(e)) continue;

        const auto& t = view.get<Transform>(e);
        const auto& b = view.get<AABB>(e);

        // Test against each cascade.
        // NOTE: casters are culled against the LIGHT frustum only. Culling by
        // the camera's Z-slice is wrong for casters — an object outside the
        // slice (behind the camera, off to the side) can still cast a shadow
        // INTO the slice, and dropping it makes shadows pop as the camera
        // moves. Receiver-side slice selection happens in the shader.
        for (size_t c = 0; c < numCascades; ++c) {
            const auto& p = cascades[c];

            // Frustum cull (Light Space)
            if (!aabbIntersectsLightFrustum(p.lightVP, b, t.modelMatrix)) continue;

            // 2c. Add to bucket
            for (const auto& m : mc.model->Meshes()) {
                DrawItem di; 
                di.mesh = &m; 
                di.model = t.modelMatrix;
                shadowCascadeItems_[c].push_back(di);
            }
        }
    }

    // 3. Draw each bucket
    ensureInstanceBuffer_();
    shadowShader.use();
    shadowShader.setInt("uUseInstancing", 0);

    // Save/Restore state if needed? (We assume caller sets global render states)

    for (size_t c = 0; c < numCascades; ++c) {
        auto& bucket = shadowCascadeItems_[c];
        if (bucket.empty()) continue;

        // Shadow maps tolerate coarse geometry: near cascades use LOD 1,
        // far cascades LOD 2 (the cascade index is already a distance proxy).
        const int shadowLod = lodEnabled_ ? ((c <= 1) ? 1 : 2) : 0;

        // NEW: Callback to bind FBO/Viewport for this cascade
        if (preDrawCallback) {
            preDrawCallback((int)c);
        }

        // Set cascade-specific uniform
        shadowShader.setMat4("uLightVP", cascades[c].lightVP);

        // Sort by mesh for instancing
        std::sort(bucket.begin(), bucket.end(),
            [](const DrawItem& a, const DrawItem& b) { return a.mesh < b.mesh; });

        // Gather every instanced matrix in this bucket and upload once
        // (per-run map/unmap cycles were a driver sync each).
        instanceMats_.clear();
        for (size_t i = 0; i < bucket.size();) {
            const Mesh* mesh = bucket[i].mesh;
            size_t j = i + 1;
            while (j < bucket.size() && bucket[j].mesh == mesh) ++j;
            if (j - i >= 2) {
                for (size_t k = i; k < j; ++k) instanceMats_.push_back(bucket[k].model);
            }
            i = j;
        }
        uploadInstanceMats_();

        // Instanced draw loop
        size_t matOffset = 0;
        for (size_t i = 0; i < bucket.size();) {
            const Mesh* mesh = bucket[i].mesh;
            size_t j = i + 1;
            while (j < bucket.size() && bucket[j].mesh == mesh) ++j;
            const size_t run = j - i;

            glBindVertexArray(mesh->VAO());

            if (run >= 2) {
                bindInstanceAttribs_(matOffset * sizeof(glm::mat4));
                matOffset += run;
                shadowShader.setInt("uUseInstancing", 1);
                mesh->IssueDrawInstanced((GLsizei)run, shadowLod);
                shadowShader.setInt("uUseInstancing", 0);
            }
            else {
                shadowShader.setMat4("model", bucket[i].model);
                mesh->IssueDraw(shadowLod);
            }
            i = j;
        }
    }
    glBindVertexArray(0);
}

void Scene::RenderDepth(Shader& prog, const glm::mat4& lightVP)
{
    items_.clear();
    items_.reserve(1024);

    // use AABB view and cull per-cascade
    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto e : view) {
        const auto& mc = view.get<ModelComponent>(e);
        const auto& t = view.get<Transform>(e);
        const auto& b = view.get<AABB>(e);
        if (!mc.model) continue;
        if (registry.any_of<NoShadow>(e)) continue;
        if (!aabbIntersectsLightFrustum(lightVP, b, t.modelMatrix)) continue;

        for (const auto& m : mc.model->Meshes()) {
            DrawItem di; di.mesh = &m; di.model = t.modelMatrix;
            items_.push_back(di);
        }
    }
    // sort by mesh so we can instance consecutive items
    std::sort(items_.begin(), items_.end(),
        [](const DrawItem& a, const DrawItem& b) { return a.mesh < b.mesh; });

    ensureInstanceBuffer_();
    prog.setInt("uUseInstancing", 0);

    for (size_t i = 0; i < items_.size();) {
        const Mesh* mesh = items_[i].mesh;
        size_t j = i + 1;
        while (j < items_.size() && items_[j].mesh == mesh) ++j;
        const size_t run = j - i;

        glBindVertexArray(mesh->VAO());

        if (run >= 2) {
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
            glBufferData(GL_ARRAY_BUFFER, run * sizeof(glm::mat4), nullptr, GL_STREAM_DRAW);
            if (void* p = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) {
                auto* mats = static_cast<glm::mat4*>(p);
                for (size_t k = 0; k < run; ++k) mats[k] = items_[i + k].model;
                glUnmapBuffer(GL_ARRAY_BUFFER);
            }
            bindInstanceAttribs_(0);
            prog.setInt("uUseInstancing", 1);
            prog.setMat4("uLightVP", lightVP);
            mesh->IssueDrawInstanced((GLsizei)run);
            prog.setInt("uUseInstancing", 0);
        }
        else {
            prog.setMat4("uLightVP", lightVP);
            prog.setMat4("model", items_[i].model);
            mesh->IssueDraw();
        }

        i = j;
    }

    glBindVertexArray(0);
}

void Scene::RenderDepthCascade(Shader& prog, const glm::mat4& lightVP, float splitNear, float splitFar, const glm::mat4& camView)
{
    // Legacy single-cascade call: wrap and forward
    CascadeParam p;
    p.lightVP = lightVP;
    p.splitNear = splitNear;
    p.splitFar = splitFar;
    p.viewMatrix = camView;
    std::vector<CascadeParam> list = { p };
    RenderShadowsCombined(prog, list);
}
bool Scene::aabbIntersectsLightFrustum(const glm::mat4& lightVP, const AABB& aabb, const glm::mat4& model)
{
    // 8 corners in local space
    const glm::vec3 mn = aabb.min, mx = aabb.max;
    glm::vec3 c[8] = {
        {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},
        {mn.x,mn.y,mx.z},{mx.x,mn.y,mx.z},{mn.x,mx.y,mx.z},{mx.x,mx.y,mx.z}
    };

    int outside[6] = { 0,0,0,0,0,0 };
    for (int i = 0; i < 8; ++i) {
        glm::vec4 wc = model * glm::vec4(c[i], 1.0f);
        glm::vec4 cc = lightVP * wc; // clip
        // test vs clip planes: -w<=x<=w, -w<=y<=w, -w<=z<=w
        if (cc.x < -cc.w) outside[0]++; if (cc.x > cc.w)  outside[1]++;
        if (cc.y < -cc.w) outside[2]++; if (cc.y > cc.w)  outside[3]++;
        if (cc.z < -cc.w) outside[4]++; if (cc.z > cc.w)  outside[5]++;
    }
    for (int p = 0; p < 6; ++p) if (outside[p] == 8) return false; // fully outside
    return true;
}


