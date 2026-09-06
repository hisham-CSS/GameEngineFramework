// THE PLACEHOLDER RIG, AS COMMITTED (ROADMAP M3.3b; ADR-019 D5, D6, D10).
//
// make_mannequin.py runs on the developer's Blender, never in CI, so what CI can
// hold is the exported BYTES: fighter_a.gltf beside its rig_manifest.json,
// rig_bones.json, fighter_a.clips.json and CREDITS.md, staged with the title's
// content to Exported/Characters/fighter_a/model/. Model::Decode is GL-free, so
// every assertion here runs headless in all three CI jobs.
//
// The three Done-when properties: the rig fits the palette and the influence
// cap the engine imposes (Skeleton.h); the body stands exactly 60 units tall
// with its feet on y = 0, one unit being one kernel pixel; and the decoded
// skeleton is the manifest bone for bone -- which is the seam every clip
// (M3.3c) and the modeled body (M3.3e) are written against.
#include <gtest/gtest.h>

#include "Engine.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using json = nlohmann::json;

namespace {

// The STAGED copy, the way the runtime reads it, found by walking up from the
// test's working directory; the source copy under Games/ is the fallback for a
// shell run from the tree. Every test ASSERTs on what it finds, so neither path
// can pass vacuously.
std::filesystem::path modelDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        for (const fs::path rel : { fs::path("Exported/Characters/fighter_a/model"),
                                    fs::path("Games/UntitledFighter/Assets/Characters/fighter_a/model") }) {
            const fs::path candidate = here / rel;
            if (fs::exists(candidate / "fighter_a.gltf")) return candidate;
        }
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return fs::path("Exported/Characters/fighter_a/model");
}

json readJson(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p.string();
    json j = json::parse(in, nullptr, false);
    EXPECT_FALSE(j.is_discarded()) << p.string() << " is not valid JSON";
    return j;
}

} // namespace

