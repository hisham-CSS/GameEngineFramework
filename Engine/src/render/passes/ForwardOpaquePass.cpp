// Engine/src/render/passes/ForwardOpaquePass.cpp
#include "ForwardOpaquePass.h"
#include <glad/glad.h>

bool ForwardOpaquePass::execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) {
    // no-op for now; we’ll call your Scene::RenderScene here in step 2/3
    return false;
}
