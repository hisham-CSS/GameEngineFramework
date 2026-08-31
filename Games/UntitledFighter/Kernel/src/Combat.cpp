#include "cse/kernel/Combat.h"

// Nothing is included here. Not <cstring>, not <algorithm>, not <cstdlib>. The
// whole file is integer comparisons, negation and addition, which is the entire
// reason a hitbox that connects on Windows connects on Linux with no flag, no
// patch and no CI matrix behind the claim (ARCHITECTURE.md D2).

namespace cse::kernel {
namespace {

// Clamp to a SYMMETRIC range. Symmetry is the load-bearing part: clamp(-v) is
// exactly -clamp(v) for every v, so applying this before a mirror gives the same
// answer as applying it after one. A clamp with asymmetric bounds -- or one
// written with a shift -- would be a rounding rule hiding inside a safety check,
// which is precisely the class of bug D2 rejected the general fixed-point type
// to avoid.
std::int32_t clampSymmetric(std::int32_t v, std::int32_t limit) {
    if (v >  limit) return  limit;
    if (v < -limit) return -limit;
    return v;
}

// Fighter::hitstun is a uint16, and a character file is untrusted data. Saturate
// rather than wrap: a move authored with 70000 ticks of hitstun is a balance
// mistake that should look like a very long stun, not like a stun of 4464.
inline constexpr std::int32_t kMaxStunTicks = 0xFFFF;

// One bit per fighter slot in Fighter::alreadyHitBits.
std::uint8_t bitForSlot(int slot) {
    return static_cast<std::uint8_t>(1u << slot);
}

// Whether this defender's guard stops this attack.
//
// It asks ONLY about height, because every other condition on being able to
// block at all -- not in hitstun, not on the floor, not airborne, not
// mid-move, and actually holding away from the attacker -- is decided in ONE
// place, where Fighter::guard is computed each tick. Splitting those conditions
// across two files is how one of them ends up enforced and the other does not.
bool defenderBlocks(const Fighter& def, const MoveDef& m) {
    if (def.guard == kGuardNone) return false;
    return GuardStops(def.guard, m.blockedAs);
}

// Pushback for a defender, signed in stage coordinates.
//
// THE DIRECTION COMES FROM THE SIGN OF A POSITION DIFFERENCE and never from
// `facing` multiplied into a coordinate. GameState.h says why facing is not a
// sign multiplier, and ADR-006 section 3.4 names this as precisely the place
// somebody reaches for `pos * facing`.
std::int32_t pushAwayFrom(const Fighter& atk, const Fighter& def,
                          std::int16_t amount) {
    if (amount == 0) return 0;
    const std::int32_t v = static_cast<std::int32_t>(amount);

    if (def.posX < atk.posX) return -v;
    if (def.posX > atk.posX) return  v;

    // Exactly co-located, which a cross-up passing through zero reaches. The
    // attacker's facing is the only information left and it is a rule both peers
    // compute identically from bytes they both hold -- which is the property that
    // matters here, rather than which way is prettier.
    return atk.facing == 0 ? v : -v;
}

} // namespace

// --- Boxes ------------------------------------------------------------------

bool BoxIsValid(const Box& b) {
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return false;
    const std::int32_t c[4] = { b.x0, b.y0, b.x1, b.y1 };
    for (int i = 0; i < 4; ++i) {
        if (c[i] > kMaxBoxCoord || c[i] < -kMaxBoxCoord) return false;
    }
    return true;
}

Box MirrorBox(const Box& local) {
    // The whole operation, and there is nothing else to it: two negations and an
    // exchange. See the long note in Combat.h for why this and not a multiply.
    Box out;
    out.x0 = -local.x1;
    out.y0 =  local.y0;
    out.x1 = -local.x0;
    out.y1 =  local.y1;
    return out;
}

Box PlaceBox(const Box& local, std::int32_t posX, std::int32_t posY,
             std::uint8_t facing) {
    // RANGE ANALYSIS, which Â§4.3 requires rather than trusting:
    // every coordinate below is bounded by kMaxBoxCoord (2^20) and every origin
    // by kMaxWorldCoord (2^24), so each sum is at most 2^24 + 2^20 = 17,825,792
    // in magnitude, against an int32 range of 2,147,483,647. Signed overflow is
    // impossible by construction, for any GameState, including one assembled by
    // hand or arriving over a wire.
    Box b;
    b.x0 = clampSymmetric(local.x0, kMaxBoxCoord);
    b.y0 = clampSymmetric(local.y0, kMaxBoxCoord);
    b.x1 = clampSymmetric(local.x1, kMaxBoxCoord);
    b.y1 = clampSymmetric(local.y1, kMaxBoxCoord);

    // Fighter::facing is 0 for +X and 1 for -X, and it is a FLAG rather than a
    // sign multiplier for exactly the reason this branch exists: the alternative
    // spelling, `x * facingSign`, is the multiply MirrorBox's comment refuses.
    if (facing != 0) b = MirrorBox(b);

    const std::int32_t ox = clampSymmetric(posX, kMaxWorldCoord);
    const std::int32_t oy = clampSymmetric(posY, kMaxWorldCoord);

    Box out;
    out.x0 = b.x0 + ox;
    out.y0 = b.y0 + oy;
    out.x1 = b.x1 + ox;
    out.y1 = b.y1 + oy;
    return out;
}

bool BoxesOverlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 &&
           a.y0 < b.y1 && b.y0 < a.y1;
}

