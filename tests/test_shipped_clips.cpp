// THE SHIPPED CLIPS, HELD TO THE FRAME DATA (ROADMAP M3.3c; ADR-019 D2).
//
// D2 is enforced four ways; this file is the second: the committed model itself,
// decoded headlessly with Assimp (Model::Decode is GL-free), checked against the
// character the kernel will run -- CharacterData loaded the way the mode loads
// it, MatchBuilder turning its moves into MoveDefs, and the kernel's own
// MoveDuration saying how long each move is. The first enforcement, the
// sidecar A21/A22 asserts, is exercised here too, by mutation, so the load
// error a mistimed clip would produce is a thing this file has seen fire.
//
// Everything here reads committed bytes staged by test_runtime_deps; CI never
// runs Blender. A regenerated model that drifts from fighter_a.json fails here
// naming the move, the clip and both counts.
#include <gtest/gtest.h>

#include "Engine.h"

#include <cse/data/CharacterData.h>
#include <cse/data/MatchBuilder.h>
#include <cse/kernel/Combat.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace MyCoreEngine;
using cse::data::CharacterData;
using cse::data::LoadOptions;
using cse::data::LoadReport;
using json = nlohmann::json;

namespace {

// The staged shipping directory, found the way every kernel test finds it; every
// load below ASSERTs, so a missing staging step is a named failure, not a pass.
std::string charactersDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / "fighter_a.json")) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
}

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = { "meter", "juggle" };
    return o;
}

void loadShipped(CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(cse::data::LoadCharacterFile(charactersDir(), "fighter_a.json", loadOptions(), out, report))
        << "fighter_a.json did not load from " << charactersDir() << "\n  rule : " << report.rule
        << "\n  error: " << report.error;
    ASSERT_FALSE(out.anim3dModel.empty()) << "fighter_a.json names no engine.anim3d.model; this WP is what makes it";
}

// The decoded model, at the root-relative path the loader stored (the content
// root of a shipped load is the characters directory, see LoadOptions::fileDir).
ModelCPUData decodeShipped(const CharacterData& c) {
    const std::filesystem::path full = std::filesystem::path(charactersDir()) / c.anim3dModel;
    ModelCPUData cpu = Model::Decode(full.string());
    EXPECT_TRUE(cpu.valid) << full.string() << ": " << cpu.importError;
    return cpu;
}

// MatchBuilder's view of the character -- the kernel's MoveDefs keyed by move
// id and the body boxes it built -- so MoveDuration, the counters and the
// hurtbox a pose must fit are the kernel's numbers and not this file's
// re-derivation of them. The body is the mode's (MatchBuilder.h's defaults).
struct KernelView {
    cse::data::MatchBuild build{};
    std::map<std::string, cse::kernel::MoveDef> moves;
    const cse::kernel::FighterData& body() const { return build.data.p[0]; }
};

KernelView kernelView(const CharacterData& c) {
    cse::data::BuildOptions options{};
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    for (const cse::data::Move& mv : c.moves) {
        cse::data::MoveBinding b{};
        b.moveId = mv.id;
        b.button = static_cast<std::uint16_t>(options.bindings.size() + 1);
        options.bindings.push_back(b);
    }
    KernelView view;
    if (!cse::data::BuildMatchData(c, options, c, options, view.build)) {
        ADD_FAILURE() << "the mirror match did not build";
        return view;
    }
    for (const cse::data::Move& mv : c.moves) {
        const std::uint16_t slot = view.build.moves[0].Find(mv.id);
        if (slot == 0) { ADD_FAILURE() << mv.id << " got no kernel slot"; continue; }
        view.moves[mv.id] = view.build.data.p[0].moves[slot];
    }
    return view;
}

// The top of the body the kernel lets a move be hit in, in pixels: the move's
// own hurtboxOverride when it authors one, else the crouch body for a
// crouching move, else the standing body -- Combat.cpp's HurtboxOf order.
float hurtboxTopPx(const cse::kernel::MoveDef& def, const cse::kernel::FighterData& body) {
    const cse::kernel::Box& box =
        cse::kernel::BoxIsValid(def.hurtboxOverride) ? def.hurtboxOverride
        : (def.stance == cse::kernel::kStanceCrouching && cse::kernel::BoxIsValid(body.crouchHurtbox))
            ? body.crouchHurtbox
            : body.hurtbox;
    return static_cast<float>(box.y1) / static_cast<float>(cse::kernel::kSubUnitsPerPixel);
}

