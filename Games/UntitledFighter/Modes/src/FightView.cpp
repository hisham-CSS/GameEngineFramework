#include "FightView.h"

#include "cse/kernel/Simulate.h"

namespace untitledfighter {

namespace {

    // --- Framing -------------------------------------------------------------
    //
    // The half-width, the deadzone and the camera height live in the
    // presentation library since M3.4c (cse::presentation::kViewHalfWidthPx and
    // friends): the orthographic scene camera is framed by the same numbers,
    // and one copy is how the two cameras stay one camera.

    // --- The palette ---------------------------------------------------------
    //
    // One table, used by the boxes here and by the legend in FightHud.cpp. The
    // entries are the things a playtester has to be able to tell apart at a
    // glance, and they are deliberately far apart in hue rather than shades of
    // one colour: the whole readout is "which of these is it".
    //
    // SPENT IS THE ONE DELIBERATE EXCEPTION TO THAT RULE, and the exception is
    // the message. It is the SAME HUE as active and half the brightness, because
    // a spent window is not a different thing from an active one -- it is the
    // same box on a later frame of the same performance, with its one hit used
    // up. A colour from somewhere else in the wheel would say "another kind of
    // box"; a dimmer red says "that box, exhausted", which is what happened.
    const glm::vec4 kIdleCol    { 0.44f, 0.47f, 0.55f, 1.0f };  // grey
    const glm::vec4 kStartupCol { 0.96f, 0.78f, 0.26f, 1.0f };  // amber
    const glm::vec4 kActiveCol  { 0.98f, 0.31f, 0.28f, 1.0f };  // red
    const glm::vec4 kSpentCol   { 0.55f, 0.21f, 0.24f, 1.0f };  // dull red
    const glm::vec4 kRecoveryCol{ 0.36f, 0.62f, 0.96f, 1.0f };  // blue
    const glm::vec4 kHitstunCol { 0.85f, 0.38f, 0.88f, 1.0f };  // magenta
    // Deep blue, and far from magenta on purpose: hitstun and knockdown are the
    // two states a playtester most needs to tell apart at a glance, because one
    // of them can be hit again and the other cannot.
    const glm::vec4 kKnockdownCol{ 0.30f, 0.44f, 0.92f, 1.0f };  // blue

    // Which BODY is which LINE in the readout. Two colours, not five: the slot
    // accent has to stay distinguishable from every phase colour above, because
    // the two are drawn on the same body at the same time.
    const glm::vec4 kSlot0Col{ 0.40f, 0.86f, 0.72f, 1.0f };   // teal  -- you
    const glm::vec4 kSlot1Col{ 0.98f, 0.62f, 0.40f, 1.0f };   // orange -- dummy

    const glm::vec4 kFloorCol { 0.15f, 0.16f, 0.20f, 1.0f };
    const glm::vec4 kFloorLine{ 0.30f, 0.33f, 0.40f, 1.0f };

    // --- The measuring floor -------------------------------------------------
    //
    // A CHECKERBOARD WHOSE MAJOR DIVISION IS ONE REACH UNIT, which is what makes
    // it a ruler rather than decoration. The loader's `px_per_reach_unit` is 100,
    // so a move authored `reach: 0.42` reaches four squares and a bit, and a
    // pushback of `0.13` carries the defender just past one. Reading a number off
    // the file and counting it off the floor is the point.
    //
    // Asked for from play (2026-08-20): "a full sized level ... with a standard
    // checkerboard pattern so we can gauge distance."
    constexpr float kCellPx      = 20.0f;    // five to a reach unit
    constexpr float kCellsPerMajor = 5;      // so a major band is 100 px
    const glm::vec4 kCellDark { 0.13f, 0.14f, 0.18f, 1.0f };
    const glm::vec4 kCellLight{ 0.18f, 0.19f, 0.24f, 1.0f };
    const glm::vec4 kMajorLine{ 0.34f, 0.38f, 0.48f, 1.0f };
    const glm::vec4 kOriginLine{ 0.55f, 0.60f, 0.72f, 1.0f };
    const glm::vec4 kCornerCol{ 0.98f, 0.58f, 0.18f, 1.0f };
    const glm::vec4 kOutOfBounds{ 0.09f, 0.07f, 0.07f, 0.85f };
    const glm::vec4 kShadowCol{ 0.00f, 0.00f, 0.00f, 0.35f };
    const glm::vec4 kOriginCol{ 0.98f, 0.98f, 0.98f, 0.75f };

