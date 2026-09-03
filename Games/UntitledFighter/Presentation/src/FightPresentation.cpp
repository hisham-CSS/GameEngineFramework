#include "cse/presentation/FightPresentation.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>

namespace cse::presentation {

// --- Camera framing (the body of FightView.cpp's FightCamera, unchanged) ------

CameraFraming FightCameraFraming(const cse::kernel::GameState& state,
                                 std::int32_t stageHalfWidthSub,
                                 float previousCentrePx) {
    CameraFraming out{};

    // The midpoint of the two ORIGINS, on the float side of WorldPx: halving
    // sub-units first would be an integer division whose rounding differs by
    // sign, and arithmetic like that on simulation integers is the habit that
    // eventually feeds a tick.
    const float p0 = WorldPx(state.p[0].posX);
    const float p1 = WorldPx(state.p[1].posX);

    // The deadzone: start where the camera was and move it only as far as it
    // must. The separation limit guarantees the pair always FITS, so most
    // frames the honest answer is "stay put" -- asked for from play
    // (2026-08-20): "the camera should remain fixed until a player moves in a
    // way where it needs to move".
    float centre = previousCentrePx;
    const float lo = (p0 < p1 ? p0 : p1) - kCameraMarginPx;
    const float hi = (p0 > p1 ? p0 : p1) + kCameraMarginPx;
    const float halfW = kViewHalfWidthPx;
    if (lo < centre - halfW) centre = lo + halfW;   // the minimum scroll, never the midpoint
    if (hi > centre + halfW) centre = hi - halfW;

    // And it stops at the walls, so a corner arrives at the side of the
    // screen and STAYS there -- that is what tells a player they have run out
    // of stage. Skipped when the stage is narrower than the view (a fixture,
    // not a level): centring is then the only sensible answer.
    if (stageHalfWidthSub > 0) {
        const float corner = WorldPx(stageHalfWidthSub);
        if (corner > kViewHalfWidthPx) {
            const float limit = corner - kViewHalfWidthPx;
            centre = centre < -limit ? -limit : (centre > limit ? limit : centre);
        }
        else {
            centre = 0.0f;
        }
    }

    out.centreX = centre;
    out.heightPx = kCameraHeightPx;
    out.halfWidthPx = kViewHalfWidthPx;
    return out;
}

// --- The look -----------------------------------------------------------------

bool FightLookIsConsistent(const FightLook& look, std::string* why) {
    auto fail = [&](const char* what) { if (why) *why = what; return false; };
    if (!(look.cameraDistancePx > 0.0f)) return fail("camera distance must be positive");
    if (!(look.roomDepthPx >= 0.0f)) return fail("room depth must not be negative");
    if (!(look.nearPx > 0.0f)) return fail("near plane must be positive");
    if (!(look.nearPx < look.cameraDistancePx - look.fighterDepthPx))
        return fail("near plane must sit in front of the fighters: near < camera distance - fighter depth");
    if (!(look.farPx > look.cameraDistancePx + look.roomDepthPx))
        return fail("far plane must be past the back wall: far > camera distance + room depth");
    if (!(look.shadowDistancePx >= look.cameraDistancePx + look.roomDepthPx))
        return fail("shadow distance must reach the back wall: shadow distance >= camera distance + room depth (ADR-019 D5)");
    if (look.cascades < 1 || look.cascades > 4) return fail("cascades must be 1..4");
    if (look.cameraPriority <= 0) return fail("camera priority must be positive to outrank a host scene's cameras");
    if (look.slotZ[0] == look.slotZ[1]) return fail("the two slots must have distinct z planes");
    if (look.tint[0] == look.tint[1]) return fail("the two slots must have distinct tints");
    return true;
}

namespace {

using json = nlohmann::json;

bool readVec3(const json& v, glm::vec3& out) {
    if (!v.is_array() || v.size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!v[i].is_number()) return false;
        out[static_cast<int>(i)] = v[i].get<float>();
    }
    return true;
}

bool readNumber(const json& v, float& out) {
    if (!v.is_number()) return false;
    out = v.get<float>();
    return true;
}

bool readInt(const json& v, int& out) {
    if (!v.is_number_integer()) return false;
    out = v.get<int>();
    return true;
}

bool readBool(const json& v, bool& out) {
    if (!v.is_boolean()) return false;
    out = v.get<bool>();
    return true;
}

// An object whose keys are exactly a closed list: anything else is refused by
// name, with the legal names listed -- the character loader's rule.
bool checkKeys(const json& obj, const char* where, const char* const* legal, std::size_t n, std::string& error) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        for (std::size_t i = 0; i < n; ++i) if (it.key() == legal[i]) { ok = true; break; }
        if (!ok) {
            error = std::string(where) + ": `" + it.key() + "` is not a field. The fields are:";
            for (std::size_t i = 0; i < n; ++i) error += std::string(" ") + legal[i];
            return false;
        }
    }
    return true;
}

} // namespace

