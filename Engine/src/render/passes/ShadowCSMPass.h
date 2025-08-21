// Engine/src/render/passes/ShadowCSMPass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

class ShadowCSMPass final : public IRenderPass {

public:
    ShadowCSMPass(int cascades = 4, int baseRes = 2048);
    const char* name() const override { return "ShadowCSM"; }

    void setup(PassContext&) override;                     // create FBO, depth tex, shader
    void resize(PassContext&, int, int) override {};        // NOP (shadow size is independent)
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

    // Read-only access for validation/inspection
    const CSMSnapshot& snapshot() const { return snap_; }

    // Update policy: when should we re-render shadow maps?
    enum class UpdatePolicy {
        Always,            // render every frame
        CameraOrSunMoved,  // render only when camera/sun changed (default)
        Manual             // render only if markDirty_() was called
    };

    // Editor toggles
    void setEnabled(bool e) { enabled_ = e; }
    void setLambda(float v);             // 0..1, mirrors csmLambda_
    void setBaseResolution(int r);       // recreates textures next frame
    void setNumCascades(int n) { cascades_ = std::clamp(n, 1, kMaxCascades); markDirty_(); }

    void setUpdatePolicy(UpdatePolicy p) { policy_ = p; }
    // n <= 0 => unlimited per-frame; otherwise update at most n cascades per frame (round-robin)
    void setCascadeUpdateBudget(int n) { budgetPerFrame_ = n; }

    void setMaxShadowDistance(float d) { maxShadowDistance_ = std::max(1.f, d); markDirty_(); }
    void setCascadePaddingMeters(float m) { cascadePaddingMeters_ = std::max(0.f, m); }
    void setDepthMarginMeters(float m) { depthMarginMeters_ = std::max(0.f, m); }

    // Depth-bias / culling controls
    void setSlopeDepthBias(float slope) { slopeBias_ = std::max(0.f, slope); }
    void setConstantDepthBias(float constant) { constBias_ = std::max(0.f, constant); }
    void setCullFrontFaces(bool on) { cullFrontFaces_ = on; }

    void setEpsilons(float posMeters, float angDegrees) {
        posEps_ = std::max(0.f, posMeters);
        angEps_ = std::max(0.f, angDegrees);
    }
    void forceUpdate() { markDirty_(); }

    // Getters
    bool  enabled()              const { return enabled_; }
    float lambda()               const { return lambda_; }
    int   baseResolution()       const { return baseRes_; }
    int   numCascades()          const { return cascades_; }
    UpdatePolicy updatePolicy()  const { return policy_; }
    int   cascadeUpdateBudget()  const { return budgetPerFrame_; }
    float maxShadowDistance()    const { return maxShadowDistance_; }
    float cascadePaddingMeters() const { return cascadePaddingMeters_; }
    float depthMarginMeters()    const { return depthMarginMeters_; }
    float slopeDepthBias()       const { return slopeBias_; }
    float constantDepthBias()    const { return constBias_; }
    bool  cullFrontFaces()       const { return cullFrontFaces_; }


    void  getEpsilons(float& posMeters, float& angDegrees) const {
        posMeters = posEps_; angDegrees = angEps_;
    }

private:
    static constexpr int kMaxCascades = 4;
    bool  enabled_{ true };
    int   cascades_{ 4 };           // you use 3–4
    int   baseRes_{ 2048 };
    float lambda_{ 0.7f };
    float splitBlendMeters_{ 20.f }; // not used here, but preserved for parity
    float maxShadowDistance_{ 1000.f };   // replaces fixed camFar for CSM splits
    float cascadePaddingMeters_{ 0.0f };  // grow/shrink XY extents per cascade
    float depthMarginMeters_{ 5.0f };     // replaces hardcoded ±5.0f on Z
    float slopeBias_{ 2.0f };             // glPolygonOffset factor
    float constBias_{ 4.0f };             // glPolygonOffset units
    bool  cullFrontFaces_{ true };

    // throttle/dirty
    uint64_t frameIndex_{ 0 };
    bool     shadowParamsDirty_{ true };
    float    posEps_{ 0.05f }, angEps_{ 0.5f };
    glm::vec3 lastCamPos_{}, lastCamFwd_{};
    glm::vec3 lastSunDir_{ 0,-1,0 };
    float     lastAspect_{ -1.f };
    float     lastFovDeg_{ -1.f };
    UpdatePolicy policy_{ UpdatePolicy::CameraOrSunMoved };
    int       budgetPerFrame_{ 0 };   // 0 == unlimited
    int       nextCascade_{ 0 };      // round-robin pointer

    // GL
    unsigned shadowFBO_{ 0 };
    std::array<unsigned, kMaxCascades> depth_{ {0,0,0,0} };
    std::array<int, kMaxCascades> resPer_{ {0,0,0,0} };

    // data for forward
    std::array<float, kMaxCascades + 1> splitZ_{ {0,0,0,0,0} };
    std::array<float, kMaxCascades>   splitFar_{ {0,0,0,0} };
    std::array<glm::mat4, kMaxCascades>   lightVP_{ {glm::mat4(1),glm::mat4(1),glm::mat4(1),glm::mat4(1)} };

    std::unique_ptr<MyCoreEngine::Shader> depthProg_; // "shadow_depth_*.glsl"

    // helpers
    void ensureTargets_();
    bool rebuild_(const Camera& cam, float aspect);
    void markDirty_() { shadowParamsDirty_ = true; }

    // published to PassContext for other passes to read
    CSMSnapshot snap_{};
};
