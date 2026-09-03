// The fight, drawn: two bodies, the boxes the kernel actually built, and the
// stage they are standing on.
//
// ---------------------------------------------------------------------------
// WHY THIS IS A SEPARATE FILE FROM THE HUD, AND FROM THE MODE
// ---------------------------------------------------------------------------
// The mode owns the SESSION -- when a tick runs, where the inputs come from,
// what happens when Demonstrate is pressed. This file owns the PICTURE, and it
// is written so that it cannot own anything else: every function below takes the
// state and the data by const reference and returns either nothing or a camera.
// It holds no members, caches nothing between frames and cannot advance a tick.
//
// That is not tidiness. A drawing layer that keeps its own copy of "where the
// fighter is" is a second source of truth for the position, and the whole
// argument this title rests on -- ARCHITECTURE.md D8, the engine and the
// analysis disagreeing by a frame -- is about exactly that class of mistake one
// layer up. Here it would be worse rather than better: a player cannot read the
// file to find out that the box they were looking at was one tick stale.
//
// ---------------------------------------------------------------------------
// EVERY BOX ON SCREEN IS THE KERNEL'S OWN BOX
// ---------------------------------------------------------------------------
// The hurtbox comes from cse::kernel::Hurtbox and the hitbox from
// cse::kernel::ActiveHitbox -- the same two functions ResolveHits calls to
// decide whether the hit lands. Nothing here re-derives a rectangle from
// startup/active/recovery, mirrors a box by hand, or decides for itself which
// frames are live. So a box drawn on this screen is the box that hit, or the box
// that did not, and "it looked like it should have connected" stops being a
// thing a playtester can be right about for the wrong reason.
//
// AND ActiveHitbox IS NOT THE WHOLE TEST, WHICH IS WHY THERE IS A SIXTH COLOUR.
// ResolveHits asks TWO questions and asks them in this order (Combat.cpp):
//
//     1. has this window already connected on that defender?
//            (state.p[a].alreadyHitBits & bitForSlot(d)) != 0   -> no hit
//     2. is a box live this frame?
//            ActiveHitbox(...)                                  -> no box, no hit
//
// A file that drew from (2) alone paints a red ACTIVE box on every remaining
// frame of a window that has already spent its hit. On fighter_a's air_mp --
// active 4, connecting on the first of them -- that is three dead frames drawn
// red, overlapping the dummy, every single repetition, and it manufactures
// exactly the false bug report the paragraph above exists to prevent.
//
// So the multi-hit guard is READ, through WindowHasConnected below, and a window
// that has spent its hit is drawn in its own colour rather than hidden. Hidden
// would be correct and would teach nothing: "this box is live and cannot hit
// again until the move restarts" is one of the more useful sentences a training
// mode has to say, and it is worth a swatch in the legend.
#pragma once

#include "Engine.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/presentation/FightPresentation.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace untitledfighter {

// --- The one place the integer simulation and the float camera meet ----------

// Sub-units to WORLD UNITS, where one world unit is one of the kernel's pixels.
//
// This function is the entire boundary between the two number systems, and it is
// one-way by construction: sub-units in, float out, and nothing in this header
// or its implementation converts back. The simulation stays integer because
// nothing it reads was ever a float; the camera stays float because a camera has
// no reason not to be. cse::kernel::kSubUnitsPerPixel is the rate, taken from the
// kernel rather than written as 256 here, because a second copy of that constant
// is a second definition of how big a character is.
// Defined once, in the presentation library (M3.4c), where the 3D reconciler
// needs the same conversion; this name is kept so every call in the mode reads
// as before.
using cse::presentation::WorldPx;

// --- What a fighter is doing, in the vocabulary the frame data is written in --