// The highest skinned vertex of the model at one frame of one clip: the mesh
// skinned on the CPU with the same palette the renderer uploads.
float skinnedTopPx(const ModelCPUData& cpu, const Clip& clip, std::uint32_t frame) {
    std::vector<glm::mat4> palette(cpu.skeleton.joints.size());
    SamplePalette(cpu.skeleton, clip, frame, palette.data());
    float top = -1e9f;
    for (const auto& mesh : cpu.meshes) {
        if (mesh.skin.Empty()) continue;
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            const glm::vec4 p(mesh.vertices[i].Position, 1.0f);
            glm::vec4 skinned(0.0f);
            for (int k = 0; k < 4; ++k) {
                const float w = mesh.skin.weights[i][k];
                if (w > 0.0f) skinned += w * (palette[static_cast<std::size_t>(mesh.skin.joints[i][k])] * p);
            }
            top = std::max(top, skinned.y);
        }
    }
    return top;
}

const char* clipNameOf(const cse::data::Move& mv) {
    return mv.anim3dClip.empty() ? mv.id.c_str() : mv.anim3dClip.c_str();
}

// A file beside the model (rig_bones.json, the semantic names the poses use).
json readJsonBeside(const CharacterData& c, const char* name) {
    const std::filesystem::path p =
        (std::filesystem::path(charactersDir()) / c.anim3dModel).parent_path() / name;
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p.string();
    json j = json::parse(in, nullptr, false);
    EXPECT_FALSE(j.is_discarded()) << p.string() << " is not valid JSON";
    return j;
}

} // namespace

// Every move's clip is exactly MoveDuration frames -- the kernel's number, so
// Blender frame k is moveFrame k; `knockdown` is exactly the largest
// knockdownTicks (the getup lands on counter 0 whatever the authored duration);
// the countdown cycles are at least as long as the largest counter they answer;
// every one of the fourteen reserved cycles is a clip; and the root never
// translates in the ground plane -- posX/posY belong to the kernel (D2).
TEST(ShippedClips, MatchTheFrameData) {
    CharacterData c;
    loadShipped(c);
    const ModelCPUData cpu = decodeShipped(c);
    ASSERT_TRUE(cpu.valid);
    const KernelView kv = kernelView(c);
    ASSERT_EQ(kv.moves.size(), c.moves.size());

    int largestKnockdown = 0;
    for (const cse::data::Move& mv : c.moves) {
        const cse::kernel::MoveDef& def = kv.moves.at(mv.id);
        const Clip* clip = cpu.clips.Find(clipNameOf(mv));
        ASSERT_NE(clip, nullptr) << "move `" << mv.id << "` has no clip `" << clipNameOf(mv) << "`";
        EXPECT_EQ(static_cast<std::int32_t>(clip->frames), cse::kernel::MoveDuration(def))
            << "move `" << mv.id << "`: clip `" << clipNameOf(mv) << "` has " << clip->frames
            << " frames, MoveDuration is " << cse::kernel::MoveDuration(def);
        largestKnockdown = std::max(largestKnockdown, static_cast<int>(def.knockdownTicks));
    }
    ASSERT_GT(largestKnockdown, 0) << "no move knocks down; the knockdown rule has nothing to hold";

    for (const char* cycle : cse::data::kReservedCycleNames) {
        const Clip* clip = cpu.clips.Find(cycle);
        ASSERT_NE(clip, nullptr) << "reserved cycle `" << cycle << "` is not a clip of the model";
        EXPECT_GE(clip->frames, 2u) << cycle << ": a cycle is any length >= 2";
    }
    EXPECT_EQ(static_cast<int>(cpu.clips.Find("knockdown")->frames), largestKnockdown)
        << "knockdown is indexed from the end and must land its getup on counter 0";

    // Root motion: the one joint without a parent holds its ground-plane
    // position on every frame of every clip. Vertical motion (a crouch, a fall)
    // is the clip's; x and z are the kernel's.
    int root = -1;
    for (std::size_t j = 0; j < cpu.skeleton.joints.size(); ++j)
        if (cpu.skeleton.joints[j].parent < 0) { ASSERT_EQ(root, -1) << "more than one root"; root = static_cast<int>(j); }
    ASSERT_GE(root, 0);
    for (const Clip& clip : cpu.clips.clips) {
        ASSERT_EQ(clip.joints, cpu.skeleton.joints.size()) << clip.name;
        const glm::vec4 first = clip.LocalAt(0, static_cast<std::uint32_t>(root))[3];
        for (std::uint32_t f = 1; f < clip.frames; ++f) {
            const glm::vec4 t = clip.LocalAt(f, static_cast<std::uint32_t>(root))[3];
            EXPECT_NEAR(t.x, first.x, 1e-3f) << clip.name << " frame " << f << ": the root moved along x";
            EXPECT_NEAR(t.z, first.z, 1e-3f) << clip.name << " frame " << f << ": the root moved along z";
        }
    }
}

