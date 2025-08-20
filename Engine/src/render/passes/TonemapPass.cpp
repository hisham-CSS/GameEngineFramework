// Engine/src/render/passes/TonemapPass.cpp
#include "TonemapPass.h"
#include <glad/glad.h>

bool TonemapPass::execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) {
    // no-op stub; we’ll bind hdrColorTex_ and draw fsQuad next step
    return false;
}