    // Sort layers. Named rather than inlined, because the ORDER is the statement
    // -- an active hitbox drawn under a body is an active hitbox nobody sees.
    constexpr int kLayerFloor  = 0;
    constexpr int kLayerStage  = 1;
    constexpr int kLayerShadow = 2;
    constexpr int kLayerBody   = 3;
    constexpr int kLayerHit    = 4;
    constexpr int kLayerOrigin = 5;

    // World units. One unit is one kernel pixel, so a 1-unit stroke is exactly
    // as thick as the smallest distance the character data can express in
    // pixels, and a box that is one pixel short of reaching looks it.
    constexpr float kStrokePx = 1.0f;

    glm::vec4 withAlpha(const glm::vec4& c, float a) {
        return glm::vec4(c.x, c.y, c.z, a);
    }

    // A filled rectangle from a kernel Box, already in stage coordinates.
    //
    // +Y IS UP IN BOTH, which is the reason this is three lines rather than a
    // flip. Renderer2D::BeginWorld is "+y UP, world units" and cse::kernel::Box
    // is authored with "+X stage right and +Y up, matching Fighter::posX/posY"
    // (Combat.h). The two conventions already agree, so the only thing to do is
    // scale -- and writing a flip here anyway would put a sign error one edit
    // away from a hitbox that hangs below the floor.
    void fillBox(MyCoreEngine::Renderer2D& r2d, const cse::kernel::Box& b,
                 const glm::vec4& colour, int layer) {
        const float x0 = WorldPx(b.x0);
        const float y0 = WorldPx(b.y0);
        const float x1 = WorldPx(b.x1);
        const float y1 = WorldPx(b.y1);
        r2d.DrawQuad({ x0, y0 }, { x1 - x0, y1 - y0 }, colour, layer);
    }

    // An outline, as four quads rather than as a DrawBox border.
    //
    // DrawBox's border is a signed-distance band measured in the quad's own
    // units, which in world mode are world units -- so it would work. It is not
    // used because it also degrades to a plain square when the box shader failed
    // to compile (Renderer2D::supportsRoundedBoxes), and an outline that
    // silently becomes a filled rectangle would turn a hurtbox into a solid
    // block over the fighter inside it. Four quads cannot lose their hole.
    void strokeBox(MyCoreEngine::Renderer2D& r2d, const cse::kernel::Box& b,
                   const glm::vec4& colour, int layer) {
        const float x0 = WorldPx(b.x0);
        const float y0 = WorldPx(b.y0);
        const float x1 = WorldPx(b.x1);
        const float y1 = WorldPx(b.y1);
        const float t  = kStrokePx;
        r2d.DrawQuad({ x0, y0 }, { x1 - x0, t }, colour, layer);          // bottom
        r2d.DrawQuad({ x0, y1 - t }, { x1 - x0, t }, colour, layer);      // top
        r2d.DrawQuad({ x0, y0 }, { t, y1 - y0 }, colour, layer);          // left
        r2d.DrawQuad({ x1 - t, y0 }, { t, y1 - y0 }, colour, layer);      // right
    }