// The sidecar the loader read (A21/A22 were asserted on it) and the model the
// renderer will decode agree clip for clip -- same names, same frame counts,
// nothing on either side the other lacks. A sidecar written by anything but
// the export that produced the model would show here.
TEST(ShippedClips, TheShippedModelAgreesWithItsSidecarClipForClip) {
    CharacterData c;
    loadShipped(c);
    const ModelCPUData cpu = decodeShipped(c);
    ASSERT_TRUE(cpu.valid);
    ASSERT_FALSE(c.anim3dClips.empty());
    EXPECT_EQ(c.anim3dClips.size(), cpu.clips.clips.size()) << "the sidecar and the model list different clip counts";
    for (const cse::data::ClipLength& entry : c.anim3dClips) {
        const Clip* clip = cpu.clips.Find(entry.name);
        ASSERT_NE(clip, nullptr) << "the sidecar names `" << entry.name << "`, which the model lacks";
        EXPECT_EQ(clip->frames, static_cast<std::uint32_t>(entry.frames))
            << "`" << entry.name << "`: sidecar " << entry.frames << ", model " << clip->frames;
    }
    for (const Clip& clip : cpu.clips.clips) {
        bool listed = false;
        for (const cse::data::ClipLength& entry : c.anim3dClips) listed = listed || entry.name == clip.name;
        EXPECT_TRUE(listed) << "the model carries `" << clip.name << "`, which the sidecar lacks";
    }
}

