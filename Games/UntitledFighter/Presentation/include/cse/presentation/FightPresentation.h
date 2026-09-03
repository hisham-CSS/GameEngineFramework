// THE RECONCILER'S ARITHMETIC (ROADMAP M3.4c; ADR-019 D3, D4, D5, D9).
//
// Everything that turns one GameState into the numbers a scene needs -- which
// clip and frame each fighter wears, where its model matrix puts it, which
// way it faces, what tint and depth its slot owns, and where the orthographic
// camera stands -- computed here as a pure function and handed to the mode,
// which owns the entities and writes them. Nothing here holds state between
// frames (ADR-019 D3, T0), nothing opens a file except ParseFightLook on a
// string the caller read, and nothing knows what a renderer is: this library
// links CseGame and two header-only libraries, glm for the matrices and
// nlohmann for the look file, and Presentation/CMakeLists.txt refuses more.
//
// WHY THE CAMERA'S PURE FUNCTION LIVES HERE NOW. FightCamera (FightView.cpp)
// framed the 2D box overlay; ADR-019 D4 makes the scene camera orthographic
// and DRIVEN BY THE SAME FUNCTION, so the scene camera and the overlay project
// a fighter's origin to the same pixel. Two copies of that function would be
// two cameras. The framing moved here and FightView adapts it to a Camera2D.
#pragma once

#include "cse/presentation/CycleFrame.h"
#include "cse/presentation/FighterClips.h"
#include "cse/game/PoseSelect.h"
#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace cse::presentation {

// --- Units ------------------------------------------------------------------

// Sub-units to WORLD UNITS, where one world unit is one of the kernel's pixels
// (ADR-019 D5: 1 Blender unit = 1 kernel pixel). The one conversion, one-way:
// sub-units in, float out, and nothing in this library converts back.
inline float WorldPx(std::int32_t subUnits) {
    return static_cast<float>(subUnits) /
           static_cast<float>(cse::kernel::kSubUnitsPerPixel);
}

// --- The fight camera's framing (moved from FightView.cpp, M3.4c) -------------

// Half the view, in world pixels. Derived from the kernel's separation limit
// so the pair always fits with a body's margin at each edge: if the wall moves,
// the framing follows. (374 px / 2 + 13 = 200.)
inline constexpr float kViewHalfWidthPx =
    static_cast<float>(cse::kernel::kMaxSeparationSub / cse::kernel::kSubUnitsPerPixel) * 0.5f + 13.0f;
// The deadzone: how close a fighter may come to the edge before the camera
// scrolls. Without it every step drags the world.
inline constexpr float kCameraMarginPx = 34.0f;
// Camera height: roughly shoulder height on a 60 px body, so the floor lands
// in the lower third with headroom for a jump.
inline constexpr float kCameraHeightPx = 42.0f;

struct CameraFraming {
    float centreX     = 0.0f;               // world px
    float heightPx    = kCameraHeightPx;
    float halfWidthPx = kViewHalfWidthPx;   // the visible half-width, whatever the window
};

// Where the camera looks this frame: the deadzone scroll and the wall clamp.
// `previousCentrePx` is last frame's centre (the deadzone's memory -- the one
// piece of presentation state, and it is the CAMERA's, not a fighter's).
// `stageHalfWidthSub` 0 disables the clamp.
CameraFraming FightCameraFraming(const cse::kernel::GameState& state,
                                 std::int32_t stageHalfWidthSub,
                                 float previousCentrePx);

// --- The look ---------------------------------------------------------------

// What the committed fight_look.json sets on adopt (ADR-019 D5): the renderer's
// metre-tuned constants retuned for a world where one unit is one pixel. Every
// distance is in world px. The defaults ARE a consistent look, so a missing or
// refused file still fights under a sane one and says so.
struct FightLook {
    glm::vec3 sunDir{ -0.35f, -1.0f, -0.45f };
    glm::vec3 sunColor{ 1.0f, 1.0f, 1.0f };
    float     sunIntensity = 3.0f;
    float     exposure = 1.0f;

    bool  outline = true;
    float outlineThickness = 1.5f;
    float outlineThreshold = 0.15f;
    float outlineStrength = 0.9f;

    bool  ibl = true;
    float iblIntensity = 1.0f;

