// THE TRAINING ROOM AND THE LICENCE RULE (ROADMAP M3.5a; ADR-019 D5, D10).
//
// The room is the kernel's stage, drawn: its floor spans exactly the width the
// wall clamp allows (kStageHalfWidthSub), its heavy lines fall on the reach
// unit the character file authors, and every model that ships carries a
// CREDITS.md beside it. All of it read from the committed, staged bytes with
// Model::Decode (GL-free); CI never runs Blender.
#include <gtest/gtest.h>

#include "Engine.h"

#include <cse/kernel/GameState.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// The staged Exported/ tree (test_runtime_deps merges both content roots into
// it), found by walking up from the working directory; every test ASSERTs on
// what it finds, so neither path passes vacuously.
fs::path stagedExported() {
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(here / "Exported" / "UntitledFighter" / "Stage" / "training_room.gltf")) return here / "Exported";
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported";
}

// The repository, by the one file this test shares with the Player install.
fs::path repoRoot() {
    fs::path here = fs::current_path();
    for (int i = 0; i < 10; ++i) {
        if (fs::exists(here / "Player" / "unshipped_assets.txt")) return here;
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return ".";
}

json readJson(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p.string();
    json j = json::parse(in, nullptr, false);
    EXPECT_FALSE(j.is_discarded()) << p.string() << " is not valid JSON";
    return j;
}

const ModelCPUData::MeshData* meshOf(const ModelCPUData& cpu, const char* material) {
    for (const auto& m : cpu.meshes)
        if (m.materialIndex >= 0 && cpu.materials[static_cast<std::size_t>(m.materialIndex)].name == material)
            return &m;
    return nullptr;
}

} // namespace

