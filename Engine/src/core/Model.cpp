#include "Model.h"
#include "Shader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "stb_image.h"   // declarations only; implementation is in stb_image_impl.cpp
#include <iostream>

namespace MyCoreEngine {

// ---- Mesh ----
Mesh::Mesh(const std::vector<Vertex>& vertices,
           const std::vector<unsigned int>& indices,
           const std::vector<Texture>& textures)
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

// ---- Model ----
Model::Model(const std::string& path, bool gamma)
: gammaCorrection_(gamma) {
    loadModel(path);
}

void Model::Draw(Shader& shader) {
    for (auto& m : meshes_) m.Draw(shader);
}

void Model::loadModel(const std::string& path) {
    Assimp::Importer importer;
    const ::aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cerr << "ASSIMP error: " << importer.GetErrorString() << "\n";
        return;
    }

    // compute directory
    directory_ = path.substr(0, path.find_last_of("/\\"));

    processNode(scene->mRootNode, scene);
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

    if (mesh->mMaterialIndex >= 0) {
        ::aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        auto diffuse  = loadMaterialTextures(material, aiTextureType_DIFFUSE,  "texture_diffuse");
        auto specular = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        auto normal   = loadMaterialTextures(material, aiTextureType_HEIGHT,   "texture_normal");
        auto height   = loadMaterialTextures(material, aiTextureType_AMBIENT,  "texture_height");
        textures.insert(textures.end(), diffuse.begin(),  diffuse.end());
        textures.insert(textures.end(), specular.begin(), specular.end());
        textures.insert(textures.end(), normal.begin(),   normal.end());
        textures.insert(textures.end(), height.begin(),   height.end());
    }

    return Mesh(vertices, indices, textures);
}

std::vector<Texture> Model::loadMaterialTextures(::aiMaterial* mat, int type, const std::string& typeName) {
    std::vector<Texture> textures;
    for (unsigned int i = 0; i < mat->GetTextureCount((::aiTextureType)type); i++) {
        ::aiString str;
        mat->GetTexture((::aiTextureType)type, i, &str);
        Texture tex{};
        tex.id   = TextureFromFile(str.C_Str(), directory_, false);
        tex.type = typeName;
        tex.path = str.C_Str();
        textures.push_back(tex);
    }
    return textures;
}

unsigned int Model::TextureFromFile(const char* path, const std::string& directory, bool /*gamma*/) {
    std::string filename = path;
    if (!directory.empty()) {
        char sep =
#ifdef _WIN32
            '\\';
#else
            '/';
#endif
        if (path[0] != '/' && path[0] != '\\')
            filename = directory + sep + filename;
    }

    unsigned int id;
    glGenTextures(1, &id);

    int w, h, comp;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(filename.c_str(), &w, &h, &comp, 0);
    if (data) {
        GLenum fmt = (comp == 1) ? GL_RED : (comp == 3) ? GL_RGB : GL_RGBA;
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
    } else {
        std::cerr << "Texture failed to load at: " << filename << "\n";
        stbi_image_free(data);
    }
    return id;
}

} // namespace MyCoreEngine

