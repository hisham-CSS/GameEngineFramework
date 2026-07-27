#pragma once
// The C++ half of the sample HUD — and there is very little of it left.
//
// Everything the sample HUD shows lives in Exported/UI/hud.cxml + hud.cstyle, and
// it reaches the screen because the default scene has an ENTITY carrying a
// UIDocumentComponent. Nothing about it is installed by the executable, which
// is the point: a game's UI is scene content, exactly like its meshes, lights,
// sounds and scripts.
//
// What CANNOT be authored is a named C++ function, so this registers the two
// the sample uses — an `addScore` action and a `healthTint` converter — plus
// starting values for the properties the markup binds to. Call it once, before
// the scene loads.
#include "../core/Core.h"

namespace MyCoreEngine {

    class UIWorld;

    // Registers the sample HUD's model, action and converter into `world`.
    //
    // A real game calls the equivalent for its own content, or nothing at all
    // if its UI needs no derived values and no named actions.
    ENGINE_API void InstallDemoUIContent(UIWorld& world);

} // namespace MyCoreEngine
