// THE FRAME-INDEXED SAMPLER, PROVEN WITHOUT A PICTURE (ROADMAP M3.2d; ADR-019
// D2/D3; DETERMINISM P4's "sampled at integer moveFrame, never at wall-clock").
//
// The sampler is one pure function over (skeleton, clip, integer frame). What
// this file pins is the part a rollback host and a frame-stepping playtester
// both depend on: the same frame yields the same bytes whatever was sampled
// before it; a frame past the end holds the last sample rather than wrapping
// or reading off the end; the rest pose composes to the identity palette; and
// there is no way to ask for a fractional frame at all.
#include <gtest/gtest.h>

#include "Engine.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

using namespace MyCoreEngine;

namespace {

std::string modelFixturesDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = here / "tests" / "fixtures" / "models";
        if (fs::exists(candidate / "two_bone_strip.gltf")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/models";
}

bool sameBytes(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::mat4)) == 0;
}

std::vector<glm::mat4> palette(const Skeleton& s, const Clip& c, std::uint32_t frame) {
    std::vector<glm::mat4> out(s.joints.size());
    SamplePalette(s, c, frame, out.data());
    return out;
}

struct Strip {
    ModelCPUData cpu;
    const Clip*  fourteen = nullptr;
    const Clip*  held     = nullptr;
    void Load() {
        cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
        ASSERT_TRUE(cpu.valid) << cpu.importError;
        fourteen = cpu.clips.Find("fourteen");
        held     = cpu.clips.Find("held");
        ASSERT_NE(fourteen, nullptr);
        ASSERT_NE(held, nullptr);
    }
};

} // namespace

TEST(ClipSampler, TheSameFrameYieldsTheSamePaletteInAnyOrder) {
    Strip s;
    s.Load();
    if (::testing::Test::HasFatalFailure()) return;

    // 13, then 0, then 7, then 13 again: a sampler with any memory of the
    // previous call -- a cached frame, an accumulated time -- gives a different
    // second 13. Bit-identical, not EXPECT_NEAR.
    const auto a = palette(s.cpu.skeleton, *s.fourteen, 13);
    const auto z = palette(s.cpu.skeleton, *s.fourteen, 0);
    const auto m = palette(s.cpu.skeleton, *s.fourteen, 7);
    const auto b = palette(s.cpu.skeleton, *s.fourteen, 13);
    EXPECT_TRUE(sameBytes(a, b)) << "frame 13 sampled after 0 and 7 is not the frame 13 sampled first";
    EXPECT_FALSE(sameBytes(a, z)) << "frames 13 and 0 must differ: the clip rotates tip every frame";
    EXPECT_FALSE(sameBytes(m, z));

    // And the composition is real: tip's palette entry at frame 0 is the
    // identity (rest), and at frame 13 it is a rotation about X whose
    // translation part is still zero after the inverse bind.
    EXPECT_EQ(z[1], glm::mat4(1.0f)) << "tip at frame 0 is its rest pose";
    EXPECT_NEAR(b[1][1][1], b[1][2][2], 1e-5f) << "a rotation about X keeps cos on both y and z";
}

TEST(ClipSampler, AFramePastTheEndClampsToTheLastSample) {
    Strip s;
    s.Load();
    if (::testing::Test::HasFatalFailure()) return;

    const auto last = palette(s.cpu.skeleton, *s.fourteen, 13);
    const auto past = palette(s.cpu.skeleton, *s.fourteen, 14);
    const auto far  = palette(s.cpu.skeleton, *s.fourteen, 1000);
    EXPECT_TRUE(sameBytes(last, past)) << "frame N must hold frame N-1, not wrap to 0 or read past the end";
    EXPECT_TRUE(sameBytes(last, far));
    const auto first = palette(s.cpu.skeleton, *s.fourteen, 0);
    EXPECT_FALSE(sameBytes(first, past)) << "frame N wrapped to frame 0";
}

TEST(ClipSampler, TheRestPoseYieldsAnIdentityPalette) {
    Strip s;
    s.Load();
    if (::testing::Test::HasFatalFailure()) return;

    std::vector<glm::mat4> rest(s.cpu.skeleton.joints.size());
    RestPalette(s.cpu.skeleton, rest.data());
    for (std::size_t j = 0; j < rest.size(); ++j)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                EXPECT_NEAR(rest[j][c][r], glm::mat4(1.0f)[c][r], 1e-5f)
                    << "joint " << j << " (" << s.cpu.skeleton.joints[j].name
                    << ") at rest is not the identity: the bind hierarchy and the inverse "
                       "bind matrices disagree";

    // 'held' keeps tip at a constant 30 degrees: every frame equals frame 0,
    // and none of them is the rest pose.
    const auto h0 = palette(s.cpu.skeleton, *s.held, 0);
    for (std::uint32_t k = 1; k < s.held->frames; ++k)
        EXPECT_TRUE(sameBytes(h0, palette(s.cpu.skeleton, *s.held, k))) << "held frame " << k;
    EXPECT_FALSE(sameBytes(h0, rest));
}

namespace {

// Detection over the overload SET (a function plus a constrained deleted
// template), which decltype(name) cannot name: does `SamplePalette(s, c, T,
// out)` form a valid call for a T? A call to a deleted function is ill-formed,
// which is a substitution failure here -- so the answer is "no" exactly for
// the fractional frames the header deletes.
template <class T, class = void>
struct CanSamplePalette : std::false_type {};
template <class T>
struct CanSamplePalette<T, std::void_t<decltype(SamplePalette(std::declval<const Skeleton&>(), std::declval<const Clip&>(),
                                                              std::declval<T>(), std::declval<glm::mat4*>()))>>
    : std::true_type {};

template <class T, class = void>
struct CanSampleWorld : std::false_type {};
template <class T>
struct CanSampleWorld<T, std::void_t<decltype(SampleWorld(std::declval<const Skeleton&>(), std::declval<const Clip&>(),
                                                          std::declval<T>(), std::declval<glm::mat4*>()))>>
    : std::true_type {};

} // namespace

TEST(ClipSampler, ThereIsNoSamplerForAFractionalFrame) {
    // The compile-time property: a caller who computes "seconds * 60" cannot
    // hand the result in, while an integer -- including a plain int literal,
    // which must not be ambiguous -- still reaches the sampler.
    static_assert(CanSamplePalette<std::uint32_t>::value, "the integer sampler must be callable");
    static_assert(CanSamplePalette<int>::value, "an int frame must resolve to the integer sampler, not be ambiguous");
    static_assert(!CanSamplePalette<double>::value, "a double frame must not be a valid call");
    static_assert(!CanSamplePalette<float>::value, "a float frame must not be a valid call");
    static_assert(CanSampleWorld<std::uint32_t>::value);
    static_assert(!CanSampleWorld<double>::value, "a double frame must not be a valid call");
    SUCCEED();
}
