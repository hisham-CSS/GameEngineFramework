// Engine/src/render/passes/ColorGradePass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

namespace MyCoreEngine { class Shader; }

// Procedural colour grade (white balance / lift-gain / contrast / saturation)
// on the tonemapped image -- a self-contained stand-in for a LUT workflow, no
// external asset. An LDR post pass on the ping-pong chain. Cheap fullscreen
// pass; parameters from the scene's PostFXSettings.
class ColorGradePass : public IRenderPass {
public:
    ColorGradePass();
    ~ColorGradePass() override;
    const char* name() const override { return "ColorGrade"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    std::unique_ptr<MyCoreEngine::Shader> shader_;
};
