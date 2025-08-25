// Engine/src/render/passes/TonemapPass.h
#pragma once
#include "../IRenderPass.h"

class ENGINE_API TonemapPass final : public IRenderPass {
public:
    const char* name() const override { return "Tonemap"; }
    void setup(PassContext&) override {}
    void resize(PassContext&, int, int) override {}
    bool execute(PassContext& ctx, Scene& scene, Camera& camera, const FrameParams & fp) override;
};
