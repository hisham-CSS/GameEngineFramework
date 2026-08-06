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
        // THE DIGITAL GRAIN, and it accelerates.
        //
        // `keyStep` is what ONE press moves -- fine, because a tap is how you
        // ask for a small change, and a control that can only move in 5%
        // jumps cannot be set to 43%. `keyStepMax` is what a HELD key or d-pad
        // reaches, because holding is how you ask for a large one, and 1% a
        // notch across a whole range is a long wait.
        //
        // Independent of `step` so a continuous slider still has a sensible
        // keyboard and D-PAD grain -- both are digital, and a digital control
        // has to move in notches.
        float keyStep = 0.01f;
        float keyStepMax = 0.05f;
        // How many presses of a HELD run reach keyStepMax. Counted rather than
        // timed on purpose: the keyboard's auto-repeat rate is the operating
        // system's and the pad's is UINavRepeater's, so a time-based ramp would
        // feel like two different controls. "Ten notches to full speed" is the
        // same promise on both.
        int keyRamp = 10;
        // How fast a fully deflected STICK moves it, in value per second. An
        // analog input deserves an analog response; stepping it in keyStep
        // notches is what made a pad feel like it was snapping.
        //
        // Expressed as a fraction of the range so it means the same thing on a
        // 0..1 volume and a 60..110 field of view: the default crosses the
        // whole range in about a second and a half.
        float analogSeconds = 1.5f;
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

        // ONE digital press. The step grows the longer a run of them lasts, so
        // a tap is fine and a hold is fast, out of the same gesture.
        //
        // A "run" is broken by REVERSING or by going quiet -- see Tick. Both
        // matter: reversing means you overshot and want the fine grain back,
        // and quiet means you let go. Without the reset, tapping ten times to
        // creep up on a value would accelerate as if you had held it.
        bool Nudge(float dir) {
            const float sign = dir < 0.0f ? -1.0f : 1.0f;
            if (sign != runDir_) run_ = 0;
            const float t = keyRamp > 0
                ? std::clamp(float(run_) / float(keyRamp), 0.0f, 1.0f) : 1.0f;
            const float grain = keyStep + (keyStepMax - keyStep) * t;
            ++run_;
            runDir_ = sign;
            idle_ = 0.0f;
            return SetValue(value + sign * grain);
        }

        // Per-frame clock for the acceleration above, called from
        // UIDocument::AdvanceTime. Nudge itself cannot do this: it has no dt,
        // and "the presses stopped" is a thing that happens when nothing calls
        // it at all.
        //
        // The idle window has to clear the PAD's auto-repeat delay (0.40s in
        // UINavRepeater) or a held d-pad would reset its own run between the
        // first press and the second and never accelerate at all.
        void Tick(float dt) {
            if (runDir_ == 0.0f) return;
            idle_ += dt;
            if (idle_ > 0.5f) { run_ = 0; runDir_ = 0.0f; idle_ = 0.0f; }
        }

        // Exposed for tests: how many presses into the current run we are.
        int digitalRun() const { return run_; }

    private:
        int   run_ = 0;
        float runDir_ = 0.0f;
        float idle_ = 0.0f;
    };

} // namespace MyCoreEngine::ui
