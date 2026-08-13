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
    // RANGE ANALYSIS, which §4.3 requires rather than trusting:
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
    return PlaceBox(data.hurtbox, f.posX, f.posY, f.facing);
}

// --- Cancels ----------------------------------------------------------------

bool CancelIsOpen(const Fighter& f, const CancelEdge& edge) {
    if (f.moveId == 0) return false;
    if (edge.from != f.moveId) return false;

    // The contact gate. `onHit` collapses the schema's four Contact values into
    // the one distinction the kernel can actually observe -- see CancelEdge in
    // Combat.h -- and alreadyHitBits is the observation.
    if (edge.onHit != 0 && f.alreadyHitBits == 0) return false;

    // Both bounds inclusive. An edge whose earliest is past its latest matches
    // nothing for any frame, which is how an authored delay longer than the
    // source move ends up inert instead of ending up wrong.
    const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
    return frame >= edge.earliestFrame && frame <= edge.latestFrame;
}

const CancelEdge* FindCancel(const FighterData& data, const Fighter& f, Input in) {
    // A fighter with no move in progress has nothing to cancel, and a moveId this
    // character's table does not describe is INERT here for the same reason it is
    // inert in StepAttack: a harness that drives moveId by hand must keep getting
    // the behaviour it got before this file grew a cancel system.
    if (MoveAt(data, f.moveId) == nullptr) return nullptr;

    for (std::int32_t i = 0; i < data.cancelCount && i < kMaxCancelsPerFighter; ++i) {
        const CancelEdge& e = data.cancels[i];
        if (!CancelIsOpen(f, e)) continue;

        // The target has to be a move this character actually has. An edge whose
        // `to` fell outside the table would otherwise put the fighter into a
        // moveId nothing can describe, which is exactly the state MatchBuilder
        // refuses to build and the kernel should not be able to reach either.
        const MoveDef* target = MoveAt(data, e.to);
        if (target == nullptr) continue;

        // HELD, not pressed -- the same limitation StepAttack's button scan has
        // and for the same missing field. It bites slightly differently here: a
        // player holding the follow-up button through the whole source move takes
        // the cancel on the first frame of its window rather than on the frame
        // they chose. That is a real difference from the genre and it is the
        // second thing prevButtons would fix.
        if (target->button == 0) continue;
        if ((in.bits & target->button) != target->button) continue;

        return &e;
    }
    return nullptr;
}

// --- The move lifecycle -----------------------------------------------------

void StepAttack(Fighter& f, const FighterData& data, Input in, bool actionable) {
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
            // per match.
            f.alreadyHitBits = 0;
        }
    }

    // Hitstun and blockstun gate EVERYTHING a fighter chooses to do, cancels
    // included. A fighter who was hit has already had their move interrupted by
    // ResolveHits, so this is belt and braces -- but it is the belt that keeps
    // "cancel" from quietly becoming "act out of hitstun", which is a different
    // and much larger feature.
    if (!actionable) return;

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
        const CancelEdge* edge = FindCancel(data, f, in);
        if (edge != nullptr) {
            f.moveId    = edge->to;
            f.moveFrame = 0;
            // A NEW ACTIVE WINDOW, so the record of who the old one hit goes with
            // it. Without this line the follow-up inherits the source's hit bit
            // and can never connect on the same defender -- the whole combo would
            // consist of one hit and a lot of animation. It is the same clear
            // that a normal move start does, and it is deliberately the same
            // three assignments, because a cancel that started a move by a
            // slightly different route than StepAttack's other start is a bug
            // waiting for the fourth field.
            f.alreadyHitBits = 0;
        }
        return;
    }

    // Fixed iteration order over a dense array, lowest slot first, so two moves
    // sharing a button resolve to the same one on every machine. Slot 0 is the
    // reserved idle slot and is skipped.
    //
    // HELD, not pressed. Distinguishing a press from a hold needs the previous
    // tick's buttons, and a rollback hands Simulate only the current tick's input
    // -- so honest edge detection needs a `prevButtons` field inside GameState,
    // which is a state addition with its own consequences (D6 puts the input ring
    // OUTSIDE the state on purpose). Holding a button therefore repeats a move as
    // soon as the previous one recovers. That is a real gap, named rather than
    // papered over, and it is the next field this file will want.
    for (std::int32_t i = 1; i < data.moveCount && i < kMaxMovesPerFighter; ++i) {
        const MoveDef& m = data.moves[i];
        if (m.button == 0) continue;
        if ((in.bits & m.button) != m.button) continue;

        f.moveId         = static_cast<std::uint16_t>(i);
        f.moveFrame      = 0;
        f.alreadyHitBits = 0;
        return;
    }
}

// --- Hit resolution ---------------------------------------------------------

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
    bool lands[2] = { false, false };

    for (int a = 0; a < 2; ++a) {
        const int d = 1 - a;
        // The multi-hit guard. An active window that has already connected on
        // this defender cannot connect again, however many frames it stays live
        // -- without this, a 3-frame jab deals its damage three times and every
        // combo in the game is an infinite for a reason that has nothing to do
        // with the character.
        if ((state.p[a].alreadyHitBits & bitForSlot(d)) != 0) continue;

        Box hit{};
        if (!ActiveHitbox(data.p[a], state.p[a], hit)) continue;

        const Box hurt = Hurtbox(data.p[d], state.p[d]);
        lands[a] = BoxesOverlap(hit, hurt);
    }

    for (int a = 0; a < 2; ++a) {
        if (!lands[a]) continue;
        const int d = 1 - a;

        // Non-null: ActiveHitbox already said so, on this same unmodified state.
        const MoveDef* m = MoveAt(data.p[a], state.p[a].moveId);
        if (m == nullptr) continue;

        state.p[a].alreadyHitBits =
            static_cast<std::uint8_t>(state.p[a].alreadyHitBits | bitForSlot(d));

        std::int32_t stun = m->hitstun;
        if (stun < 0) stun = 0;
        if (stun > kMaxStunTicks) stun = kMaxStunTicks;
        // Set, not add. A fresh hit REFRESHES stun rather than stacking it;
        // stacking is how a two-hit string becomes an unescapable loop, and it is
        // not what any of the Phase-0 characters' frame data describes.
        state.p[d].hitstun = static_cast<std::uint16_t>(stun);

        std::int32_t dmg = m->damage;
        if (dmg < 0) dmg = 0;          // a negative damage value does not heal
        state.p[d].health = state.p[d].health > dmg ? state.p[d].health - dmg : 0;
    }

    for (int d = 0; d < 2; ++d) {
        const int a = 1 - d;
        if (!lands[a]) continue;
        // Being hit interrupts whatever the defender was doing. Without this a
        // fighter in hitstun keeps a live hitbox, because hitstun gates STARTING
        // a move and nothing else -- so it would go on swinging while it was
        // being hit, which is neither how a fighting game works nor something the
        // frame data could describe.
        state.p[d].moveId         = 0;
        state.p[d].moveFrame      = 0;
        state.p[d].alreadyHitBits = 0;
    }
}

} // namespace cse::kernel
