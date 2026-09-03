// THE PRESENTATION LIBRARY, LINKED WITH NO WINDOW (ROADMAP M3.4a; ADR-019 D9).
//
// This executable exists twice over. Once for what it asserts -- the stateless
// cycle phases floor rather than truncate, so a fighter left of stage centre
// never hands the sampler a negative frame -- and once for what it PROVES BY
// LINKING: UntitledFighterPresentation builds and links into a test with no GL
// context in every CI job, before M3.4b-c put the clip table, the matrices and
// the P4 acceptance tests into it. Five later Done-whens ride on this link; the
// plan's critic asked for it to be shown rather than assumed.
#include <gtest/gtest.h>

#include "cse/presentation/CycleFrame.h"

#include <cstdint>

using cse::presentation::CycleFrame;
using cse::presentation::FloorDiv;
using cse::presentation::WalkCycleFrame;

// Half the stage is negative. `%` truncates toward zero, so the naive index goes
// 2, 1, 0, -1, -2 across centre and hands a sampler a frame that does not exist.
TEST(CycleFrame, FloorModKeepsANegativePositionOnTheCycle) {
    constexpr std::uint32_t n = 8;
    EXPECT_EQ(CycleFrame(0, n), 0u);
    EXPECT_EQ(CycleFrame(7, n), 7u);
    EXPECT_EQ(CycleFrame(8, n), 0u);
    EXPECT_EQ(CycleFrame(-1, n), 7u) << "one step left of zero must be the LAST frame, never -1";
    EXPECT_EQ(CycleFrame(-8, n), 0u);
    EXPECT_EQ(CycleFrame(-9, n), 7u);

    // Walking across stage centre: contiguous frames, no jump and no negative.
    std::uint32_t prev = CycleFrame(-3, n);
    for (std::int64_t phase = -2; phase <= 3; ++phase) {
        const std::uint32_t now = CycleFrame(phase, n);
        EXPECT_LT(now, n);
        EXPECT_EQ(now, (prev + 1) % n) << "phase " << phase << " skipped a frame";
        prev = now;
    }
}

TEST(CycleFrame, AZeroLengthCycleHasExactlyOneFrame) {
    EXPECT_EQ(CycleFrame(0, 0), 0u);
    EXPECT_EQ(CycleFrame(12345, 0), 0u);
    EXPECT_EQ(CycleFrame(-12345, 0), 0u);
}

TEST(CycleFrame, FloorDivRoundsTowardNegativeInfinity) {
    EXPECT_EQ(FloorDiv(7, 2), 3);
    EXPECT_EQ(FloorDiv(-7, 2), -4) << "truncation would say -3";
    EXPECT_EQ(FloorDiv(-8, 2), -4);
    EXPECT_EQ(FloorDiv(-1, 256), -1) << "one sub-unit left of centre is already stride -1";
    EXPECT_EQ(FloorDiv(0, 256), 0);
    EXPECT_EQ(FloorDiv(255, 256), 0);
}

// The walk keys on posX so the feet do not slide: one frame per stride of ground
// covered. Crossing centre from -1 to +1 sub-unit must read n-1, 0, 0.
TEST(CycleFrame, TheWalkCycleCrossesStageCentreWithoutAJump) {
    constexpr std::int32_t  stride = 256;   // one kernel pixel
    constexpr std::uint32_t n      = 6;
    EXPECT_EQ(WalkCycleFrame(-1, stride, n), n - 1);
    EXPECT_EQ(WalkCycleFrame(0, stride, n), 0u);
    EXPECT_EQ(WalkCycleFrame(1, stride, n), 0u);
    EXPECT_EQ(WalkCycleFrame(256, stride, n), 1u);
    EXPECT_EQ(WalkCycleFrame(-256, stride, n), n - 1);
    EXPECT_EQ(WalkCycleFrame(-257, stride, n), n - 2);
    EXPECT_EQ(WalkCycleFrame(1000, 0, n), 0u) << "no stride authored means frame 0, never a divide by zero";
    EXPECT_EQ(WalkCycleFrame(1000, -5, n), 0u);
}


// ============================================================================
// The clip table (ROADMAP M3.4b; ADR-019 D9)
// ============================================================================

#include "cse/presentation/FighterClips.h"

using cse::data::CharacterData;
using cse::data::ClipLength;
using cse::data::Move;
using cse::game::PoseKind;
using cse::presentation::ClipRef;
using cse::presentation::FighterClips;
using cse::presentation::MoveSlot;

