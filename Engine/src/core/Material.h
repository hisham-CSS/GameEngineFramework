#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace MyCoreEngine {

    // Fixed slots for now; easy to expand later
    enum class TexSlot : uint8_t {
        Albedo = 0, Normal, Metallic, Roughness, AO, Emissive
    };

    struct Material {
        // Scalar params (used when map missing / disabled)
        glm::vec3 baseColor = glm::vec3(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;   // clamped >= ~0.045 in shader
        float ao = 1.0f;
        glm::vec3 emissive = glm::vec3(0.0f);

        // Texture ids (0 = none) — GL handles, linear unless noted
        unsigned albedoTex = 0; // sRGB internal format at upload time
        unsigned normalTex = 0; // linear
        unsigned metallicTex = 0; // linear, R or RGB
        unsigned roughnessTex = 0; // linear, R or RGB
        unsigned aoTex = 0; // linear
        unsigned emissiveTex = 0; // sRGB (usually)

        // Quick presence helpers
        bool hasAlbedo()    const { return albedoTex != 0; }
        bool hasNormal()    const { return normalTex != 0; }
        bool hasMetallic()  const { return metallicTex != 0; }
        bool hasRoughness() const { return roughnessTex != 0; }
        bool hasAO()        const { return aoTex != 0; }
        bool hasEmissive()  const { return emissiveTex != 0; }
    };

    using MaterialHandle = std::shared_ptr<Material>;

} // namespace MyCoreEngine