bool ParseFightLook(const std::string& jsonText, FightLook& out, std::string& error) {
    error.clear();
    const json doc = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) { error = "fight_look: not a JSON object"; return false; }

    static const char* const kTop[] = {
        "sun_dir", "sun_color", "sun_intensity", "exposure", "outline", "ibl", "camera",
        "room_depth_px", "fighter_depth_px", "shadow_distance_px", "cascades", "slots",
    };
    if (!checkKeys(doc, "fight_look", kTop, sizeof(kTop) / sizeof(kTop[0]), error)) return false;

    FightLook look{};
    auto bad = [&](const char* key, const char* type) {
        error = std::string("fight_look: `") + key + "` is not " + type;
        return false;
    };
    if (doc.contains("sun_dir") && !readVec3(doc["sun_dir"], look.sunDir)) return bad("sun_dir", "an array of three numbers");
    if (doc.contains("sun_color") && !readVec3(doc["sun_color"], look.sunColor)) return bad("sun_color", "an array of three numbers");
    if (doc.contains("sun_intensity") && !readNumber(doc["sun_intensity"], look.sunIntensity)) return bad("sun_intensity", "a number");
    if (doc.contains("exposure") && !readNumber(doc["exposure"], look.exposure)) return bad("exposure", "a number");
    if (doc.contains("outline")) {
        const json& o = doc["outline"];
        static const char* const kOutline[] = { "enabled", "thickness", "threshold", "strength" };
        if (!o.is_object()) return bad("outline", "an object");
        if (!checkKeys(o, "fight_look.outline", kOutline, 4, error)) return false;
        if (o.contains("enabled") && !readBool(o["enabled"], look.outline)) return bad("outline.enabled", "a boolean");
        if (o.contains("thickness") && !readNumber(o["thickness"], look.outlineThickness)) return bad("outline.thickness", "a number");
        if (o.contains("threshold") && !readNumber(o["threshold"], look.outlineThreshold)) return bad("outline.threshold", "a number");
        if (o.contains("strength") && !readNumber(o["strength"], look.outlineStrength)) return bad("outline.strength", "a number");
    }
    if (doc.contains("ibl")) {
        const json& o = doc["ibl"];
        static const char* const kIbl[] = { "enabled", "intensity" };
        if (!o.is_object()) return bad("ibl", "an object");
        if (!checkKeys(o, "fight_look.ibl", kIbl, 2, error)) return false;
        if (o.contains("enabled") && !readBool(o["enabled"], look.ibl)) return bad("ibl.enabled", "a boolean");
        if (o.contains("intensity") && !readNumber(o["intensity"], look.iblIntensity)) return bad("ibl.intensity", "a number");
    }
    if (doc.contains("camera")) {
        const json& o = doc["camera"];
        static const char* const kCam[] = { "distance_px", "near_px", "far_px", "priority" };
        if (!o.is_object()) return bad("camera", "an object");
        if (!checkKeys(o, "fight_look.camera", kCam, 4, error)) return false;
        if (o.contains("distance_px") && !readNumber(o["distance_px"], look.cameraDistancePx)) return bad("camera.distance_px", "a number");
        if (o.contains("near_px") && !readNumber(o["near_px"], look.nearPx)) return bad("camera.near_px", "a number");
        if (o.contains("far_px") && !readNumber(o["far_px"], look.farPx)) return bad("camera.far_px", "a number");
        if (o.contains("priority") && !readInt(o["priority"], look.cameraPriority)) return bad("camera.priority", "an integer");
    }
    if (doc.contains("room_depth_px") && !readNumber(doc["room_depth_px"], look.roomDepthPx)) return bad("room_depth_px", "a number");
    if (doc.contains("fighter_depth_px") && !readNumber(doc["fighter_depth_px"], look.fighterDepthPx)) return bad("fighter_depth_px", "a number");
    if (doc.contains("shadow_distance_px") && !readNumber(doc["shadow_distance_px"], look.shadowDistancePx)) return bad("shadow_distance_px", "a number");
    if (doc.contains("cascades") && !readInt(doc["cascades"], look.cascades)) return bad("cascades", "an integer");
    if (doc.contains("slots")) {
        const json& s = doc["slots"];
        // One object per slot the fight uses (two today); slots the file does
        // not name keep the defaults. More than the kernel has is a typo.
        if (!s.is_array() || s.empty() || s.size() > static_cast<std::size_t>(cse::kernel::kMaxFighters))
            return bad("slots", "an array of one object per fighter slot (at least one, at most kMaxFighters)");
        static const char* const kSlot[] = { "tint", "z" };
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (!s[i].is_object()) return bad("slots[]", "an object");
            if (!checkKeys(s[i], "fight_look.slots[]", kSlot, 2, error)) return false;
            if (s[i].contains("tint") && !readVec3(s[i]["tint"], look.tint[i])) return bad("slots[].tint", "an array of three numbers");
            if (s[i].contains("z") && !readNumber(s[i]["z"], look.slotZ[i])) return bad("slots[].z", "a number");
        }
    }
    std::string why;
    if (!FightLookIsConsistent(look, &why)) { error = "fight_look: " + why; return false; }
    out = look;
    return true;
}

// --- The frame ----------------------------------------------------------------

