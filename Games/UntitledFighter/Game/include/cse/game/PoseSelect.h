// THE POSE IS A KIND AND AN INTEGER (ROADMAP M3.4a; ADR-019 D3 and D9;
// DETERMINISM.md P4; ADR-011 decision 6).
//
// SelectPose answers one question for one fighter on one tick: WHICH clip
// should be on screen, and at WHICH frame -- and it answers it from GameState
// alone. It returns kinds and integers: no clip name, no float, no pointer
// into anything a renderer owns. The mode maps (kind, moveSlot) to a clip; this
// function never learns that clips exist.
//
// WHY IT LIVES IN CseGame AND NOT IN THE MODE. FightView::PhaseOf decides the
// same question for the box colours -- seven values, computed from the kernel's
// own fields -- beside the drawing code, because the drawing code was its only
// reader. A 3D presentation is a second reader, and two copies of "which state
// wins" is how the picture and the boxes come to disagree about the same tick.
// This function is that second reader, placed in the library that is held to
// the sim's arithmetic rules (DETERMINISM.md: Games/UntitledFighter/Game/ is
// bound by K3 and K6 -- no float, no wall clock) so that the decision is
// testable with no window and a rollback host can call it on a restored state
// and get the same answer (PoseSelect.RestoreAndResimulateReproduceEveryPose).
// The knockdown-over-stun ordering is shared with PhaseOf until ROADMAP M3.4c
// makes PhaseOf read this function, which is when the decision gets one home.
//
// THE PRECEDENCE, AND THE KERNEL FACT BEHIND EACH STEP:
//
//   inactive slot        -> None. A benched partner has no position and no boxes.
//   knockdown > 0        -> Knockdown. Outranks stun because a downed fighter is
//                           usually in hitstun too, and the more specific state
//                           is the one worth drawing (FightView learned this from
//                           play: "we can't really tell any knockdowns yet").
//   hitstun > 0          -> HitstunAir or HitstunStand by AirborneNow. There is
//                           no crouching hit reaction ON PURPOSE: StepPhysics
//                           clears Fighter::crouching on the first UNFROZEN tick
//                           a fighter cannot act (it is preserved through hitstop
//                           and unchanged on the tick of contact unless the hit
//                           launches), so a crouched defender who is hit reads as
//                           standing once the freeze ends, and inferring "was
//                           crouching" from history would be presentation owning
//                           state the sim does not.
//   blockstun > 0        -> BlockstunCrouch when guard == kGuardLow, else
//                           BlockstunStand. Guard is recomputed from held input
//                           on every unfrozen tick and blockstun does not forbid
//                           guarding, so a defender who releases Down
//                           mid-blockstun really is standing-blocking the next
//                           hit (resolveGuard). On a FROZEN tick the kernel
//                           zeroes guard without reading the pad, so the height
//                           falls back to the preserved crouching byte -- the
//                           pose does not flicker inside a crouch-blocked hit's
//                           own hitstop.
//   moveId described     -> Move at frame == moveFrame, exactly. Hitstop freezes
//                           moveFrame, so the pose freezes with it for free; the
//                           tick a move starts, moveFrame is 0 and so is the
//                           frame, which is what makes "a move start is never
//                           delayed" a property rather than a promise.
//   round over + idle    -> Ko (health <= 0), Win (an opposing team is out), or
//                           Win on a time-out (timer at zero, every team standing,
//                           this team's health total strictly the largest --
//                           stepRound's own sums; equal is its draw and poses
//                           nobody). ONLY for a fighter doing nothing: the
//                           training host keeps simulating past kRoundOver
//                           (stepRound is the only stage the round state gates,
//                           and training never refills health), so a winner who
//                           walks after the KO must be posed by the walk. A pose
//                           that stopped following the sim here would be the one
//                           thing this layer exists to forbid.
//   airborne             -> JumpRise while velY > 0, else JumpFall. Held poses,
//                           never a clip that depends on the arc's length -- the
//                           kernel is frozen for the art pass (ADR-019 D11) and
//                           the placeholder jump's 39 ticks are not a contract.
//   crouching            -> CrouchWalk when velX != 0 (the kernel lets a croucher
//                           walk), else CrouchIdle.
//   velX != 0            -> WalkFwd when the velocity points where the fighter
//                           faces, else WalkBack. Facing is 0 for +X. On the one
//                           tick a forward or back jump lands, the kernel keeps
//                           the ballistic velX and the fighter DID move by it, so
//                           this reads as one walk frame between JumpFall and
//                           Idle: honest to the bytes, and no byte in GameState
//                           separates it from a walk. A landing pose, if ever
//                           wanted, is a new kind, never a memo.
//   otherwise            -> Idle.
//
// Countdown kinds carry `remaining` so the mode can index the clip FROM THE END
// (frame = N - remaining), which is the only pure function of a ticks-remaining
// counter that lands a getup on counter 0 whatever the authored clip length.
// Cycles carry `tick` and `posXSub` so the mode can key them statelessly.
#pragma once

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <cstdint>

namespace cse::game {

enum class PoseKind : std::uint8_t {
    None = 0,
    Move,
    Idle,
    WalkFwd,
    WalkBack,
    CrouchIdle,
    CrouchWalk,
    JumpRise,
    JumpFall,
    HitstunStand,
    HitstunAir,
    BlockstunStand,
    BlockstunCrouch,
    Knockdown,
    Ko,
    Win,
};
inline constexpr int kPoseKindCount = 16;

// What the presentation needs and nothing it does not. Every byte is explicit
// -- including the padding -- so two requests for the same state compare equal
// with memcmp, which is how the restore test and the hitstop test are written.
struct PoseRequest {
    std::uint32_t tick;       // GameState::tick, for stateless cycle phases
    std::int32_t  posXSub;    // Fighter::posX, for the walk cycle's phase
    std::int32_t  posYSub;    // Fighter::posY
    std::uint16_t moveSlot;   // Move: Fighter::moveId. Otherwise 0.
    std::uint16_t frame;      // Move: Fighter::moveFrame. Otherwise 0.
    std::uint16_t remaining;  // Knockdown / Hitstun* / Blockstun*: ticks left. Otherwise 0.
    PoseKind      kind;
    std::uint8_t  mirror;     // Fighter::facing: 0 faces +X, 1 faces -X
    std::uint8_t  visible;    // Fighter::active
    std::uint8_t  pad_[3];    // explicit, zeroed: a memcmp reads these bytes
};

static_assert(sizeof(PoseRequest) == 24, "PoseRequest grew or acquired implicit padding");

// The pose for `slot` on this tick, as a pure function of (data, state). A slot
// at or above GameState::fighterCount, or an inactive fighter, yields kind None
// with visible == 0. Never allocates, never reads a clock, never writes.
PoseRequest SelectPose(const cse::kernel::MatchData& data,
                       const cse::kernel::GameState& state,
                       std::uint8_t                  slot);

// For failure messages and the HUD. Never used to select anything.
const char* PoseKindName(PoseKind kind);

} // namespace cse::game
