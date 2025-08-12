#pragma once
#include "Core.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

// Assimp forward decls must be in the GLOBAL namespace
struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;

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
        void Draw(Shader& shader);
        const std::vector<Mesh>& Meshes() const { return meshes_; }

    private:
        std::vector<Mesh> meshes_;
        std::string       directory_;
        bool              gammaCorrection_ = false;

        void loadModel(const std::string& path);
        void processNode(::aiNode* node, const ::aiScene* scene);
        Mesh processMesh(::aiMesh* mesh, const ::aiScene* scene);
        std::vector<Texture> loadMaterialTextures(::aiMaterial* mat, int type, const std::string& typeName);

        static unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma);
    };

} // namespace MyCoreEngine
