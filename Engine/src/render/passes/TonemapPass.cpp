// Engine/src/render/passes/TonemapPass.cpp
#include "TonemapPass.h"
#include <glad/glad.h>

bool TonemapPass::execute(PassContext& ctx, Scene& scene, Camera& camera, const FrameParams& fp) {
	// With any LDR post pass active this is no longer the last step: tonemap
	// lands in ping-pong buffer A and the chain (vignette/outline/FXAA/...)
	// resolves to the output. With none, it writes straight to defaultFBO.
	glBindFramebuffer(GL_FRAMEBUFFER, ctx.tonemapTarget());
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
