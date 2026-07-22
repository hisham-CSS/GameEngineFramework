// Engine/src/render/passes/VignettePass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

namespace MyCoreEngine { class Shader; }

// Radial edge darkening -- a cinematic framing effect. An LDR post pass: it
// runs after tonemapping on the ping-pong chain (see PassContext), reading the
// current chain texture and writing the next. Cheap (one fullscreen pass, a
// handful of ALU), so it is allowed at every quality tier. Parameters come from
// the scene's PostFXSettings each frame.
class VignettePass : public IRenderPass {
public:
    VignettePass();
    ~VignettePass() override;
    const char* name() const override { return "Vignette"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    std::unique_ptr<MyCoreEngine::Shader> shader_;
};
