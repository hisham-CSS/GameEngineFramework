#pragma once
// UI as SCENE CONTENT: an entity carrying a markup + stylesheet path.
//
// Until now the host installed a draw callback, which meant the UI a game shows
// was a property of the executable rather than of the scene. That is backwards
// for everything else in this engine — a mesh, a light, a sound and a script
// are all entities — and it makes "the editor previews what ships" depend on
// both hosts remembering to install the same callback.
//
// With this, a scene declares its own UI, the Player and the Game view both
// pick it up from the saved file, and nothing in either host mentions it.
#include "../core/Core.h"
#include "UIScale.h"

#include <string>

namespace MyCoreEngine {

    struct UIDocumentComponent {
        // Project-relative, and run through PathIsContained before opening —
        // markup is authored content flowing into a parser, like every model,
        // script, clip and HDRi path in a scene file.
        std::string markup;      // e.g. "Exported/UI/hud.cxml"
        std::string stylesheet;  // may be empty
        // Larger draws later, so a HUD can sit over a background panel without
        // either of them knowing about the other. Ties break on entity order,
        // which is stable across a save and reload.
        int  sortOrder = 0;
        // Off hides the document AND stops it consuming input, which is what a
        // pause menu wants from every other document while it is up.
        bool enabled = true;
        // Whether this document may take pointer and keyboard input. A purely
        // decorative overlay (a vignette, a damage flash) should say no, or it
        // will swallow clicks meant for whatever is beneath it.
        bool interactive = true;

        // The part of the UI surface this document occupies, as FRACTIONS of
        // the surface (0..1). The default covers all of it.
        //
        // Normalized rather than pixels so a layout does not silently change
        // meaning between a 1080p screen and a 4K one — the same reason
        // percentages exist in the stylesheet. Layout runs at the region's
        // size, so a sidebar's `width: 50%` is half the SIDEBAR, and the
        // document's rects are offset into place, which makes painting,
        // hit-testing and clipping all follow for free.
        float regionX = 0.0f, regionY = 0.0f;
        float regionW = 1.0f, regionH = 1.0f;

        // How authored pixels become screen pixels. Computed from the whole UI
        // SURFACE, never from this document's region: a sidebar occupying a
        // quarter of the screen must scale by how big the screen is, or it
        // would shrink its own text on exactly the large display the feature
        // exists to serve.
        //
        // Defaults to Constant, so a scene saved before this existed lays out
        // byte-identically.
        ui::UIScaleSettings scale{};
    };

} // namespace MyCoreEngine