// --- Reading the state through the data -------------------------------------

std::int32_t WallLimitFor(const FighterData& data, const Fighter& f) {
    // The argument for the pushbox and the placed-box reading lives with the
    // wall clamp in Simulate.cpp, where it was written; this is the same
    // arithmetic, moved here when the corner push became its second asker.
    if (data.pushbox.x1 <= data.pushbox.x0) return kStageHalfWidthSub;

    const Box placed = PlaceBox(data.pushbox, 0, 0, f.facing);
    const std::int32_t back  = -placed.x0;
    const std::int32_t front =  placed.x1;
    const std::int32_t reach = back > front ? back : front;
    if (reach >= kStageHalfWidthSub) return kStageHalfWidthSub;
    return kStageHalfWidthSub - reach;
}

const MoveDef* MoveAt(const FighterData& data, std::uint16_t moveId) {
    if (moveId == 0) return nullptr;
    const std::int32_t id = static_cast<std::int32_t>(moveId);
    if (id >= data.moveCount || id >= kMaxMovesPerFighter) return nullptr;
    return &data.moves[id];
}

std::int32_t MoveDuration(const MoveDef& m) {
    // Each part is clamped at zero on the way in so that a file authoring a
    // negative recovery cannot produce a move that ends before it begins, which
    // would leave a fighter with a moveId nothing can clear.
    const std::int32_t s = m.startup  > 0 ? m.startup  : 0;
    const std::int32_t a = m.active   > 0 ? m.active   : 0;
    const std::int32_t r = m.recovery > 0 ? m.recovery : 0;
    return s + a + r;
}

bool ActiveHitbox(const FighterData& data, const Fighter& f, Box& out) {
    const MoveDef* m = MoveAt(data, f.moveId);
    if (m == nullptr) return false;
    if (m->active <= 0) return false;

    const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
    const std::int32_t from  = m->startup > 0 ? m->startup : 0;
    if (frame < from || frame >= from + m->active) return false;

    out = PlaceBox(m->hitbox, f.posX, f.posY, f.facing);
    return true;
}

Box Hurtbox(const FighterData& data, const Fighter& f) {
    // A BODY ON THE FLOOR IS LYING DOWN: the standing box tipped over, as long
    // as it was tall and as tall as it was wide, floor edge at zero. Checked
    // FIRST, ahead of any move override, because a downed fighter has no move.
    //
    // This is the sim's own answer and not a drawing choice -- the view renders
    // Hurtbox() and may not invent a pose the state did not produce, so the one
    // honest place a knockdown can LOOK like one is here. It changes no
    // exchange: InvulnerableTo already refuses every hit against a downed body
    // (P2Knockdown.AFighterOnTheFloorCannotBeHit), so this shape is presentation
    // routed through the state, which is the only routing ADR-011 allows.
    // Asked for from play: a knockdown that keeps standing height looks like a
    // fighter who is standing, whatever colour it is drawn in.
    if (f.knockdown > 0) {
        const std::int32_t w = data.hurtbox.x1 - data.hurtbox.x0;
        const std::int32_t h = data.hurtbox.y1 - data.hurtbox.y0;
        // Tipped toward the fighter's own back: head away from the opponent,
        // which mirrors with facing exactly as every other box does.
        const Box lying{ -h + (data.hurtbox.x1 - w / 2), 0,
                          data.hurtbox.x1 - w / 2,        w };
        return PlaceBox(lying, f.posX, f.posY, f.facing);
    }

    // A move may replace the body for the ticks it runs. That is the whole
    // low-profile mechanism: a crouching attack ducks a high one because its body
    // is SHORTER for those frames, and no move ever names another move.
    const MoveDef* m = MoveAt(data, f.moveId);
    if (m != nullptr && m->hasHurtboxOverride != 0)
        return PlaceBox(m->hurtboxOverride, f.posX, f.posY, f.facing);

    // Otherwise the posture decides. A MOVE'S OVERRIDE OUTRANKS THE CROUCH,
    // above, because the move is the more specific statement: a crouching move
    // that authors its own body has already said what crouching looks like for
    // those frames, and layering the generic crouch under it would silently
    // ignore half of what the file said.
    //
    // Degenerate means unauthored -- see FighterData::crouchHurtbox -- so a
    // character with no crouch body simply keeps its standing one, exactly as
    // before this field existed.
    const bool crouched = f.crouching != 0;
    const Box& cb = data.crouchHurtbox;
    const bool authored = cb.x1 > cb.x0 && cb.y1 > cb.y0;

    return PlaceBox(crouched && authored ? cb : data.hurtbox,
                    f.posX, f.posY, f.facing);
}

// --- Stance, guard, priority and invincibility -------------------------------

