// Engine/src/render/passes/OutlinePass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

namespace MyCoreEngine { class Shader; }

// Ink outline from scene-depth discontinuities. An LDR post pass on the
// ping-pong chain that ALSO samples the scene depth texture (PassContext::
// hdrDepthTex). Depth-only (a forward renderer has no normal buffer), so it
// draws silhouettes and depth steps -- the contour line that pairs with cel
// shading. Cheap fullscreen pass; parameters from the scene's PostFXSettings.
class OutlinePass : public IRenderPass {
public:
    OutlinePass();
    ~OutlinePass() override;
    const char* name() const override { return "Outline"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    std::unique_ptr<MyCoreEngine::Shader> shader_;
};
