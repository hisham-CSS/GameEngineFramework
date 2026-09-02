// See PoseSelect.h for the precedence and the kernel fact behind each step.
#include "cse/game/PoseSelect.h"

namespace cse::game {

namespace {

using cse::kernel::Fighter;
using cse::kernel::GameState;

// An opposing team with no body left standing. Mirrors stepRound's own rule --
// "a team is out when its LAST body is out" -- so that Win never appears on a
// tick the kernel would not call a win. Benched partners count, as there.
bool anOpposingTeamIsOut(const GameState& s, const Fighter& f) {
    bool present[cse::kernel::kMaxTeams] = {};
    bool alive[cse::kernel::kMaxTeams]   = {};
    for (int i = 0; i < s.fighterCount && i < cse::kernel::kMaxFighters; ++i) {
        const Fighter& o = s.p[i];
        if (o.team >= cse::kernel::kMaxTeams) continue;
        present[o.team] = true;
        if (o.health > 0) alive[o.team] = true;
    }
    for (int t = 0; t < cse::kernel::kMaxTeams; ++t) {
        if (t == f.team) continue;
        if (present[t] && !alive[t]) return true;
    }
    return false;
}

// stepRound's OTHER way to award a round: the timer ran out and this fighter's
// team has strictly more health left than every other team present. Exactly
// equal totals are the kernel's draw and award nothing, so they return false.
bool teamHealthBeatsEveryOther(const GameState& s, const Fighter& f) {
    std::int32_t total[cse::kernel::kMaxTeams] = {};
    bool present[cse::kernel::kMaxTeams]       = {};
    for (int i = 0; i < s.fighterCount && i < cse::kernel::kMaxFighters; ++i) {
        const Fighter& o = s.p[i];
        if (o.team >= cse::kernel::kMaxTeams) continue;
        present[o.team] = true;
        total[o.team] += o.health;
    }
    if (f.team >= cse::kernel::kMaxTeams) return false;
    bool anyOther = false;
    for (int t = 0; t < cse::kernel::kMaxTeams; ++t) {
        if (t == f.team || !present[t]) continue;
        anyOther = true;
        if (total[t] >= total[f.team]) return false;
    }
    return anyOther;
}

} // namespace

PoseRequest SelectPose(const cse::kernel::MatchData& data,
                       const cse::kernel::GameState& s,
                       std::uint8_t                  slot) {
    PoseRequest r{};   // value-initialised: kind None, visible 0, padding zero

    if (slot >= cse::kernel::kMaxFighters || slot >= s.fighterCount) return r;
    const Fighter& f = s.p[slot];

    r.tick    = s.tick;
    r.posXSub = f.posX;
    r.posYSub = f.posY;
    r.mirror  = f.facing;
    r.visible = f.active;
    if (f.active == 0) return r;

    const cse::kernel::FighterData& fd = data.p[slot];

    if (f.knockdown > 0) {
        r.kind      = PoseKind::Knockdown;
        r.remaining = f.knockdown;
        return r;
    }
    if (f.hitstun > 0) {
        r.kind      = cse::kernel::AirborneNow(fd, f) ? PoseKind::HitstunAir : PoseKind::HitstunStand;
        r.remaining = f.hitstun;
        return r;
    }
    if (f.blockstun > 0) {
        // The height is the GUARD the kernel resolved -- except on a frozen
        // tick, where resolveGuard returns early with guard == kGuardNone while
        // the pad has not moved (Simulate.cpp). StepPhysics returns early on the
        // same ticks, BEFORE its posture write, so Fighter::crouching still holds
        // the posture the block was resolved with for exactly those ticks. Read
        // it there and a crouch-blocked hit does not flicker crouch -> stand ->
        // crouch inside its own hitstop. Both are kernel bytes; nothing is
        // remembered. (Unreachable in the shipped title until ROADMAP M3.0b
        // carries blockstun; test_pose_select.cpp drives it on Simulate.)
        const bool crouchBlock = f.guard == cse::kernel::kGuardLow ||
                                 (f.hitstop > 0 && f.crouching != 0);
        r.kind      = crouchBlock ? PoseKind::BlockstunCrouch : PoseKind::BlockstunStand;
        r.remaining = f.blockstun;
        return r;
    }
    if (f.moveId != 0) {
        // Null covers a moveId this character's table does not describe. The
        // kernel advances its frame counter and gives it no boxes, so there is
        // nothing honest to draw but the free-state pose below -- the same call
        // FightView::PhaseOf makes.
        if (cse::kernel::MoveAt(fd, f.moveId) != nullptr) {
            r.kind     = PoseKind::Move;
            r.moveSlot = f.moveId;
            r.frame    = f.moveFrame;
            return r;
        }
    }

    // Only a fighter doing NOTHING may wear the round's outcome. Everything
    // below this line is an action, and an action outranks a result.
    const bool doingNothing = f.moveId == 0 && f.airborne == 0 && f.crouching == 0 && f.velX == 0;
    if (s.roundState != cse::kernel::kRoundFighting && doingNothing) {
        if (f.health <= 0) {
            r.kind = PoseKind::Ko;
            return r;
        }
        if (anOpposingTeamIsOut(s, f)) {
            r.kind = PoseKind::Win;
            return r;
        }
        // A time-out. Only a TIMED round can leave kRoundFighting with every
        // team still standing, and only by its timer reaching zero: stepRound
        // decrements the timer only while no team is out, so a KO never lowers
        // it, and an untimed round (timer 0 from the start) can end only by a
        // KO, which the branch above already took. The kernel gives the round
        // to the side with more health and calls exactly equal a draw; the
        // pose sums the same bytes.
        if (s.roundTimer == 0 && teamHealthBeatsEveryOther(s, f)) {
            r.kind = PoseKind::Win;
            return r;
        }
    }

    if (f.airborne != 0) {
        r.kind = (f.velY > 0) ? PoseKind::JumpRise : PoseKind::JumpFall;
        return r;
    }
    if (f.crouching != 0) {
        r.kind = (f.velX != 0) ? PoseKind::CrouchWalk : PoseKind::CrouchIdle;
        return r;
    }
    if (f.velX != 0) {
        const bool forward = (f.velX > 0) == (f.facing == 0);
        r.kind = forward ? PoseKind::WalkFwd : PoseKind::WalkBack;
        return r;
    }
    r.kind = PoseKind::Idle;
    return r;
}

const char* PoseKindName(PoseKind kind) {
    switch (kind) {
        case PoseKind::None:           return "none";
        case PoseKind::Move:           return "move";
        case PoseKind::Idle:           return "idle";
        case PoseKind::WalkFwd:        return "walk_fwd";
        case PoseKind::WalkBack:       return "walk_back";
        case PoseKind::CrouchIdle:     return "crouch_idle";
        case PoseKind::CrouchWalk:     return "crouch_walk";
        case PoseKind::JumpRise:       return "jump_rise";
        case PoseKind::JumpFall:       return "jump_fall";
        case PoseKind::HitstunStand:   return "hitstun_stand";
        case PoseKind::HitstunAir:     return "hitstun_air";
        case PoseKind::BlockstunStand: return "blockstun_stand";
        case PoseKind::BlockstunCrouch:return "blockstun_crouch";
        case PoseKind::Knockdown:      return "knockdown";
        case PoseKind::Ko:             return "ko";
        case PoseKind::Win:            return "win";
    }
    return "?";
}

} // namespace cse::game
