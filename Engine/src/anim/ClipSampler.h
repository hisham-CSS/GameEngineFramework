// THE FRAME-INDEXED SAMPLER (ROADMAP M3.2d; ADR-019 D2/D3; ARCHITECTURE section
// 2 rejects a time-based animator and says "build the frame-indexed sampler").
//
// One pure function: the skinning palette for integer frame k of a clip. It
// composes each joint's local transform under its parent -- the skeleton is
// parent-first, so one pass suffices -- and multiplies by the inverse bind
// matrix. No clock, no delta time, no member state, no interpolation, and NO
// OVERLOAD THAT TAKES A FRACTIONAL FRAME: the ones below are deleted so that a
// caller who computes "elapsed seconds * 60" gets a compile error rather than
// a pose that drifts from the frame data by a rounding. The presentation
// calls this with the kernel's own moveFrame; hitstop freezes moveFrame, so it
// freezes the pose for free (test_pose_select.cpp).
//
// Same frame in, same bytes out, in any order: the restore test in
// tests/test_clip_sampler.cpp samples 13, 0, 7, 13 and requires the two 13s to
// be bit-identical, which is the property a rollback host needs.
#pragma once

#include "ClipSet.h"
#include "Skeleton.h"
#include "../core/Core.h"   // ENGINE_API: the sampler is called from tests and the title across the DLL boundary

#include <glm/glm.hpp>

#include <cstdint>
#include <type_traits>

namespace MyCoreEngine {

// Model-space joint transforms at `frame` (clamped to [0, N-1]), BEFORE the
// inverse bind: what a bounds computation or a debug skeleton draw wants.
// `out` must hold skeleton.joints.size() matrices.
ENGINE_API void SampleWorld(const Skeleton& skeleton, const Clip& clip, std::uint32_t frame, glm::mat4* out);

// The skinning palette at `frame`: world * inverseBind per joint. `out` must
// hold skeleton.joints.size() matrices.
ENGINE_API void SamplePalette(const Skeleton& skeleton, const Clip& clip, std::uint32_t frame, glm::mat4* out);

// The rest palette -- every joint at its bind pose -- which is the identity
// for a skeleton whose inverse bind matrices invert its bind hierarchy.
ENGINE_API void RestPalette(const Skeleton& skeleton, glm::mat4* out);

// There is no sampler for a fractional frame, on purpose. Constrained
// templates rather than plain deleted overloads: a plain `double` overload
// would make an `int` argument ambiguous (int converts equally well to
// uint32_t, float and double), while these match ONLY floating-point
// arguments, so an integer literal still resolves to the real sampler and a
// `seconds * 60` still fails to compile.
template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
void SampleWorld(const Skeleton&, const Clip&, T, glm::mat4*) = delete;
template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
void SamplePalette(const Skeleton&, const Clip&, T, glm::mat4*) = delete;

} // namespace MyCoreEngine
