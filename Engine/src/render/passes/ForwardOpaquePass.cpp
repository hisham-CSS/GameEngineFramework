// Engine/src/render/passes/ForwardOpaquePass.cpp
#include "ForwardOpaquePass.h"
#include "ForwardShading.h"
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

	// Per-frame shading state. Uniforms are PER PROGRAM: the skinned colour
	// program (M3.2e) is a second program, and until M3.2f nothing uploaded
	// projection/view to it, so a posed item drew through identity matrices --
	// which lands the mesh in clip space with its winding reversed, and the
	// back-face cull removed every triangle. Nothing on screen, nothing
	// logged; SkinnedDraw.TwoFightersSharingOneMeshNeverShareOnePose caught it.
	// Both programs now take the same state through the one helper
	// TransparentPass already uses, so they cannot drift again. Texture-unit
	// binds inside it are global GL state and harmlessly repeat.
	if (skinnedShader_ && skinnedShader_->isValid()) {
		skinnedShader_->use();
		ApplyForwardShadingState(*skinnedShader_, ctx, fp);
	}
	shader_->use();
	ApplyForwardShadingState(*shader_, ctx, fp);

	// Tell the scene whether the IBL maps are there. Scene::RenderScene sets
	// uUseIBL from its own iblEnabled_ flag, which defaults to true and runs
	// AFTER this -- so without it the shader took the IBL branch and sampled
	// unbound cubemaps. Those read as black, making ambient exactly ZERO
	// rather than the intended 0.03 fallback, which is why unlit surfaces
	// were pure black instead of merely dim.
	scene.SetIBLAvailable(ctx.ibl.irradiance && ctx.ibl.prefiltered && ctx.ibl.brdfLUT);
	
	// draw scene — culling frustum must use the same clip planes as the
	// projection in fp.proj (both read the camera's NearClip/FarClip)
	const Frustum camFrustum = createFrustumFromCamera(
	cam, float(fp.viewportW) / float(fp.viewportH), glm::radians(cam.Zoom),
	cam.NearClip, cam.FarClip);
	// viewport pixel height drives the projected-size cull (0 would disable it)
	scene.RenderScene(camFrustum, *shader_, cam, fp.viewportH);
	return true;
}