// The six states a training mode colours by. Not an enum the kernel has -- it
// has no such field -- but every one of them is decided by a kernel field or by
// a kernel function. PhaseOf below decides the frame split (startup / active /
// spent / recovery); the ORDERING of knockdown over stun over move is the same
// rule cse::game::SelectPose applies for the 3D presentation (ROADMAP M3.4a),
// and since M3.4c PhaseOf reads it, so that decision has one home.
//
// SPENT IS THE ONE THAT IS NOT ABOUT THE MOVE. The other five are properties of
// the move and the frame; `Spent` is a property of THIS PERFORMANCE of it -- the
// window is still live and has already used up the one hit the multi-hit guard
// allows it against that body. It sits next to Active in this list because it is
// the same frames of the same move seen a second time round.
//
// `Knockdown` is last because it OUTRANKS the others: a fighter on the floor is
// also, usually, in hitstun, and drawing them as merely stunned is what made the
// author say "we can't really tell any knockdowns yet". It is a different state
// -- nothing can hit them and they cannot act -- so it gets its own colour.
enum class Phase : std::uint8_t { Idle, Startup, Active, Spent, Recovery, Hitstun, Knockdown };

// Whether the fighter's CURRENT attack has already landed, i.e. whether
// ResolveHits' multi-hit guard will refuse it before it looks for a box at all.
//
// `Fighter::alreadyHitBits != 0` IS THE TEST, and it is the kernel's own reading
// of that field rather than a re-derivation: Combat.h says a nonzero value means
// exactly "the attack currently in progress has landed on somebody", and
// CancelIsOpen tests it with those same four characters. In a two-slot match the
// only bit ResolveHits can ever set on fighter `a` is the one for `1 - a`
// (`state.p[a].alreadyHitBits |= bitForSlot(d)`), so "nonzero" and "has hit the
// other fighter" are the same sentence here and no slot has to be passed in.
//
// StepAttack clears the field whenever a move starts, cancels or ends, so this
// goes false again the instant the next window opens -- which is what makes the
// colour below flicker back to ACTIVE on exactly the frames a hit is possible.
bool WindowHasConnected(const cse::kernel::Fighter& f);

// Read off Fighter::hitstun, Fighter::blockstun, Fighter::moveId,
// Fighter::moveFrame and Fighter::alreadyHitBits, with the ACTIVE window
// answered by cse::kernel::ActiveHitbox itself rather than by comparing
// moveFrame against startup a second time.
//
// That delegation is the point of the function. ActiveHitbox is what ResolveHits
// asks, and it carries two rules a re-implementation drops: a move whose `active`
// is not positive is never live at all, and the window is measured from
// max(startup, 0) rather than from a negative authored startup. A HUD that
// painted "ACTIVE" on a frame the kernel would not hit with is telling a
// playtester the move whiffed because of spacing when it whiffed because of the
// data.
//
// The multi-hit guard is the SECOND half of that same argument and arrived with
// it: a live box whose window has already connected is `Spent`, not `Active`,
// because ResolveHits tests the guard FIRST and never reaches the box. See the
// note at the top of this header.
//
// SINCE M3.4c THE PRECEDENCE IS READ FROM cse::game::SelectPose -- knockdown
// over stun over move -- so that decision has ONE home (the 3D presentation
// selects its pose by the same call); this function adds only the frame split
// the 2D readout colours by. Hence the wider signature: SelectPose answers for
// a slot of a match, not for one Fighter.
Phase PhaseOf(const cse::kernel::MatchData& data, const cse::kernel::GameState& state,
              std::uint8_t slot);

// Short, stable, and the same strings the HUD's legend prints.
const char* PhaseName(Phase phase);

// ONE colour table for the boxes and for the legend beside them. A legend with
// its own colours is a legend that eventually disagrees with the picture, and
// the picture is the thing being explained.
glm::vec4 PhaseColour(Phase phase);

// The per-slot accent: which body on screen is which line in the readout. Slot
// is clamped, so a caller cannot index off the end with a fighter index it
// computed.
glm::vec4 SlotColour(int slot);