bool AirborneNow(const FighterData& data, const Fighter& f) {
    if (f.airborne != 0) return true;

    // A grounded move that takes off partway through. This is what lets a hop
    // kick pass over a low without either move knowing the other exists.
    const MoveDef* m = MoveAt(data, f.moveId);
    if (m == nullptr) return false;
    if (m->hasAirborneFrom == 0) return false;
    return static_cast<std::int32_t>(f.moveFrame) >= m->airborneFromTick;
}

bool StanceAllows(const MoveDef& m, const Fighter& f, bool crouchHeld) {
    // The standing/crouching cases read the INPUT (`crouchHeld`), never
    // Fighter::crouching -- the header says why, and the difference is exactly
    // the cross-posture cancel. On a free tick the two agree by construction:
    // StepPhysics computed `crouching` from the same held Down this tick.
    switch (m.stance) {
        case kStanceAny:       return true;
        case kStanceGround:    return f.airborne == 0;
        case kStanceStanding:  return f.airborne == 0 && !crouchHeld;
        case kStanceCrouching: return f.airborne == 0 && crouchHeld;
        case kStanceAir:       return f.airborne != 0;
        default: break;
    }
    // A value this kernel does not recognise is PERMISSIVE, which is the same
    // choice MoveAt makes for a moveId the table does not describe: inert rather
    // than an error. It is also the compatible direction -- an unenforced stance
    // is exactly the kernel that shipped before this field existed -- and the
    // loader is where a bad value is supposed to be refused.
    return true;
}

std::uint16_t AttackKinds(const FighterData& attackerData, const Fighter& attacker,
                          const MoveDef& m) {
    std::uint16_t kinds = 0;

    // One token from the guard-height axis. Anything unrecognised reads as mid,
    // which is both the authored default and the safe reading: mid is the value
    // BOTH guards stop, so a corrupt byte cannot invent an unblockable attack.
    if (m.blockedAs == kBlockedAsHigh)     kinds |= kAttackHigh;
    else if (m.blockedAs == kBlockedAsLow) kinds |= kAttackLow;
    else                                   kinds |= kAttackMid;

    // One token from the attacker-state axis.
    if (AirborneNow(attackerData, attacker)) kinds |= kAttackAerial;

    return kinds;
}

bool GuardStops(std::uint8_t guard, std::uint8_t blockedAs) {
    const bool isHigh = (blockedAs == kBlockedAsHigh);
    const bool isLow  = (blockedAs == kBlockedAsLow);

    // high guard stops { high, mid } == everything that is not low
    // low  guard stops { low,  mid } == everything that is not high
    if (guard == kGuardHigh) return !isLow;
    if (guard == kGuardLow)  return !isHigh;
    return false;
}

bool InvulnerableTo(const FighterData& data, const Fighter& f, std::uint16_t kinds) {
    // A BODY ON THE FLOOR IS NOT A TARGET, to every kind of attack, for as long
    // as it takes to get up.
    //
    // Checked FIRST, and before the move lookup, because a knocked-down fighter
    // has no move: `moveId` is zero, `MoveAt` returns null, and the window scan
    // below would have answered "not invulnerable" for the one state in the game
    // where that is most wrong.
    //
    // This is what the kernel MEANS by knockdown rather than a mechanic of its
    // own, so it gets no field: the opt-in is already the authored
    // `causes_knockdown` and its duration, and a move that knocks nobody down
    // grants nobody this. Without it a sweep OPENS an unescapable loop instead
    // of ending pressure, which is the exact inversion of what a knockdown is
    // for -- and from the other side of the screen a knockdown that only stopped
    // the defender ACTING is indistinguishable from a long hitstun.
    //
    // OTG -- hitting a downed opponent on purpose -- is a later mechanic and
    // will arrive as an authored per-move field, not as a loosening here.
    if (f.knockdown > 0) return true;

    const MoveDef* m = MoveAt(data, f.moveId);
    if (m == nullptr) return false;

    const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
    const std::int32_t n = m->invulnCount < kMaxInvulnWindows
                               ? static_cast<std::int32_t>(m->invulnCount)
                               : kMaxInvulnWindows;

    for (std::int32_t i = 0; i < n; ++i) {
        const InvincibilityWindow& w = m->invuln[i];

        // A zero-tick window is the empty set, not a short window. The loader
        // refuses one; this is the kernel declining to trust that it did.
        if (w.ticks <= 0) continue;
        if (frame < w.fromTick) continue;
        if (frame >= w.fromTick + w.ticks) continue;

        // Naming no kinds narrows nothing, and the identity of a narrowing is
        // everything.
        if (w.kinds == 0) return true;

        // INTERSECTION, not containment. An aerial medium arrives as
        // {aerial, high} and a window naming {aerial} stops it on one shared bit.
        // Under containment an anti-air would have to enumerate all three guard
        // heights, and the field would be useless for its motivating case.
        if ((w.kinds & kinds) != 0) return true;
    }
    return false;
}

// --- Cancels ----------------------------------------------------------------