    // The camera stands this far in FRONT of the fighting plane (z = 0), on +Z,
    // looking down -Z. Parallel projection: the distance moves no pixel, it
    // only has to clear the geometry.
    float cameraDistancePx = 400.0f;
    // The room's back wall sits this far BEHIND the plane (M3.5a's room).
    float roomDepthPx = 240.0f;
    // How far a posed limb may reach toward the camera.
    float fighterDepthPx = 60.0f;
    float nearPx = 1.0f;
    float farPx = 2000.0f;
    // The CSM range; D5: >= camera distance + room depth, or the back wall's
    // shadows are cut off mid-room.
    float shadowDistancePx = 1200.0f;
    int   cascades = 4;
    // Outranks every camera in the host scene (the shipped scenes author 0).
    int   cameraPriority = 100;

    // Per-slot: the toon tint that tells P1 from P2, and a z tie-break so two
    // overlapping bodies never z-fight on the shared plane.
    glm::vec3 tint[cse::kernel::kMaxFighters] = { { 0.95f, 0.55f, 0.35f }, { 0.40f, 0.65f, 0.95f } };
    float     slotZ[cse::kernel::kMaxFighters] = { 0.0f, 0.5f };
};

// D5's invariants: the back wall is inside the shadow range and the far
// plane, the fighters are past the near plane, the slots differ. `why` (if
// given) names the first one broken.
bool FightLookIsConsistent(const FightLook& look, std::string* why = nullptr);

// From fight_look.json's text. Absent keys keep the defaults; an unknown key
// is refused BY NAME, the character file's rule -- a typo that loaded
// silently would be a look nobody authored.
bool ParseFightLook(const std::string& jsonText, FightLook& out, std::string& error);

// --- One rendered frame -------------------------------------------------------

struct FighterFrame {
    bool                visible = false;
    cse::game::PoseKind kind = cse::game::PoseKind::None;
    const ClipRef*      clip = nullptr;   // null: no table or no clip -- the rest pose
    std::uint32_t       frame = 0;        // into `clip`, already clamped
    glm::vec3           position{ 0.0f }; // world px: (posX, posY, the slot's z)
    float               yawDeg = 0.0f;    // 180 when facing == 1: a rotation, never a negative scale
    glm::mat4           model{ 1.0f };    // translate(position) * rotateY(yawDeg)
    glm::vec3           tint{ 1.0f };
    float               z = 0.0f;
    bool                toon = true;
};

struct CameraFrame {
    CameraFraming framing;
    glm::vec3     position{ 0.0f };      // (centreX, heightPx, cameraDistancePx), looking down -Z
    float         orthoHalfHeight = 0.0f; // kViewHalfWidthPx / aspect: the 2D and 3D cameras agree
    float         nearClip = 1.0f;
    float         farClip = 2000.0f;
    int           priority = 100;
};

struct FrameComposition {
    FighterFrame fighter[cse::kernel::kMaxFighters];
    CameraFrame  camera;
};

// The whole frame from (data, state): SelectPose -> the clip table -> the
// frame -> the matrix, for every fighter; then the camera. Reads its inputs
// and writes only the result -- FightPresentation.ReconcilingAFrameLeavesThe
// GameStateBytesUntouched holds it to that.
FrameComposition ComposeFrame(const cse::kernel::MatchData& data,
                              const cse::kernel::GameState& state,
                              const FighterClips& clips,
                              const FightLook& look,
                              std::int32_t stageHalfWidthSub,
                              float previousCentrePx,
                              int viewportW, int viewportH);

// The clip frame for a pose (ADR-019 D2). Move: the move frame, clamped to
// the clip. Knockdown / hitstun / blockstun: indexed FROM THE END -- frame =
// frames - remaining, clamped at 0 -- so the clip lands on its last frame as
// the counter reaches zero however long the authored counter was. The walk
// cycles: one frame per tick of walking, keyed by posX so the feet do not
// slide. Every other cycle: keyed by the tick.
std::uint32_t ClipFrameFor(const cse::game::PoseRequest& request,
                           const ClipRef& clip,
                           std::int32_t walkSpeedSub);

// --- The two cameras, stated as matrices so a test can compare pixels -------

// The scene camera the mode authors: glm::ortho(-h*aspect, h*aspect, -h, h,
// near, far) about a camera at `position` looking down -Z. Mirrors
// Camera::ProjectionFor without depending on it.
glm::mat4 SceneCameraProjection(const CameraFrame& cam, int viewportW, int viewportH);
glm::mat4 SceneCameraView(const CameraFrame& cam);
// The 2D overlay's camera as Renderer2D::BeginWorld builds it from FightView's
// Camera2D: half-extents (viewport/2)/zoom with zoom = (W/2)/halfWidth, +y up.
glm::mat4 OverlayProjection(const CameraFraming& framing, int viewportW, int viewportH);
glm::mat4 OverlayView(const CameraFraming& framing);

} // namespace cse::presentation
