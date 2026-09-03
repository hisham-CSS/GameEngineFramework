// Engine/src/render/passes/ForwardOpaquePass.cpp
#include "ForwardOpaquePass.h"
#include "../SkinPalette.h"
#include <glad/glad.h>

void ForwardOpaquePass::setup(PassContext&) {
	if (!prepassShader_) {
		prepassShader_ = std::make_unique<Shader>(
			"Exported/Shaders/vertex.glsl",
			"Exported/Shaders/prepass_frag.glsl");
	}
	// The skinned variants (M3.2e): same files, one define. Their `uBones`
	// block is routed to the palette's binding point once, here.
	if (!skinnedShader_) {
		skinnedShader_ = std::make_unique<Shader>(
			"Exported/Shaders/vertex.glsl",
			"Exported/Shaders/frag.glsl",
			"#define SKINNED 1");
		if (skinnedShader_->isValid())
			skinnedShader_->bindUniformBlock(MyCoreEngine::SkinPaletteUBO::kBlockName,
			                                 MyCoreEngine::SkinPaletteUBO::kBinding);
	}
	if (!prepassSkinnedShader_) {
		prepassSkinnedShader_ = std::make_unique<Shader>(
			"Exported/Shaders/vertex.glsl",
			"Exported/Shaders/prepass_frag.glsl",
			"#define SKINNED 1");
		if (prepassSkinnedShader_->isValid())
			prepassSkinnedShader_->bindUniformBlock(MyCoreEngine::SkinPaletteUBO::kBlockName,
			                                        MyCoreEngine::SkinPaletteUBO::kBinding);
	}
}

bool ForwardOpaquePass::execute(PassContext& ctx, Scene& scene, Camera& cam, const FrameParams& fp) {

	// bind HDR FBO and clear
	glViewport(0, 0, fp.viewportW, fp.viewportH);
	glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// depth-prepass program shares the color pass's camera uniforms
	if (prepassShader_ && prepassShader_->isValid()) {
		prepassShader_->use();
		prepassShader_->setMat4("projection", fp.proj);
		prepassShader_->setMat4("view", fp.view);
		scene.SetDepthPrepassShader(prepassShader_.get());
	}
	else {
		scene.SetDepthPrepassShader(nullptr);
	}
	// The skinned pair, for the items the Scene routes to them (M3.2f). A
	// variant that failed to compile is handed over as null, and a skinned
	// item then draws through the static program in its rest pose -- visible
	// and logged, never a crash.
	scene.SetSkinnedShaders(
		(skinnedShader_ && skinnedShader_->isValid()) ? skinnedShader_.get() : nullptr,
		(prepassSkinnedShader_ && prepassSkinnedShader_->isValid()) ? prepassSkinnedShader_.get() : nullptr);

	// main shader
	shader_->use();
	shader_->setMat4("projection", fp.proj);
	shader_->setMat4("view", fp.view);
	
	// CSM block
	shader_->setInt("uShadowsOn", ctx.csm.enabled ? 1 : 0);
	shader_->setInt("uCascadeCount", ctx.csm.cascades);
	shader_->setFloat("uSplitBlend", ctx.splitBlend);
	shader_->setInt("uCSMDebug", ctx.csmDebug);
	shader_->setFloat("uShadowBiasConst", ctx.shadowBiasConst);
	shader_->setFloat("uShadowBiasSlope", ctx.shadowBiasSlope);

	for (int i = 0; i < ctx.csm.cascades; ++i) {
		char name[32];
		snprintf(name, sizeof(name), "uLightVP[%d]", i);
		shader_->setMat4(name, ctx.csm.lightVP[i]);
		snprintf(name, sizeof(name), "uCSMSplits[%d]", i);
		shader_->setFloat(name, ctx.csm.splitFar[i]);
		snprintf(name, sizeof(name), "uCascadeTexel[%d]", i);
		shader_->setFloat(name, (ctx.csm.resPer[i] > 0) ? (1.0f / float(ctx.csm.resPer[i])) : 1.0f);
		snprintf(name, sizeof(name), "uCascadeKernel[%d]", i);
		shader_->setInt(name, ctx.cascadeKernel[i]);
	}
	for (int i = 0; i < ctx.csm.cascades; ++i) {
		const int unit = kBaseUnit + i;
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, ctx.csm.depthTex[i]);
		char name[32];
		snprintf(name, sizeof(name), "uShadowCascade[%d]", i);
		shader_->setInt(name, unit);
	}
	// IBL (optional)
	if (ctx.ibl.irradiance && ctx.ibl.prefiltered && ctx.ibl.brdfLUT) {
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.ibl.irradiance);
		shader_->setInt("irradianceMap", 5);
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.ibl.prefiltered);
		shader_->setInt("prefilteredMap", 6);
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, ctx.ibl.brdfLUT);
		shader_->setInt("brdfLUT", 7);
		shader_->setFloat("uPrefilterMipCount", ctx.ibl.mipCount);
		scene.SetIBLAvailable(true);
	}
	else {
		shader_->setFloat("uPrefilterMipCount", 0.0f);
		// Tell the scene the maps are NOT there. Scene::RenderScene sets
		// uUseIBL from its own iblEnabled_ flag, which defaults to true and
		// runs AFTER this block -- so before this, the shader took the IBL
		// branch and sampled unbound cubemaps. Those read as black, making
		// ambient exactly ZERO rather than the intended 0.03 fallback, which
		// is why unlit surfaces were pure black instead of merely dim.
		scene.SetIBLAvailable(false);
	}
	
	// draw scene — culling frustum must use the same clip planes as the
	// projection in fp.proj (both read the camera's NearClip/FarClip)
	const Frustum camFrustum = createFrustumFromCamera(
	cam, float(fp.viewportW) / float(fp.viewportH), glm::radians(cam.Zoom),
	cam.NearClip, cam.FarClip);
	// viewport pixel height drives the projected-size cull (0 would disable it)
	scene.RenderScene(camFrustum, *shader_, cam, fp.viewportH);
	return true;
}