bool CancelIsOpen(const Fighter& f, const CancelEdge& edge) {
    if (f.moveId == 0) return false;
    if (edge.from != f.moveId) return false;

    // The contact gate (M1.3 slice (a)). A nonzero mask names the outcomes
    // that open this edge, and the edge is open when ANY recorded outcome is
    // in it: whiff is `alreadyHitBits == 0` (nothing has connected -- which
    // is also what makes a kara, `on: whiff` in the startup frames, work
    // with no extra state), a clean hit is a contact bit WITHOUT its blocked
    // mirror, and a block is a contact bit WITH it. Any-of over the
    // per-defender bits, because one window can meet two defenders and land
    // differently on each; a mask of 0 is UNGATED, the byte `on: always`
    // and every hand-built bench already carry.
    if (edge.contactMask != 0) {
        const std::uint8_t contact = f.alreadyHitBits;
        const std::uint8_t stopped =
            static_cast<std::uint8_t>(f.flags & kFlagsBlockedBits);
        bool open = false;
        if ((edge.contactMask & kContactWhiff) != 0)
            open = open || contact == 0;
        if ((edge.contactMask & kContactHit) != 0)
            open = open || (contact & static_cast<std::uint8_t>(~stopped)) != 0;
        if ((edge.contactMask & kContactBlock) != 0)
            open = open || (contact & stopped) != 0;
        if (!open) return false;
    }

    // Both bounds inclusive. An edge whose earliest is past its latest matches
    // nothing for any frame, which is how an authored delay longer than the
    // source move ends up inert instead of ending up wrong.
    const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
    return frame >= edge.earliestFrame && frame <= edge.latestFrame;
}

// Declared here and defined beside ApplyEffects further down, because both the
// cancel scan and the button scan below need it and both come before the place
// resources are otherwise dealt with.
bool GuardsMet(const Fighter& f, const MoveDef& m);

const CancelEdge* FindCancel(const FighterData& data, const Fighter& f, Input in) {
    // A fighter with no move in progress has nothing to cancel, and a moveId this
    // character's table does not describe is INERT here for the same reason it is
    // inert in StepAttack: a harness that drives moveId by hand must keep getting
    // the behaviour it got before this file grew a cancel system.
    if (MoveAt(data, f.moveId) == nullptr) return nullptr;

    // Derived from the LIVE bits, not from Fighter::crouching: a cancel is a
    // selection, and selection asks what the player is holding NOW.
    const bool crouchHeld = (in.bits & kInputDown) != 0;

    for (std::int32_t i = 0; i < data.cancelCount && i < kMaxCancelsPerFighter; ++i) {
        const CancelEdge& e = data.cancels[i];
        if (!CancelIsOpen(f, e)) continue;

        // The target has to be a move this character actually has. An edge whose
        // `to` fell outside the table would otherwise put the fighter into a
        // moveId nothing can describe, which is exactly the state MatchBuilder
        // refuses to build and the kernel should not be able to reach either.
        const MoveDef* target = MoveAt(data, e.to);
        if (target == nullptr) continue;

        // A HELD BUTTON STILL TAKES A CANCEL, and unlike the button scan that is
        // deliberate rather than a limitation. A player holding the follow-up
        // through the whole source move takes the cancel on the first frame of
        // its window rather than on the frame they chose -- which is what a
        // genre player expects of a chain, and what makes holding a button a
        // reasonable way to buffer before the buffer field existed.
        if (target->button == 0) continue;

        // A CANCEL TAKES A BUFFERED PRESS, and that is what makes links and
        // cancels doable by a human. A player aiming at a two-frame link presses
        // slightly early far more often than slightly late, so a cancel that
        // only reads the CURRENT tick's bits is a cancel that punishes the
        // common miss. The buffered press is exactly the input that was meant
        // for this cancel, arriving one or two frames before the window opened.
        //
        // Held bits still count, because a chord is still a chord and this is
        // the same all-bits-wanted rule the button scan uses; what the buffer
        // adds is the press that has already been let go of.
        const bool heldNow  = (in.bits & target->button) == target->button;
        const bool buffered = (f.bufferedButtons & target->button) == target->button;
        if (!heldNow && !buffered) continue;

        // AND IT HAS TO BE AFFORDABLE. A cancel into a super the fighter cannot
        // pay for is not a cancel that fails halfway -- it is an edge that is
        // not available, so the scan keeps looking and a cheaper edge later in
        // file order can still take the input. Checked here rather than after
        // the loop for exactly that reason.
        if (!GuardsMet(f, *target)) continue;

        // The follow-up's own stance condition applies to a cancel exactly as it
        // does to a fresh start. Without this a grounded chain could cancel into
        // an air-only move from the floor, which is one of the two routes
        // test_gap_extent measured keeping 32 of the 33 runaway cycles alive.
        if (!StanceAllows(*target, f, crouchHeld)) continue;

        return &e;
    }
    return nullptr;
}

// --- The move lifecycle -----------------------------------------------------

bool Actionable(const Fighter& f) {
    return f.hitstun == 0 && f.blockstun == 0 && f.knockdown == 0;
}

