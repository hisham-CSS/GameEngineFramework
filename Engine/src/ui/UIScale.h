#pragma once
// How authored pixels become screen pixels.
//
// Every length in a .cstyle is a number of pixels, which is the only thing an
// author can reasonably write — but a HUD authored against a 1920x1080 window
// occupies a quarter of a 4K screen and overflows a 1280x720 one. The fix is
// the one every UI toolkit converges on: author against a REFERENCE resolution
// and scale the whole thing by how far the real surface departs from it.
//
// THE UNIT RULE, which the rest of the UI system depends on:
//
//   Style        is in AUTHORED units (reference-resolution pixels).
//   Everything else — ComputedLayout, scroll offsets, pointer positions,
//   ScrollIntoView, SetOrigin — is in REAL surface pixels, always, at every
//   scale.
//
// The conversion happens where a Style length enters layout, and nowhere else.
// That is what keeps the document origin, hit-testing, clipping and every
// scroll offset out of the scale's way entirely: they were already real, and
// they stay real.
//
// The corollary matters to app code: `el->style().width = Px(100)` is 100
// AUTHORED pixels and scales; `el->SetScrollOffset({0, 100})` is 100 REAL
// pixels and does not. One is authoring, the other is geometry.
#include "../core/Core.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace MyCoreEngine::ui {

    enum class UIScaleMode : std::uint8_t {
        // 1.0, always. The default, so every document that existed before this
        // feature lays out byte-identically.
        Constant,
        // Reference resolution plus a match factor, as Unity's CanvasScaler.
        ScaleWithScreen,
    };

    struct ENGINE_API UIScaleSettings {
        UIScaleMode mode = UIScaleMode::Constant;
        glm::vec2   reference{ 1920.0f, 1080.0f };
        // 0 = follow width, 1 = follow height, between = blend. Clamped.
        //
        // Width is the default because a HUD's horizontal furniture — a health
        // bar, a hotbar, a minimap — is what runs out of room first, and
        // because an ultrawide monitor should show MORE, not bigger.
        float match = 0.0f;
    };

    // PURE, and exported, for the same reason ComputeScrollBars is: the whole
    // formula is asserted from the test suite with hand-supplied numbers, with
    // no document, no font and no GL context.
    //
    // The blend is in LOG space, which is what makes a half match the geometric
    // mean of the two axis ratios rather than their average — a linear blend
    // between 0.5x and 2.0x would give 1.25x, visibly favouring whichever axis
    // grew, and the asymmetry gets worse the further the surface is from the
    // reference.
    //
    // Returns exactly 1.0f for Constant, and for any surface or reference with
    // a non-positive component: a degenerate surface must produce an unchanged
    // UI, never a zero or an infinity propagated into every box in the tree.
    ENGINE_API float ComputeUIScale(const UIScaleSettings& s, const glm::vec2& surfacePx);

} // namespace MyCoreEngine::ui