// --- The stage ---------------------------------------------------------------

// Where the corner is, ASKED OF THE KERNEL rather than restated here.
//
// The stage clamp is `kStageHalfWidth` in an anonymous namespace inside
// Kernel/src/Simulate.cpp. It is not exported and there is no header to include
// it from, so a corner marker drawn at a number typed into this file would be a
// second copy of it -- and the two would part company silently, on the one
// screen element whose entire job is to say where the wall is. The prover is
// corner-only by construction (ProverAdapter.h note 2), so this mode draws that
// wall and puts the dummy against it; a wall in the wrong place would make every
// verdict on screen answer a question about a stage that does not exist.
//
// So it is MEASURED. StepPhysics's last act on posX is
// `posX = clampInt(posX + velX + pushX, -limit, +limit)`; with neutral input
// and nothing landed, velX and pushX are zero,
// and the two-argument Simulate runs against kNoMoves so no move can start and
// nothing else can touch the field. Put a fighter past any possible bound, run
// one tick of a pure function, and what comes back IS the clamp.
//
// Costs one tick of integer arithmetic, once, at Enter.
std::int32_t ProbeStageHalfWidthSub();

// --- The camera --------------------------------------------------------------

// Frames the pair, in world units, for a viewport of this size.
//
// Resolution-independent by construction: the zoom is derived from the viewport
// width so that a FIXED number of stage pixels is visible however big the window
// is. A camera with a fixed zoom would show twice the stage on a 4K monitor,
// which would make "am I in range" a question about the display.
//
// `stageHalfWidthSub` clamps the framing so the view never passes a wall: pass 0
// to centre on the pair unconditionally, which is what a caller with no stage
// wants. See the note at the clamp for why an unclamped corner reads as the edge
// of the screen rather than as a wall.
// `previousCentrePx` is where the camera was last frame, in world pixels. The
// camera HOLDS STILL unless a fighter would otherwise leave the view -- pass the
// returned `position.x` back in next frame. A caller with no previous frame
// should pass the pair's midpoint, which is the right framing to open on.
//
// SINCE M3.4c the framing itself -- the deadzone, the wall clamp, the 200 px
// half-width -- is cse::presentation::FightCameraFraming, the same function the
// orthographic scene camera is driven by (ADR-019 D4), and this adapts it to a
// Camera2D. Two copies of the framing would be two cameras.
MyCoreEngine::Camera2D FightCamera(const cse::kernel::GameState& state,
                                   int viewportW, int viewportH,
                                   std::int32_t stageHalfWidthSub,
                                   float previousCentrePx);

// --- The picture --------------------------------------------------------------

// PRECONDITION: `r2d` is already in WORLD mode, begun with the camera above.
// Bracketing is the caller's job because the caller is the one who knows what
// mode the renderer was handed to it in -- the UI pass begins SCREEN mode around
// IGameMode::Draw (UIPass.cpp), so a mode that wants a world pass swaps modes and
// swaps back, and doing that inside this function would hide a Begin/End pair
// from the file that has to restore what it borrowed.
// `boxesOnly` (M3.4c): when a presentation model is on screen the 3D scene IS
// the floor and the bodies, so only the kernel's boxes -- the Hurtbox outline,
// the ActiveHitbox, the origin -- are drawn over it. The floor, the ruler, the
// body fill and the facing wedge stay 2D-placeholder furniture.
// `drawBoxes` (M3.4e): the overlay mode's answer. Off, the kernel's boxes are
// not drawn -- and with them, over the 2D placeholders, the ruler, which is a
// measurement of nothing without a box to measure.
void DrawFightWorld(MyCoreEngine::Renderer2D& r2d,
                    const cse::kernel::GameState& state,
                    const cse::kernel::MatchData& data,
                    std::int32_t stageHalfWidthSub,
                    bool boxesOnly = false,
                    bool drawBoxes = true);

} // namespace untitledfighter
