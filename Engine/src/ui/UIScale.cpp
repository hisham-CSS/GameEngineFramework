#include "UIScale.h"

#include <algorithm>
#include <cmath>

namespace MyCoreEngine::ui {

float ComputeUIScale(const UIScaleSettings& s, const glm::vec2& surfacePx) {
    if (s.mode == UIScaleMode::Constant) return 1.0f;
    // A zero or negative anywhere would make a log undefined and poison every
    // box in the tree with a NaN. One surface of a collapsed panel is not worth
    // a UI that never recovers, so it degrades to "unchanged".
    if (!(surfacePx.x > 0.0f) || !(surfacePx.y > 0.0f) ||
        !(s.reference.x > 0.0f) || !(s.reference.y > 0.0f)) {
        return 1.0f;
    }

    const float m = std::clamp(s.match, 0.0f, 1.0f);
    const float lw = std::log(surfacePx.x / s.reference.x);
    const float lh = std::log(surfacePx.y / s.reference.y);
    const float scale = std::exp(lw + (lh - lw) * m);

    // Bounded. A pathological surface (one pixel tall while a window is being
    // dragged between monitors) would otherwise hand yoga a scale near zero and
    // collapse every box, and the frame after would hand it a huge one.
    return std::clamp(scale, 0.05f, 20.0f);
}

} // namespace MyCoreEngine::ui