namespace {

// POSTURE FOLLOWS THE MOVE (docs/adr/ADR-012 rule 3) -- `crouching`'s second
// authorized writer, the move-start rule. A crouching move makes the fighter
// crouching and a standing move stands them up, because the move's frame data
// was authored against that body; a move that states no ground posture
// (any/ground/air) imposes none, so the posture the input rule established
// rides through -- an any-stance chain that silently stood a croucher up would
// be hittable by everything the crouch was ducking. One helper for both start
// routes, so a cancel cannot start a move by a slightly different rule than
// the button scan does.
void adoptStance(Fighter& f, const MoveDef& m) {
    if (m.stance == kStanceCrouching)     f.crouching = 1;
    else if (m.stance == kStanceStanding) f.crouching = 0;
}

} // namespace

void StepAttack(Fighter& f, const FighterData& data, const Intent& it) {
    // Benched does nothing; frozen does not advance. Gated HERE so a future
    // caller cannot forget it -- the freeze skipping this whole function is
    // what keeps startup 5 meaning five ticks OF THE MOVE under hitstop.
    if (f.active == 0 || it.frozen) return;

    if (f.moveId != 0) {
        const MoveDef* m = MoveAt(data, f.moveId);
        if (m == nullptr) {
            // An id this character's table does not describe. Advance the frame
            // counter and do nothing else -- this is exactly what the kernel did
            // before this file existed, and keeping it identical is what lets a
            // harness drive moveId by hand without the simulation second-guessing
            // it.
            ++f.moveFrame;
            return;
        }

        ++f.moveFrame;
        if (static_cast<std::int32_t>(f.moveFrame) >= MoveDuration(*m)) {
            f.moveId    = 0;
            f.moveFrame = 0;
            // Clearing the record of who this window already hit is what makes
            // the NEXT repetition of the same move able to hit again. The bug on
            // the other side of this line is the one where a jab connects once
            // per match. The blocked mirror travels with it, here and at the
            // other three clear sites: a blocked bit outliving its hit bit
            // would break the subset invariant GameState.h promises.
            f.alreadyHitBits = 0;
            f.flags = static_cast<std::uint16_t>(f.flags & ~kFlagsBlockedBits);
        }
    }

    // Hitstun and blockstun gate EVERYTHING a fighter chooses to do, cancels
    // included. A fighter who was hit has already had their move interrupted by
    // ResolveHits, so this is belt and braces -- but it is the belt that keeps
    // "cancel" from quietly becoming "act out of hitstun", which is a different
    // and much larger feature. Derived rather than passed in: physics has
    // already burned this tick's stun down, and one definition answers both.
    if (!Actionable(f)) return;

    // --- The cancel ---------------------------------------------------------
    //
    // A move that is still running may be interrupted into a follow-up. This is
    // the whole difference between a kernel that can perform the chains the combo
    // prover reasons about and one that can only throw single buttons from
    // neutral, and it is placed HERE, between the lifecycle above and the button
    // scan below, on purpose: the fighter is mid-move, so the scan below cannot
    // reach them, and the move has already been given its chance to end normally,
    // so a cancel never resurrects a move that expired on this very tick.
    if (f.moveId != 0) {
        const CancelEdge* edge = FindCancel(data, f, Input{it.bits});
        if (edge != nullptr) {
            // The buffered press this may have spent is NOT zeroed here: the
            // bookkeeping stage derives "a move started this tick" from
            // moveFrame == 0 and clears it there, so the buffer keeps exactly
            // one writing stage (docs/adr/ADR-012 rule 2).
            f.moveId    = edge->to;
            f.moveFrame = 0;
            adoptStance(f, *MoveAt(data, edge->to));
            // A NEW ACTIVE WINDOW, so the record of who the old one hit goes with
            // it. Without this line the follow-up inherits the source's hit bit
            // and can never connect on the same defender -- the whole combo would
            // consist of one hit and a lot of animation. It is the same clear
            // that a normal move start does, and it is deliberately the same
            // assignments, because a cancel that started a move by a slightly
            // different route than StepAttack's other start is a bug waiting
            // for the next field.
            f.alreadyHitBits = 0;
            f.flags = static_cast<std::uint16_t>(f.flags & ~kFlagsBlockedBits);
        }
        return;
    }

    // Fixed iteration order over a dense array, lowest slot first, so two moves
    // sharing a button resolve to the same one on every machine. Slot 0 is the
    // reserved idle slot and is skipped.
    //
    // PRESSED, not held -- and this is the field the note here used to ask for.
    // Holding a button used to restart the move the tick it recovered, which is
    // not a fighting-game mechanic and made any recorded "combo" suspect: one
    // key held is not a link. Fighter::prevButtons is in the STATE because a
    // rollback hands Simulate only the current tick's bits, so an edge computed
    // anywhere else is recomputed wrongly on every re-simulation.
    //
    // Three ways a move can be asked for, and a move takes the first that
    // applies:
    //
    //   press     a rising edge on every bit the move wants
    //   buffered  a press that arrived while this fighter could not act, kept
    //             for inputBufferFrames ticks and consumed here
    //   release   a falling edge, for a move that authored negativeEdge
    //
    // The edges arrive on the Intent, computed ONCE at the top of the tick, so
    // two stages cannot disagree about what was pressed.
    for (std::int32_t i = 1; i < data.moveCount && i < kMaxMovesPerFighter; ++i) {
        const MoveDef& m = data.moves[i];
        if (m.button == 0) continue;
        if (!StanceAllows(m, f, it.crouchWish)) continue;

        // Every bit the move wants must be HELD -- a chord is still a chord --
        // and at least one of them must have arrived this tick, or the move
        // would start again on every subsequent tick of the same hold.
        const bool byPress = (it.bits & m.button) == m.button &&
                             (it.pressed & m.button) != 0;
        // A buffered press is spent on any start -- the bookkeeping stage
        // clears it -- so it cannot fire again on the next actionable tick,
        // the rapid-fire bug in a different hat. Same all-bits-wanted rule.
        const bool byBuffer = (f.bufferedButtons & m.button) == m.button;
        // Release fires only for a move that asked for it, and only when the
        // whole chord was held on the previous tick.
        const bool byRelease = m.negativeEdge != 0 &&
                               (it.released & m.button) != 0 &&
                               (f.prevButtons & m.button) == m.button;

        if (!byPress && !byBuffer && !byRelease) continue;

        // Affordable, or this slot is not the one this press starts. Slot order
        // still decides, so a guarded move that cannot be paid for falls through
        // to the next slot sharing the button -- which is how a super and a
        // heavy normal live on one button in the genre.
        if (!GuardsMet(f, m)) continue;

        f.moveId         = static_cast<std::uint16_t>(i);
        f.moveFrame      = 0;
        f.alreadyHitBits = 0;
        f.flags = static_cast<std::uint16_t>(f.flags & ~kFlagsBlockedBits);
        adoptStance(f, m);
        return;
    }
}

