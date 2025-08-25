// Engine/src/render/passes/ForwardOpaquePass.h
#pragma once
#include "../IRenderPass.h"

class ForwardOpaquePass final : public IRenderPass {
public:
    // Renderer (or Editor) gives the compiled forward shader you already pass to run()
    explicit ForwardOpaquePass(Shader& shader) : shader_(&shader) {}
    const char* name() const override { return "ForwardOpaque"; }

    void setup(PassContext&) override {};
    void resize(PassContext&, int, int) override {};
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    Shader* shader_; // not owned
    static constexpr int kBaseUnit = 8; // uShadowCascade[] start at texture unit 8
};
