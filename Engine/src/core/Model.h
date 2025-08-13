#pragma once


#include "Core.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <unordered_map>
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

        void Draw(Shader& shader) const;
        const std::vector<Vertex>& Vertices() const { return vertices_; }
        unsigned int IndexCount() const;
        unsigned int VAO() const;          // returns VAO_ (your VAO handle)
        // Pack up to first 4 texture ids into a 64-bit signature for bucketing
        uint64_t TextureSignature() const;

        // split draw into bind vs issue
        void BindForDraw(MyCoreEngine::Shader& shader) const; // bind textures + VAO (no draw)
        void IssueDraw() const;                               // just glDrawElements
        void IssueDrawInstanced(GLsizei instanceCount) const;

    private:
        std::vector<Vertex>       vertices_;
        std::vector<unsigned int> indices_;
        std::vector<Texture>      textures_;

        unsigned int VAO_ = 0, VBO_ = 0, EBO_ = 0;

        void setupMesh();
    };

    // ----- Model -----
    class ENGINE_API Model {
    public:
        explicit Model(const std::string& path, bool gamma = false);
        
        // ECS-friendly: forbid copies (GL objects & internal state are not copy-safe)
        Model(const Model&) = delete;
        Model & operator=(const Model&) = delete;
        // Allow moves
        Model(Model&&) noexcept = default;
        Model & operator=(Model&&) noexcept = default;
        
        void Draw(Shader& shader);
        const std::vector<Mesh>& Meshes() const { return meshes_; }

    private:
        std::vector<Mesh> meshes_;
        std::string       directory_;
        bool              gammaCorrection_ = false;
        // file path -> GL texture id
        std::unordered_map<std::string, unsigned int> textureCache_;

        std::vector<Texture> loadMaterialTextures(::aiMaterial* mat,
            aiTextureType type,
            const std::string& typeName);

        // small helper: returns cached texture id if exists, otherwise loads and caches it
        unsigned int getOrLoadTexture(const std::string& file, const std::string& directory);

        void loadModel(const std::string& path);
        void processNode(::aiNode* node, const ::aiScene* scene);
        Mesh processMesh(::aiMesh* mesh, const ::aiScene* scene);
        std::vector<Texture> loadMaterialTextures(::aiMaterial* mat, int type, const std::string& typeName);

        static unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma);
    };

} // namespace MyCoreEngine
