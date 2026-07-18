#pragma once

#include "Material.h"
#include "Core.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Assimp forward decls must be in the GLOBAL namespace
struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;

typedef int GLsizei;

namespace MyCoreEngine {

    class Shader;

    // ----- Texture -----
    struct Texture {
        unsigned int id = 0;
        std::string  type;
        std::string  path;
    };

    // ----- Vertex -----
    struct Vertex {
        glm::vec3 Position{};
        glm::vec3 Normal{};
        glm::vec2 TexCoords{};
        glm::vec3 Tangent{};
        glm::vec3 Bitangent{};
    };

    // ----- Mesh -----
    class ENGINE_API Mesh {
    public:
        Mesh() = default;
        Mesh(const std::vector<Vertex>& vertices,
            const std::vector<unsigned int>& indices,
            const std::vector<Texture>& textures);
        // Owns VAO/VBO/EBO (textures are shared via the global cache): move-only
        ~Mesh();
        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;
        Mesh& operator=(Mesh&& other) noexcept;

        void Draw(Shader& shader) const;
        const std::vector<Vertex>& Vertices() const { return vertices_; }
        unsigned int IndexCount() const;
        unsigned int VAO() const;          // returns VAO_ (your VAO handle)
        // Pack up to first 4 texture ids into a 64-bit signature for bucketing
        uint64_t TextureSignature() const;

        // --- LOD ---
        // Simplified index buffers over the SAME vertex buffer, generated at
        // load with meshoptimizer. Level 0 is the full mesh; higher levels
        // fall back to the previous one when simplification isn't useful.
        static constexpr int kLodCount = 3;
        struct LodRange {
            unsigned ebo = 0;
            GLsizei  indexCount = 0;
        };
        const LodRange& Lod(int level) const;

        // Per-level simplified index buffers; an EMPTY vector at level l
        // means "fall back to the previous level" (level 0 is always the
        // base mesh and its slot is unused).
        using LodIndexArrays = std::array<std::vector<unsigned int>, kLodCount>;
        // WORKER-SAFE (pure meshoptimizer, no GL): the CPU half of LOD
        // building. The GL half (EBO creation) happens in the constructors.
        static LodIndexArrays ComputeLodIndices(const std::vector<Vertex>& vertices,
                                                const std::vector<unsigned int>& indices);
        // MAIN THREAD (GL): build from data a worker already decoded —
        // uploads buffers and the precomputed LOD indices without re-running
        // meshoptimizer.
        Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices,
             LodIndexArrays precomputedLods);

        // split draw into bind vs issue
        void BindForDraw(MyCoreEngine::Shader& shader) const; // bind textures + VAO (no draw)
        void BindForDrawWith(MyCoreEngine::Shader& shader, const MyCoreEngine::Material& mat) const;
        void IssueDraw(int lod = 0) const;                    // just glDrawElements
        void IssueDrawInstanced(GLsizei instanceCount, int lod = 0) const;
        void SetMaterial(const MyCoreEngine::MaterialHandle& m) { material_ = m; }
        const MyCoreEngine::MaterialHandle& GetMaterial() const { return material_; }
        size_t MaterialIndex() const { return materialIndex_; }

    private:
        std::vector<Vertex>       vertices_;
        std::vector<unsigned int> indices_;
        std::vector<Texture>      textures_;
        MyCoreEngine::MaterialHandle material_; // optional
        size_t materialIndex_ = 0;
        unsigned int VAO_ = 0, VBO_ = 0, EBO_ = 0;
        LodRange lods_[kLodCount]{};

