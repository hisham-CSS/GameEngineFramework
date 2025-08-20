// Engine/src/render/passes/TonemapPass.h
#pragma once
#include "../IRenderPass.h"

class TonemapPass : public IRenderPass {
public:
    const char* name() const override { return "Tonemap"; }
    void setup(PassContext&) override {}
    void resize(PassContext&, int, int) override {}
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;
};
