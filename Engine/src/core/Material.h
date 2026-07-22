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

    // How a material's alpha is treated. Deliberately the glTF model, because
    // it is the one most authoring tools already speak:
    //  - Opaque: alpha ignored entirely (the fast path; writes depth, batches).
    //  - Mask:   binary coverage — fragments below alphaCutoff are discarded
    //            (foliage, chain-link). Still writes depth; no blending.
    //  - Blend:  true translucency — sorted back-to-front and alpha-composited
    //            (glass, water). Does NOT write depth.
    enum class AlphaMode : uint8_t { Opaque = 0, Mask = 1, Blend = 2 };

    struct Material {
        // Scalar params (used when map missing / disabled)
        glm::vec3 baseColor = glm::vec3(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;   // clamped >= ~0.045 in

        float ao = 1.0f;
        glm::vec3 emissive = glm::vec3(0.0f);

        // Transparency.
        AlphaMode alphaMode = AlphaMode::Opaque;
        float opacity = 1.0f;       // Blend: multiplies the albedo alpha
        float alphaCutoff = 0.5f;   // Mask: discard below this
        // Draw back faces too. Glass and foliage both usually want this, since
        // you see the inside surface through the outside. Off by default so the
        // opaque fast path keeps back-face culling.
        bool doubleSided = false;

        // Texture ids (0 = none) � GL handles, linear unless noted
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

        bool isBlended() const { return alphaMode == AlphaMode::Blend; }
        bool isMasked()  const { return alphaMode == AlphaMode::Mask; }
    };

    using MaterialHandle = std::shared_ptr<Material>;

} // namespace MyCoreEngine