namespace {

Move moveOf(const char* id, int startup, int active, int recovery, const char* clip = "") {
    Move m{};
    m.id = id;
    m.startup = startup;
    m.active = active;
    m.recovery = recovery;
    m.anim3dClip = clip;
    return m;
}

// A character as CseData would hand it over after A21/A22 passed: three moves,
// one with a clip override, and every reserved cycle in the sidecar.
CharacterData threeMoveCharacter() {
    CharacterData c{};
    c.anim3dModel = "Characters/fighter_a/model/fighter_a.gltf";
    c.moves = { moveOf("a", 3, 2, 9), moveOf("b", 4, 2, 10), moveOf("c", 5, 3, 12, "c_alt") };
    c.anim3dClips = { { "a", 14 }, { "b", 16 }, { "c_alt", 20 } };
    std::int32_t n = 4;
    for (const char* cycle : cse::data::kReservedCycleNames) c.anim3dClips.push_back({ cycle, n++ });
    c.RebuildIndices();
    return c;
}

} // namespace

// BuildMatchData numbers moves in file order, so a reload that reorders them
// renumbers every slot. The table is keyed by slot for the hot path but BOUND
// by id, so after a reorder each slot still wears its own move's clip.
TEST(FighterClips, AReloadThatReordersMovesRebindsEveryClipByName) {
    const CharacterData c = threeMoveCharacter();
    FighterClips clips;

    clips.Rebuild(c, { { 1, "a" }, { 2, "b" }, { 3, "c" } });
    ASSERT_EQ(clips.MoveClipCount(), 3u);
    ASSERT_NE(clips.Find(PoseKind::Move, 1), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "a");
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->frames, 14u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->name, "c_alt") << "the per-move override names the clip";
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->frames, 20u);

    // the reload: c moved to the front, everything renumbered
    clips.Rebuild(c, { { 1, "c" }, { 2, "a" }, { 3, "b" } });
    ASSERT_EQ(clips.MoveClipCount(), 3u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "c_alt") << "slot 1 is c now; a table keyed by the old slots would say a";
    EXPECT_EQ(clips.Find(PoseKind::Move, 2)->name, "a");
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->name, "b");
    EXPECT_EQ(clips.Find(PoseKind::Move, 3)->frames, 16u);

    // a move this build did not give a slot has no clip; slot 0 never does
    clips.Rebuild(c, { { 0, "a" }, { 1, "b" } });
    EXPECT_EQ(clips.MoveClipCount(), 1u);
    EXPECT_EQ(clips.Find(PoseKind::Move, 0), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Move, 2), nullptr) << "no slot 2 was handed in";
    EXPECT_EQ(clips.Find(PoseKind::Move, 1)->name, "b");
}

// The reserved cycles are keyed by kind alone and are the same fourteen names
// in the same order as the loader's list; None and Move are never cycles.
TEST(FighterClips, EveryReservedCycleIsFoundByItsKind) {
    const CharacterData c = threeMoveCharacter();
    FighterClips clips;
    clips.Rebuild(c, { { 1, "a" } });
    for (std::size_t i = 0; i < cse::data::kReservedCycleNames.size(); ++i) {
        const PoseKind kind = static_cast<PoseKind>(static_cast<int>(PoseKind::Idle) + static_cast<int>(i));
        const ClipRef* r = clips.Find(kind, 0);
        ASSERT_NE(r, nullptr) << cse::data::kReservedCycleNames[i];
        EXPECT_EQ(r->name, cse::data::kReservedCycleNames[i]);
        EXPECT_EQ(r->frames, 4u + static_cast<std::uint32_t>(i));
    }
    EXPECT_EQ(clips.Find(PoseKind::None, 0), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Idle, 7), clips.Find(PoseKind::Idle, 0)) << "a cycle ignores the move slot";
}

// Off by default: a character without engine.anim3d.model has no table, so the
// mode keeps drawing the 2D placeholders and asks nothing of a renderer.
TEST(FighterClips, ACharacterWithNoModelHasAnEmptyTable) {
    CharacterData c = threeMoveCharacter();
    c.anim3dModel.clear();
    FighterClips clips;
    clips.Rebuild(c, { { 1, "a" }, { 2, "b" }, { 3, "c" } });
    EXPECT_TRUE(clips.Empty());
    EXPECT_EQ(clips.Find(PoseKind::Move, 1), nullptr);
    EXPECT_EQ(clips.Find(PoseKind::Idle, 0), nullptr);
}


