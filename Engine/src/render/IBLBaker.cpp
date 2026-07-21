#include "IBLBaker.h"

#include "../core/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <cstdio>
#include <filesystem>

namespace MyCoreEngine {

    namespace {

        // The six canonical cube-face views. Y is flipped relative to the
        // "obvious" up vectors because GL cubemap faces are specified in a
        // left-handed, top-left-origin convention inherited from RenderMan --
        // getting this wrong renders a sky that is upside down on four faces
        // and looks like a bug in the projection rather than in a constant.
        const glm::mat4 kCaptureProj =
            glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

        const glm::mat4 kCaptureViews[6] = {
            glm::lookAt(glm::vec3(0.f), glm::vec3( 1.f,  0.f,  0.f), glm::vec3(0.f, -1.f,  0.f)),
            glm::lookAt(glm::vec3(0.f), glm::vec3(-1.f,  0.f,  0.f), glm::vec3(0.f, -1.f,  0.f)),
            glm::lookAt(glm::vec3(0.f), glm::vec3( 0.f,  1.f,  0.f), glm::vec3(0.f,  0.f,  1.f)),
            glm::lookAt(glm::vec3(0.f), glm::vec3( 0.f, -1.f,  0.f), glm::vec3(0.f,  0.f, -1.f)),
            glm::lookAt(glm::vec3(0.f), glm::vec3( 0.f,  0.f,  1.f), glm::vec3(0.f, -1.f,  0.f)),
            glm::lookAt(glm::vec3(0.f), glm::vec3( 0.f,  0.f, -1.f), glm::vec3(0.f, -1.f,  0.f)),
        };

