// CLIPS ON THE 60 HZ GRID, INTEGER FRAMES ONLY (ROADMAP M3.2c; ADR-019 D2;
// ARCHITECTURE section 2 rejects a time-based animator: "build the
// frame-indexed sampler").
//
// A clip is N frames, and frame k is one local transform per joint, in the
// skeleton's parent-first order. That is the whole type. There is no duration
// in seconds, no key times, no interpolation mode and no way to ask for frame
// 3.5, because the presentation samples an attack clip at the kernel's integer
// moveFrame and a clip that could be asked for "the pose at 0.0583 s" would be
// a clip that could drift from the frame data by a frame-rate's rounding.
//
// The grid is asserted at DECODE, not at draw: every multi-key channel in the
// file must carry exactly N keys at exactly k/60 s (glTF stores seconds and
// Assimp reports them at 1000 ticks per second), else the import is refused
// naming the clip and the key. A single-key channel is a constant -- Assimp
// synthesises one for every un-animated component of an animated joint, and
// a rotation-only joint is ordinary, not an error. A joint with no channel at
// all wears its bind pose in every frame.
//
// Sample k IS key k. Nothing here ever interpolates; the Guilty Gear Xrd look
// (every frame a keyframe, no in-betweening) is also the only way "the contact
// pose is on screen for exactly the active ticks" can be true.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace MyCoreEngine {

struct Clip {
    std::string   name;
    std::uint32_t frames = 0;      // N; sample k is key k, 0 <= k < N
    std::uint32_t joints = 0;      // the skeleton's joint count, for indexing
    // frames * joints local transforms, frame-major: local[k * joints + j].
    std::vector<glm::mat4> local;

    const glm::mat4& LocalAt(std::uint32_t frame, std::uint32_t joint) const {
        return local[static_cast<std::size_t>(frame) * joints + joint];
    }
};

// A clip's frame count is an integer and nothing in the type is a time: the
// compile-time half of "the sampler has no clock" (test_model_decode.cpp
// asserts the same on the decoded fixtures).
static_assert(std::is_same_v<decltype(Clip::frames), std::uint32_t>,
              "Clip::frames must stay an integer frame count, never a duration");

struct ClipSet {
    std::vector<Clip> clips;

    bool Empty() const { return clips.empty(); }

    const Clip* Find(const std::string& name) const {
        for (const Clip& c : clips)
            if (c.name == name) return &c;
        return nullptr;
    }
};

} // namespace MyCoreEngine
