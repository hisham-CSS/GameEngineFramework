// Engine/src/render/passes/TonemapPass.cpp
#include "TonemapPass.h"
#include <glad/glad.h>

bool TonemapPass::execute(PassContext& ctx, Scene& scene, Camera& camera, const FrameParams& fp) {
	// With a post-AA pass active this is no longer the last step: tonemap
	// lands in the LDR intermediate and FXAA resolves that to the output.
	const unsigned target = (ctx.postAAEnabled && ctx.ldrFBO) ? ctx.ldrFBO
	                                                          : ctx.defaultFBO;
	glBindFramebuffer(GL_FRAMEBUFFER, target);
	glViewport(0, 0, fp.viewportW, fp.viewportH);
	glDisable(GL_DEPTH_TEST);
	
	ctx.tonemapShader->use();
	ctx.tonemapShader->setFloat("uExposure", ctx.exposure);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ctx.hdrColorTex);
	ctx.tonemapShader->setInt("uHDRColor", 0);
	
	glBindVertexArray(ctx.fsQuadVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	
	glEnable(GL_DEPTH_TEST);
	return true;
}
