#pragma once
// Bakes the three textures the split-sum IBL in frag.glsl consumes, plus the
// environment cube the skybox draws.
//
// The shader side of IBL was already written and correct; what was missing was
// anything to feed it, so `uUseIBL` was permanently 0 and every surface fell
// back to `0.03 * albedo * ao`. This class is that missing half.
//
// Requires a CURRENT GL CONTEXT — it renders. All GL state it touches
// (viewport, bound FBO, depth test, cull face) is saved and restored, because
// baking happens mid-application (a scene load, or the user picking an HDRi),
// not during a quiet startup phase.

#include "../core/Core.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace MyCoreEngine {

    class Shader;

    // GL texture ids owned by the baker; 0 when that stage did not run.
    struct IBLTextures {
        unsigned environment = 0;  // cubemap, mipped — drawn by the skybox
        unsigned irradiance = 0;   // cubemap — diffuse term
        unsigned prefiltered = 0;  // cubemap, mipped — specular term
        unsigned brdfLUT = 0;      // 2D RG16F — view/env independent
        // The MAX MIP INDEX of `prefiltered`, not the mip COUNT: frag.glsl
        // does `mip = roughness * uPrefilterMipCount`, so roughness 1 must
        // land on the last mip. Off by one here and rough metal samples a
        // sharp mip and sparkles.
        float maxMip = 0.0f;

        bool valid() const { return irradiance && prefiltered && brdfLUT; }
    };

    class ENGINE_API IBLBaker {
    public:
        struct Settings {
            int envSize = 512;        // environment cube face
            int irradianceSize = 32;  // diffuse is very low frequency
            int prefilterSize = 128;  // mip 0 of the specular chain
            int prefilterMips = 5;    // roughness steps
            int brdfSize = 512;
        };

        // A sky to bake when no HDRi is supplied. Driven by the scene's sun so
        // the environment and the direct light agree about where the sun is.
        struct SkyParams {
            glm::vec3 sunDir{ 0.f, -1.f, 0.f }; // direction light TRAVELS
            glm::vec3 zenith{ 0.10f, 0.22f, 0.45f };
            glm::vec3 horizon{ 0.62f, 0.72f, 0.86f };
            glm::vec3 ground{ 0.22f, 0.20f, 0.18f };
            float     sunIntensity = 3.0f;
        };

        IBLBaker();
        ~IBLBaker();

        IBLBaker(const IBLBaker&) = delete;
        IBLBaker& operator=(const IBLBaker&) = delete;

        // Equirectangular .hdr (radiance) file. Returns false and leaves any
        // PREVIOUS bake intact on failure, so a bad path in the Inspector does
        // not black out a scene that was lit a moment ago.
        bool BakeFromFile(const std::string& hdrPath, const Settings& s = {});

        // Analytic sky — needs no asset, so IBL works out of the box.
        bool BakeProceduralSky(const SkyParams& sky, const Settings& s = {});

        void Release();

        const IBLTextures& textures() const { return tex_; }
        const std::string& lastError() const { return error_; }
        // Where the shader files live (defaults to "Exported/Shaders").
        void SetShaderDirectory(std::string dir) { shaderDir_ = std::move(dir); }

    private:
        struct GLState; // saved/restored around a bake

        bool ensureResources_();
        // Renders `shader` into the 6 faces of `cube` at `mip`.
        void renderCubeFaces_(Shader& shader, unsigned cube, int size, int mip);
        // Shared tail: env cube -> irradiance + prefilter + BRDF LUT.
        bool bakeFromEnvironment_(const Settings& s);
        void drawUnitCube_();

        IBLTextures tex_{};
        std::string error_;
        std::string shaderDir_ = "Exported/Shaders";

        unsigned captureFBO_ = 0;
        unsigned cubeVAO_ = 0, cubeVBO_ = 0;
        unsigned quadVAO_ = 0, quadVBO_ = 0;

        std::unique_ptr<Shader> equirectShader_;
        std::unique_ptr<Shader> skyShader_;
        std::unique_ptr<Shader> irradianceShader_;
        std::unique_ptr<Shader> prefilterShader_;
        std::unique_ptr<Shader> brdfShader_;
        bool resourcesReady_ = false;
    };

} // namespace MyCoreEngine
