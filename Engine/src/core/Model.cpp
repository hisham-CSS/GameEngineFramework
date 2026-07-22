#include "Model.h"
#include "Shader.h"

#include <meshoptimizer.h>

#include <algorithm>

#include "stb_image.h"   // declarations only; implementation is in stb_image_impl.cpp
#include <iostream>

#include <glad/glad.h>

#include <cstdio>
#include <GLFW/glfw3.h>
// Diagnostics go to STDERR (stdout belongs to tools like AssetCooker whose
// stdout is a machine-readable protocol) as ONE fprintf per line — decode
// logs come from parallel workers, and the old separate trailing-newline
// call let two workers' lines splice together mid-line.
#define MLOG(...) do { char mlogBuf_[512]; \
    std::snprintf(mlogBuf_, sizeof(mlogBuf_), __VA_ARGS__); \
    std::fprintf(stderr, "[Model] %s\n", mlogBuf_); } while (0)


static std::string normPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

namespace MyCoreEngine {

    // ---- Mesh ----
    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::vector<Texture>& textures)
    : vertices_(vertices), indices_(indices), textures_(textures) {
        setupBuffers_();
        // synchronous path: compute LODs inline (same total work as before
        // the decode split — meshoptimizer always ran at load)
        uploadLods_(ComputeLodIndices(vertices_, indices_));
    }

    Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices,
               LodIndexArrays precomputedLods)
    : vertices_(std::move(vertices)), indices_(std::move(indices)) {
        setupBuffers_();
        uploadLods_(precomputedLods); // worker already ran meshoptimizer
    }

    static void deleteLodBuffers_(const Mesh::LodRange* lods, unsigned baseEBO) {
        // fallback levels alias the previous level's EBO; delete each unique
        // buffer exactly once (level 0 aliases the mesh's own EBO)
        for (int l = 1; l < Mesh::kLodCount; ++l) {
            if (lods[l].ebo && lods[l].ebo != lods[l - 1].ebo && lods[l].ebo != baseEBO) {
                unsigned ebo = lods[l].ebo;
                glDeleteBuffers(1, &ebo);
            }
        }
    }

    Mesh::~Mesh() {
        // texture ids are shared via the global cache; only buffers are owned here.
        // Guard: if the GL context is already gone (late static teardown), skip.
        if (!glfwGetCurrentContext()) return;
        deleteLodBuffers_(lods_, EBO_);
        if (EBO_) glDeleteBuffers(1, &EBO_);
        if (VBO_) glDeleteBuffers(1, &VBO_);
        if (VAO_) glDeleteVertexArrays(1, &VAO_);
    }

    Mesh::Mesh(Mesh&& other) noexcept
        : vertices_(std::move(other.vertices_)),
          indices_(std::move(other.indices_)),
          textures_(std::move(other.textures_)),
          material_(std::move(other.material_)),
          materialIndex_(other.materialIndex_),
          VAO_(other.VAO_), VBO_(other.VBO_), EBO_(other.EBO_) {
        for (int l = 0; l < kLodCount; ++l) { lods_[l] = other.lods_[l]; other.lods_[l] = {}; }
        other.VAO_ = other.VBO_ = other.EBO_ = 0;
    }

    Mesh& Mesh::operator=(Mesh&& other) noexcept {
        if (this != &other) {
            if (glfwGetCurrentContext()) {
                deleteLodBuffers_(lods_, EBO_);
                if (EBO_) glDeleteBuffers(1, &EBO_);
                if (VBO_) glDeleteBuffers(1, &VBO_);
                if (VAO_) glDeleteVertexArrays(1, &VAO_);
            }
            vertices_ = std::move(other.vertices_);
            indices_ = std::move(other.indices_);
            textures_ = std::move(other.textures_);
            material_ = std::move(other.material_);
            materialIndex_ = other.materialIndex_;
            VAO_ = other.VAO_; VBO_ = other.VBO_; EBO_ = other.EBO_;
            for (int l = 0; l < kLodCount; ++l) { lods_[l] = other.lods_[l]; other.lods_[l] = {}; }
            other.VAO_ = other.VBO_ = other.EBO_ = 0;
        }
        return *this;
    }
    void Mesh::setupBuffers_() {
        glGenVertexArrays(1, &VAO_);
        glGenBuffers(1, &VBO_);
        glGenBuffers(1, &EBO_);

        glBindVertexArray(VAO_);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), indices_.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        glBindVertexArray(0);
    }

    Mesh::LodIndexArrays Mesh::ComputeLodIndices(const std::vector<Vertex>& vertices,
                                                 const std::vector<unsigned int>& indices) {
        LodIndexArrays out{}; // all empty = every level falls back to base
        if (indices.size() < 3 * 64 || vertices.empty()) return out; // tiny meshes aren't worth simplifying

        // Simplified index buffers over the same vertex buffer: cheap on VRAM
        // (indices only) and the VAO/material state is shared across levels.
        const float targets[kLodCount] = { 1.f, 0.25f, 0.08f };
        size_t prevCount = indices.size(); // last ACCEPTED level's index count
        for (int l = 1; l < kLodCount; ++l) {
            const size_t targetCount = (size_t(indices.size() * targets[l]) / 3) * 3;
            std::vector<unsigned int> lodIndices(indices.size());
            float error = 0.f;
            const size_t count = meshopt_simplify(
                lodIndices.data(), indices.data(), indices.size(),
                &vertices[0].Position.x, vertices.size(), sizeof(Vertex),
                targetCount, /*target_error*/ 0.05f, /*options*/ 0, &error);

            // accept only if it meaningfully reduced the previous level
            if (count >= 3 && count < prevCount * 9 / 10) {
                lodIndices.resize(count);
                out[l] = std::move(lodIndices);
                prevCount = count;
            }
            // else: slot stays empty -> uploadLods_ aliases the previous level
        }
        return out;
    }

    void Mesh::uploadLods_(const LodIndexArrays& lodIndices) {
        lods_[0] = { EBO_, static_cast<GLsizei>(indices_.size()) };
        for (int l = 1; l < kLodCount; ++l) {
            const auto& src = lodIndices[l];
            if (!src.empty()) {
                unsigned ebo = 0;
                glGenBuffers(1, &ebo);
                // COPY_WRITE target: fills the buffer without touching any
                // VAO's element-array binding
                glBindBuffer(GL_COPY_WRITE_BUFFER, ebo);
                glBufferData(GL_COPY_WRITE_BUFFER, src.size() * sizeof(unsigned int), src.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
                lods_[l] = { ebo, static_cast<GLsizei>(src.size()) };
            }
            else {
                lods_[l] = lods_[l - 1]; // fall back to the previous level
            }
        }
    }

    const Mesh::LodRange& Mesh::Lod(int level) const {
        level = std::max(0, std::min(level, kLodCount - 1));
        return lods_[level];
    }
    void Mesh::Draw(Shader& shader) const {
        unsigned int diffuseNr=1, specularNr=1, normalNr=1, heightNr=1;

        for (unsigned int i = 0; i < textures_.size(); i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            std::string number;
            const std::string& name = textures_[i].type;
            if (name == "texture_diffuse")  number = std::to_string(diffuseNr++);
            else if (name == "texture_specular") number = std::to_string(specularNr++);
            else if (name == "texture_normal")   number = std::to_string(normalNr++);
            else if (name == "texture_height")   number = std::to_string(heightNr++);

            shader.setInt((name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, textures_[i].id);
        }

        glBindVertexArray(VAO_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_); // LOD draws may have rebound the VAO's element buffer
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }
    uint64_t Mesh::TextureSignature() const {
        auto idAt = [&](size_t i)->uint16_t {
            return i < textures_.size() ? (uint16_t)(textures_[i].id & 0xFFFFu) : 0u;
        };
        // [t0,t1,t2,t3] -> 64-bit key
        return (uint64_t)idAt(0)
            | ((uint64_t)idAt(1) << 16)
            | ((uint64_t)idAt(2) << 32)
            | ((uint64_t)idAt(3) << 48);
    }
    unsigned int Mesh::IndexCount() const { return static_cast<unsigned int>(indices_.size()); }
    unsigned int Mesh::VAO() const { return VAO_; } // whatever your VAO member is named
    void Mesh::IssueDrawInstanced(GLsizei instanceCount, int lod) const {
        // Explicitly bind the level's index buffer: LOD draws mutate the
        // shared VAO's element-array binding, so every issue path binds its own.
        const LodRange& lr = Lod(lod);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lr.ebo);
        glDrawElementsInstanced(GL_TRIANGLES, lr.indexCount, GL_UNSIGNED_INT, 0, instanceCount);
    }
    void Mesh::BindForDraw(MyCoreEngine::Shader& shader) const {

        if (material_) {
            // Per-material scalar uniforms (override scene defaults)
            shader.setVec3("uBaseColor", material_->baseColor);
            shader.setVec3("uEmissive", material_->emissive);
            shader.setFloat("uMetallic", material_->metallic);
            shader.setFloat("uRoughness", material_->roughness);
            shader.setFloat("uAO", material_->ao);
            // Fixed units: 0 albedo, 1 normal, 2 metal, 3 rough, 4 ao, 5 emissive (optional)
            // Albedo
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, material_->albedoTex);
            shader.setInt("diffuseMap", 0); // your shader uses this name
            // Normal
            const bool hasNormal = material_->hasNormal();
            if (hasNormal) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, material_->normalTex);
                shader.setInt("normalMap", 1);
            }
            shader.setInt("uHasNormalMap", hasNormal ? 1 : 0);

            // MR/A
            if (material_->hasMetallic()) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, material_->metallicTex);
                shader.setInt("metallicMap", 2);
            }
            if (material_->hasRoughness()) {
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, material_->roughnessTex);
                shader.setInt("roughnessMap", 3);
            }
            if (material_->hasAO()) {
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, material_->aoTex);
                shader.setInt("aoMap", 4);
            }
            shader.setInt("uHasMetallicMap", material_->hasMetallic() ? 1 : 0);
            shader.setInt("uHasRoughnessMap", material_->hasRoughness() ? 1 : 0);
            shader.setInt("uHasAOMap", material_->hasAO() ? 1 : 0);

            // Scalars fallback (shader already uses uMetallic/uRoughness/uAO set per-frame;
            // baseColor/emissive we can set per-draw here if you want)
            // If you want baseColor as a factor, add a uniform and set it here.

            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(VAO_);
            return;
        }

        // --- Find first diffuse & first normal ---
        unsigned int diffuseId = 0, normalId = 0;
        for (const auto& t : textures_) {
            if (!diffuseId && (t.type == "texture_diffuse" || t.type == "albedo" || t.type == "basecolor"))
                diffuseId = t.id;
            if (!normalId && (t.type == "texture_normal" || t.type == "normal" || t.type == "normalmap"))
                normalId = t.id;
            if (diffuseId && normalId) break;
        }

        // --- Bind fixed texture units ---
        // diffuse -> unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseId ? diffuseId : 0);
        shader.setInt("diffuseMap", 0);

        // normal -> unit 1 (if present)
        const bool hasNormal = (normalId != 0);
        if (hasNormal) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, normalId);
            shader.setInt("normalMap", 1);
        }
        shader.setInt("uHasNormalMap", hasNormal ? 1 : 0);

        // --- Metallic / Roughness / AO maps: bind to fixed slots if present ---
        // Common names we may see via Assimp importers:
        static auto isMetal = [](const std::string& t) {
            return t == "texture_metallic" || t == "metallic" || t == "metalness";
        };
        static auto isRough = [](const std::string& t) {
            return t == "texture_roughness" || t == "roughness" || t == "diffuse_roughness";
        };
        static auto isAO = [](const std::string& t) {
            return t == "texture_ambient" || t == "ao" || t == "occlusion" || t == "ambient_occlusion";
        };

        unsigned int metalId = 0, roughId = 0, aoId = 0;
        for (const auto& t : textures_) {
            if (!metalId && isMetal(t.type)) metalId = t.id;
            if (!roughId && isRough(t.type)) roughId = t.id;
            if (!aoId && isAO(t.type))    aoId = t.id;
            if (metalId && roughId && aoId) break;
        }

        // metallic -> unit 2
        if (metalId) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, metalId);
        }
        // roughness -> unit 3
        if (roughId) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, roughId);
        }
        // ao -> unit 4
        if (aoId) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, aoId);
        }

        // tell the shader
        shader.setInt("metallicMap", 2);
        shader.setInt("roughnessMap", 3);
        shader.setInt("aoMap", 4);
        shader.setInt("uHasMetallicMap", metalId ? 1 : 0);
        shader.setInt("uHasRoughnessMap", roughId ? 1 : 0);
        shader.setInt("uHasAOMap", aoId ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(VAO_);
    }
    void Mesh::BindForDrawWith(MyCoreEngine::Shader& shader, const MyCoreEngine::Material& m) const
    {
        // Per-material scalar uniforms
        shader.setVec3("uBaseColor", m.baseColor);
        shader.setVec3("uEmissive", m.emissive);
        shader.setFloat("uMetallic", m.metallic);
        shader.setFloat("uRoughness", m.roughness);
        shader.setFloat("uAO", m.ao);

        // Alpha: 1 Mask (discard below cutoff), 2 Blend (output opacity*alpha).
        // Uploaded ONLY for non-opaque materials, so an opaque bind emits the
        // exact same uniform traffic it did before transparency existed -- the
        // opaque hot path must not pay 3 extra glUniform calls per bind. Opaque
        // relies on the per-frame uAlphaMode=0 default set in
        // uploadGlobalShadingUniforms_, and opaque runs are always drawn before
        // any masked run, so uAlphaMode is never left stale at an opaque draw.
        if (m.alphaMode != MyCoreEngine::AlphaMode::Opaque) {
            shader.setInt("uAlphaMode", static_cast<int>(m.alphaMode));
            shader.setFloat("uOpacity", m.opacity);
            shader.setFloat("uAlphaCutoff", m.alphaCutoff);
        }

        // Textures: identical layout to your existing BindForDraw(...)
        // 0: albedo (sRGB), 1: normal (linear), 2: metallic, 3: roughness, 4: AO, 5: emissive (optional)
        const bool hasNormal = (m.normalTex != 0);
        const bool hasMetallic = (m.metallicTex != 0);
        const bool hasRoughness = (m.roughnessTex != 0);
        const bool hasAO = (m.aoTex != 0);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.albedoTex);    shader.setInt("diffuseMap", 0);
        if (hasNormal) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m.normalTex);    shader.setInt("normalMap", 1); }
        if (hasMetallic) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m.metallicTex);  shader.setInt("metallicMap", 2); }
        if (hasRoughness) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m.roughnessTex); shader.setInt("roughnessMap", 3); }
        if (hasAO) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, m.aoTex);        shader.setInt("aoMap", 4); }

        shader.setInt("uHasNormalMap", hasNormal ? 1 : 0);
        shader.setInt("uHasMetallicMap", hasMetallic ? 1 : 0);
        shader.setInt("uHasRoughnessMap", hasRoughness ? 1 : 0);
        shader.setInt("uHasAOMap", hasAO ? 1 : 0);

        glBindVertexArray(VAO_);
        glActiveTexture(GL_TEXTURE0);
    }

    void Mesh::IssueDraw(int lod) const {
        const LodRange& lr = Lod(lod);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lr.ebo);
        glDrawElements(GL_TRIANGLES, lr.indexCount, GL_UNSIGNED_INT, 0);
    }

    // ---- Model: decode stage (WORKER-SAFE — no GL, no shared state) ----

    namespace {
        // same filename resolution the old TextureFromFile used
        std::string resolveTexFilename(const std::string& file, const std::string& directory) {
            if (file.empty() || directory.empty()) return file;
            if (file[0] == '/' || file[0] == '\\') return file;
    #ifdef _WIN32
            return directory + '\\' + file;
    #else
            return directory + '/' + file;
    #endif
        }

        // probe primary-then-fallback slot for the first texture path
        std::string texFileFromSlot(::aiMaterial* mat, aiTextureType primary, aiTextureType fallback) {
            if (!mat) return {};
            aiString str;
            if (mat->GetTexture(primary, 0, &str) == AI_SUCCESS && str.length > 0)
                return normPath(std::string(str.C_Str()));
            if (fallback != aiTextureType_UNKNOWN &&
                mat->GetTexture(fallback, 0, &str) == AI_SUCCESS && str.length > 0)
                return normPath(std::string(str.C_Str()));
            return {};
        }

        // vertex/index extraction — unchanged from the old processMesh
        void collectMeshes(const ::aiScene* scene, ::aiNode* node, ModelCPUData& cpu) {
            for (unsigned int i = 0; i < node->mNumMeshes; i++) {
                ::aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
                ModelCPUData::MeshData md;

                md.vertices.reserve(mesh->mNumVertices);
                for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
                    Vertex vert{};
                    vert.Position = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };
                    vert.Normal   = mesh->HasNormals()
                                  ? glm::vec3{ mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z }
                                  : glm::vec3{ 0,0,0 };
                    if (mesh->mTextureCoords[0]) {
                        vert.TexCoords = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };
                        vert.Tangent   = { mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z };
                        vert.Bitangent = { mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z };
                    }
                    md.vertices.push_back(vert);
                }
                for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
                    const ::aiFace& face = mesh->mFaces[f];
                    for (unsigned int j = 0; j < face.mNumIndices; j++)
                        md.indices.push_back(face.mIndices[j]);
                }

                md.lodIndices = Mesh::ComputeLodIndices(md.vertices, md.indices);
                md.materialIndex = (mesh->mMaterialIndex < cpu.materials.size())
                                 ? (int)mesh->mMaterialIndex : -1;
                cpu.meshes.push_back(std::move(md));
            }
            for (unsigned int i = 0; i < node->mNumChildren; i++) {
                collectMeshes(scene, node->mChildren[i], cpu);
            }
        }
    } // namespace

    int Model::decodeTextureSlot_(ModelCPUData& cpu, ::aiMaterial* mat,
                                  aiTextureType primary, aiTextureType fallback,
                                  bool isSRGB, const std::string& directory,
                                  const std::unordered_set<std::string>* skipDecodeKeys)
    {
        const std::string file = texFileFromSlot(mat, primary, fallback);
        if (file.empty()) return -1; // slot absent on the material

        const std::string key = makeTexKey_(file, directory, isSRGB);
        for (size_t i = 0; i < cpu.textures.size(); ++i) {
            if (cpu.textures[i].key == key) return (int)i; // deduped within this model
        }

        ModelCPUData::TextureData t;
        t.key = key;
        t.srgb = isSRGB;
        t.file = resolveTexFilename(file, directory);

        // already uploaded (caller snapshotted the cache on the main
        // thread): record the slot, skip disk + decode entirely — finalize
        // resolves the key from the cache, pixels never needed. This keeps
        // reloads as cheap as the pre-split loader's cache-first lookup.
        if (skipDecodeKeys && skipDecodeKeys->count(key)) {
            cpu.textures.push_back(std::move(t)); // decoded=false, no pixels
            return (int)cpu.textures.size() - 1;
        }

        // per-THREAD flip flag: multiple workers decode concurrently and the
        // global stbi flag would be a data race
        stbi_set_flip_vertically_on_load_thread(1);
        int w = 0, h = 0, c = 0;
        unsigned char* data = stbi_load(t.file.c_str(), &w, &h, &c, 4);
        if (data) {
            t.width = w;
            t.height = h;
            t.pixels.assign(data, data + (size_t)w * (size_t)h * 4);
            stbi_image_free(data);
            t.decoded = true;
            MLOG("texture decoded: %s (%dx%d)", t.file.c_str(), w, h);
        }
        else {
            // kept anyway: finalize caches id 0 under the key, matching the
            // old failed-TextureFromFile behavior (no retry storm per mesh)
            MLOG("stbi_load failed: %s", t.file.c_str());
        }
        cpu.textures.push_back(std::move(t));
        return (int)cpu.textures.size() - 1;
    }

    ModelCPUData Model::Decode(const std::string& path, bool /*gamma*/,
                               const std::unordered_set<std::string>* skipDecodeKeys)
    {
        ModelCPUData cpu;
        cpu.sourcePath = normPath(path);
        MLOG("decode begin: %s", path.c_str());

        Assimp::Importer importer; // one importer per call: Assimp is thread-safe this way
        const ::aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            // Merge identical vertices into indexed geometry. Without this the
            // OBJ importer emits per-face vertices (disconnected soup):
            // meshopt_simplify can't collapse a single edge, so every LOD
            // level is rejected and the LOD system is inert for OBJ assets.
            // Positions are only deduplicated, never moved — AABBs unchanged.
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            // Untrusted-input hardening. The scene serializer already refuses
            // to point us at a file outside the project (see PathSandbox), but
            // a hostile mesh *inside* the tree can still carry face indices
            // that reference vertices past the end of the array. collectMeshes
            // copies those indices verbatim, and they later feed meshopt (LOD)
            // and the GL element buffer — an out-of-range index is a CPU-side
            // out-of-bounds read. ValidateDataStructure fails such a scene
            // (INCOMPLETE below rejects it) instead of letting the bad indices
            // through. NOTE: this is a post-parse pass and cannot prevent a
            // heap overflow *inside* an importer's parser — the only real
            // defence there is not opening attacker-chosen files, which the
            // containment gate provides. Well-formed assets validate clean;
            // a mere fixable issue sets VALIDATION_WARNING, not INCOMPLETE, so
            // this does not reject legitimate models.
            aiProcess_ValidateDataStructure |
            aiProcess_FlipUVs);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            std::cerr << "ERROR::MODEL::LOAD_FAILED '" << path << "': "
                      << importer.GetErrorString() << std::endl;
            return cpu; // valid=false: finalize yields an empty model
        }

        cpu.directory = path.substr(0, path.find_last_of("/\\"));
        cpu.valid = true;
        MLOG("scene meshes=%u materials=%u", scene->mNumMeshes, scene->mNumMaterials);

        cpu.materials.resize(scene->mNumMaterials);
        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            ::aiMaterial* aim = scene->mMaterials[i];
            ModelCPUData::MaterialData& md = cpu.materials[i];

            aiColor3D col;
            if (AI_SUCCESS == aim->Get(AI_MATKEY_COLOR_DIFFUSE, col)) {
                md.base.baseColor = { col.r, col.g, col.b };
            }
            if (AI_SUCCESS == aim->Get(AI_MATKEY_COLOR_EMISSIVE, col)) {
                md.base.emissive = { col.r, col.g, col.b };
            }
            float f;
            if (AI_SUCCESS == aim->Get(AI_MATKEY_METALLIC_FACTOR, f))  md.base.metallic = std::clamp(f, 0.f, 1.f);
            if (AI_SUCCESS == aim->Get(AI_MATKEY_ROUGHNESS_FACTOR, f)) md.base.roughness = std::clamp(f, 0.f, 1.f);

            // linear formats for MR/AO/normal, sRGB for albedo/emissive
            md.albedo    = decodeTextureSlot_(cpu, aim, aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, /*srgb=*/true, cpu.directory, skipDecodeKeys);
            md.normal    = decodeTextureSlot_(cpu, aim, aiTextureType_NORMALS, aiTextureType_HEIGHT,   /*srgb=*/false, cpu.directory, skipDecodeKeys);
            md.metallic  = decodeTextureSlot_(cpu, aim, aiTextureType_METALNESS, aiTextureType_UNKNOWN,  /*srgb=*/false, cpu.directory, skipDecodeKeys);
            md.roughness = decodeTextureSlot_(cpu, aim, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN, /*srgb=*/false, cpu.directory, skipDecodeKeys);
            md.ao        = decodeTextureSlot_(cpu, aim, aiTextureType_AMBIENT_OCCLUSION, aiTextureType_AMBIENT, /*srgb=*/false, cpu.directory, skipDecodeKeys);
            md.emissive  = decodeTextureSlot_(cpu, aim, aiTextureType_EMISSIVE, aiTextureType_EMISSIVE, /*srgb=*/true, cpu.directory, skipDecodeKeys);
        }

        collectMeshes(scene, scene->mRootNode, cpu);
        MLOG("decode end: meshes=%zu textures=%zu", cpu.meshes.size(), cpu.textures.size());
        return cpu;
    }

    // ---- Model: finalize stage (MAIN THREAD — GL context current) ----

    unsigned int Model::uploadTextureRGBA_(const ModelCPUData::TextureData& t)
    {
        // Guard GL readiness just in case
        if (glfwGetCurrentContext() == nullptr || !glad_glGenTextures) {
            MLOG("GL not ready when uploading texture: %s", t.file.c_str());
            return 0;
        }

        unsigned int tex = 0;
        const GLint internalFormat = t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        MLOG("texture OK: %s (%dx%d)", t.file.c_str(), t.width, t.height);
        return tex;
    }

    void Model::finalize_(ModelCPUData&& cpu)
    {
        directory_ = std::move(cpu.directory);
        if (!cpu.valid) return; // empty model, same as a failed sync load

        // Textures first. The shared cache is MAIN-THREAD-ONLY: workers ship
        // pixels, this stage does every lookup/insert. A cache hit resolves
        // skip-set entries (no pixels shipped) and discards any redundant
        // worker decode — never wrong, at worst spare CPU.
        std::vector<unsigned int> texIds(cpu.textures.size(), 0);
        for (size_t i = 0; i < cpu.textures.size(); ++i) {
            auto& t = cpu.textures[i];
            auto it = sTextureCache_.find(t.key);
            if (it != sTextureCache_.end()) {
                texIds[i] = it->second;
            }
            else {
                const unsigned int id = t.decoded ? uploadTextureRGBA_(t) : 0;
                sTextureCache_.emplace(t.key, id); // failed decodes cache 0 (no retry storm)
                texIds[i] = id;
            }
            // pixels are dead the moment the id exists: release now instead
            // of holding every texture's pixels through mesh building (a
            // backpack-class model carries >100MB of decoded pixels)
            t.pixels.clear();
            t.pixels.shrink_to_fit();
        }

        auto slot = [&](int idx) -> unsigned int {
            return (idx >= 0 && (size_t)idx < texIds.size()) ? texIds[idx] : 0;
        };
        materials_.clear();
        materials_.resize(cpu.materials.size());
        for (size_t i = 0; i < cpu.materials.size(); ++i) {
            const auto& md = cpu.materials[i];
            auto mat = std::make_shared<MyCoreEngine::Material>(md.base);
            mat->albedoTex    = slot(md.albedo);
            mat->normalTex    = slot(md.normal);
            mat->metallicTex  = slot(md.metallic);
            mat->roughnessTex = slot(md.roughness);
            mat->aoTex        = slot(md.ao);
            mat->emissiveTex  = slot(md.emissive);
            materials_[i] = std::move(mat);
        }

        meshes_.reserve(cpu.meshes.size());
        for (auto& md : cpu.meshes) {
            Mesh mesh(std::move(md.vertices), std::move(md.indices), std::move(md.lodIndices));
            if (md.materialIndex >= 0 && (size_t)md.materialIndex < materials_.size() &&
                materials_[md.materialIndex]) {
                mesh.SetMaterial(materials_[md.materialIndex]); // share the same Material object
            }
            meshes_.push_back(std::move(mesh));
        }
        MLOG("finalize end: meshes_=%zu", meshes_.size());
    }

    std::unordered_set<std::string> Model::CachedTextureKeys()
    {
        std::unordered_set<std::string> keys;
        keys.reserve(sTextureCache_.size());
        for (const auto& [k, id] : sTextureCache_) keys.insert(k);
        return keys;
    }

    ModelCPUData Model::decodeSyncWithCache_(const std::string& path, bool gamma)
    {
        // sync loads run on the main thread, where the cache is legally
        // consultable: skip decoding textures that are already uploaded so a
        // reload costs a hash lookup again, like the pre-split loader
        const auto cached = CachedTextureKeys();
        return Decode(path, gamma, &cached);
    }

    Model::Model(const std::string& path, bool gamma)
        : Model(decodeSyncWithCache_(path, gamma)) {} // same pipeline, both stages inline

    Model::Model(ModelCPUData&& cpu)
        : sourcePath_(std::move(cpu.sourcePath)) {
        finalize_(std::move(cpu));
    }

    void Model::Draw(Shader& shader) {
        for (auto& m : meshes_) m.Draw(shader);
    }

    std::string Model::makeTexKey_(const std::string& file, const std::string& directory, bool isSRGB)
    {
        // normalize once; key must be unique per (path + color space)
        std::string key = directory;
        if (!key.empty() && key.back() != '/' && key.back() != '\\') key.push_back('/');
        key += file;
        for (auto& c : key) if (c == '\\') c = '/';
        key += isSRGB ? "|srgb" : "|lin";
        return key;
    }

    std::unordered_map<std::string, unsigned int> Model::sTextureCache_;
} // namespace MyCoreEngine

