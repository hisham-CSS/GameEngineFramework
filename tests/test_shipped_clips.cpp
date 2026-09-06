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

// MatchBuilder's MoveDefs for the character, keyed by move id: the kernel's view
// of the frame data, so MoveDuration is the kernel's arithmetic and not this
// file's re-derivation of it.
std::map<std::string, cse::kernel::MoveDef> kernelMoves(const CharacterData& c) {
    cse::data::BuildOptions options{};
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    for (const cse::data::Move& mv : c.moves) {
        cse::data::MoveBinding b{};
        b.moveId = mv.id;
        b.button = static_cast<std::uint16_t>(options.bindings.size() + 1);
        options.bindings.push_back(b);
    }
    cse::data::MatchBuild build{};
    std::map<std::string, cse::kernel::MoveDef> out;
    if (!cse::data::BuildMatchData(c, options, c, options, build)) {
        ADD_FAILURE() << "the mirror match did not build";
        return out;
    }
    for (const cse::data::Move& mv : c.moves) {
        const std::uint16_t slot = build.moves[0].Find(mv.id);
        if (slot == 0) { ADD_FAILURE() << mv.id << " got no kernel slot"; continue; }
        out[mv.id] = build.data.p[0].moves[slot];
    }
    return out;
}

const char* clipNameOf(const cse::data::Move& mv) {
    return mv.anim3dClip.empty() ? mv.id.c_str() : mv.anim3dClip.c_str();
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
    const auto defs = kernelMoves(c);
    ASSERT_EQ(defs.size(), c.moves.size());

    int largestKnockdown = 0, largestHitstun = 0, largestAirHitstun = 0, largestBlockstun = 0;
    for (const cse::data::Move& mv : c.moves) {
        const cse::kernel::MoveDef& def = defs.at(mv.id);
        const Clip* clip = cpu.clips.Find(clipNameOf(mv));
        ASSERT_NE(clip, nullptr) << "move `" << mv.id << "` has no clip `" << clipNameOf(mv) << "`";
        EXPECT_EQ(static_cast<std::int32_t>(clip->frames), cse::kernel::MoveDuration(def))
            << "move `" << mv.id << "`: clip `" << clipNameOf(mv) << "` has " << clip->frames
            << " frames, MoveDuration is " << cse::kernel::MoveDuration(def);
        largestKnockdown  = std::max(largestKnockdown, static_cast<int>(def.knockdownTicks));
        largestHitstun    = std::max(largestHitstun, static_cast<int>(def.hitstun));
        largestAirHitstun = std::max(largestAirHitstun, static_cast<int>(def.airHitstun));
        largestBlockstun  = std::max(largestBlockstun, static_cast<int>(def.blockstun));
    }
    ASSERT_GT(largestKnockdown, 0) << "no move knocks down; the knockdown rule has nothing to hold";

    for (const char* cycle : cse::data::kReservedCycleNames) {
        const Clip* clip = cpu.clips.Find(cycle);
        ASSERT_NE(clip, nullptr) << "reserved cycle `" << cycle << "` is not a clip of the model";
        EXPECT_GE(clip->frames, 2u) << cycle << ": a cycle is any length >= 2";
    }
    EXPECT_EQ(static_cast<int>(cpu.clips.Find("knockdown")->frames), largestKnockdown);
    EXPECT_GE(static_cast<int>(cpu.clips.Find("hitstun_stand")->frames), largestHitstun);
    EXPECT_GE(static_cast<int>(cpu.clips.Find("hitstun_air")->frames), largestAirHitstun);
    EXPECT_GE(static_cast<int>(cpu.clips.Find("blockstun_stand")->frames), largestBlockstun);
    EXPECT_GE(static_cast<int>(cpu.clips.Find("blockstun_crouch")->frames), largestBlockstun);

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
