// Engine/src/render/passes/ShadowCSMPass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

class ShadowCSMPass : public IRenderPass {

public:
    ShadowCSMPass(int cascades = 4, int baseRes = 2048);
    const char* name() const override { return "ShadowCSM"; }

    void setup(PassContext&) override;                     // create FBO, depth tex, shader
    void resize(PassContext&, int, int) override {};        // NOP (shadow size is independent)
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

    // Read-only access for validation/inspection
    const CSMSnapshot& snapshot() const { return snap_; }

    // Editor toggles
    void setEnabled(bool e) { enabled_ = e; }
    void setLambda(float v);             // 0..1, mirrors csmLambda_
    void setBaseResolution(int r);       // recreates textures next frame

private:
    // internal state mirrors your current fields
    static constexpr int kMaxCascades = 4;
    bool     enabled_{ true };
    int      cascades_{ 4 };
    int      baseRes_{ 2048 };
    float    lambda_{ 0.7f };
    float    posEps_{ 0.05f };
    float    angEps_{ 0.5f };
    uint64_t frameIndex_{ 0 };

    unsigned fbo_{ 0 };
    std::array<unsigned, kMaxCascades> depth_{ {0,0,0,0} };
    std::array<int, kMaxCascades> resPer_{ {0,0,0,0} };

    std::unique_ptr<Shader> depthProg_;  // loads your "shadow_depth_*" shaders

    // published to PassContext for other passes to read
    CSMSnapshot snap_{};
};