// --- Hit resolution ---------------------------------------------------------

// --- Resources ---------------------------------------------------------------

// Every guard this move declares is satisfied by what the fighter currently
// holds. A guard is a MINIMUM, checked before the move starts, which is how a
// super refuses at zero meter rather than starting and going negative.
//
// The mask and not a zero test: zero is a legal minimum for a resource whose
// floor is negative, so "guard[i] == 0" cannot mean "no guard on i".
bool GuardsMet(const Fighter& f, const MoveDef& m) {
    if (m.guardMask == 0) return true;      // the common case, and free
    for (std::int32_t r = 0; r < kMaxResources; ++r)
        if ((m.guardMask & (1u << r)) != 0 && f.res[r] < m.guard[r]) return false;
    return true;
}

// Apply this move's deltas, clamped to each resource's authored range.
//
// CLAMPED RATHER THAN REFUSED, because the guard is where refusal lives: by the
// time this runs the move has already connected, and a gain that would exceed
// the ceiling is a full meter rather than a hit that did not happen. The floor
// does the same at the bottom, which is what stops a cost the guard let through
// -- a resource with no guard at all -- driving a counter negative forever.
void ApplyEffects(const FighterData& d, Fighter& f, const MoveDef& m) {
    for (std::int32_t r = 0; r < kMaxResources; ++r) {
        if (m.effect[r] == 0) continue;
        const ResourceDef& def = d.resources[r];
        std::int32_t v = f.res[r] + m.effect[r];
        if (v < def.floor) v = def.floor;
        if (def.hasCeiling != 0 && v > def.ceiling) v = def.ceiling;
        f.res[r] = v;
    }
}