    // One fighter: shadow, body, facing wedge, slot accent, live hitbox, origin.
    void drawFighter(MyCoreEngine::Renderer2D& r2d,
                     const cse::kernel::MatchData& match,
                     const cse::kernel::GameState& state, int slot, bool boxesOnly) {
        const cse::kernel::FighterData& data = match.p[slot];
        const cse::kernel::Fighter&     f    = state.p[slot];
        const Phase     phase  = PhaseOf(match, state, static_cast<std::uint8_t>(slot));
        const glm::vec4 colour = PhaseColour(phase);
        const glm::vec4 accent = SlotColour(slot);

        // THE BODY IS THE HURTBOX. There is no separate "character sprite"
        // rectangle to draw beside it and there must not be: the kernel has one
        // body box per fighter (Combat.h's FighterData::hurtbox), it is what
        // ResolveHits tests against, and a placeholder quad drawn at some other
        // size would be a picture of a character who is not the one being hit.
        const cse::kernel::Box body = cse::kernel::Hurtbox(data, f);

        // The shadow is drawn at the body's X and always on the floor, so an
        // airborne fighter reads as airborne. Derived from the body box and
        // nothing else -- there is no depth in this game and this is a cue, not
        // a claim about a third axis.
        // Over a 3D body (M3.4c) the shadow, the fill, the slot bar and the
        // wedge are the model's job; the kernel's boxes stay, as outlines, so
        // the fist can be judged against the box it is supposed to be inside.
        if (!boxesOnly) {
            cse::kernel::Box shadow = body;
            shadow.y0 = 0;
            shadow.y1 = cse::kernel::kSubUnitsPerPixel;   // 1 px tall, on the floor
            r2d.DrawQuad({ WorldPx(shadow.x0), WorldPx(shadow.y0) },
                         { WorldPx(shadow.x1 - shadow.x0), WorldPx(shadow.y1 - shadow.y0) },
                         kShadowCol, kLayerShadow);
            fillBox(r2d, body, withAlpha(colour, 0.22f), kLayerBody);
        }
        strokeBox(r2d, body, withAlpha(colour, 0.95f), kLayerBody);

        // A bar along the bottom edge in the slot's colour, so the readout's
        // "YOU" and "DUMMY" have something on screen to point at that does not
        // change when the phase colour does.
        if (!boxesOnly)
            r2d.DrawQuad({ WorldPx(body.x0), WorldPx(body.y0) },
                         { WorldPx(body.x1 - body.x0), 2.0f },
                         accent, kLayerBody);

        // WHICH WAY THEY ARE FACING, read from Fighter::facing and not from the
        // positions. They are the same thing today -- Simulate resolves facing
        // from relative position every tick -- and they are the same thing only
        // because a line in Simulate.cpp says so. Reading the field means this
        // stays right on the tick a future rule (a knockdown, a cross-up) makes
        // them differ, which is exactly the tick a playtester would want to see
        // it.
        if (!boxesOnly) {
            // Halved on the FLOAT side of WorldPx. Halving in sub-units first is
            // an integer division whose rounding differs by sign, and although
            // nothing here feeds a tick, doing that to a simulation integer is
            // the habit that eventually does.
            const float mid = (WorldPx(body.y0) + WorldPx(body.y1)) * 0.5f;
            const float x   = f.facing == 0 ? WorldPx(body.x1)
                                            : WorldPx(body.x0) - 6.0f;
            r2d.DrawQuad({ x, mid - 1.5f }, { 6.0f, 3.0f },
                         withAlpha(accent, 0.9f), kLayerBody);
        }

        // THE HITBOX, ON THE FRAMES IT IS LIVE, IN THE COLOUR OF WHAT IT CAN DO.
        //
        // ActiveHitbox is the kernel's own answer to "is there a box", so this
        // box appears on exactly the ticks ResolveHits would test with one. No
        // frame arithmetic happens here at all, which is what makes the picture
        // usable as evidence: if the RED box overlapped the other body and no hit
        // landed, the disagreement is real and worth reporting, rather than being
        // this file drawing a frame early.
        //
        // THE COLOUR IS THE OTHER HALF OF THE ANSWER, and it is read from
        // ResolveHits' FIRST test rather than its second. A window that has
        // already connected still returns a box for every remaining active frame,
        // and that box cannot hit. Drawing it red would make three of air_mp's
        // four active frames a lie -- so it is drawn dull, and "it looked like it
        // should have connected" is answered on the screen instead of in a bug
        // report. Filled fainter as well as darker, because the thing being said
        // is "there is less here than there was".
        cse::kernel::Box hit{};
        if (cse::kernel::ActiveHitbox(data, f, hit)) {
            const bool      spent  = WindowHasConnected(f);
            const glm::vec4 boxCol = PhaseColour(spent ? Phase::Spent : Phase::Active);
            fillBox(r2d, hit, withAlpha(boxCol, spent ? 0.16f : 0.30f), kLayerHit);
            strokeBox(r2d, hit, boxCol, kLayerHit);
        }

        // The ORIGIN, as a small cross. It is worth two quads because the stage
        // clamp is on the ORIGIN and not on the body (Simulate.cpp clamps
        // Fighter::posX), so a fighter pinned in the corner has half a body
        // hanging past the wall -- and without this marker that looks like the
        // corner marker being in the wrong place.
        {
            const float x = WorldPx(f.posX);
            const float y = WorldPx(f.posY);
            r2d.DrawQuad({ x - 3.0f, y - 0.5f }, { 6.0f, 1.0f }, kOriginCol, kLayerOrigin);
            r2d.DrawQuad({ x - 0.5f, y - 3.0f }, { 1.0f, 6.0f }, kOriginCol, kLayerOrigin);
        }
    }

} // namespace

// --- Phase --------------------------------------------------------------------

bool WindowHasConnected(const cse::kernel::Fighter& f) {
    // One comparison, and it is CancelIsOpen's own (`f.alreadyHitBits == 0`).
    // See the header for why no defender slot is needed to ask it.
    return f.alreadyHitBits != 0;
}

Phase PhaseOf(const cse::kernel::MatchData& match, const cse::kernel::GameState& state,
              std::uint8_t slot) {
    // THE PRECEDENCE IS SelectPose's (M3.4c): knockdown over stun over move.
    // A fighter on the floor is usually in hitstun too and the more specific
    // state is the one worth drawing -- asked for from play (2026-08-20): "we
    // can't really tell any knockdowns yet" -- and both kinds of stun count
    // because Simulate.cpp's `actionable()` is `hitstun == 0 && blockstun == 0`.
    // That ordering used to be spelled here AND in PoseSelect.cpp; now the
    // selector is asked and this function adds only the frame split below.
    const cse::game::PoseRequest pose = cse::game::SelectPose(match, state, slot);
    switch (pose.kind) {
    case cse::game::PoseKind::Knockdown:      return Phase::Knockdown;
    case cse::game::PoseKind::HitstunStand:
    case cse::game::PoseKind::HitstunAir:
    case cse::game::PoseKind::BlockstunStand:
    case cse::game::PoseKind::BlockstunCrouch: return Phase::Hitstun;
    case cse::game::PoseKind::Move:            break;
    default:                                   return Phase::Idle;
    }

    const cse::kernel::Fighter&     f    = state.p[slot];
    const cse::kernel::FighterData& data = match.p[slot];
    const cse::kernel::MoveDef* const move = cse::kernel::MoveAt(data, f.moveId);
    // Null covers BOTH "idle" and "a moveId this character's table does not
    // describe". They are drawn the same because the kernel treats them the same
    // -- StepAttack advances the frame counter of an undescribed id and gives it
    // no boxes -- so there is nothing else honest to draw.
    if (move == nullptr) return Phase::Idle;

    // The ACTIVE window, from the kernel. See the header: this is a delegation
    // and not a convenience.
    //
    // BOTH OF ResolveHits' TESTS, IN ITS OWN ORDER. The guard is asked second
    // here only because there is no box to describe until ActiveHitbox says so;
    // the answer is the same either way, and painting the body ACTIVE on a frame
    // whose hit is already spent would put the same lie on the fighter that the
    // hitbox note in this file's header is about.
    cse::kernel::Box box{};
    if (cse::kernel::ActiveHitbox(data, f, box))
        return WindowHasConnected(f) ? Phase::Spent : Phase::Active;

    const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
    const std::int32_t from  = move->startup > 0 ? move->startup : 0;
    return frame < from ? Phase::Startup : Phase::Recovery;
}

const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle:     return "idle";
        case Phase::Startup:  return "startup";
        case Phase::Active:   return "ACTIVE";
        // Lower case where ACTIVE is upper, and that is the readout: the shout
        // is for the frames that can hit, and this is the same window after it
        // has stopped being one of them.
        case Phase::Spent:    return "spent";
        case Phase::Recovery: return "recovery";
        // "down" rather than "knockdown", because the legend is a row of short
        // words and the row beside it already says the count.
        case Phase::Knockdown: return "down";
        case Phase::Hitstun:  return "hitstun";
    }
    return "?";
}

