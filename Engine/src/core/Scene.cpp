#include <glad/glad.h>
#include <entt/entt.hpp>
#include <vector>
#include <algorithm>
#include <GLFW/glfw3.h>


#include "Scene.h"

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
        // Depth is optional (kept 0.0 here). If you later expose camera pos/dir via Frustum,
        // you can compute front-to-back ordering.
        for (const auto& mesh : mc.model->Meshes()) {
            DrawItem di;
            di.texKey = mesh.TextureSignature();
            di.mesh = &mesh;
            di.model = t.modelMatrix;
            di.depth = glm::dot(glm::vec3(di.model[3]) - camera.Position, camera.Front);
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


    shader.setInt("uNormalMapEnabled", normalMapEnabled_ ? 1 : 0);

    uint64_t currentKey = ~0ull;
    const Mesh* currentMesh = nullptr;

    for (std::size_t i = 0; i < items_.size(); ) {
        const auto key = items_[i].texKey;
        const auto* mesh = items_[i].mesh;

        // bind textures+VAO bucket
        if (key != currentKey) {
            mesh->BindForDraw(shader);
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
            mesh->IssueDraw();
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