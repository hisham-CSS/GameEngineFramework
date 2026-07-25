#pragma once
// Screen-space 2D overlay, drawn LAST.
//
// ORDERING: after FXAA, i.e. after tonemapping and the entire LDR post chain.
// UI must not be bloomed, colour-graded, vignetted or anti-aliased: those
// effects are tuned for the rendered world, and text and thin UI edges are the
// first things to visibly suffer from them. Drawing after post also means the
// UI is composited at final resolution in gamma space, which is where a 2D
// renderer belongs.
//
// This pass is NOT part of the LDR ping-pong chain and must never call
// nextPostTarget(): it does not transform the image, it paints ON TOP of the
// finished frame in ctx.defaultFBO. It is deliberately absent from
// Renderer::countLdrPostPasses_ for the same reason.
//
// The pass owns no UI itself. It hands a Renderer2D, already set up in
// screen space for the current viewport, to a host-supplied callback — so the
// engine does not mandate a UI model, and a 2D game can use the same hook to
// draw its sprites.
#include "../IRenderPass.h"

#include <functional>

namespace MyCoreEngine {
    class Renderer2D;
    // Called each frame with a Renderer2D already in screen space (origin
    // top-left, +y down, pixel units) and the viewport size. Draw only —
    // Begin/End are handled by the pass so GL state hygiene is guaranteed.
    using UIDrawFn = std::function<void(Renderer2D&, int widthPx, int heightPx)>;
}

class ENGINE_API UIPass final : public IRenderPass {
public:
    // Both pointers are owned by the Renderer and outlive the pass.
    UIPass(MyCoreEngine::Renderer2D* r2d, const MyCoreEngine::UIDrawFn* draw)
        : r2d_(r2d), draw_(draw) {}

    const char* name() const override { return "UI"; }
    void setup(PassContext&) override;
    bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) override;

private:
    MyCoreEngine::Renderer2D* r2d_ = nullptr;
    const MyCoreEngine::UIDrawFn* draw_ = nullptr;
    bool initTried_ = false;
};