void ResolveHits(GameState& state, const MatchData& data) {
    // THE ORDER PROBLEM, AND WHY THIS IS THREE LOOPS.
    //
    // If p0's hit were applied before p1's overlap were tested, a trade would
    // stop being a trade: p1 would already be in hitstun, its move already
    // interrupted, and whether it got to land its own blow would depend on which
    // slot happened to be checked first. That is not a rounding bug or a
    // container-order bug -- it is worse, because it is stable, so it never looks
    // like nondeterminism locally and instead just makes player 1 lose trades.
    //
    // So: every overlap is decided from the same pre-hit state, then the effects
    // are applied, then the interruptions. Each loop's writes are disjoint from
    // its own reads, so the result does not depend on the order within any loop
    // either. Priority and trade resolution proper -- a move that beats another
    // outright, clashes, counter-hits -- are Phase 3; the rule here is the
    // symmetric one, and both fighters land.
    // WHAT CHANGED WHEN THIS STOPPED BEING TWO FIGHTERS. The three loops survive
    // intact and for the same reason. What is gone is `const int d = 1 - a`: the
    // opponent is now any ACTIVE fighter on a DIFFERENT TEAM, tested as an
    // inequality so a third side would be a constant change rather than a logic
    // one (ADR-009 section 3). An attacker still lands at most ONE hit per tick,
    // and it lands it on the lowest-numbered defender it overlaps -- a fixed
    // order over a dense array, which is the same tie-break rule the button scan
    // and the cancel scan already use, chosen for the same reason.
    const int n = state.fighterCount < kMaxFighters
                      ? static_cast<int>(state.fighterCount)
                      : kMaxFighters;

    int  target[kMaxFighters];
    bool blocked[kMaxFighters];
    for (int i = 0; i < kMaxFighters; ++i) {
        target[i]  = -1;
        blocked[i] = false;
    }

    // --- DECIDE -------------------------------------------------------------
    // Every overlap is read out of the same pre-hit state. Nothing is written.
    for (int a = 0; a < n; ++a) {
        const Fighter& atk = state.p[a];
        if (atk.active == 0) continue;

        // A frozen fighter's hitbox is frozen with it. Without this the attacker
        // would keep connecting during its own hitstop and a heavy hit would
        // multi-hit for exactly as long as it felt good.
        if (atk.hitstop > 0) continue;

        Box hit{};
        if (!ActiveHitbox(data.p[a], atk, hit)) continue;

        const MoveDef* m = MoveAt(data.p[a], atk.moveId);
        if (m == nullptr) continue;

        const std::uint16_t kinds = AttackKinds(data.p[a], atk, *m);

        for (int d = 0; d < n; ++d) {
            if (d == a) continue;
            const Fighter& def = state.p[d];
            if (def.active == 0) continue;
            if (def.team == atk.team) continue;

            // The multi-hit guard. An active window that has already connected on
            // this defender cannot connect again, however many frames it stays
            // live -- without this, a 3-frame jab deals its damage three times and
            // every combo in the game is an infinite for a reason that has nothing
            // to do with the character.
            if ((atk.alreadyHitBits & bitForSlot(d)) != 0) continue;

            if (!BoxesOverlap(hit, Hurtbox(data.p[d], def))) continue;

            // INVINCIBILITY GOES HERE, IN THE BODY OF THE DECIDE LOOP, and that
            // placement is the point: it is one more reason the hit does not land,
            // resolved BEFORE priority is consulted at all. ADR-006 section 2 --
            // "a reversal beats a meaty because it is invincible, not because it
            // out-prioritises, and those are different games."
            if (InvulnerableTo(data.p[d], def, kinds)) continue;

            // The juggle budget refuses the hit that would overspend it. This is
            // the mechanism the prover's ranking certificate is written in: its
            // `nonNegative` condition is what ends all 41 of fighter_a's cycles in
            // the model, and its absence here is what let 33 of them run forever.
            if (m->juggleCost > 0 && def.juggle < m->juggleCost) continue;

            target[a]  = d;
            blocked[a] = defenderBlocks(def, *m);
            break;
        }
    }

    // --- PRIORITY -----------------------------------------------------------
    // The tail of the decide loop: every overlap is known and nothing has been
    // applied. Only a MUTUAL hit is arbitrated -- if a hits b and b hits a on the
    // same tick, the higher priority wins outright and the loser takes nothing.
    // EQUAL IS A TRADE and both land, which is what this function did before the
    // field existed and is why every character authored without it plays
    // identically. test_combat.cpp's ASimultaneousTradeIsSymmetric is that claim
    // as a test, and it must not move.
    //
    // Pairs are visited with a < b so each is arbitrated exactly once, in an
    // order two peers agree on without having to share anything but the state.
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (target[a] != b || target[b] != a) continue;

            const MoveDef* ma = MoveAt(data.p[a], state.p[a].moveId);
            const MoveDef* mb = MoveAt(data.p[b], state.p[b].moveId);
            if (ma == nullptr || mb == nullptr) continue;

            if (ma->priority > mb->priority)      target[b] = -1;
            else if (mb->priority > ma->priority) target[a] = -1;
            // else: equal, and a trade is what equal means.
        }
    }

    // --- APPLY --------------------------------------------------------------
    // Each iteration's writes are disjoint from every iteration's reads of the
    // pre-hit state, because the only thing read here is the attacker's own move
    // and the defender's own accumulators.
    for (int a = 0; a < n; ++a) {
        const int d = target[a];
        if (d < 0) continue;

        const MoveDef* m = MoveAt(data.p[a], state.p[a].moveId);
        if (m == nullptr) continue;

        Fighter& atk = state.p[a];
        Fighter& def = state.p[d];

        atk.alreadyHitBits =
            static_cast<std::uint8_t>(atk.alreadyHitBits | bitForSlot(d));

        // ON CONTACT, AND ON THE ATTACKER. A block is still contact -- a blocked
        // special that builds meter is the genre norm -- so this sits above the
        // blocked/hit fork rather than inside either arm. If a character ever
        // needs to pay differently for a block, that is a second authored field
        // and not a branch here.
        ApplyEffects(data.p[a], atk, *m);

        // Hitstop freezes BOTH fighters, which is what makes it read as impact
        // rather than as the defender lagging.
        if (m->hitstop > 0) {
            atk.hitstop = m->hitstop;
            def.hitstop = m->hitstop;
        }

        if (blocked[a]) {
            // The attacker's half of the outcome (M1.3 slice (a)): the contact
            // recorded in alreadyHitBits above was STOPPED, and the blocked
            // mirror is what lets an `on: hit` cancel refuse it and an
            // `on: block` cancel accept it. Set only here, in the arm that
            // knows; cleared wherever alreadyHitBits clears.
            atk.flags = static_cast<std::uint16_t>(atk.flags | bitForSlot(d));

            std::int32_t stun = m->blockstun;
            if (stun < 0) stun = 0;
            if (stun > kMaxStunTicks) stun = kMaxStunTicks;
            def.blockstun = static_cast<std::uint16_t>(stun);

            std::int32_t chip = m->chipDamage;
            if (chip < 0) chip = 0;
            // Chip does not kill. A block that finishes a round makes defence a
            // losing option in exactly the situation defence exists for.
            if (def.health > chip) def.health -= chip;
            else if (def.health > 0) def.health = 1;

            def.pushX += pushAwayFrom(atk, def, m->pushbackBlock);
            continue;
        }

        std::int32_t stun = m->hitstun;
        if (stun < 0) stun = 0;

        // HITSTUN DECAY, and it reads def.comboHits BEFORE this hit increments it
        // -- so the first hit of a combo decays by nothing, which is the only
        // reading under which "the third hit stuns less than the first" is what
        // the sentence means. The rule belongs to the DEFENDER, because it
        // describes how much stun this body suffers.
        const FighterData& dd = data.p[d];
        if (dd.hitstunDecayStep > 0) {
            stun -= dd.hitstunDecayStep * static_cast<std::int32_t>(def.comboHits);
            if (stun < dd.hitstunDecayFloor) stun = dd.hitstunDecayFloor;
            if (stun < 0) stun = 0;
        }

        if (stun > kMaxStunTicks) stun = kMaxStunTicks;
        // Set, not add. A fresh hit REFRESHES stun rather than stacking it;
        // stacking is how a two-hit string becomes an unescapable loop, and it is
        // not what any of the Phase-0 characters' frame data describes.
        def.hitstun = static_cast<std::uint16_t>(stun);

        // Damage, scaled by the combo already received. The multiply is done in
        // 32 bits against a percent, and the division truncates toward zero --
        // stated once here rather than at each site, which is D8's rule about
        // having exactly one documented quantization.
        std::int32_t dmg = m->damage;
        if (dmg < 0) dmg = 0;          // a negative damage value does not heal
        dmg = (dmg * static_cast<std::int32_t>(def.scaling)) / kScalingFull;
        def.health = def.health > dmg ? def.health - dmg : 0;

        // Proration compounds: each connected move multiplies what the NEXT one
        // will be worth. The reduction is clamped rather than trusted, because a
        // value above 100 would make the remaining scale negative and a uint16
        // would wrap it into an enormous damage multiplier -- the loader refuses
        // one, and this is the kernel declining to depend on that.
        std::int32_t cut = static_cast<std::int32_t>(m->scalingReduction);
        if (cut < 0)             cut = 0;
        if (cut > kScalingFull)  cut = kScalingFull;
        def.scaling = static_cast<std::uint16_t>(
            (static_cast<std::int32_t>(def.scaling) * (kScalingFull - cut)) /
            kScalingFull);

        if (m->juggleCost > 0) {
            def.juggle = static_cast<std::int16_t>(def.juggle - m->juggleCost);
        }

        if (def.comboHits < 0xFF) ++def.comboHits;

        if (m->knockdownTicks > 0) {
            def.knockdown = m->knockdownTicks;
            // A body on the ground may not block, so the guard already computed
            // this tick is revoked. `crouching` is deliberately NOT cleared
            // here: the physics stage clears it on the next tick's "cannot act"
            // rule, nothing reads it in between (the lying Hurtbox outranks
            // it), and a second stage clearing it is exactly the multi-writer
            // shape docs/adr/ADR-012 rule 2 exists to forbid.
            def.guard = kGuardNone;
        }

        def.pushX += pushAwayFrom(atk, def, m->pushbackHit);

        // CORNER PUSH (M1.6's microwalk slice): when the wall already stops
        // the defender, their pushback is absorbed and the pressure has to go
        // SOMEWHERE -- the genre sends it back through the attacker, which is
        // what re-opens the gap a microwalk then closes. "Cornered" is the
        // defender's origin standing AT its own wall limit, a byte test
        // against the same WallLimitFor the physics clamp uses -- one rule,
        // two askers, no reach model. The recoil rides pushX for pushback's
        // own recorded reason: velX is zeroed by commitment and stun exactly
        // when a fighter cannot act, and a recoil stored there would be
        // erased the tick it was applied.
        if (m->cornerPushHit != 0) {
            const std::int32_t lim = WallLimitFor(data.p[d], def);
            if (def.posX <= -lim || def.posX >= lim)
                atk.pushX += pushAwayFrom(def, atk, m->cornerPushHit);
        }
    }

    // --- INTERRUPT ----------------------------------------------------------
    for (int a = 0; a < n; ++a) {
        const int d = target[a];
        if (d < 0) continue;
        if (blocked[a]) continue;   // blocking does not interrupt; blockstun does

        // Being hit interrupts whatever the defender was doing. Without this a
        // fighter in hitstun keeps a live hitbox, because hitstun gates STARTING
        // a move and nothing else -- so it would go on swinging while it was
        // being hit, which is neither how a fighting game works nor something the
        // frame data could describe.
        state.p[d].moveId         = 0;
        state.p[d].moveFrame      = 0;
        state.p[d].alreadyHitBits = 0;
        state.p[d].flags =
            static_cast<std::uint16_t>(state.p[d].flags & ~kFlagsBlockedBits);
    }
}

} // namespace cse::kernel
