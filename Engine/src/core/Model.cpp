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

        materials_.clear();
        materials_.resize(scene->mNumMaterials);

        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            aiMaterial* aim = scene->mMaterials[i];
            auto mat = std::make_shared<MyCoreEngine::Material>();

            // Optional: read base color / emissive from aiMaterial if present
            aiColor3D col;
            if (AI_SUCCESS == aim->Get(AI_MATKEY_COLOR_DIFFUSE, col)) {
                mat->baseColor = { col.r, col.g, col.b };
            }
            if (AI_SUCCESS == aim->Get(AI_MATKEY_COLOR_EMISSIVE, col)) {
                mat->emissive = { col.r, col.g, col.b };
            }
            float f;
            if (AI_SUCCESS == aim->Get(AI_MATKEY_METALLIC_FACTOR, f))  mat->metallic = std::clamp(f, 0.f, 1.f);
            if (AI_SUCCESS == aim->Get(AI_MATKEY_ROUGHNESS_FACTOR, f)) mat->roughness = std::clamp(f, 0.f, 1.f);
            if (AI_SUCCESS == aim->Get(AI_MATKEY_REFLECTIVITY, f)) {/*optional*/ }

            // Load textures using your cache (linear formats for MR/A/normal, sRGB for albedo/emissive)
            mat->albedoTex = getOrLoadTexture_(aim, aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, /*srgb=*/true);
            mat->normalTex = getOrLoadTexture_(aim, aiTextureType_NORMALS, aiTextureType_HEIGHT,   /*srgb=*/false);
            mat->metallicTex = getOrLoadTexture_(aim, aiTextureType_METALNESS, aiTextureType_UNKNOWN,  /*srgb=*/false);
            mat->roughnessTex = getOrLoadTexture_(aim, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN, /*srgb=*/false);
            mat->aoTex = getOrLoadTexture_(aim, aiTextureType_AMBIENT_OCCLUSION, aiTextureType_AMBIENT, /*srgb=*/false);
            mat->emissiveTex = getOrLoadTexture_(aim, aiTextureType_EMISSIVE, aiTextureType_EMISSIVE, /*srgb=*/true);

            materials_[i] = std::move(mat);
        }

        

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
        Mesh newMesh;

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
        newMesh = Mesh(vertices, indices, textures);
        unsigned idx = mesh->mMaterialIndex;
        if (idx < materials_.size() && materials_[idx]) {
            newMesh.SetMaterial(materials_[idx]);        // << share the same Material object
        }

        //if (mesh && mesh->mMaterialIndex < scene->mNumMaterials) {
        //    ::aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        //    // Only read however many the material actually has; cache prevents duplicates
        //    auto diffuse = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        //    auto specular = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        //    // Many assets store normals as aiTextureType_NORMALS, not HEIGHT:
        //    auto normals = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normal");
        //    // If your asset uses HEIGHT for normals, keep this too (harmless if count==0):
        //    auto height = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");

        //    textures.insert(textures.end(), diffuse.begin(), diffuse.end());
        //    textures.insert(textures.end(), specular.begin(), specular.end());
        //    textures.insert(textures.end(), normals.begin(), normals.end());
        //    textures.insert(textures.end(), height.begin(), height.end());

        //    MaterialHandle mat = std::make_shared<Material>();
        //    // Set textures using your cache outputs:
        //    mat->albedoTex = findTexId(textures, { "texture_diffuse","albedo","basecolor" });
        //    mat->normalTex = findTexId(textures, { "texture_normal","normal","normalmap" });
        //    mat->metallicTex = findTexId(textures, { "texture_metallic","metallic","metalness" });
        //    mat->roughnessTex = findTexId(textures, { "texture_roughness","roughness" });
        //    mat->aoTex = findTexId(textures, { "texture_ambient","ao","ambient_occlusion" });
        //    // Optional scalars if present via Assimp material properties (safe defaults otherwise)
        //    // mat->metallic = ...; mat->roughness = ...; mat->ao = ...;
        //    newMesh = Mesh(vertices, indices, textures);
        //    newMesh.SetMaterial(mat);
        //}

        return newMesh;
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
    unsigned int Model::getOrLoadTexture_(aiMaterial* mat, aiTextureType primary, aiTextureType fallback, bool isSRGB)
    {
        if (!mat) return 0;

        aiString str;
        // Try primary slot first
        if (mat->GetTexture(primary, 0, &str) == AI_SUCCESS && str.length > 0) {
            const std::string file = normPath(std::string(str.C_Str()));
            return getOrLoadTexture_(file, directory_, isSRGB);
        }
        // Fallback slot if primary is empty
        if (fallback != aiTextureType_UNKNOWN &&
            mat->GetTexture(fallback, 0, &str) == AI_SUCCESS && str.length > 0)
        {
            const std::string file = normPath(std::string(str.C_Str()));
            return getOrLoadTexture_(file, directory_, isSRGB);
        }

        return 0; // no texture declared on this material for these types
    }
    unsigned Model::findTexId(const std::vector<Texture>& texList,
        std::initializer_list<const char*> names)
    {
        for (auto& t : texList) {
            for (auto* n : names) {
                if (t.type == n) return t.id;
            }
        }
        return 0;
    }
    std::unordered_map<std::string, unsigned int> Model::sTextureCache_;
} // namespace MyCoreEngine

