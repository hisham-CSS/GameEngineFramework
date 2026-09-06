// STATELESS CYCLE PHASES (ROADMAP M3.4a; ADR-019 D2 and D3).
//
// A cycle -- idle, walk, crouch, the air poses -- has no start tick, because a
// start tick would be presentation-owned state and T0 (ADR-019 D3) owns none.
// Its frame is a pure function of something the sim already has: the tick for
// most cycles, posX for the walk so the feet do not slide. Both are integers
// that can be NEGATIVE -- half the stage is left of centre -- and C++'s `/` and
// `%` truncate toward zero, so a naive `(posX / stride) % n` walks the frame
// index BACKWARDS through negative numbers and hands the sampler -3. These
// helpers floor instead, so a fighter crossing stage centre sees frame n-1,
// then 0, then 1, with no jump and no negative index.
#pragma once

#include <cstdint>

namespace cse::presentation {

// Floor division: rounds toward negative infinity. `b` must be > 0.
std::int64_t FloorDiv(std::int64_t a, std::int64_t b);

// `phase` floor-reduced into [0, n). A zero-length cycle has exactly one frame,
// frame 0, so a clip the artist has not authored yet cannot index anything.
std::uint32_t CycleFrame(std::int64_t phase, std::uint32_t n);

// The walk cycle's frame from the fighter's own position: one frame per
// `strideSub` sub-units of ground covered, floored, then reduced into [0, n).
// A non-positive stride means "no stride authored" and yields frame 0.
std::uint32_t WalkCycleFrame(std::int32_t posXSub, std::int32_t strideSub, std::uint32_t n);

} // namespace cse::presentation
