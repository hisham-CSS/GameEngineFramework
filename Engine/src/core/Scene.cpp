#include <glad/glad.h>
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <GLFW/glfw3.h>
#include "Scene.h"


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

void Scene::UpdateTransforms()
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
void Scene::RenderScene(const Frustum& camFrustum, Shader& shader, Camera& camera)
{
    shader.setVec3("uCamPos", camera.Position);
    RenderStats stats{}; // local accumulator for this frame
    items_.clear();
    items_.reserve(1024);

    // 1) Build draw list with frustum test
    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto entity : view) {
        auto& mc = view.get<ModelComponent>(entity);
        auto& t = view.get<Transform>(entity);
        auto& bounds = view.get<AABB>(entity);
        stats.entitiesTotal++;

        if (!mc.model) continue;
        if (!bounds.isOnFrustum(camFrustum, t)) { stats.culled++; continue; }

        // Push one DrawItem per mesh in the model
        for (const auto& mesh : mc.model->Meshes()) {
            DrawItem di;
            di.entity = entity;
            di.mesh = &mesh;
            di.model = t.modelMatrix;
            di.depth = glm::dot(glm::vec3(di.model[3]) - camera.Position, camera.Front);
            // Batch key is derived from the material actually used by this entity
            if (const Material* m = chooseMaterial_(entity, mesh)) {
                di.texKey = texKeyFromMaterial_(*m);
            }
            else {
            // Fallback: keep old key if no material (rare)
                di.texKey = mesh.TextureSignature();
            }
            items_.push_back(di);
        }
    }
    stats.itemsBuilt = static_cast<unsigned>(items_.size());

    // 2) Sort: primarily by texKey to minimize texture/VAO rebinds
    // (If you later add camera depth, sort by {depth asc, texKey asc})
    std::sort(items_.begin(), items_.end(), 
        [](const DrawItem& a, const DrawItem& b) {
            if (a.texKey != b.texKey) return a.texKey < b.texKey;              // bucket by textures
            if (a.mesh != b.mesh)   return (uintptr_t)a.mesh < (uintptr_t)b.mesh; // group identical VAOs
            return a.depth < b.depth;                                           // front-to-back within a mesh
        });
    ensureInstanceBuffer_();
    shader.setInt("uUseInstancing", 0);

    shader.setInt("uUsePBR", pbrEnabled_ ? 1 : 0);
    shader.setFloat("uMetallic", metallic_);
    shader.setFloat("uRoughness", roughness_);
    shader.setFloat("uAO", ao_);
    shader.setVec3("uLightDir", lightDir_);
    shader.setVec3("uLightColor", lightColor_);
    shader.setFloat("uLightIntensity", lightIntensity_);
    shader.setInt("uUseMetallicMap", metallicMapEnabled_ ? 1 : 0);
    shader.setInt("uUseRoughnessMap", roughnessMapEnabled_ ? 1 : 0);
    shader.setInt("uUseAOMap", aoMapEnabled_ ? 1 : 0);
    shader.setInt("uUseIBL", iblEnabled_ ? 1 : 0);
    shader.setFloat("uIBLIntensity", iblIntensity_);

    shader.setInt("uNormalMapEnabled", normalMapEnabled_ ? 1 : 0);

    uint64_t currentKey = ~0ull;
    const Mesh* currentMesh = nullptr;

    for (std::size_t i = 0; i < items_.size(); ) {
        const auto key = items_[i].texKey;
        const auto* mesh = items_[i].mesh;

        // bind textures+VAO bucket
        if (key != currentKey) {
            bindMaterialForItem_(items_[i], shader);
            currentKey = key;
            currentMesh = mesh;
            stats.textureBinds++;
            stats.vaoBinds++; // BindForDraw set VAO
        }
        else if (mesh != currentMesh) {
            glBindVertexArray(mesh->VAO());
            currentMesh = mesh;
            stats.vaoBinds++;
        }

        // count consecutive items with same key+mesh
        std::size_t runStart = i, runEnd = i + 1;
        while (runEnd < items_.size() &&
            items_[runEnd].texKey == key &&
            items_[runEnd].mesh == mesh) {
            ++runEnd;
        }
        const std::size_t runCount = runEnd - runStart;

        if (instancingEnabled_ && runCount >= 2) {
            // upload instance matrices
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
            glBufferData(GL_ARRAY_BUFFER, runCount * sizeof(glm::mat4), nullptr, GL_STREAM_DRAW);
            if (void* p = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) {
                auto* mats = static_cast<glm::mat4*>(p);
                for (std::size_t k = 0; k < runCount; ++k)
                    mats[k] = items_[runStart + k].model;
                glUnmapBuffer(GL_ARRAY_BUFFER);
            }

            // enable instanced attributes on current VAO & draw once
            bindInstanceAttribs_();
            shader.setInt("uUseInstancing", 1);
            mesh->IssueDrawInstanced(static_cast<GLsizei>(runCount));
            shader.setInt("uUseInstancing", 0);

            i = runEnd;
            stats.instancedDraws++;
            stats.instances += static_cast<unsigned>(runCount);
            stats.submitted += static_cast<unsigned>(runCount);
        }
        else {
            // single draw (exactly your old path)
            shader.setMat4("model", items_[i].model);
            // Single draw: bind chosen material for this item (needed if the last bucket’s material differs)
            bindMaterialForItem_(items_[i], shader);
            shader.setMat4("model", items_[i].model);
            items_[i].mesh->IssueDraw();
            ++i;
            stats.draws++;
            stats.submitted++;
        }
    }

    // tidy
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);

    lastStats_ = stats; // publish render stats for the last frame
}

void Scene::ensureInstanceBuffer_() {
    if (!instanceVBO_) {
        glGenBuffers(1, &instanceVBO_);
    }
}

void Scene::bindInstanceAttribs_() const {
    // current mesh VAO must already be bound
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    const GLsizei stride = sizeof(glm::mat4);
    const GLsizei vec4sz = sizeof(glm::vec4);

    for (int i = 0; i < 4; ++i) {
        GLuint loc = 8 + i; // 8,9,10,11
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(i * vec4sz));
        glVertexAttribDivisor(loc, 1);
    }
}

// Depth-only traversal for directional shadow map
void Scene::RenderShadowDepth(Shader & shadowShader, const glm::mat4 & lightVP)
{
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
            bindInstanceAttribs_();
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
    items_.clear();
    items_.reserve(1024);

    auto view = registry.view<ModelComponent, Transform, AABB>();
    for (auto e : view) {
        const auto& mc = view.get<ModelComponent>(e);
        const auto& t = view.get<Transform>(e);
        const auto& b = view.get<AABB>(e);
        if (!mc.model) continue;
        if (registry.any_of<NoShadow>(e)) continue;

        // quick camera-space Z test
        glm::vec3 center = (b.min + b.max) * 0.5f;
        glm::vec4 vc = camView * t.modelMatrix * glm::vec4(center, 1.0f);
        float r = glm::length(b.max - center); // loose radius
        float vz = -vc.z; // camera looks -Z in view space

        if (vz + r < splitNear || vz - r > splitFar) continue; // outside slice

        // then the precise light-frustum test
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
            bindInstanceAttribs_();
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


