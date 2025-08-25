// Engine/src/render/passes/ForwardOpaquePass.cpp
#include "ForwardOpaquePass.h"
#include <glad/glad.h>

bool ForwardOpaquePass::execute(PassContext& ctx, Scene& scene, Camera& cam, const FrameParams& fp) {
	
	// bind HDR FBO and clear
	glViewport(0, 0, fp.viewportW, fp.viewportH);
	glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	// main shader
	shader_->use();
	shader_->setMat4("projection", fp.proj);
	shader_->setMat4("view", fp.view);
	
	// CSM block
	shader_->setInt("uShadowsOn", ctx.csm.enabled ? 1 : 0);
	shader_->setInt("uCascadeCount", ctx.csm.cascades);
	shader_->setFloat("uSplitBlend", ctx.splitBlend);
	shader_->setInt("uCSMDebug", ctx.csmDebug);
	
	for (int i = 0; i < ctx.csm.cascades; ++i) {
		char name[32];
		snprintf(name, sizeof(name), "uLightVP[%d]", i);
		shader_->setMat4(name, ctx.csm.lightVP[i]);
		snprintf(name, sizeof(name), "uCSMSplits[%d]", i);
		shader_->setFloat(name, ctx.csm.splitFar[i]);
		snprintf(name, sizeof(name), "uCascadeTexel[%d]", i);
		shader_->setFloat(name, (ctx.csm.resPer[i] > 0) ? (1.0f / float(ctx.csm.resPer[i])) : 1.0f);
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
	}	
	else {
		shader_->setFloat("uPrefilterMipCount", 0.0f);
	}
	
	// draw scene
	const Frustum camFrustum = createFrustumFromCamera(
	cam, float(fp.viewportW) / float(fp.viewportH), glm::radians(cam.Zoom), 0.1f, 1000.0f);
	scene.RenderScene(camFrustum, *shader_, cam);
	return true;
}
