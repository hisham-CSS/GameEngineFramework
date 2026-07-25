#include <glad/glad.h>

#include "UIPass.h"
#include "../../render2d/Renderer2D.h"

#include <iostream>

void UIPass::setup(PassContext&) {
    // Renderer2D::Init needs a GL context, which exists by the time any pass is
    // set up. Done once and remembered even on failure, so a missing shader
    // logs a single line instead of once per frame forever.
    if (initTried_ || !r2d_) return;
    initTried_ = true;
    if (!r2d_->Init()) {
        std::cerr << "[UIPass] Renderer2D failed to initialise "
                     "(sprite2d shaders missing?) - UI will not draw\n";
    }
}

bool UIPass::execute(PassContext& ctx, MyCoreEngine::Scene&, Camera&,
                     const FrameParams& fp) {
    if (!r2d_ || !draw_ || !*draw_ || !r2d_->IsReady()) return false;
    if (fp.viewportW <= 0 || fp.viewportH <= 0) return false;

    // Paint onto the FINISHED frame. Deliberately NOT ctx.nextPostTarget():
    // this is an overlay, not a chain stage, so consuming a ping-pong slot
    // would desync the routing and send the real final image off-screen.
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFBO);
    glViewport(0, 0, fp.viewportW, fp.viewportH);

    // Begin/End are the pass's job, not the callback's: they capture and
    // restore every GL bit the 2D layer touches, which the bare pass loop
    // (no inter-pass reset) depends on.
    r2d_->BeginScreen(fp.viewportW, fp.viewportH);
    (*draw_)(*r2d_, fp.viewportW, fp.viewportH, fp.deltaTime);
    r2d_->End();
    return true;
}