// The sidecar assert fires: one frame added to stand_lp's recovery in the
// character text, loaded against the shipped model's sidecar, is refused under
// A21 naming the move, the clip and both counts. This is the loader half of D2
// seen firing on the SHIPPED files rather than on a fixture.
TEST(ShippedClips, MutatingStandLpRecoveryMakesTheClipLengthCheckFire) {
    const std::filesystem::path file = std::filesystem::path(charactersDir()) / "fighter_a.json";
    std::ifstream in(file, std::ios::binary);
    ASSERT_TRUE(in.good()) << file.string();
    json doc = json::parse(in, nullptr, false);
    ASSERT_FALSE(doc.is_discarded());
    bool found = false;
    int expected = 0;
    for (json& mv : doc["moves"])
        if (mv["id"] == "stand_lp") {
            mv["recovery"] = mv["recovery"].get<int>() + 1;
            expected = mv["startup"].get<int>() + mv["active"].get<int>() + mv["recovery"].get<int>();
            found = true;
        }
    ASSERT_TRUE(found);

    LoadOptions o = loadOptions();
    o.contentRoot = charactersDir();          // what LoadCharacterFile would have filled in
    CharacterData c; LoadReport r;
    EXPECT_FALSE(cse::data::LoadCharacterJson("fighter_a.json", doc.dump(), o, c, r));
    EXPECT_EQ(r.rule, "A21") << r.error;
    EXPECT_NE(r.error.find("stand_lp"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find(std::to_string(expected)), std::string::npos) << "the expected count is not named: " << r.error;
    EXPECT_NE(r.error.find(std::to_string(expected - 1)), std::string::npos) << "the actual count is not named: " << r.error;

    // and the unmutated text loads, so the refusal above was the mutation's
    CharacterData ok; LoadReport rr;
    std::ifstream again(file, std::ios::binary);
    const json pristine = json::parse(again, nullptr, false);
    EXPECT_TRUE(cse::data::LoadCharacterJson("fighter_a.json", pristine.dump(), o, ok, rr)) << rr.error;
}

// The countdown cycles are indexed FROM THE END (frame = N - remaining, clamped
// at 0; FightPresentation::ClipFrameFor), so a cycle shorter than the longest
// counter it answers would spend its first ticks clamped on frame 0 and the
// reaction would start late. hitstun_stand covers the largest hitstun,
// hitstun_air the largest airHitstun, both blockstun cycles the largest
// blockstun, knockdown the largest knockdownTicks -- the kernel's MoveDefs,
// read the way the kernel reads them (ROADMAP M3.3d).
TEST(ShippedClips, StunAndKnockdownClipsCoverTheLongestAuthoredCounters) {
    CharacterData c;
    loadShipped(c);
    const ModelCPUData cpu = decodeShipped(c);
    ASSERT_TRUE(cpu.valid);
    const KernelView kv = kernelView(c);
    int hitstun = 0, airHitstun = 0, blockstun = 0, knockdown = 0;
    for (const auto& [id, def] : kv.moves) {
        hitstun    = std::max(hitstun, static_cast<int>(def.hitstun));
        airHitstun = std::max(airHitstun, static_cast<int>(def.airHitstun));
        blockstun  = std::max(blockstun, static_cast<int>(def.blockstun));
        knockdown  = std::max(knockdown, static_cast<int>(def.knockdownTicks));
    }
    ASSERT_GT(hitstun, 0); ASSERT_GT(airHitstun, 0); ASSERT_GT(blockstun, 0); ASSERT_GT(knockdown, 0);
    const auto frames = [&](const char* name) {
        const Clip* clip = cpu.clips.Find(name);
        return clip ? static_cast<int>(clip->frames) : -1;
    };
    EXPECT_GE(frames("hitstun_stand"), hitstun)   << "the largest hitstun is " << hitstun;
    EXPECT_GE(frames("hitstun_air"), airHitstun)  << "the largest airHitstun is " << airHitstun;
    EXPECT_GE(frames("blockstun_stand"), blockstun)  << "the largest blockstun is " << blockstun;
    EXPECT_GE(frames("blockstun_crouch"), blockstun) << "the largest blockstun is " << blockstun;
    EXPECT_GE(frames("knockdown"), knockdown)     << "the largest knockdownTicks is " << knockdown;
}

// The walk cycles are indexed by posX at the kernel's walk speed -- one frame
// per walkSpeedSub of travel, forward or back (WalkCycleFrame) -- so the only
// clip that does not skate is one whose planted foot slides back exactly that
// far per frame: N x walkSpeedPx is the stride, in whole pixels, and each foot
// is planted for half the cycle. Measured on the exported joints (SampleWorld),
// for walk_fwd, walk_back and crouch_walk (ROADMAP M3.3d).
TEST(ShippedClips, AWalkCycleAdvancesItsStrideInWholeTicks) {
    CharacterData c;
    loadShipped(c);
    const ModelCPUData cpu = decodeShipped(c);
    ASSERT_TRUE(cpu.valid);
    const KernelView kv = kernelView(c);
    const std::int32_t speedSub = kv.body().walkSpeedSub;
    ASSERT_GT(speedSub, 0);
    const float v = static_cast<float>(speedSub) / static_cast<float>(cse::kernel::kSubUnitsPerPixel);

    const json bones = readJsonBeside(c, "rig_bones.json");
    for (const char* cycle : { "walk_fwd", "walk_back", "crouch_walk" }) {
        const Clip* clip = cpu.clips.Find(cycle);
        ASSERT_NE(clip, nullptr) << cycle;
        const std::uint32_t n = clip->frames;
        ASSERT_GE(n, 4u) << cycle;
        EXPECT_EQ((static_cast<std::int64_t>(n) * speedSub) % cse::kernel::kSubUnitsPerPixel, 0)
            << cycle << ": " << n << " frames at " << speedSub << " sub-units is not a whole number of pixels";
        std::vector<glm::mat4> world(cpu.skeleton.joints.size());
        for (const char* side : { "l_foot", "r_foot" }) {
            const int joint = cpu.skeleton.Find(bones["bones"][side].get<std::string>());
            ASSERT_GE(joint, 0) << side;
            std::vector<float> x(n), y(n);
            for (std::uint32_t f = 0; f < n; ++f) {
                SampleWorld(cpu.skeleton, *clip, f, world.data());
                x[f] = world[static_cast<std::size_t>(joint)][3].x;
                y[f] = world[static_cast<std::size_t>(joint)][3].y;
            }
            // The longest cyclic run of "slid back exactly v" frames is the
            // stance; it must last half the cycle, planted (no height change).
            std::uint32_t best = 0;
            for (std::uint32_t start = 0; start < n; ++start) {
                std::uint32_t run = 0;
                for (std::uint32_t k = 0; k < n; ++k) {
                    const std::uint32_t a = (start + k) % n, b = (start + k + 1) % n;
                    if (std::abs((x[b] - x[a]) + v) > 0.05f || std::abs(y[b] - y[a]) > 0.05f) break;
                    ++run;
                }
                best = std::max(best, run);
            }
            // n/2 stance frames give n/2 - 1 slides; the swing's first sample
            // sits on the same line (lift 0 at takeoff), so one more is allowed.
            EXPECT_GE(best, n / 2 - 1) << cycle << " " << side << ": the planted foot slides back " << v
                                         << " px on " << best + 1 << " consecutive frames, less than the half cycle of " << n / 2;
            EXPECT_LE(best, n / 2) << cycle << " " << side << ": the foot never swings";
            const float range = *std::max_element(x.begin(), x.end()) - *std::min_element(x.begin(), x.end());
            EXPECT_NEAR(range, static_cast<float>(n / 2) * v, 0.1f)
                << cycle << " " << side << ": the step is " << range << " px, N/2 x speed is " << (n / 2) * v;
        }
    }
}

// ADR-019 D2: the contact pose sits inside the live hitbox for exactly the
// active ticks -- and inside the BODY the kernel says can be hit. The kernel's
// hurtbox for a move (Combat.cpp) is the move's own hurtboxOverride when it
// authors one (fighter_a: crouch_mk 26 px, crouch_hp 36 px, crouch_hk 20 px),
// else the crouch body for a crouching move, else the standing body; a pose
// taller than that is a head a low attack passes through while the picture
// says it connects. The mesh is skinned on the CPU at the first active frame
// with the palette the renderer uploads, and its highest vertex must sit
// within 2 px of the box's top (ROADMAP M3.3d's bar).
TEST(ShippedClips, AContactPoseFitsItsMovesAuthoredHurtboxHeight) {
    CharacterData c;
    loadShipped(c);
    const ModelCPUData cpu = decodeShipped(c);
    ASSERT_TRUE(cpu.valid);
    const KernelView kv = kernelView(c);
    int authoredInFile = 0, carriedByKernel = 0;
    for (const cse::data::Move& mv : c.moves) {
        const cse::kernel::MoveDef& def = kv.moves.at(mv.id);
        const Clip* clip = cpu.clips.Find(clipNameOf(mv));
        ASSERT_NE(clip, nullptr) << mv.id;
        if (mv.hurtboxOverride.y1 > mv.hurtboxOverride.y0) ++authoredInFile;
        if (cse::kernel::BoxIsValid(def.hurtboxOverride)) ++carriedByKernel;
        const std::uint32_t contact = static_cast<std::uint32_t>(std::max(def.startup, 0));
        ASSERT_LT(contact, clip->frames) << mv.id << ": the first active frame is past the clip";
        const float top   = skinnedTopPx(cpu, *clip, contact);
        const float limit = hurtboxTopPx(def, kv.body());
        EXPECT_LE(top, limit + 2.0f)
            << "move `" << mv.id << "`: the contact pose reaches " << top << " px, the body the kernel can hit ends at "
            << limit << " px" << (cse::kernel::BoxIsValid(def.hurtboxOverride) ? " (the move's own hurtbox)" : "");
    }
    // The file authors a hurtbox on three moves (crouch_mk 26 px, crouch_hp 36,
    // crouch_hk 20), the loader parses them, and MatchBuilder carries NONE into
    // MoveDef::hurtboxOverride -- every crouching move is hit in the 34 px
    // crouch body, and that body is what the poses above were held to. Pinned
    // here so the day the builder wires them (ROADMAP: the M3.3d finding), this
    // test fails on purpose and the three contact poses are re-fitted to 26,
    // 36 and 20.
    EXPECT_EQ(authoredInFile, 3) << "fighter_a authors a hurtbox on three moves; the file changed under this test";
    EXPECT_EQ(carriedByKernel, 0) << "MatchBuilder now carries a move's hurtbox into the kernel: re-fit the crouching "
                                     "contact poses to their authored heights and update this pin";
}