glm::vec4 PhaseColour(Phase phase) {
    switch (phase) {
        case Phase::Idle:     return kIdleCol;
        case Phase::Startup:  return kStartupCol;
        case Phase::Active:   return kActiveCol;
        case Phase::Spent:    return kSpentCol;
        case Phase::Recovery: return kRecoveryCol;
        case Phase::Hitstun:  return kHitstunCol;
        case Phase::Knockdown: return kKnockdownCol;
    }
    return kIdleCol;
}

glm::vec4 SlotColour(int slot) {
    return slot == 0 ? kSlot0Col : kSlot1Col;
}

// --- The stage ----------------------------------------------------------------

std::int32_t ProbeStageHalfWidthSub() {
    cse::kernel::GameState probe{};
    // Seeded with anything non-zero; nothing in this probe reads the stream, and
    // ResetMatch substitutes for a zero seed anyway.
    cse::kernel::ResetMatch(probe, 1u);

    // Combat.h's own outer bound on a fighter origin, so the probe is placed at
    // the furthest point the kernel promises to remain total for rather than at
    // a large number chosen here.
    probe.p[0].posX = cse::kernel::kMaxWorldCoord;

    const cse::kernel::InputPair neutral{};
    // The TWO-ARGUMENT overload: it runs against kNoMoves, which has no moves
    // and a degenerate hurtbox, so StepAttack can start nothing and ResolveHits
    // can find no overlap. The only thing that can have touched posX is the
    // clamp.
    cse::kernel::Simulate(probe, neutral);
    return probe.p[0].posX;
}

