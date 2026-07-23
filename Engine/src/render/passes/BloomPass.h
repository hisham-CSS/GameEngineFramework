// Engine/src/render/passes/BloomPass.h
#pragma once
#include "../IRenderPass.h"
#include <memory>

namespace MyCoreEngine { class Shader; }

// Bloom: a bright-pass + separable-Gaussian glow composited additively back into
// the HDR buffer BEFORE tonemap (an HDR pass, NOT part of the LDR ping-pong
// chain). Works at half resolution through a pair of RGBA16F buffers, so the
// blur is cheap; the glow width comes from repeating the separable blur. Owns
// its own buffers (rebuilt when the viewport size changes). Pure GL 3.3.
class BloomPass : public IRenderPass {
public:
    BloomPass();
    ~BloomPass() override;
    const char* name() const override { return "Bloom"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    void ensureTargets_(int fullW, int fullH); // (re)allocate the half-res A/B pair
    void release_();

    std::unique_ptr<MyCoreEngine::Shader> brightShader_;
    std::unique_ptr<MyCoreEngine::Shader> blurShader_;
    std::unique_ptr<MyCoreEngine::Shader> compositeShader_;

    unsigned fboA_ = 0, texA_ = 0;
    unsigned fboB_ = 0, texB_ = 0;
    int halfW_ = 0, halfH_ = 0;
};
