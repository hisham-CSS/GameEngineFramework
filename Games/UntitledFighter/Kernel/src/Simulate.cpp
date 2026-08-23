#include "cse/kernel/Simulate.h"

// <cstring> only. No <cmath>, no <algorithm> that would drag in a float
// overload, nothing from the engine. If this include list ever grows something
// that pulls libm, the kernel has stopped being portable-by-arithmetic and the
// crossplay guarantee (NORTHSTAR Q1) goes with it.
#include <cstring>

namespace cse::kernel {
namespace {

// Tuning constants. Sub-units per tick. These are placeholders for values that
// will come from character data once ADR-001's schema v2 lands -- the point of
// this file today is the SHAPE (integer, pure, snapshot-able), not the balance.
constexpr std::int32_t kWalkSpeed   = 2 * kSubUnitsPerPixel;      //  2 px/tick
constexpr std::int32_t kGravity     = kSubUnitsPerPixel / 4;      // .25 px/tick^2
constexpr std::int32_t kJumpImpulse = 5 * kSubUnitsPerPixel;

// A player is actionable when nothing is holding them still.
//
// Knockdown joins hitstun and blockstun here rather than getting its own gate,
// because "cannot act" is one idea and this is the one function that decides it.
// Hitstop is NOT in this list and must not be: a frozen fighter does not act
// because it does not advance at all, which is handled a level up. Folding it in
// here would freeze the fighter's agency while still ticking its move frames.
bool actionable(const Fighter& f) {
    return f.hitstun == 0 && f.blockstun == 0 && f.knockdown == 0;
}

// Integer-only clamp. Deliberately not std::clamp: that would be fine here, but
// keeping <algorithm> out of this translation unit removes an entire category of
// "someone reaches for std::max and gets the double overload" accident.
std::int32_t clampInt(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// xorshift32. Chosen over anything from <random> because the standard library's
// engines are not specified to produce identical sequences across
// implementations, and libstdc++ and the MSVC STL genuinely differ. Nine lines
// we own beats a portability question we cannot close.
std::uint32_t nextRandom(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// How far an ORIGIN may travel before the BODY reaches the wall.
//
// Asked for from play (2026-08-20): "we should calculate corner bounds from the
// back edge of the collider rather than the middle ... we don't want the player
// or enemy to disappear half into the corner." Clamping the origin let half a
// fighter hang past the wall, which reads as the character being eaten by the
// edge of the stage rather than standing against it.
//
// The allowance is the PUSHBOX rather than the hurtbox, because the pushbox is
// the space a fighter OCCUPIES -- it is already the box that stops two of them
// sharing ground, and the wall is the same question asked against the stage. A
// character with no pushbox gets the plain origin clamp, which is what everybody
// had before.
//
// Read off the PLACED box, so an asymmetric body is handled by facing instead of
// by assuming the origin sits in the middle of it. A body wider than the stage
// cannot be contained at all; the origin clamp is then the only answer that
// keeps posX inside the world.
std::int32_t wallLimitFor(const FighterData& data, const Fighter& f) {
    if (data.pushbox.x1 <= data.pushbox.x0) return kStageHalfWidthSub;

    const Box placed = PlaceBox(data.pushbox, 0, 0, f.facing);
    const std::int32_t back  = -placed.x0;
    const std::int32_t front =  placed.x1;
    const std::int32_t reach = back > front ? back : front;
    if (reach >= kStageHalfWidthSub) return kStageHalfWidthSub;
    return kStageHalfWidthSub - reach;
}

void stepFighter(Fighter& f, Input in, const FighterData& data) {
    // A benched tag partner keeps its health and its meter and does nothing else.
    // Returning before the stun counters is deliberate: a fighter tagged out
    // while stunned should still be stunned when it comes back, otherwise tagging
    // out is a free escape from every combo in the game.
    if (f.active == 0) return;

    // HITSTOP IS A FREEZE, AND IT IS THE FIRST THING BECAUSE IT FREEZES
    // EVERYTHING. Not movement only, and not agency only -- stun does not burn
    // down, moveFrame does not advance, gravity does not apply. That is what
    // keeps Combat.h's promise that hitstop "changes the meaning of every frame
    // number" only in wall-clock terms: startup 5 is still five ticks OF THE
    // MOVE, they simply take longer to arrive.
    if (f.hitstop > 0) {
        --f.hitstop;
        return;
    }

    // Stun burns down first, and while it does the player has no agency.
    if (f.hitstun   > 0) --f.hitstun;
    if (f.blockstun > 0) --f.blockstun;
    if (f.knockdown > 0) --f.knockdown;

    // NOT IN A COMBO. Everything that accumulates over one combo is restored in
    // one place, so the three cannot drift apart. Being out of hitstun is the
    // definition of not being in a combo, because a combo IS a sequence in which
    // the defender cannot act -- the same sentence the prover's model is built on.
    //
    // Written as a restore-every-tick rather than as an edge ("the tick hitstun
    // reached zero") on purpose. An edge would leave a freshly reset GameState
    // with a juggle budget of zero until it had been hit once and recovered,
    // which means the first combo of every round silently refuses every move
    // that costs juggle. Idempotent assignment costs three stores and has no
    // such state to get wrong.
    if (f.hitstun == 0) {
        f.comboHits = 0;
        f.scaling   = kScalingFull;
        f.juggle    = static_cast<std::int16_t>(data.juggleMax);
    }

    const bool canAct = actionable(f);

    // COMMITTED: a move that was already running at the top of this tick owns
    // the fighter for it. No walking, no jumping, no change of posture.
    //
    // Asked for from play (2026-08-21): "we can move and attack and crouch and
    // move as well ... not possible in normal fighting games (with certain moves
    // being an exception - rather than a rule)." That is what a frame count
    // MEANS in the genre -- startup, active and recovery are the frames you gave
    // up on the press -- and a range measured against an attacker who can slide
    // forward during startup is not a range. Every measured number in this repo
    // already assumed an attacker who stands still.
    //
    // READ AT THE TOP OF THE TICK, NOT AFTER StepAttack, and that one tick is
    // the whole of what keeps aerials startable. Up+button on one tick must take
    // off and then attack: stepFighter runs first, no move is running yet, the
    // jump goes through, and StepAttack then finds an airborne fighter asking
    // for an air move. Up pressed one tick INTO a grounded move is refused.
    //
    // THE EXCEPTION IS AUTHORED, NOT HERE. A move that carries the fighter -- a
    // lunge, a slide, a hop kick -- authors its own motion, which is ROADMAP
    // M1.3(b) and ADR-011's rule that no mechanic is a kernel constant. Until a
    // file says otherwise, nothing moves during an attack, which is the
    // conservative default.
    const bool committed = f.moveId != 0;
    const bool free      = canAct && !committed;

    if (free) {
        // THE FILE'S NUMBER WHEN THERE IS ONE. Zero means the character authored
        // none, and the placeholder above is then used unchanged -- see
        // FighterData::walkSpeedSub for why walk speed cannot stay a constant.
        const std::int32_t walk =
            data.walkSpeedSub != 0 ? data.walkSpeedSub : kWalkSpeed;

        std::int32_t wish = 0;
        if (in.bits & kInputLeft)  wish -= walk;
        if (in.bits & kInputRight) wish += walk;
        f.velX = wish;

        if ((in.bits & kInputUp) && !f.airborne) {
            f.velY = data.jumpImpulseSub != 0 ? data.jumpImpulseSub : kJumpImpulse;
            f.airborne = 1;
        }
    } else {
        f.velX = 0;
    }

    // Crouching is an INPUT posture and airborne is a POSITION fact, which is why
    // they are two fields; the impossible combination is closed here, by
    // construction, rather than by a convention somebody has to remember.
    //
    // A committed fighter KEEPS the posture the move started in rather than
    // dropping to standing: a crouching move's frame data was authored against
    // the crouching body, and a fighter whose hurtbox grew back to 60 px on
    // frame 2 of a slide would be hittable by everything the slide was built to
    // duck.
    if (free)
        f.crouching = (!f.airborne && (in.bits & kInputDown)) ? 1u : 0u;
    else if (!canAct)
        f.crouching = 0;   // stunned or downed: nobody is holding a crouch

    if (f.airborne) {
        // The file's number when there is one; zero means unauthored and the
        // placeholder above stands. No schema key sets this yet -- see
        // FighterData::gravitySub for why the field exists ahead of its author.
        f.velY -= data.gravitySub != 0 ? data.gravitySub : kGravity;
    }

    // Pushback rides its own field precisely so that this line survives the `else`
    // above zeroing velX -- being hit is exactly when a fighter cannot act, so
    // pushback stored in velX would be erased on the tick it was applied.
    //
    // The decay is a HALVING WRITTEN AS `/ 2` AND NOT `>> 1`. D2 spells out the
    // difference and it is not stylistic: `>>` rounds toward minus infinity while
    // `/` truncates toward zero, so a shift would decay leftward pushback and
    // rightward pushback by different amounts and hand the mirror-asymmetry bug
    // this kernel is built to avoid a way back in.
    // THE WALL STOPS THE BODY, NOT THE ORIGIN.
    //
    // Asked for from play (2026-08-20): "we should calculate corner bounds from
    // the back edge of the collider rather than the middle ... we don't want the
    // player or enemy to disappear half into the corner." Clamping the origin
    // let half a fighter hang past the wall, which reads as the character being
    // eaten by the edge of the stage rather than standing against it.
    //
    // The allowance is the PUSHBOX, not the hurtbox: the pushbox is the space a
    // fighter occupies, and it is already the box that decides they cannot share
    // ground with each other. A character with no pushbox falls back to the
    // origin clamp, which is what everybody had before.
    //
    // Read off the PLACED box so an asymmetric body is handled by facing rather
    // than by assuming the origin sits in the middle of it.
    const std::int32_t limit = wallLimitFor(data, f);
    f.posX = clampInt(f.posX + f.velX + f.pushX, -limit, limit);
    f.pushX /= 2;

    f.posY += f.velY;

    if (f.posY <= 0) {
        f.posY     = 0;
        f.velY     = 0;
        f.airborne = 0;
    }

    // The attack lifecycle: end a move that has run out, CANCEL one that is still
    // running into the follow-up the fighter is asking for, or start one from
    // idle. It replaces the bare `if (moveId != 0) ++moveFrame;` this file used to
    // end on, and for a moveId this character's table does not describe it does
    // exactly that and nothing more -- which is what keeps a state driven by a
    // harness behaving as it did before boxes existed.
    //
    // It runs AFTER movement so that a move started this tick sees the position
    // the fighter actually reached, and BEFORE hit resolution, which happens once
    // for both fighters below.
    //
    // THAT SECOND ORDERING IS WHAT SETS THE FASTEST CANCEL. A cancel is gated on
    // the source having connected, and connecting is decided by ResolveHits at
    // the bottom of this tick -- so a hit landing on tick N is first visible to a
    // cancel test on tick N+1, and an edge with an authored delay of zero fires
    // one tick after contact rather than on it. Moving the cancel test below
    // ResolveHits would buy that tick back and cost something much worse: a
    // fighter could then cancel a move on the very tick it started.
    StepAttack(f, data, in, canAct);

    // --- Input bookkeeping, LAST, and in this order -------------------------
    //
    // A press this tick that StepAttack did not use is remembered, so that a
    // link attempted a few frames early still comes out when the fighter
    // becomes actionable. That is what makes timing feel like timing rather
    // than a coin flip, and it is a per-character window rather than a constant
    // here (docs/adr/ADR-011 decision 1) -- zero means no buffering, which is
    // the kernel that shipped before this field.
    //
    // AFTER StepAttack, never before: the scan consumes a buffered press by
    // zeroing it, and recording first would immediately re-buffer the press it
    // just spent.
    if (data.inputBufferFrames > 0) {
        const std::uint16_t pressed =
            static_cast<std::uint16_t>(in.bits & ~f.prevButtons);
        // ONLY A PRESS THAT STARTED NOTHING, and the condition is subtler than
        // it first looks. `canAct` means NOT STUNNED, not "not busy": a fighter
        // in the middle of a move is actionable by that measure, and StepAttack
        // handles them through the cancel path instead. So `!canAct` buffers
        // almost nothing -- most early presses arrive mid-move, exactly when a
        // player is trying to link -- and a first draft of this used it and
        // reported the buffer dropping every press.
        //
        // A MOVE STARTED THIS TICK is `moveFrame == 0` with a move in progress,
        // which is the same signal ComboWatcher, the demonstration cursor and
        // the drivers all key on: a self-cancel keeps moveId the same, so a
        // transition detector sees nothing. If nothing started, the press went
        // unused and is worth keeping.
        const bool startedThisTick = f.moveId != 0 && f.moveFrame == 0;
        if (pressed != 0 && !startedThisTick) {
            f.bufferedButtons = pressed;
            f.bufferAge       = 0;
        } else if (f.bufferedButtons != 0) {
            // Aged, then dropped. A buffer that never expired would fire a move
            // minutes after the press, which is worse than not buffering.
            ++f.bufferAge;
            if (static_cast<std::int32_t>(f.bufferAge) >= data.inputBufferFrames) {
                f.bufferedButtons = 0;
                f.bufferAge       = 0;
            }
        }
    } else {
        f.bufferedButtons = 0;
        f.bufferAge       = 0;
    }

    // LAST of all: this tick's buttons become next tick's `previous`. Every edge
    // above is computed against the value from before this line ran.
    f.prevButtons = in.bits;
}

// How many slots this match uses, clamped so a corrupt byte cannot walk the
// array. Zero is legal and means a state nothing has set up yet.
int liveCount(const GameState& state) {
    return state.fighterCount < kMaxFighters
               ? static_cast<int>(state.fighterCount)
               : kMaxFighters;
}

// Each active fighter faces its NEAREST active opposing fighter.
//
// The two-fighter version of this was a single comparison. With N there is no
// single comparison, and the replacement has to satisfy something the old rule
// got for free: IT MUST NOT DEPEND ON ITERATION ORDER. Ties are broken by the
// lowest slot index -- reachable, because it is the opening position of a
// symmetric 2v2 -- and "whichever the loop saw first" is the hash-ordering
// desync this file already names twice.
void resolveFacing(GameState& state, int n) {
    for (int a = 0; a < n; ++a) {
        Fighter& f = state.p[a];
        if (f.active == 0) continue;

        int          best     = -1;
        std::int32_t bestDist = 0;
        for (int d = 0; d < n; ++d) {
            if (d == a) continue;
            const Fighter& o = state.p[d];
            if (o.active == 0) continue;
            if (o.team == f.team) continue;

            std::int32_t dist = o.posX - f.posX;
            if (dist < 0) dist = -dist;

            // STRICTLY less, so an equal distance keeps the lower index.
            if (best < 0 || dist < bestDist) {
                best     = d;
                bestDist = dist;
            }
        }

        // Nobody to face -- a whole team benched or defeated. Keep the facing
        // that was already there rather than inventing one.
        if (best < 0) continue;

        const std::int32_t dx = state.p[best].posX - f.posX;
        if (dx > 0)      f.facing = 0;
        else if (dx < 0) f.facing = 1;
        // Exactly co-located: the lower slot faces +X. This reproduces the old
        // two-fighter rule's behaviour at equality bit for bit, which is worth
        // having even though the golden moves for other reasons anyway.
        else             f.facing = (a < best) ? 0u : 1u;
    }
}

// What each fighter is guarding this tick.
//
// EVERY CONDITION ON BEING ABLE TO BLOCK AT ALL IS HERE, in one function, and
// Combat.cpp's defenderBlocks then asks only about height. Splitting them is how
// one condition ends up enforced and another does not.
//
// It runs AFTER facing, because "back" is defined relative to the opponent and a
// facing resolved one tick late is a cross-up that blocks the wrong way.
void resolveGuard(Fighter& f, Input in) {
    f.guard = kGuardNone;

    if (f.active == 0) return;
    if (f.hitstop > 0) return;

    // Hitstun and knockdown forbid guarding; BLOCKSTUN DOES NOT, and that
    // distinction is the whole of a blockstring: a fighter already blocking stays
    // in guard for the next hit of the string.
    if (f.hitstun > 0 || f.knockdown > 0) return;

    if (f.airborne != 0) return;   // no air blocking
    if (f.moveId != 0)   return;   // committed to a move

    const std::uint16_t back = (f.facing == 0) ? kInputLeft : kInputRight;
    if ((in.bits & back) == 0) return;

    // Read from the INPUT rather than from Fighter::crouching, because crouching
    // is cleared while a fighter cannot act and blockstun is exactly that state.
    // Reading the derived field would silently turn every low in a blockstring
    // into an unblockable after the first hit.
    f.guard = (in.bits & kInputDown) ? kGuardLow : kGuardHigh;
}

// Round and match flow.
//
// THE KERNEL DECIDES THAT A ROUND ENDED AND NEVER THAT A NEW ONE BEGINS. Setting
// up the next round is placing fighters, restoring health and choosing who is
// active -- which is ResetMatch's job and therefore the host's call, because only
// the host knows whether this is a best-of-three, a training reset, or a World
// Tour battle that ends the moment the story fight is won.
void stepRound(GameState& state, int n) {
    if (state.roundState != kRoundFighting) return;

    bool alive[kMaxTeams] = {};
    for (int i = 0; i < n; ++i) {
        const Fighter& f = state.p[i];
        if (f.team >= kMaxTeams) continue;
        // Benched partners count. A team is out when its LAST body is out, which
        // is what makes tag a team game rather than a sequence of duels.
        if (f.health > 0) alive[f.team] = true;
    }

    int winner = -1;
    for (int t = 0; t < kMaxTeams; ++t) {
        if (alive[t]) continue;
        // The other team took it. With two sides that is the one that is left;
        // if both were wiped out on the same tick, the lower index wins the
        // scan and neither peer has to arbitrate anything.
        winner = (t == 0) ? 1 : 0;
        break;
    }

    if (winner < 0 && state.roundTimer > 0) {
        --state.roundTimer;
        if (state.roundTimer == 0) {
            // Time out. The team with more health left takes it; exactly equal is
            // a draw, which awards nothing and is a real outcome rather than a
            // coin flip two peers would have to agree on.
            std::int32_t total[kMaxTeams] = {};
            for (int i = 0; i < n; ++i) {
                const Fighter& f = state.p[i];
                if (f.team < kMaxTeams) total[f.team] += f.health;
            }
            if (total[0] > total[1])      winner = 0;
            else if (total[1] > total[0]) winner = 1;
            else                          state.roundState = kRoundOver;
        }
    }

    if (winner < 0) return;

    state.roundState = kRoundOver;
    if (state.roundsWon[winner] < 0xFF) ++state.roundsWon[winner];
    if (state.roundsToWin != 0 && state.roundsWon[winner] >= state.roundsToWin) {
        state.roundState = kMatchOver;
    }
}

} // namespace

// --- Push boxes ---------------------------------------------------------------

// Two fighters may not stand in the same place.
//
// Asked for from play (2026-08-20): "the enemy collider should be blocking
// collisions rather than trigger -- we don't want to move through them ... if
// you just run into them you should be blocked by the character's hurtbox
// without taking damage - this prevents players and enemies overlapping
// hurtboxes and missing attacks because of that." That last clause is the real
// cost of not having this: two bodies in the same place make ranges meaningless
// and attacks whiff for reasons nobody can see.
//
// AIRBORNE FIGHTERS PASS OVER, which is what makes a jump a way past somebody
// rather than a bounce off them. It is also why crossing up works at all: the
// jump arc carries you through the space the pushbox would otherwise hold.
// Getting past a grounded opponent on the ground is left to the moves that say
// they can -- a teleport, a lunge -- which is a per-move field when those
// arrive, not a hole here.
//
// THE SPLIT IS EQUAL AND ROUNDS UP, and both halves of that are load-bearing.
// Equal, so the resolution is a MIRROR: the same collision reflected through
// x = 0 must produce reflected positions, which an "always push the left one"
// rule breaks immediately. Rounds up, so an ODD overlap is actually resolved --
// truncating leaves the two a sub-unit inside each other forever, and "never
// overlap" would be true only for even numbers.
void separatePushboxes(GameState& state, const MatchData& data) {
    Fighter& a = state.p[0];
    Fighter& b = state.p[1];

    // A degenerate box is how FighterData spells "unauthored"; a character
    // without one is not separated from anybody.
    const Box& ba = data.p[0].pushbox;
    const Box& bb = data.p[1].pushbox;
    if (ba.x1 <= ba.x0 || bb.x1 <= bb.x0) return;
    if (a.airborne != 0 || b.airborne != 0) return;

    // Two passes. The first splits the overlap evenly, which is the answer
    // whenever both fighters have room; the second exists for the CORNER, where
    // the stage clamp undoes one fighter's share and the other has to absorb the
    // whole of it. Two is enough because the second pass moves only one body and
    // the stage cannot push back twice.
    for (int pass = 0; pass < 2; ++pass) {
        const Box pa = PlaceBox(ba, a.posX, a.posY, a.facing);
        const Box pb = PlaceBox(bb, b.posX, b.posY, b.facing);

        const std::int32_t overlap = (pa.x1 < pb.x1 ? pa.x1 : pb.x1) -
                                     (pa.x0 > pb.x0 ? pa.x0 : pb.x0);
        if (overlap <= 0) return;

        // Who is on the left is decided by the ORIGINS, not by the boxes: the
        // boxes are what overlap, so asking them which is left is circular when
        // the two are nearly coincident. Equal origins fall to slot order, which
        // is arbitrary and has to be SOMETHING -- and it is the same fixed order
        // Simulate uses everywhere else, so it is at least the arbitrary choice
        // this kernel already made.
        const bool aIsLeft = a.posX <= b.posX;
        const std::int32_t half = (overlap + 1) / 2;

        Fighter& left  = aIsLeft ? a : b;
        Fighter& right = aIsLeft ? b : a;

        // The SAME body-aware limit stepFighter uses, or separation would shove
        // into the corner the half-body the wall clamp just refused.
        const std::int32_t lLimit = wallLimitFor(aIsLeft ? data.p[0] : data.p[1], left);
        const std::int32_t rLimit = wallLimitFor(aIsLeft ? data.p[1] : data.p[0], right);

        left.posX  = clampInt(left.posX - half,  -lLimit, lLimit);
        right.posX = clampInt(right.posX + half, -rLimit, rLimit);
    }
}

void Simulate(GameState& state, const InputPair& inputs, const MatchData& data) {
    // Fixed order, always. Iterating a container whose order can vary -- the
    // hash-ordering hazard that bit SimplePhysicsBackend and ScriptWorld in this
    // repository -- is how a simulation stops being deterministic without
    // anybody changing the arithmetic. A dense array walked by index is that
    // rule's simplest possible form, and it is why widening from two fighters to
    // eight changed nothing about determinism.
    const int n = liveCount(state);

    // RESOURCES ARE PRIMED ON THE FIRST TICK, not in ResetMatch, and the reason
    // is the one already written beside the juggle restore below it: ResetMatch
    // does not take the character data, and "restored from the character data on
    // the first tick, before anything can read it" is where that rule already
    // lives. Doing it here rather than in stepFighter keeps it in ONE visible
    // place instead of eight copies of a first-tick condition.
    //
    // It cannot join the per-combo restore beside juggle, and that is the whole
    // design point. Juggle is spent within one combo and restored when the
    // defender leaves hitstun; METER IS NOT -- `fighter_a` opens with 300 of it
    // and spends it across a round. A restore-every-idle-tick would hand the
    // meter back the instant the defender recovered, which is not a balance
    // choice, it is a resource that cannot be spent.
    //
    // Tick zero rather than a "primed" flag because GameState may not grow here
    // (this WP is the data path onto M1.1a's fields, not a second expansion) and
    // because a flag would be a byte two peers could disagree about. Re-running
    // tick zero after a rollback re-primes to exactly the same numbers, which is
    // what makes a tick-index condition safe in a re-simulating kernel at all.
    if (state.tick == 0) {
        for (int i = 0; i < n; ++i)
            for (std::int32_t r = 0; r < kMaxResources; ++r)
                state.p[i].res[r] = data.p[i].resources[r].initial;
    }

    // WHERE EVERYONE WAS BEFORE ANYBODY MOVED. The separation clamp below reads
    // this snapshot rather than the live positions, and that is what makes it
    // ORDER-INDEPENDENT: clamping p0 against p1's new position and then p1
    // against p0's newly-clamped one would give a different answer depending on
    // which slot was stepped first, which is the exact class of bug D3 exists to
    // keep out of this kernel.
    std::int32_t wasAtX[kMaxFighters];
    for (int i = 0; i < n; ++i) wasAtX[i] = state.p[i].posX;

    for (int i = 0; i < n; ++i) {
        stepFighter(state.p[i], inputs.p[i], data.p[i]);
    }

    // --- The invisible wall ---------------------------------------------------
    //
    // Neither fighter may end further than kMaxSeparationSub from where the
    // OTHER ONE STOOD AT THE TOP OF THIS TICK. That one sentence is the whole
    // Street Fighter behaviour the author described: a player walking backwards
    // stops dead at the limit, and if the opponent then advances a pixel the
    // limit follows them by a pixel and the retreat resumes. Retreat is possible
    // for exactly as long as somebody is chasing.
    //
    // Reading the PRE-MOVE opponent is also what makes that true rather than
    // approximately true. Against the post-move position a retreating player
    // would be allowed the opponent's step in the same tick they took it, which
    // lets two fighters walking apart drift forever at walking speed.
    //
    // TWO FIGHTERS ONLY. With three or more there is no "the opponent" and the
    // rule would have to say which pair the camera belongs to -- a real question
    // for tag modes and not one to answer by accident here. ADR-009 widened the
    // state to eight and said the rules would arrive one at a time; this is one
    // of them arriving for the 1v1 case.
    //
    // It only ever pulls INWARD, so it cannot push anyone through the stage
    // clamp stepFighter already applied.
    if (n == 2) {
        for (int i = 0; i < 2; ++i) {
            const std::int32_t anchor = wasAtX[1 - i];
            state.p[i].posX = clampInt(state.p[i].posX,
                                       anchor - kMaxSeparationSub,
                                       anchor + kMaxSeparationSub);
        }

        separatePushboxes(state, data);
    }

    // Facing is derived from relative position, evaluated AFTER everyone has
    // moved so it cannot depend on which fighter was stepped first.
    resolveFacing(state, n);

    // Guard is derived from facing, so it comes after it.
    for (int i = 0; i < n; ++i) {
        resolveGuard(state.p[i], inputs.p[i]);
    }

    // Hits are resolved after facing, because every box is authored facing +X
    // and mirrored by facing when it is placed -- so a box built before facing
    // settled would be the box the fighter had LAST tick. One tick of a stale
    // mirror is a hit that lands behind a character who just turned around.
    ResolveHits(state, data);

    // And the round rule reads the healths hits have just written.
    stepRound(state, n);

    // Advance the stream every tick whether or not anything consumed it, so the
    // RNG position is a function of the tick count alone. A stream that advances
    // only on some ticks makes the sequence depend on gameplay history, which is
    // still deterministic but far harder to reason about when a desync appears.
    nextRandom(state.rng);

    ++state.tick;
}

void Simulate(GameState& state, const InputPair& inputs) {
    // kNoMoves has moveCount 0 and a degenerate hurtbox, so StepAttack can start
    // nothing, ActiveHitbox can return nothing, and ResolveHits can find no
    // overlap. This overload is therefore the pre-hitbox kernel exactly, not
    // approximately -- which is the point, because the cross-toolchain golden
    // hashes were recorded against it and re-recording a golden is how the
    // evidence that two platforms agree gets destroyed.
    Simulate(state, inputs, kNoMoves);
}

MatchSetup DefaultMatchSetup(std::uint32_t seed) {
    MatchSetup s{};
    s.seed          = seed;
    s.roundTicks    = 0;   // untimed
    s.roundsToWin   = 0;   // the set never ends
    s.fighterCount  = 2;

    s.p[0].startPosX   = -100 * kSubUnitsPerPixel;
    s.p[0].startHealth = 1000;
    s.p[0].team        = 0;
    s.p[0].active      = 1;
    s.p[0].facing      = 0;

    s.p[1].startPosX   =  100 * kSubUnitsPerPixel;
    s.p[1].startHealth = 1000;
    s.p[1].team        = 1;
    s.p[1].active      = 1;
    s.p[1].facing      = 1;

    return s;
}

void ResetMatch(GameState& state, const MatchSetup& setup) {
    // Zeroing FIRST is what makes the unused slots deterministic. ADR-009 section
    // 6 accepts that a 1v1 match hashes six zeroed fighters every tick; this line
    // is the reason that is safe rather than merely cheap.
    std::memset(&state, 0, sizeof(GameState));

    // Never zero: xorshift is absorbing at zero and would return 0 forever.
    state.rng = setup.seed != 0u ? setup.seed : 0x9E3779B9u;

    state.roundTimer  = setup.roundTicks;
    state.roundsToWin = setup.roundsToWin;
    state.roundNumber = 1;
    state.roundState  = kRoundFighting;

    const int n = setup.fighterCount < kMaxFighters
                      ? static_cast<int>(setup.fighterCount)
                      : kMaxFighters;
    state.fighterCount = static_cast<std::uint8_t>(n);

    for (int i = 0; i < n; ++i) {
        const FighterSetup& in = setup.p[i];
        Fighter&            f  = state.p[i];

        f.posX    = in.startPosX;
        f.health  = in.startHealth;
        f.team    = in.team;
        f.active  = in.active;
        f.facing  = in.facing;
        f.scaling = kScalingFull;
        // juggle is deliberately left at zero: stepFighter restores it from the
        // character data on the first tick, before anything can read it, and that
        // is the ONE place the restore rule lives.
    }
}

void ResetMatch(GameState& state, std::uint32_t seed) {
    // Expressed in terms of the general path rather than beside it, so the 1v1
    // default and the setup-driven path cannot drift. That drift is the whole
    // hazard ADR-009 section 5 describes: every fight in the game started at
    // +/-100 pixels with 1000 health because the only way to start one said so.
    ResetMatch(state, DefaultMatchSetup(seed));
}

std::uint32_t Checksum(const GameState& state) {
    // Reading the object representation through unsigned char is the one aliasing
    // route the standard actually blesses.
    const auto* bytes = reinterpret_cast<const unsigned char*>(&state);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < sizeof(GameState); ++i) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

} // namespace cse::kernel