std::uint32_t ClipFrameFor(const cse::game::PoseRequest& r, const ClipRef& clip, std::int32_t walkSpeedSub) {
    using cse::game::PoseKind;
    if (clip.frames == 0) return 0;
    const std::uint32_t last = clip.frames - 1;
    switch (r.kind) {
    case PoseKind::Move:
        return std::min<std::uint32_t>(r.frame, last);
    case PoseKind::Knockdown:
    case PoseKind::HitstunStand:
    case PoseKind::HitstunAir:
    case PoseKind::BlockstunStand:
    case PoseKind::BlockstunCrouch: {
        // indexed from the end: the clip's last frame lands as the counter
        // reaches zero, however long the authored counter was
        const std::int64_t f = static_cast<std::int64_t>(clip.frames) - static_cast<std::int64_t>(r.remaining);
        if (f <= 0) return 0;
        return std::min<std::uint32_t>(static_cast<std::uint32_t>(f), last);
    }
    case PoseKind::WalkFwd:
    case PoseKind::WalkBack:
        // one frame per tick of walking, keyed by position so the feet plant
        return WalkCycleFrame(r.posXSub, walkSpeedSub, clip.frames);
    case PoseKind::None:
        return 0;
    default:
        return CycleFrame(static_cast<std::int64_t>(r.tick), clip.frames);
    }
}

FrameComposition ComposeFrame(const cse::kernel::MatchData& data,
                              const cse::kernel::GameState& state,
                              const FighterClips& clips,
                              const FightLook& look,
                              std::int32_t stageHalfWidthSub,
                              float previousCentrePx,
                              int viewportW, int viewportH) {
    FrameComposition out{};
    for (std::uint8_t slot = 0; slot < cse::kernel::kMaxFighters; ++slot) {
        FighterFrame& f = out.fighter[slot];
        const cse::game::PoseRequest r = cse::game::SelectPose(data, state, slot);
        f.visible = r.visible != 0;
        f.kind = r.kind;
        f.clip = clips.Find(r.kind, r.moveSlot);
        f.frame = f.clip ? ClipFrameFor(r, *f.clip, data.p[slot].walkSpeedSub) : 0u;
        f.z = look.slotZ[slot];
        f.tint = look.tint[slot];
        f.toon = true;
        f.position = glm::vec3(WorldPx(r.posXSub), WorldPx(r.posYSub), f.z);
        // Facing left is a 180 degree YAW: a rotation with determinant +1, so
        // the skin, the normals and the outline all stay right-handed. A
        // negative x scale would mirror the mesh with determinant -1 and turn
        // every normal inside out (ADR-019 D5).
        f.yawDeg = (r.mirror != 0) ? 180.0f : 0.0f;
        f.model = glm::translate(glm::mat4(1.0f), f.position) *
                  glm::rotate(glm::mat4(1.0f), glm::radians(f.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    CameraFrame& cam = out.camera;
    cam.framing = FightCameraFraming(state, stageHalfWidthSub, previousCentrePx);
    const float w = static_cast<float>(viewportW > 1 ? viewportW : 1);
    const float h = static_cast<float>(viewportH > 1 ? viewportH : 1);
    // The 2D overlay shows exactly kViewHalfWidthPx of world per half-screen
    // whatever the window (Renderer2D::BeginWorld: half-height = (H/2)/zoom
    // with zoom = (W/2)/halfWidth). The same half-height here is what makes
    // the two cameras one camera.
    cam.orthoHalfHeight = cam.framing.halfWidthPx * (h / w);
    cam.position = glm::vec3(cam.framing.centreX, cam.framing.heightPx, look.cameraDistancePx);
    cam.nearClip = look.nearPx;
    cam.farClip = look.farPx;
    cam.priority = look.cameraPriority;
    return out;
}

// --- The two cameras as matrices ------------------------------------------------

glm::mat4 SceneCameraProjection(const CameraFrame& cam, int viewportW, int viewportH) {
    const float w = static_cast<float>(viewportW > 1 ? viewportW : 1);
    const float h = static_cast<float>(viewportH > 1 ? viewportH : 1);
    const float hh = cam.orthoHalfHeight > 1e-3f ? cam.orthoHalfHeight : 1e-3f;
    const float hw = hh * (w / h);
    return glm::ortho(-hw, hw, -hh, hh, cam.nearClip, cam.farClip);
}

glm::mat4 SceneCameraView(const CameraFrame& cam) {
    return glm::lookAt(cam.position, cam.position + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OverlayProjection(const CameraFraming& framing, int viewportW, int viewportH) {
    const float w = static_cast<float>(viewportW > 1 ? viewportW : 1);
    const float h = static_cast<float>(viewportH > 1 ? viewportH : 1);
    const float zoom = (w * 0.5f) / framing.halfWidthPx;
    const float halfW = (w * 0.5f) / zoom;
    const float halfH = (h * 0.5f) / zoom;
    return glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
}

glm::mat4 OverlayView(const CameraFraming& framing) {
    return glm::translate(glm::mat4(1.0f), glm::vec3(-framing.centreX, -framing.heightPx, 0.0f));
}

} // namespace cse::presentation