        void setupBuffers_();                       // VAO/VBO/EBO upload (GL)
        void uploadLods_(const LodIndexArrays&);    // LOD EBOs / aliasing (GL)
    };

    // ----- ModelCPUData -----
    // Everything a WORKER thread can produce for a model without touching
    // GL: parsed geometry, precomputed LOD indices, material scalars, and
    // stb-decoded texture pixels. Model::Decode fills one (job stage);
    // Model's ModelCPUData constructor turns it into GL objects (main-
    // thread completion stage). The synchronous Model(path) is the same
    // two stages back-to-back — ONE pipeline, two halves.
    struct ModelCPUData {
        struct TextureData {
            std::string key;    // Model texture-cache key (path + colorspace)
            std::string file;   // resolved filename (logging)
            int width = 0, height = 0;
            std::vector<unsigned char> pixels; // RGBA8, stb-decoded (flipped)
            bool srgb = false;
            bool decoded = false; // false: stbi failed — finalize caches id 0
        };
        struct MaterialData {
            Material base;      // scalar values; texture ids stay 0 until finalize
            // indices into `textures` (-1 = slot absent on the material)
            int albedo = -1, normal = -1, metallic = -1, roughness = -1,
                ao = -1, emissive = -1;
        };
        struct MeshData {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
            Mesh::LodIndexArrays lodIndices; // precomputed on the worker
            int materialIndex = -1;          // into `materials`
        };
        std::vector<TextureData> textures; // unique by key
        std::vector<MaterialData> materials;
        std::vector<MeshData> meshes;
        std::string sourcePath;            // normalized
        std::string directory;
        bool valid = false;                // Assimp import succeeded
    };

    // ----- Model -----
    class ENGINE_API Model {
    public:
        // Synchronous load: Decode + finalize back-to-back on the calling
        // thread (which must be the main thread — GL uploads happen here).
        explicit Model(const std::string& path, bool gamma = false);

        // WORKER-SAFE (no GL, no shared state): the CPU half of a load —
        // Assimp import, vertex/index extraction, LOD index generation,
        // material scalars, stb texture decode. Run it on a JobSystem
        // worker; hand the result to the ModelCPUData constructor on the
        // main thread.
        //
        // `skipDecodeKeys` (optional): texture-cache keys whose pixels are
        // ALREADY uploaded — those slots are recorded without touching disk
        // (finalize resolves them from the cache; entries never evict, so a
        // snapshot can't go stale). The sync ctor passes a live snapshot so
        // reloading a model whose textures are resident costs a hash lookup
        // again, like the pre-split loader. Callers submitting to WORKERS
        // must pass a snapshot taken ON THE MAIN THREAD (or nothing —
        // workers then redundantly decode and finalize discards: never
        // wrong, just spare CPU).
        static ModelCPUData Decode(const std::string& path, bool gamma = false,
                                   const std::unordered_set<std::string>* skipDecodeKeys = nullptr);

        // MAIN THREAD ONLY: snapshot of every texture-cache key currently
        // uploaded — the input for Decode's skipDecodeKeys.
        static std::unordered_set<std::string> CachedTextureKeys();

        // MAIN THREAD (GL context current): the upload half — mesh buffers,
        // precomputed LOD EBOs, texture upload through the shared cache.
        // An invalid ModelCPUData (failed import) yields an empty model,
        // exactly like a failed synchronous load.
        explicit Model(ModelCPUData&& cpu);

        // ECS-friendly: forbid copies (GL objects & internal state are not copy-safe)
        Model(const Model&) = delete;
        Model & operator=(const Model&) = delete;
        // Allow moves
        Model(Model&&) noexcept = default;
        Model & operator=(Model&&) noexcept = default;

        void Draw(Shader& shader);
        const std::vector<Mesh>& Meshes() const { return meshes_; }
        const std::vector<MyCoreEngine::MaterialHandle>& Materials() const { return materials_; }
        // Path this model was loaded from (normalized) — used by serialization
        const std::string& SourcePath() const { return sourcePath_; }

    private:
        std::vector<Mesh> meshes_;
        std::string       directory_;
        std::string       sourcePath_;

        // Global cache keyed by (normalized path + "|srgb"/"|lin").
        // MAIN-THREAD-ONLY (unsynchronized): workers deliver pixels, the
        // finalize stage does every lookup/insert.
        static std::unordered_map<std::string, unsigned int> sTextureCache_;

        std::vector<MyCoreEngine::MaterialHandle> materials_; // size = scene->mNumMaterials

        void finalize_(ModelCPUData&& cpu); // the GL half (main thread)
        static ModelCPUData decodeSyncWithCache_(const std::string& path, bool gamma);
        static std::string makeTexKey_(const std::string & file, const std::string & directory, bool isSRGB);
        // Decode helper: probe primary-then-fallback texture slot, stb-decode
        // it into cpu.textures (deduped by key). Returns the index or -1.
        static int decodeTextureSlot_(ModelCPUData& cpu, ::aiMaterial* mat,
                                      aiTextureType primary, aiTextureType fallback,
                                      bool isSRGB, const std::string& directory,
                                      const std::unordered_set<std::string>* skipDecodeKeys);
        static unsigned int uploadTextureRGBA_(const ModelCPUData::TextureData& t);
    };
} // namespace MyCoreEngine