// Engine/src/anim/Skeleton.h caps the palette at kMaxSkeletonJoints and the
// influences at kMaxJointInfluences; Model::Decode refuses a rig over the first
// and silently trims to the second. The committed export must need neither: the
// generator drops the control layer, the breast bones and every fifth
// influence BEFORE the exporter runs, so the file already obeys both caps.
TEST(PlaceholderRig, FitsThePaletteAndTheFourInfluenceCap) {
    const auto dir = modelDir();
    const ModelCPUData cpu = Model::Decode((dir / "fighter_a.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    ASSERT_FALSE(cpu.skeleton.Empty()) << "the mannequin has no skeleton";
    EXPECT_LE(static_cast<int>(cpu.skeleton.joints.size()), kMaxSkeletonJoints);
    EXPECT_GE(cpu.skeleton.joints.size(), 20u) << "a humanoid deform set is a few dozen joints, not a paddle";
    EXPECT_TRUE(cpu.skeleton.ParentsPrecedeChildren());

    // The cap at EXPORT, read from the file itself: one skin, its joint list the
    // skeleton's, and a single JOINTS_0/WEIGHTS_0 pair per primitive -- a fifth
    // influence would need JOINTS_1, which the pinned exporter never writes.
    const json gltf = readJson(dir / "fighter_a.gltf");
    ASSERT_TRUE(gltf.contains("skins") && gltf["skins"].size() == 1) << "exactly one skin";
    EXPECT_EQ(gltf["skins"][0]["joints"].size(), cpu.skeleton.joints.size());
    EXPECT_LE(static_cast<int>(gltf["skins"][0]["joints"].size()), kMaxSkeletonJoints);
    ASSERT_TRUE(gltf.contains("meshes") && !gltf["meshes"].empty());
    int primitives = 0;
    for (const json& mesh : gltf["meshes"])
        for (const json& prim : mesh["primitives"]) {
            ++primitives;
            const json& a = prim["attributes"];
            EXPECT_TRUE(a.contains("JOINTS_0") && a.contains("WEIGHTS_0")) << "an unskinned primitive on the mannequin";
            EXPECT_FALSE(a.contains("JOINTS_1") || a.contains("WEIGHTS_1"))
                << "a second influence set: more than " << kMaxJointInfluences << " influences reached the file";
            if (a.contains("WEIGHTS_0"))
                EXPECT_EQ(gltf["accessors"][a["WEIGHTS_0"].get<int>()]["type"], "VEC4");
        }
    EXPECT_GE(primitives, 1);
    for (const auto& m : cpu.meshes)
        EXPECT_FALSE(m.vertices.empty());
}

// ADR-019 D5: 1 Blender unit = 1 kernel pixel, the fighter is 60 units tall
// with its feet at y = 0, and the exporter's +Y-up conversion is what puts
// Blender's z on the engine's y. The rest mesh is asserted directly; the pose
// bounds (every frame of the 2-frame idle) must stay within a breath of it.
TEST(PlaceholderRig, StandsSixtyPixelsTallAtRest) {
    const auto dir = modelDir();
    const ModelCPUData cpu = Model::Decode((dir / "fighter_a.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
    std::size_t count = 0;
    for (const auto& m : cpu.meshes)
        for (const auto& v : m.vertices) {
            minY = std::min(minY, v.Position.y); maxY = std::max(maxY, v.Position.y);
            minX = std::min(minX, v.Position.x); maxX = std::max(maxX, v.Position.x);
            ++count;
        }
    ASSERT_GT(count, 0u);
    EXPECT_NEAR(minY, 0.0f, 0.01f) << "the feet are not on the floor";
    EXPECT_NEAR(maxY, 60.0f, 0.01f) << "the crown is not at 60 units (fighter_a's height_px)";
    // a body, not a pole: shoulders and feet give it width, symmetric about x = 0
    EXPECT_GT(maxX - minX, 10.0f);
    EXPECT_NEAR(maxX, -minX, 1.0f) << "the mannequin is not symmetric about its origin";
    // The pose bounds are the CULLING box: per-joint rest boxes swept corner by
    // corner through every clip frame (M3.2d), conservative by design -- a
    // 1.5-degree breath widens them by a couple of units. They must contain
    // the rest body and stay within a body's margin of it; they are not the
    // height claim, the vertices above are.
    ASSERT_TRUE(cpu.poseBounds.valid);
    EXPECT_LE(cpu.poseBounds.min.y, 0.001f) << "the culling box does not reach the feet";
    EXPECT_GE(cpu.poseBounds.max.y, 59.999f) << "the culling box does not reach the crown";
    EXPECT_GT(cpu.poseBounds.min.y, -10.0f) << "the culling box is absurdly loose below the floor";
    EXPECT_LT(cpu.poseBounds.max.y, 70.0f) << "the culling box is absurdly loose above the crown";
}

// The seam: rig_manifest.json is the deform hierarchy the exporter was held to
// (common.enforce_rig_manifest), and the decoded skeleton must be that list bone
// for bone -- same names, same parents, nothing extra -- because every clip and
// the modeled body are written against it. rig_bones.json's semantic names must
// all resolve into it, or the pose library would name a bone that is not there.
TEST(PlaceholderRig, MatchesItsRigManifestBoneForBone) {
    const auto dir = modelDir();
    const ModelCPUData cpu = Model::Decode((dir / "fighter_a.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    const json manifest = readJson(dir / "rig_manifest.json");
    ASSERT_TRUE(manifest.contains("bones") && manifest["bones"].is_array());
    ASSERT_EQ(manifest["bones"].size(), cpu.skeleton.joints.size())
        << "the skeleton has a different bone count than the manifest";

    for (const json& b : manifest["bones"]) {
        const std::string name = b["name"].get<std::string>();
        const int idx = cpu.skeleton.Find(name);
        ASSERT_GE(idx, 0) << "manifest bone `" << name << "` is not in the skeleton";
        const Skeleton::Joint& j = cpu.skeleton.joints[static_cast<std::size_t>(idx)];
        if (b["parent"].is_null()) {
            EXPECT_EQ(j.parent, -1) << name << " should be a root";
        }
        else {
            ASSERT_GE(j.parent, 0) << name << " should have parent " << b["parent"];
            EXPECT_EQ(cpu.skeleton.joints[static_cast<std::size_t>(j.parent)].name, b["parent"].get<std::string>())
                << name << " has the wrong parent";
        }
    }
    // every skeleton joint is a manifest bone too (no un-pinned joint slipped in)
    for (const auto& j : cpu.skeleton.joints) {
        bool listed = false;
        for (const json& b : manifest["bones"]) if (b["name"] == j.name) listed = true;
        EXPECT_TRUE(listed) << "skeleton joint `" << j.name << "` is not in the manifest";
    }
    EXPECT_TRUE(cpu.skeleton.ParentsPrecedeChildren());

    const json bones = readJson(dir / "rig_bones.json");
    ASSERT_TRUE(bones.contains("bones") && bones["bones"].is_object());
    for (const auto& [semantic, deform] : bones["bones"].items()) {
        EXPECT_GE(cpu.skeleton.Find(deform.get<std::string>()), 0)
            << "rig_bones.json maps `" << semantic << "` to `" << deform << "`, which the skeleton lacks";
        EXPECT_NE(semantic.rfind("DEF-", 0), 0u) << "a semantic name that is a Rigify name: " << semantic;
    }
    for (const char* need : { "hips", "chest", "head", "l_wrist", "r_wrist", "l_foot", "r_foot" })
        EXPECT_TRUE(bones["bones"].contains(need)) << "rig_bones.json lacks `" << need << "`";
}

// The sidecar is what the character loader will assert against (A21/A22) once
// fighter_a.json names this model in M3.3c; today it must say exactly the one
// clip the generator keyed, at exactly two frames, and the decoded clip agrees.
TEST(PlaceholderRig, TheSidecarNamesTheOneIdleClipAtTwoFrames) {
    const auto dir = modelDir();
    const json sidecar = readJson(dir / "fighter_a.clips.json");
    ASSERT_TRUE(sidecar.is_object());
    EXPECT_EQ(sidecar.size(), 1u);
    ASSERT_TRUE(sidecar.contains("idle"));
    EXPECT_EQ(sidecar["idle"].get<int>(), 2);

    const ModelCPUData cpu = Model::Decode((dir / "fighter_a.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    const Clip* idle = cpu.clips.Find("idle");
    ASSERT_NE(idle, nullptr);
    EXPECT_EQ(idle->frames, 2u);
    EXPECT_EQ(idle->joints, cpu.skeleton.joints.size());
    EXPECT_TRUE(std::filesystem::exists(dir / "CREDITS.md")) << "ADR-019 D10: a CREDITS.md beside every committed model";
}