// ============================================================================
// The reconciler's arithmetic (ROADMAP M3.4c; ADR-019 D3, D4, D5)
// ============================================================================

#include "cse/presentation/FightPresentation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using cse::kernel::GameState;
using cse::kernel::MatchData;
using cse::presentation::CameraFrame;
using cse::presentation::CameraFraming;
using cse::presentation::ComposeFrame;
using cse::presentation::FightLook;
using cse::presentation::FightLookIsConsistent;
using cse::presentation::FrameComposition;
using cse::presentation::kViewHalfWidthPx;
using cse::presentation::ParseFightLook;
using cse::presentation::WorldPx;

namespace {

constexpr std::int32_t subPx(std::int32_t px) { return px * cse::kernel::kSubUnitsPerPixel; }

// Two standing fighters, nothing else: the smallest state SelectPose accepts.
GameState twoFighters(std::int32_t x0, std::int32_t x1, std::uint8_t facing0, std::uint8_t facing1) {
    GameState s{};
    s.fighterCount = 2;
    s.tick = 17;
    s.p[0].active = 1; s.p[0].posX = x0; s.p[0].posY = 0; s.p[0].facing = facing0;
    s.p[1].active = 1; s.p[1].posX = x1; s.p[1].posY = 0; s.p[1].facing = facing1;
    return s;
}

// A table with every reserved cycle so a standing fighter resolves to `idle`.
cse::presentation::FighterClips cyclesOnly() {
    cse::data::CharacterData c{};
    c.anim3dModel = "m.gltf";
    std::int32_t n = 6;
    for (const char* cycle : cse::data::kReservedCycleNames) c.anim3dClips.push_back({ cycle, n++ });
    c.RebuildIndices();
    cse::presentation::FighterClips clips;
    clips.Rebuild(c, {});
    return clips;
}

glm::vec2 pixelOf(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& world, int W, int H) {
    const glm::vec4 c = proj * view * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(c) / c.w;
    return { (ndc.x + 1.0f) * 0.5f * static_cast<float>(W), (ndc.y + 1.0f) * 0.5f * static_cast<float>(H) };
}

// The committed look, found by walking up from the working directory, so the
// test proves the FILE that ships and not a copy of its numbers.
std::string committedFightLookText() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = here / "Games" / "UntitledFighter" / "Assets" / "UntitledFighter" / "fight_look.json";
        if (fs::exists(candidate)) {
            std::ifstream in(candidate, std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            return ss.str();
        }
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return {};
}

} // namespace