// --- Camera -------------------------------------------------------------------

MyCoreEngine::Camera2D FightCamera(const cse::kernel::GameState& state,
                                   int viewportW, int viewportH,
                                   std::int32_t stageHalfWidthSub,
                                   float previousCentrePx) {
    (void)viewportH;   // the vertical extent falls out of the aspect
    // The framing -- midpoint, deadzone, wall clamp, height -- is the
    // presentation library's (M3.4c), shared with the scene camera.
    const cse::presentation::CameraFraming framing =
        cse::presentation::FightCameraFraming(state, stageHalfWidthSub, previousCentrePx);

    MyCoreEngine::Camera2D cam{};
    cam.position = glm::vec2(framing.centreX, framing.heightPx);
    // BeginWorld computes its half-extents as (viewport / 2) / zoom, so this is
    // the zoom that makes the visible half-width exactly the framing's
    // whatever the window is. max(1) mirrors BeginWorld's own guard against a
    // zero viewport during a resize.
    const float w = static_cast<float>(viewportW > 1 ? viewportW : 1);
    cam.zoom = (w * 0.5f) / framing.halfWidthPx;
    return cam;
}

// --- The picture --------------------------------------------------------------

void DrawFightWorld(MyCoreEngine::Renderer2D& r2d,
                    const cse::kernel::GameState& state,
                    const cse::kernel::MatchData& data,
                    std::int32_t stageHalfWidthSub,
                    bool boxesOnly) {
    const float corner = WorldPx(stageHalfWidthSub);

    // Over a presentation model (M3.4c) the scene is the floor and the room's
    // grid is the ruler (M3.5a); only the boxes are drawn here.
    if (boxesOnly) {
        for (int slot = 0; slot < 2; ++slot)
            drawFighter(r2d, data, state, slot, /*boxesOnly*/ true);
        return;
    }

    // The floor, drawn well past both corners so it never ends inside the view.
    // A slab rather than a line, because a fighter standing ON zero needs
    // something to stand on for the picture to read at all.
    const float span = corner * 2.0f + 400.0f;
    r2d.DrawQuad({ -corner - 200.0f, -80.0f }, { span, 80.0f }, kFloorCol, kLayerFloor);

    // THE RULER. Squares from the stage's own centre outward, so the pattern is
    // symmetric about x = 0 and the two corners are the same distance from the
    // middle in squares as they are in pixels -- a checkerboard laid from the
    // left edge would put the seam somewhere arbitrary and make the two halves
    // read differently.
    //
    // Drawn only across the stage proper. Past the corner is not stage, and
    // tiling the out-of-bounds band would suggest there is somewhere to stand.
    {
        const int cells = static_cast<int>(corner / kCellPx) + 1;
        for (int i = -cells; i < cells; ++i) {
            const float x = static_cast<float>(i) * kCellPx;
            const float w = (x + kCellPx > corner) ? (corner - x) : kCellPx;
            if (x >= corner || w <= 0.0f) continue;
            if (x < -corner) continue;

            // Alternating by the cell INDEX rather than by position, so the
            // parity does not flip when the stage width changes.
            const bool light = ((i % 2) + 2) % 2 == 0;
            r2d.DrawQuad({ x, -80.0f }, { w, 80.0f },
                         light ? kCellLight : kCellDark, kLayerFloor);

            // A heavier line every reach unit, which is what turns counting
            // squares into reading a distance.
            if (((i % static_cast<int>(kCellsPerMajor)) + static_cast<int>(kCellsPerMajor))
                    % static_cast<int>(kCellsPerMajor) == 0)
                r2d.DrawQuad({ x - 0.5f, -80.0f }, { 1.0f, 84.0f },
                             kMajorLine, kLayerStage);
        }

        // CENTRE STAGE, marked once and brighter. It is the position `V` puts
        // the pair at, and the one place a distance can be read off in both
        // directions at once.
        r2d.DrawQuad({ -1.0f, -80.0f }, { 2.0f, 96.0f }, kOriginLine, kLayerStage);
    }

    r2d.DrawQuad({ -corner - 200.0f, -1.0f }, { span, 1.0f }, kFloorLine, kLayerFloor);

    // BEYOND THE CORNER IS NOT STAGE. Tinted rather than left blank so the wall
    // has a side to it: the clamp is on the origin, so a cornered fighter's body
    // really does hang over this band, and seeing that is the point.
    r2d.DrawQuad({ -corner - 200.0f, -80.0f }, { 200.0f, 400.0f }, kOutOfBounds, kLayerStage);
    r2d.DrawQuad({ corner, -80.0f }, { 200.0f, 400.0f }, kOutOfBounds, kLayerStage);

    // THE CORNER MARKERS, and they matter more here than in most games.
    //
    // ProverAdapter.h note 2: the in-engine decision procedure is CORNER-ONLY by
    // construction -- it answers for a defender pinned against the wall with no
    // room to walk away. Every verdict this mode shows is about this line. A
    // training mode that did not draw it would be quoting a corner verdict at a
    // player standing in the middle of the stage.
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -corner : corner;
        r2d.DrawQuad({ x - 1.0f, -80.0f }, { 2.0f, 300.0f }, kCornerCol, kLayerStage);
        // Three ticks up the wall, so it reads as a marked edge rather than as a
        // stray line at whatever zoom the window produced.
        for (int i = 0; i < 3; ++i) {
            const float y = 20.0f + static_cast<float>(i) * 40.0f;
            const float dx = side == 0 ? 0.0f : -10.0f;
            r2d.DrawQuad({ x + dx, y }, { 10.0f, 2.0f }, kCornerCol, kLayerStage);
        }
    }

    // Slot order, always, matching Simulate's own fixed iteration order. It
    // decides nothing here -- these are quads -- and it is written the same way
    // so that a reader comparing this loop with the tick's does not have to
    // wonder whether the difference means something.
    for (int slot = 0; slot < 2; ++slot)
        drawFighter(r2d, data, state, slot, /*boxesOnly*/ false);
}

} // namespace untitledfighter
