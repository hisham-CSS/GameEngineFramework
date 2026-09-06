#include "ClipSampler.h"

namespace MyCoreEngine {

namespace {

std::uint32_t clampFrame(const Clip& clip, std::uint32_t frame) {
    // A frame past the end holds the last sample rather than wrapping: the
    // presentation indexes an attack clip at moveFrame, and moveFrame can
    // exceed a clip authored one frame short only through a bug that the load
    // assertion (A21, ROADMAP M3.4b) exists to refuse -- holding is the honest
    // picture of that bug, wrapping would hide it.
    if (clip.frames == 0) return 0;
    return frame < clip.frames ? frame : clip.frames - 1;
}

} // namespace

void SampleWorld(const Skeleton& skeleton, const Clip& clip, std::uint32_t frame, glm::mat4* out) {
    const std::uint32_t k = clampFrame(clip, frame);
    const std::size_t   n = skeleton.joints.size();
    const bool fromClip = clip.frames > 0 && clip.joints == n;
    for (std::size_t j = 0; j < n; ++j) {
        const glm::mat4& local = fromClip ? clip.LocalAt(k, static_cast<std::uint32_t>(j))
                                          : skeleton.joints[j].localBind;
        const int parent = skeleton.joints[j].parent;
        out[j] = (parent < 0) ? local : out[parent] * local;
    }
}

void SamplePalette(const Skeleton& skeleton, const Clip& clip, std::uint32_t frame, glm::mat4* out) {
    SampleWorld(skeleton, clip, frame, out);
    const std::size_t n = skeleton.joints.size();
    for (std::size_t j = 0; j < n; ++j)
        out[j] = out[j] * skeleton.joints[j].inverseBind;
}

void RestPalette(const Skeleton& skeleton, glm::mat4* out) {
    static const Clip kNoClip{};
    SamplePalette(skeleton, kNoClip, 0, out);
}

} // namespace MyCoreEngine
