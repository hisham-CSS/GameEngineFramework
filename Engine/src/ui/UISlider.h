#pragma once
// A slider's value, held on the element the way UITextEdit is.
//
// Deliberately NOT a painted widget. A <Slider> is an interactive BOX: it owns
// a number and knows how to turn a cursor position into one, and the look is
// whatever the author puts inside it and styles. That is the same division the
// rest of this system uses — structure in markup, appearance in the stylesheet,
// and only the part that cannot be authored written in C++.
//
// It is two-way through the SAME `bind-value` a TextField uses, because it is
// the same relationship: the element owns a value, the source wants it, and
// either end may move it. UIBinding's Kind::Value handles both.
#include "../core/Core.h"

#include <algorithm>
#include <cmath>

namespace MyCoreEngine::ui {

    struct ENGINE_API UISliderState {
        // The value the APP cares about, in the app's own units. A volume
        // slider is 0..1, a field-of-view slider is 60..110; neither should
        // have to know about the pixels underneath.
        float value = 0.0f;
        float min = 0.0f;
        float max = 1.0f;
        // 0 means continuous. A non-zero step quantises, which is what a
        // "quality 1..5" slider wants and what a volume slider must not have —
        // the whole complaint that started this was a control that moved in
        // tenths.
        float step = 0.0f;
        // How far a single arrow press moves it. Independent of `step` so a
        // continuous slider still has a sensible keyboard and gamepad grain.
        float keyStep = 0.05f;
        // Vertical sliders exist (a mixer channel), and the drag maths differs
        // only in which axis it reads.
        bool vertical = false;

        float span() const { return max - min; }

        // Value -> 0..1, for a fill width or a thumb position.
        float normalised() const {
            const float s = span();
            return s > 0.0f ? std::clamp((value - min) / s, 0.0f, 1.0f) : 0.0f;
        }

        // 0..1 -> value, quantised and clamped. The one place the rules live,
        // so a drag, an arrow key and a write from the data source cannot
        // disagree about what a legal value is.
        float fromNormalised(float t) const {
            float v = min + std::clamp(t, 0.0f, 1.0f) * span();
            if (step > 0.0f) v = min + std::round((v - min) / step) * step;
            return std::clamp(v, std::min(min, max), std::max(min, max));
        }

        // Returns true when the value actually moved, so callers can stay
        // equality-gated like every other setter in this system.
        bool SetValue(float v) {
            if (step > 0.0f) v = min + std::round((v - min) / step) * step;
            v = std::clamp(v, std::min(min, max), std::max(min, max));
            if (v == value) return false;
            value = v;
            return true;
        }

        bool SetNormalised(float t) { return SetValue(fromNormalised(t)); }
        bool Nudge(float dir) { return SetValue(value + dir * keyStep); }
    };

} // namespace MyCoreEngine::ui
