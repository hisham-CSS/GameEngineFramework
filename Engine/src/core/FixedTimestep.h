#pragma once
#include <algorithm>

namespace MyCoreEngine {

    // Fixed-timestep accumulator (header-only, no GL/window dependencies).
    //
    //   FixedTimestep step(1.f / 60.f);
    //   // each frame:
    //   step.advance(frameDt, [](float fixedDt) { /* simulate */ });
    //
    // advance() runs the callback once per whole step consumed, capped at
    // maxSteps per call so a long stall can't trigger a spiral of death
    // (sim falling behind -> more steps -> longer frame -> further behind).
    // When the cap is hit the remaining backlog is dropped.
    class FixedTimestep {
    public:
        explicit FixedTimestep(float stepSeconds = 1.f / 60.f)
            : step_(std::max(kMinStep, stepSeconds)) {}

        void setStep(float stepSeconds) { step_ = std::max(kMinStep, stepSeconds); }
        float step() const { return step_; }

        template <typename Fn>
        int advance(float dt, Fn&& fn, int maxSteps = 8) {
            accumulator_ += std::max(0.f, dt);
            int steps = 0;
            while (accumulator_ >= step_ && steps < maxSteps) {
                fn(step_);
                accumulator_ -= step_;
                ++steps;
            }
            if (steps == maxSteps && accumulator_ >= step_) {
                accumulator_ = 0.f; // drop backlog instead of spiraling
            }
            return steps;
        }

        // Fraction of a step accumulated but not yet simulated, in [0,1).
        // A renderer can use this to interpolate between sim states.
        float alpha() const { return accumulator_ / step_; }

        void reset() { accumulator_ = 0.f; }

    private:
        static constexpr float kMinStep = 1e-4f; // 10 kHz ceiling; guards div-by-zero
        float step_;
        float accumulator_ = 0.f;
    };

} // namespace MyCoreEngine