// ADR-019 D5: facing left is a 180 degree yaw with a positive determinant,
// never a negative scale. A mirror would flip every normal and the outline.
TEST(FightPresentation, FacingLeftIsAYawWithPositiveDeterminantNeverANegativeScale) {
    const MatchData data{};
    const GameState state = twoFighters(subPx(-40), subPx(40), /*facing0*/ 0, /*facing1*/ 1);
    const FrameComposition f = ComposeFrame(data, state, cyclesOnly(), FightLook{}, 0, 0.0f, 1280, 720);

    EXPECT_FLOAT_EQ(f.fighter[0].yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(f.fighter[1].yawDeg, 180.0f);
    for (int slot = 0; slot < 2; ++slot) {
        const glm::mat3 linear(f.fighter[slot].model);
        EXPECT_NEAR(glm::determinant(linear), 1.0f, 1e-5f) << "slot " << slot << " is not a proper rotation";
        // the columns stay unit length: no scale anywhere
        for (int c = 0; c < 3; ++c) EXPECT_NEAR(glm::length(linear[c]), 1.0f, 1e-5f);
    }
    // and the left-facing body's +X points down -X: turned, not mirrored
    const glm::vec3 xAxis = glm::vec3(f.fighter[1].model * glm::vec4(1, 0, 0, 0));
    EXPECT_NEAR(xAxis.x, -1.0f, 1e-5f);
    EXPECT_NEAR(xAxis.z, 0.0f, 1e-5f);
    // the up axis is untouched by the yaw
    const glm::vec3 yAxis = glm::vec3(f.fighter[1].model * glm::vec4(0, 1, 0, 0));
    EXPECT_NEAR(yAxis.y, 1.0f, 1e-5f);
}

// One world unit is one kernel pixel: the translation is pos / 256, for both
// facings, and the slot's z plane rides in the third component.
TEST(FightPresentation, TheModelMatrixTranslationIsPosOver256ForBothFacings) {
    const MatchData data{};
    GameState state = twoFighters(12345, -7000, 0, 1);
    state.p[0].posY = 777;
    state.p[1].posY = 256 * 30;
    FightLook look{};
    look.slotZ[0] = 0.0f; look.slotZ[1] = 0.5f;
    const FrameComposition f = ComposeFrame(data, state, cyclesOnly(), look, 0, 0.0f, 1280, 720);

    const glm::vec3 t0(f.fighter[0].model[3]);
    const glm::vec3 t1(f.fighter[1].model[3]);
    EXPECT_FLOAT_EQ(t0.x, 12345.0f / 256.0f);
    EXPECT_FLOAT_EQ(t0.y, 777.0f / 256.0f);
    EXPECT_FLOAT_EQ(t0.z, 0.0f);
    EXPECT_FLOAT_EQ(t1.x, -7000.0f / 256.0f) << "facing left must not move the origin";
    EXPECT_FLOAT_EQ(t1.y, 30.0f);
    EXPECT_FLOAT_EQ(t1.z, 0.5f);
    EXPECT_EQ(f.fighter[0].position, t0);
    EXPECT_EQ(f.fighter[1].position, t1);
}

// ADR-019 D4: the scene camera is orthographic and driven by the same framing
// as the box overlay, so a fighter's origin lands on the same pixel through
// both -- within half a pixel, across the stage, at every window shape.
TEST(FightPresentation, TheSceneCameraAndTheFightCameraProjectTheFighterOriginToTheSamePixel) {
    const MatchData data{};
    const int viewports[][2] = { { 1280, 720 }, { 1920, 1080 }, { 800, 600 }, { 1000, 1000 }, { 640, 1136 } };
    const std::int32_t stageHalf = cse::kernel::kStageHalfWidthSub;
    float previous = 0.0f;
    int checked = 0;
    for (const auto& vp : viewports) {
        for (std::int32_t px = -470; px <= 470; px += 47) {
            const GameState state = twoFighters(subPx(px), subPx(px + 60 > 470 ? px - 60 : px + 60), 0, 1);
            const FrameComposition f = ComposeFrame(data, state, cyclesOnly(), FightLook{}, stageHalf, previous, vp[0], vp[1]);
            previous = f.camera.framing.centreX;
            for (int slot = 0; slot < 2; ++slot) {
                const glm::vec3 origin(WorldPx(state.p[slot].posX), WorldPx(state.p[slot].posY), 0.0f);
                const glm::vec2 scenePx = pixelOf(cse::presentation::SceneCameraProjection(f.camera, vp[0], vp[1]),
                                                  cse::presentation::SceneCameraView(f.camera), origin, vp[0], vp[1]);
                const glm::vec2 overlayPx = pixelOf(cse::presentation::OverlayProjection(f.camera.framing, vp[0], vp[1]),
                                                    cse::presentation::OverlayView(f.camera.framing), origin, vp[0], vp[1]);
                EXPECT_LE(std::fabs(scenePx.x - overlayPx.x), 0.5f) << vp[0] << "x" << vp[1] << " px " << px << " slot " << slot;
                EXPECT_LE(std::fabs(scenePx.y - overlayPx.y), 0.5f) << vp[0] << "x" << vp[1] << " px " << px << " slot " << slot;
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 100);
    // and the premise the agreement rests on: 200 px of world per half-screen
    const FrameComposition f = ComposeFrame(data, twoFighters(0, subPx(60), 0, 1), cyclesOnly(), FightLook{}, stageHalf, 0.0f, 1280, 720);
    EXPECT_FLOAT_EQ(f.camera.framing.halfWidthPx, 200.0f);
    EXPECT_FLOAT_EQ(f.camera.orthoHalfHeight * (1280.0f / 720.0f), 200.0f);
}

// ADR-019 D5: the committed fight_look.json keeps the back wall inside the
// shadow range and the far plane, and the fighters past the near plane. The
// FILE is proved, not a copy of its numbers, and a broken look is refused
// with the broken rule named.
TEST(FightPresentation, TheBackWallIsInsideTheShadowRange) {
    const std::string text = committedFightLookText();
    ASSERT_FALSE(text.empty()) << "Games/UntitledFighter/Assets/UntitledFighter/fight_look.json not found from " << std::filesystem::current_path();
    FightLook look{};
    std::string error;
    ASSERT_TRUE(ParseFightLook(text, look, error)) << error;
    EXPECT_GE(look.shadowDistancePx, look.cameraDistancePx + look.roomDepthPx);
    EXPECT_GT(look.farPx, look.cameraDistancePx + look.roomDepthPx);
    EXPECT_LT(look.nearPx, look.cameraDistancePx - look.fighterDepthPx);
    EXPECT_GE(look.cameraPriority, 100) << "the shipped scenes author priority 0; the fight camera must outrank them";
    EXPECT_TRUE(FightLookIsConsistent(look));

    // the defaults are a consistent look too, so a missing file still fights
    EXPECT_TRUE(FightLookIsConsistent(FightLook{}));

    // a wall pushed past the shadow range is refused, naming the rule
    FightLook broken = look;
    broken.roomDepthPx = broken.shadowDistancePx;
    std::string why;
    EXPECT_FALSE(FightLookIsConsistent(broken, &why));
    EXPECT_NE(why.find("shadow"), std::string::npos) << why;

    // an unknown key is refused by name, like the character file
    FightLook ignored{};
    EXPECT_FALSE(ParseFightLook(R"({"shadow_distance_px": 1200, "sun_direction": [0,-1,0]})", ignored, error));
    EXPECT_NE(error.find("sun_direction"), std::string::npos) << error;
}

// Two fighters on one mesh must read as two people: each slot has its own
// toon tint and its own z plane, so overlapping bodies never z-fight.
TEST(FightPresentation, EachSlotHasItsOwnToonMaterialAndZPlane) {
    const MatchData data{};
    const GameState state = twoFighters(subPx(-10), subPx(10), 0, 1);
    FightLook look{};
    std::string error;
    ASSERT_TRUE(ParseFightLook(committedFightLookText(), look, error)) << error;
    const FrameComposition f = ComposeFrame(data, state, cyclesOnly(), look, 0, 0.0f, 1280, 720);

    EXPECT_TRUE(f.fighter[0].toon);
    EXPECT_TRUE(f.fighter[1].toon);
    EXPECT_NE(f.fighter[0].tint, f.fighter[1].tint);
    EXPECT_NE(f.fighter[0].z, f.fighter[1].z);
    EXPECT_FLOAT_EQ(f.fighter[0].z, look.slotZ[0]);
    EXPECT_FLOAT_EQ(f.fighter[1].z, look.slotZ[1]);
    EXPECT_FLOAT_EQ(f.fighter[0].model[3].z, look.slotZ[0]) << "the z plane is IN the matrix, not beside it";
    EXPECT_FLOAT_EQ(f.fighter[1].model[3].z, look.slotZ[1]);
}

// T0 (ADR-019 D3): composing a frame reads the simulation and writes nothing
// back -- not one byte of GameState or MatchData.
TEST(FightPresentation, ReconcilingAFrameLeavesTheGameStateBytesUntouched) {
    MatchData data{};
    data.p[0].walkSpeedSub = subPx(2);
    GameState state = twoFighters(subPx(-30), subPx(50), 0, 1);
    state.p[0].hitstun = 5;
    state.p[1].knockdown = 12;

    unsigned char stateBefore[sizeof(GameState)];
    unsigned char dataBefore[sizeof(MatchData)];
    std::memcpy(stateBefore, &state, sizeof(GameState));
    std::memcpy(dataBefore, &data, sizeof(MatchData));

    const cse::presentation::FighterClips clips = cyclesOnly();
    for (int i = 0; i < 3; ++i)
        (void)ComposeFrame(data, state, clips, FightLook{}, cse::kernel::kStageHalfWidthSub, 12.5f, 1280, 720);

    EXPECT_EQ(std::memcmp(stateBefore, &state, sizeof(GameState)), 0) << "ComposeFrame wrote into GameState";
    EXPECT_EQ(std::memcmp(dataBefore, &data, sizeof(MatchData)), 0) << "ComposeFrame wrote into MatchData";
}

// The countdown clips are indexed from the end (ADR-019 D2): the last frame
// lands as the counter reaches zero, whatever the authored counter was.
TEST(FightPresentation, CountdownClipsLandOnTheirLastFrameAsTheCounterReachesZero) {
    cse::presentation::ClipRef clip{ "hitstun_stand", 10 };
    cse::game::PoseRequest r{};
    r.kind = cse::game::PoseKind::HitstunStand;
    r.remaining = 10; EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 0u);
    r.remaining = 1;  EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 9u);
    r.remaining = 0;  EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 9u);
    r.remaining = 25; EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 0u) << "a counter longer than the clip holds frame 0";
    r.kind = cse::game::PoseKind::Move; r.frame = 3;   EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 3u);
    r.frame = 40;                                       EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 9u) << "a move frame past the clip clamps";
    r.kind = cse::game::PoseKind::Idle; r.tick = 23;    EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, 0), 3u);
    r.kind = cse::game::PoseKind::WalkFwd; r.posXSub = subPx(7); EXPECT_EQ(cse::presentation::ClipFrameFor(r, clip, subPx(2)), 3u) << "one frame per tick of walking: 7 px / 2 px per tick = frame 3";
}


