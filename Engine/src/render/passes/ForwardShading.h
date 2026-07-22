#pragma once
// Binds the forward shader's per-frame SHADING state — camera matrices, the
// CSM cascades and their uniforms, and the IBL textures — from a PassContext.
//
// Exists so the transparent pass lights and shadows its geometry with EXACTLY
// the same inputs as the opaque forward pass. If the two drifted, glass would
// be lit differently from the wall behind it and read as obviously wrong. It
// deliberately does NOT touch scene material/light-list uniforms (Scene
// uploads those) nor Scene::SetIBLAvailable (that is the opaque pass's call,
// made once per frame before this runs).

#include "../IRenderPass.h"

#include <glad/glad.h>
#include <cstdio>

inline void ApplyForwardShadingState(Shader& shader, const PassContext& ctx,
                                     const FrameParams& fp) {
    shader.setMat4("projection", fp.proj);
    shader.setMat4("view", fp.view);

    shader.setInt("uShadowsOn", ctx.csm.enabled ? 1 : 0);
    shader.setInt("uCascadeCount", ctx.csm.cascades);
    shader.setFloat("uSplitBlend", ctx.splitBlend);
    shader.setInt("uCSMDebug", ctx.csmDebug);
    shader.setFloat("uShadowBiasConst", ctx.shadowBiasConst);
    shader.setFloat("uShadowBiasSlope", ctx.shadowBiasSlope);

    for (int i = 0; i < ctx.csm.cascades; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "uLightVP[%d]", i);
        shader.setMat4(name, ctx.csm.lightVP[i]);
        std::snprintf(name, sizeof(name), "uCSMSplits[%d]", i);
        shader.setFloat(name, ctx.csm.splitFar[i]);
        std::snprintf(name, sizeof(name), "uCascadeTexel[%d]", i);
        shader.setFloat(name, (ctx.csm.resPer[i] > 0) ? (1.0f / float(ctx.csm.resPer[i])) : 1.0f);
        std::snprintf(name, sizeof(name), "uCascadeKernel[%d]", i);
        shader.setInt(name, ctx.cascadeKernel[i]);
    }
    constexpr int kBaseUnit = 8; // uShadowCascade[] start at texture unit 8
    for (int i = 0; i < ctx.csm.cascades; ++i) {
        const int unit = kBaseUnit + i;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, ctx.csm.depthTex[i]);
        char name[32];
        std::snprintf(name, sizeof(name), "uShadowCascade[%d]", i);
        shader.setInt(name, unit);
    }

    if (ctx.ibl.irradiance && ctx.ibl.prefiltered && ctx.ibl.brdfLUT) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.ibl.irradiance);
        shader.setInt("irradianceMap", 5);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.ibl.prefiltered);
        shader.setInt("prefilteredMap", 6);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, ctx.ibl.brdfLUT);
        shader.setInt("brdfLUT", 7);
        shader.setFloat("uPrefilterMipCount", ctx.ibl.mipCount);
    }
    else {
        shader.setFloat("uPrefilterMipCount", 0.0f);
    }
}