// The floor is x in [-W, W] at y = 0 with W = kStageHalfWidthSub / 256 -- the
// side walls stand exactly where the kernel's clamp stops a body -- and it is
// as deep as the room fight_look.json set the camera and the shadows up for.
// stage_dims.json is the generator's input; this is what pins it to the kernel.
TEST(StageAsset, TheFloorSpansExactlyTheKernelsStage) {
    const fs::path stage = stagedExported() / "UntitledFighter" / "Stage";
    const json dims = readJson(stage / "stage_dims.json");
    const json look = readJson(stagedExported() / "UntitledFighter" / "fight_look.json");
    ASSERT_TRUE(dims.contains("half_width_px"));
    EXPECT_EQ(dims["half_width_px"].get<std::int64_t>() * cse::kernel::kSubUnitsPerPixel, cse::kernel::kStageHalfWidthSub)
        << "stage_dims.json's half width is not the kernel's kStageHalfWidthSub";
    const float W = static_cast<float>(dims["half_width_px"].get<double>());
    const float D = look["room_depth_px"].get<float>();
    const float F = look.value("fighter_depth_px", 0.0f);
    const float H = static_cast<float>(dims["wall_height_px"].get<double>());

    const ModelCPUData cpu = Model::Decode((stage / "training_room.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    ASSERT_TRUE(cpu.skeleton.Empty()) << "the room is static";
    ASSERT_TRUE(cpu.clips.Empty()) << "the room carries no clips";

    const auto* floor = meshOf(cpu, "floor");
    ASSERT_NE(floor, nullptr) << "no mesh wears the `floor` material";
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& v : floor->vertices) {
        EXPECT_NEAR(v.Position.y, 0.0f, 1e-4f) << "the floor's top is not at y = 0";
        minX = std::min(minX, v.Position.x); maxX = std::max(maxX, v.Position.x);
        minZ = std::min(minZ, v.Position.z); maxZ = std::max(maxZ, v.Position.z);
    }
    EXPECT_NEAR(minX, -W, 1e-3f) << "the floor's left edge is not at -kStageHalfWidth";
    EXPECT_NEAR(maxX,  W, 1e-3f) << "the floor's right edge is not at +kStageHalfWidth";
    EXPECT_NEAR(minZ, -D, 1e-3f) << "the floor does not reach the back wall fight_look.json set the camera up for";
    EXPECT_NEAR(maxZ,  F, 1e-3f) << "the floor does not reach the fighters' depth";

    const auto* wall = meshOf(cpu, "wall");
    ASSERT_NE(wall, nullptr) << "no mesh wears the `wall` material";
    float wMinX = 1e9f, wMaxX = -1e9f, wMaxY = -1e9f;
    for (const auto& v : wall->vertices) {
        wMinX = std::min(wMinX, v.Position.x); wMaxX = std::max(wMaxX, v.Position.x); wMaxY = std::max(wMaxY, v.Position.y);
    }
    EXPECT_NEAR(wMinX, -W, 1e-3f) << "the left wall is not exactly at -kStageHalfWidth";
    EXPECT_NEAR(wMaxX,  W, 1e-3f) << "the right wall is not exactly at +kStageHalfWidth";
    EXPECT_NEAR(wMaxY,  H, 1e-3f) << "the walls are not stage_dims.json's height";
}

// The heavy lines are the ruler: one every reach unit, the unit the character
// file authors (engine.units.pixels_per_reach_unit), so a move's reach can be
// counted off the floor. Found by the material name the file carries (M3.2a):
// on the floor plane, every heavy-line vertex that is not a cross line's end
// sits within a half line-width of a multiple of the unit, and every multiple
// inside the stage has a line.
TEST(StageAsset, HeavyLinesFallOnReachUnits) {
    const fs::path stage = stagedExported() / "UntitledFighter" / "Stage";
    const json dims = readJson(stage / "stage_dims.json");
    const json character = readJson(stagedExported() / "Characters" / "fighter_a.json");
    const double unit = character["engine"]["units"]["pixels_per_reach_unit"].get<double>();
    ASSERT_GT(unit, 0.0);
    EXPECT_EQ(dims["heavy_every_px"].get<double>(), unit) << "stage_dims.json's heavy spacing is not the character's reach unit";
    const double W = dims["half_width_px"].get<double>();

    const ModelCPUData cpu = Model::Decode((stage / "training_room.gltf").string());
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    const auto* heavy = meshOf(cpu, "grid_heavy");
    ASSERT_NE(heavy, nullptr) << "no mesh wears the `grid_heavy` material";
    ASSERT_NE(meshOf(cpu, "grid_light"), nullptr) << "no mesh wears the `grid_light` material";

    std::set<long> multiples;
    int floorLineVertices = 0;
    for (const auto& v : heavy->vertices) {
        if (std::abs(v.Position.y) > 0.5f) continue;                 // a wall line
        if (std::abs(std::abs(v.Position.x) - W) < 1.0f) continue;   // a cross line's end, on the wall
        const double k = std::round(v.Position.x / unit);
        EXPECT_LE(std::abs(v.Position.x - k * unit), 1.0)
            << "a heavy line at x = " << v.Position.x << " is not on a multiple of the reach unit " << unit;
        multiples.insert(static_cast<long>(k));
        ++floorLineVertices;
    }
    ASSERT_GT(floorLineVertices, 0) << "no heavy line lies on the floor";
    for (long k = static_cast<long>(-std::floor(W / unit)); k <= static_cast<long>(std::floor(W / unit)); ++k) {
        if (k == 0) continue;                                        // x = 0 is the centre line
        EXPECT_TRUE(multiples.count(k)) << "no heavy line at " << k * unit << " px";
    }
    ASSERT_NE(meshOf(cpu, "centre_line"), nullptr) << "no mesh wears the `centre_line` material";
}

// ADR-019 D10: every model that ships carries its licence beside it. The
// shipped set is the staged Exported/ tree minus Player/unshipped_assets.txt --
// the one list both install routes in Player/CMakeLists.txt read -- so a file
// this test skips is a file no bundle contains, and a file it checks ships.
TEST(Assets, EveryModelHasALicenceBesideIt) {
    const fs::path exported = stagedExported();
    ASSERT_TRUE(fs::exists(exported / "Characters" / "fighter_a.json")) << exported.string() << " is not the staged tree";
    std::set<std::string> unshipped;
    {
        std::ifstream in(repoRoot() / "Player" / "unshipped_assets.txt");
        ASSERT_TRUE(in.good()) << "Player/unshipped_assets.txt not found above " << fs::current_path().string();
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            unshipped.insert(line);
        }
    }
    ASSERT_TRUE(unshipped.count("Model/backpack.obj")) << "the unlicensed sample backpack is not on the unshipped list";

    int checked = 0, skipped = 0;
    std::vector<std::string> missing;
    for (const auto& entry : fs::recursive_directory_iterator(exported)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".obj" && ext != ".gltf" && ext != ".glb") continue;
        const std::string rel = fs::relative(entry.path(), exported).generic_string();
        if (unshipped.count(rel)) { ++skipped; continue; }
        ++checked;
        if (!fs::exists(entry.path().parent_path() / "CREDITS.md")) missing.push_back(rel);
    }
    EXPECT_GE(checked, 3) << "the staged tree holds fewer models than ship (plane.obj, fighter_a.gltf, training_room.gltf)";
    EXPECT_GE(skipped, 1) << "the unshipped backpack was not in the staged tree to skip";
    EXPECT_TRUE(missing.empty()) << "model(s) shipping without a CREDITS.md beside them: " << [&] {
        std::string s; for (const auto& m : missing) s += m + " "; return s; }();
}
