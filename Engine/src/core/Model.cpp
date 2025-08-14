#include "Model.h"
#include "Shader.h"

#include <algorithm>

#include "stb_image.h"   // declarations only; implementation is in stb_image_impl.cpp
#include <iostream>

#include <glad/glad.h>

#include <cstdio>
#include <GLFW/glfw3.h>
#define MLOG(...) std::printf("[Model] " __VA_ARGS__), std::printf("\n")


static std::string normPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

namespace MyCoreEngine {

    // ---- Mesh ----
    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::vector<Texture>& textures)
    : vertices_(vertices), indices_(indices), textures_(textures) {
        setupMesh();
    }
    void Mesh::setupMesh() {
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
    void Mesh::IssueDrawInstanced(GLsizei instanceCount) const {
        glDrawElementsInstanced(GL_TRIANGLES,
            static_cast<GLsizei>(indices_.size()),
            GL_UNSIGNED_INT, 0,
            instanceCount);
    }
    void Mesh::BindForDraw(MyCoreEngine::Shader& shader) const {
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

        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(VAO_);
    }
    void Mesh::IssueDraw() const {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    }

    // ---- Model ----
    Model::Model(const std::string& path, bool gamma) {
        loadModel(path);
    }
    void Model::Draw(Shader& shader) {
        for (auto& m : meshes_) m.Draw(shader);
    }
    void Model::loadModel(const std::string& path) {
        MLOG("loadModel begin: %s", path.c_str());

        Assimp::Importer importer;
        const ::aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            MLOG("Assimp error: %s", importer.GetErrorString());
            return;
        }

        directory_ = path.substr(0, path.find_last_of("/\\"));
        MLOG("scene meshes=%u materials=%u", scene->mNumMeshes, scene->mNumMaterials);

        processNode(scene->mRootNode, scene);
        MLOG("loadModel end: meshes_=%zu", meshes_.size());
    }
    void Model::processNode(::aiNode* node, const ::aiScene* scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            ::aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes_.push_back(processMesh(mesh, scene));
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }
    Mesh Model::processMesh(::aiMesh* mesh, const ::aiScene* scene) {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<Texture> textures;

        vertices.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};
            v.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            v.Normal   = mesh->HasNormals()
                       ? glm::vec3{ mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z }
                       : glm::vec3{0,0,0};
            if (mesh->mTextureCoords[0]) {
                v.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
                v.Tangent   = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
                v.Bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
            }
            vertices.push_back(v);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            const ::aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        if (mesh && mesh->mMaterialIndex < scene->mNumMaterials) {
            ::aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            // Only read however many the material actually has; cache prevents duplicates
            auto diffuse = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
            auto specular = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
            // Many assets store normals as aiTextureType_NORMALS, not HEIGHT:
            auto normals = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normal");
            // If your asset uses HEIGHT for normals, keep this too (harmless if count==0):
            auto height = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");

            textures.insert(textures.end(), diffuse.begin(), diffuse.end());
            textures.insert(textures.end(), specular.begin(), specular.end());
            textures.insert(textures.end(), normals.begin(), normals.end());
            textures.insert(textures.end(), height.begin(), height.end());
        }

        return Mesh(vertices, indices, textures);
    }
    unsigned int Model::TextureFromFile(const char* path, const std::string& directory, bool isSRGB) {
        std::string filename = path;
        if (!directory.empty()) {
    #ifdef _WIN32
            const char sep = '\\';
    #else
            const char sep = '/';
    #endif
            if (path[0] != '/' && path[0] != '\\')
                filename = directory + sep + filename;
        }

        // Guard GL readiness just in case
        if (glfwGetCurrentContext() == nullptr || !glad_glGenTextures) {
            MLOG("GL not ready when loading texture: %s", filename.c_str());
            return 0;
        }

        int w = 0, h = 0, c = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* data = stbi_load(filename.c_str(), &w, &h, &c, 4);
        if (!data) {
            MLOG("stbi_load failed: %s", filename.c_str());
            return 0;
        }

        unsigned int tex = 0;
        const GLint internalFormat = isSRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);

        MLOG("texture OK: %s (%dx%d)", filename.c_str(), w, h);
        return tex;
    }
    std::vector<Texture> Model::loadMaterialTextures(::aiMaterial* mat, aiTextureType type, const std::string& typeName)
    {
        std::vector<Texture> textures;
        if (!mat) return textures;

        const unsigned int count = mat->GetTextureCount(type);
        for (unsigned int i = 0; i < count; ++i) {
            ::aiString str;
            if (mat->GetTexture(type, i, &str) != AI_SUCCESS) continue;

            // Assimp gives relative path in most cases
            std::string file = normPath(std::string(str.C_Str()));
            // Albedo-like maps should be uploaded as sRGB:
            const bool isSRGB = (typeName == "texture_diffuse" ||
                typeName == "albedo" ||
                typeName == "basecolor");
            unsigned int id = getOrLoadTexture_(file, directory_, isSRGB);

            Texture tex{};
            tex.id = id;
            tex.type = typeName;
            tex.path = file;
            textures.push_back(tex);
        }
        return textures;
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
    unsigned int Model::getOrLoadTexture_(const std::string& file, const std::string& directory, bool isSRGB)
    {
        const std::string key = makeTexKey_(file, directory, isSRGB);
        auto it = sTextureCache_.find(key);
        if (it != sTextureCache_.end())
            return it->second;

        unsigned int id = TextureFromFile(file.c_str(), directory, isSRGB);

        // Only log when we actually loaded it:
        // (your existing "[Model] texture OK" logging can stay in TextureFromFile)
        sTextureCache_.emplace(key, id);
        return id;
    }

    std::unordered_map<std::string, unsigned int> Model::sTextureCache_;
} // namespace MyCoreEngine