        unsigned makeCube(int size, bool mipped, GLenum internalFormat = GL_RGB16F) {
            unsigned id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_CUBE_MAP, id);
            for (int f = 0; f < 6; ++f) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, internalFormat,
                             size, size, 0, GL_RGB, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                            mipped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            if (mipped) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            return id;
        }

    } // namespace

    // Everything the bake disturbs. Baking runs mid-frame-loop (scene load,
    // or the user picking an HDRi), so leaving GL altered would corrupt the
    // very next pass in ways that look nothing like an IBL bug.
    struct IBLBaker::GLState {
        GLint viewport[4]{};
        GLint drawFBO = 0, readFBO = 0;
        GLboolean depthTest = GL_FALSE, cullFace = GL_FALSE, blend = GL_FALSE;
        GLint activeTexture = GL_TEXTURE0;

        GLState() {
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFBO);
            glGetBooleanv(GL_DEPTH_TEST, &depthTest);
            glGetBooleanv(GL_CULL_FACE, &cullFace);
            glGetBooleanv(GL_BLEND, &blend);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        }
        ~GLState() {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFBO);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFBO);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            if (cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            glActiveTexture((GLenum)activeTexture);
        }
    };

    IBLBaker::IBLBaker() = default;

    IBLBaker::~IBLBaker() { Release(); }

    void IBLBaker::Release() {
        auto killTex = [](unsigned& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
        killTex(tex_.environment);
        killTex(tex_.irradiance);
        killTex(tex_.prefiltered);
        killTex(tex_.brdfLUT);
        tex_.maxMip = 0.f;

        if (captureFBO_) { glDeleteFramebuffers(1, &captureFBO_); captureFBO_ = 0; }
        if (cubeVBO_) { glDeleteBuffers(1, &cubeVBO_); cubeVBO_ = 0; }
        if (cubeVAO_) { glDeleteVertexArrays(1, &cubeVAO_); cubeVAO_ = 0; }
        if (quadVBO_) { glDeleteBuffers(1, &quadVBO_); quadVBO_ = 0; }
        if (quadVAO_) { glDeleteVertexArrays(1, &quadVAO_); quadVAO_ = 0; }

        equirectShader_.reset();
        skyShader_.reset();
        irradianceShader_.reset();
        prefilterShader_.reset();
        brdfShader_.reset();
        resourcesReady_ = false;
    }

    bool IBLBaker::ensureResources_() {
        if (resourcesReady_) return true;

        const std::string d = shaderDir_ + "/";
        auto load = [&](const char* vs, const char* fs) {
            return std::make_unique<Shader>((d + vs).c_str(), (d + fs).c_str());
        };
        equirectShader_   = load("cube_capture_vert.glsl", "equirect_to_cube_frag.glsl");
        skyShader_        = load("cube_capture_vert.glsl", "procedural_sky_frag.glsl");
        irradianceShader_ = load("cube_capture_vert.glsl", "irradiance_conv_frag.glsl");
        prefilterShader_  = load("cube_capture_vert.glsl", "prefilter_frag.glsl");
        brdfShader_       = load("tonemap_vert.glsl", "brdf_lut_frag.glsl");

        if (!equirectShader_->isValid() || !skyShader_->isValid() ||
            !irradianceShader_->isValid() || !prefilterShader_->isValid() ||
            !brdfShader_->isValid()) {
            error_ = "IBL shaders failed to compile (looked in '" + shaderDir_ + "')";
            return false;
        }

        glGenFramebuffers(1, &captureFBO_);

        // Unit cube, positions only. Wound so the INSIDE faces the camera:
        // we render the environment from within.
        const float verts[] = {
            -1,-1,-1,  -1, 1, 1,  -1, 1,-1,  -1,-1,-1,  -1,-1, 1,  -1, 1, 1,
             1,-1,-1,   1, 1,-1,   1, 1, 1,   1,-1,-1,   1, 1, 1,   1,-1, 1,
            -1,-1,-1,   1,-1,-1,   1,-1, 1,  -1,-1,-1,   1,-1, 1,  -1,-1, 1,
            -1, 1,-1,  -1, 1, 1,   1, 1, 1,  -1, 1,-1,   1, 1, 1,   1, 1,-1,
            -1,-1,-1,  -1, 1,-1,   1, 1,-1,  -1,-1,-1,   1, 1,-1,   1,-1,-1,
            -1,-1, 1,   1,-1, 1,   1, 1, 1,  -1,-1, 1,   1, 1, 1,  -1, 1, 1,
        };
        glGenVertexArrays(1, &cubeVAO_);
        glGenBuffers(1, &cubeVBO_);
        glBindVertexArray(cubeVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        // Fullscreen quad for the BRDF LUT, matching tonemap_vert.glsl's
        // (vec2 pos, vec2 uv) layout. Owned here rather than borrowed from
        // PassContext so a bake can run before the renderer has set up.
        const float quad[] = {
            -1.f, -1.f, 0.f, 0.f,   1.f, -1.f, 1.f, 0.f,   1.f, 1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 0.f,   1.f,  1.f, 1.f, 1.f,  -1.f, 1.f, 0.f, 1.f,
        };
        glGenVertexArrays(1, &quadVAO_);
        glGenBuffers(1, &quadVBO_);
        glBindVertexArray(quadVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));
        glBindVertexArray(0);

        resourcesReady_ = true;
        return true;
    }

    void IBLBaker::drawUnitCube_() {
        glBindVertexArray(cubeVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    void IBLBaker::renderCubeFaces_(Shader& shader, unsigned cube, int size, int mip) {
        glViewport(0, 0, size, size);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
        shader.setMat4("uProjection", kCaptureProj);
        for (int f = 0; f < 6; ++f) {
            shader.setMat4("uView", kCaptureViews[f]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cube, mip);
            glClear(GL_COLOR_BUFFER_BIT);
            drawUnitCube_();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool IBLBaker::BakeProceduralSky(const SkyParams& sky, const Settings& s) {
        GLState saved;
        if (!ensureResources_()) return false;

        // Seamless filtering across cube edges. Without it the prefiltered
        // mips show hard seams along every face boundary on rough surfaces.
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        const unsigned env = makeCube(s.envSize, /*mipped=*/true);

        skyShader_->use();
        skyShader_->setVec3("uSunDir", sky.sunDir);
        skyShader_->setVec3("uZenithColor", sky.zenith);
        skyShader_->setVec3("uHorizonColor", sky.horizon);
        skyShader_->setVec3("uGroundColor", sky.ground);
        skyShader_->setFloat("uSunIntensity", sky.sunIntensity);
        renderCubeFaces_(*skyShader_, env, s.envSize, 0);

        glBindTexture(GL_TEXTURE_CUBE_MAP, env);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // prefilter samples these mips

        // Swap in only after the new environment exists, so a failure below
        // still leaves the previous bake usable.
        if (tex_.environment) glDeleteTextures(1, &tex_.environment);
        tex_.environment = env;
        error_.clear();
        return bakeFromEnvironment_(s);
    }

    bool IBLBaker::BakeFromFile(const std::string& hdrPath, const Settings& s) {
        GLState saved;

        // hdrPath comes from an untrusted scene's EnvironmentSettings. Reject
        // anything that reaches outside the project: absolute paths, drive/UNC
        // roots, and ".." components. stbi only parses image data (no code
        // execution), so this is defence-in-depth against reading arbitrary
        // files by path rather than a critical hole -- but it costs nothing
        // and keeps a hostile scene from probing the filesystem.
        {
            namespace fs = std::filesystem;
            const fs::path p(hdrPath);
            bool bad = p.is_absolute() || p.has_root_name() || p.has_root_directory();
            for (const auto& part : p) if (part == "..") bad = true;
            if (bad) {
                error_ = "rejected HDRi path outside the project: '" + hdrPath + "'";
                return false; // previous bake left intact
            }
        }

        if (!ensureResources_()) return false;

        // Radiance files are float and bottom-up; stbi's flip flag is what
        // makes the horizon land the right way up.
        stbi_set_flip_vertically_on_load(1);
        int w = 0, h = 0, comp = 0;
        float* data = stbi_loadf(hdrPath.c_str(), &w, &h, &comp, 3);
        stbi_set_flip_vertically_on_load(0);
        if (!data) {
            error_ = "could not read HDR '" + hdrPath + "'";
            return false; // previous bake left intact
        }

        unsigned equirect = 0;
        glGenTextures(1, &equirect);
        glBindTexture(GL_TEXTURE_2D, equirect);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);

        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        const unsigned env = makeCube(s.envSize, /*mipped=*/true);

        equirectShader_->use();
        equirectShader_->setInt("uEquirect", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, equirect);
        renderCubeFaces_(*equirectShader_, env, s.envSize, 0);

        glDeleteTextures(1, &equirect); // only needed for the projection

        glBindTexture(GL_TEXTURE_CUBE_MAP, env);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        if (tex_.environment) glDeleteTextures(1, &tex_.environment);
        tex_.environment = env;
        error_.clear();
        return bakeFromEnvironment_(s);
    }

    bool IBLBaker::bakeFromEnvironment_(const Settings& s) {
        // ---- diffuse irradiance ----
        const unsigned irr = makeCube(s.irradianceSize, /*mipped=*/false);
        irradianceShader_->use();
        irradianceShader_->setInt("uEnvironment", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex_.environment);
        renderCubeFaces_(*irradianceShader_, irr, s.irradianceSize, 0);

        // ---- specular prefilter: one mip per roughness step ----
        const unsigned pre = makeCube(s.prefilterSize, /*mipped=*/true);
        prefilterShader_->use();
        prefilterShader_->setInt("uEnvironment", 0);
        prefilterShader_->setFloat("uEnvResolution", static_cast<float>(s.envSize));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex_.environment);

        const int mips = (s.prefilterMips < 2) ? 2 : s.prefilterMips;
        for (int mip = 0; mip < mips; ++mip) {
            const int mipSize = s.prefilterSize >> mip;
            if (mipSize < 1) break;
            const float roughness = static_cast<float>(mip) / static_cast<float>(mips - 1);
            prefilterShader_->setFloat("uRoughness", roughness);
            renderCubeFaces_(*prefilterShader_, pre, mipSize, mip);
        }

        // ---- environment BRDF LUT ----
        unsigned lut = 0;
        glGenTextures(1, &lut);
        glBindTexture(GL_TEXTURE_2D, lut);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, s.brdfSize, s.brdfSize, 0,
                     GL_RG, GL_FLOAT, nullptr);
        // CLAMP_TO_EDGE is load-bearing: the LUT is indexed by NdotV and
        // roughness, and wrapping at the edges maps grazing angles onto
        // head-on ones, which shows up as a bright rim on every object.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lut, 0);
        glViewport(0, 0, s.brdfSize, s.brdfSize);
        glClear(GL_COLOR_BUFFER_BIT);
        brdfShader_->use();
        glBindVertexArray(quadVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (tex_.irradiance) glDeleteTextures(1, &tex_.irradiance);
        if (tex_.prefiltered) glDeleteTextures(1, &tex_.prefiltered);
        if (tex_.brdfLUT) glDeleteTextures(1, &tex_.brdfLUT);
        tex_.irradiance = irr;
        tex_.prefiltered = pre;
        tex_.brdfLUT = lut;
        // MAX MIP INDEX, not the count — see IBLTextures::maxMip.
        tex_.maxMip = static_cast<float>(mips - 1);

        const GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::printf("[ibl] bake finished with GL error 0x%04X\n", err);
        }
        return tex_.valid();
    }

} // namespace MyCoreEngine
