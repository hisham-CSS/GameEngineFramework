// Engine/src/render/passes/ForwardOpaquePass.h
#pragma once
#include "../IRenderPass.h"

#include <memory>

class ENGINE_API ForwardOpaquePass final : public IRenderPass {
public:
    // Renderer (or Editor) gives the compiled forward shader you already pass to run()
    explicit ForwardOpaquePass(Shader& shader) : shader_(&shader) {}
    const char* name() const override { return "ForwardOpaque"; }

    void setup(PassContext&) override;
    void resize(PassContext&, int, int) override {};
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    Shader* shader_; // not owned
    // Depth-prepass program: the SAME vertex shader as the color pass with a
    // no-op fragment stage (bit-identical gl_Position -> GL_EQUAL is exact).
    std::unique_ptr<Shader> prepassShader_;
    // The SKINNED variants of both (ROADMAP M3.2e): the same two sources with
    // "#define SKINNED 1" injected, so a posed fighter is shaded and
    // prepassed by programs that differ from the static ones in nothing but
    // the skin. Built here because this pass already owns the prepass
    // program and the forward pair's paths; handed to the Scene each frame,
    // which routes skinned items to them (M3.2f).
    std::unique_ptr<Shader> skinnedShader_;
    std::unique_ptr<Shader> prepassSkinnedShader_;
    static constexpr int kBaseUnit = 8; // uShadowCascade[] start at texture unit 8
};