// ============================================================================
// Overlay modes for validation over the mesh (ROADMAP M3.4e)
// ============================================================================

// Three views of one frame, cycled from the default: boxes over the mesh,
// boxes over a translucent mesh, mesh only, and round again. Each says what is
// drawn; none changes a tick. The 2D ruler draws only with the boxes on and no
// model on screen, because the room's grid is the ruler.
TEST(FightPresentation, OverlayModeCyclesThreeStatesAndStartsWithBoxes) {
    using cse::presentation::kDefaultOverlayMode;
    using cse::presentation::NextOverlayMode;
    using cse::presentation::OverlayLook;
    using cse::presentation::OverlayLookFor;
    using cse::presentation::OverlayMode;
    using cse::presentation::OverlayModeName;

    EXPECT_EQ(kDefaultOverlayMode, OverlayMode::BoxesOverMesh) << "the default is the boxes over the body";
    OverlayMode m = kDefaultOverlayMode;
    m = NextOverlayMode(m); EXPECT_EQ(m, OverlayMode::BoxesOverTranslucentMesh);
    m = NextOverlayMode(m); EXPECT_EQ(m, OverlayMode::MeshOnly);
    m = NextOverlayMode(m); EXPECT_EQ(m, OverlayMode::BoxesOverMesh) << "three states, then round";

    // with a model on screen
    const OverlayLook boxes = OverlayLookFor(OverlayMode::BoxesOverMesh, /*modelOnScreen*/ true);
    EXPECT_TRUE(boxes.drawBoxes);
    EXPECT_FLOAT_EQ(boxes.meshOpacity, 1.0f);
    EXPECT_FALSE(boxes.drawRuler) << "the room's grid is the ruler once there is a model";

    const OverlayLook through = OverlayLookFor(OverlayMode::BoxesOverTranslucentMesh, true);
    EXPECT_TRUE(through.drawBoxes);
    EXPECT_LT(through.meshOpacity, 1.0f) << "translucent means the material blends";
    EXPECT_GT(through.meshOpacity, 0.0f) << "but the silhouette must survive";
    EXPECT_FALSE(through.drawRuler);

    const OverlayLook mesh = OverlayLookFor(OverlayMode::MeshOnly, true);
    EXPECT_FALSE(mesh.drawBoxes);
    EXPECT_FLOAT_EQ(mesh.meshOpacity, 1.0f) << "mesh only is the opaque body, nothing over it";
    EXPECT_FALSE(mesh.drawRuler);

    // over the 2D placeholders (no model): the ruler follows the boxes
    EXPECT_TRUE(OverlayLookFor(OverlayMode::BoxesOverMesh, false).drawRuler);
    EXPECT_TRUE(OverlayLookFor(OverlayMode::BoxesOverTranslucentMesh, false).drawRuler);
    EXPECT_FALSE(OverlayLookFor(OverlayMode::MeshOnly, false).drawRuler) << "no boxes, no ruler";
    EXPECT_FLOAT_EQ(OverlayLookFor(OverlayMode::BoxesOverTranslucentMesh, false).meshOpacity,
                    cse::presentation::kTranslucentMeshOpacity);

    // three distinct names for the HUD
    EXPECT_STRNE(OverlayModeName(OverlayMode::BoxesOverMesh), OverlayModeName(OverlayMode::BoxesOverTranslucentMesh));
    EXPECT_STRNE(OverlayModeName(OverlayMode::BoxesOverTranslucentMesh), OverlayModeName(OverlayMode::MeshOnly));
    EXPECT_STRNE(OverlayModeName(OverlayMode::MeshOnly), OverlayModeName(OverlayMode::BoxesOverMesh));
}
