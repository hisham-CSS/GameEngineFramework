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
// Promoted to Combat.h/.cpp as WallLimitFor when ResolveHits' corner push
// became its second asker (M1.6's microwalk slice) -- the essay above rides
// with it. This alias keeps the file's call sites reading as before.
std::int32_t wallLimitFor(const FighterData& data, const Fighter& f) {
    return WallLimitFor(data, f);
}

// --- The tick pipeline (docs/adr/ADR-012) ------------------------------------
//
// Four fixed stages: ReadIntent (pure -- what does this input MEAN), StepPhysics
// (movement, stuns and the freeze), StepAttack (the move lifecycle, Combat.cpp)
// and Resolve, whose per-fighter head is LatchInputs below. Each stage's writes
// are its contract, listed at its definition; a field written by two stages is
// a bug by definition, and the deliberate exceptions are named in Simulate.h's
// audit table.

// What this fighter's input means, computed ONCE and read by every stage after
// it. Pure, so a re-simulated tick cannot re-derive it differently.
Intent ReadIntent(const Fighter& f, Input in, const FighterData& data) {
    Intent it{};
    it.bits     = in.bits;
    it.pressed  = static_cast<std::uint16_t>(in.bits & ~f.prevButtons);
    it.released = static_cast<std::uint16_t>(~in.bits & f.prevButtons);

    // ONLY BITS SOME MOVE CAN USE may enter the buffer. `pressed` carries all
    // sixteen bits, directions included, and the buffer is replace-on-write --
    // so without this mask a nervous forward tap during hitstun REPLACES a
    // buffered reversal with a direction that can never match any move's
    // button. The tap could start nothing; the press it destroyed could. Found
    // by adversarial review (2026-08-21): it plays as "the game eats my
    // inputs sometimes", the exact feel a buffer exists to remove. The union
    // is re-computed each tick rather than cached in a field, because a cached
    // union would be one more byte the connect handshake hashes.
    if (data.inputBufferFrames > 0) {
        std::uint16_t usable = 0;
        for (std::int32_t i = 1; i < data.moveCount && i < kMaxMovesPerFighter; ++i)
            usable |= data.moves[i].button;
        it.buffable = static_cast<std::uint16_t>(it.pressed & usable);
    }

    // THE FILE'S NUMBER WHEN THERE IS ONE; zero means unauthored and the
    // placeholder stands -- see FighterData::walkSpeedSub for why.
    const std::int32_t walk =
        data.walkSpeedSub != 0 ? data.walkSpeedSub : kWalkSpeed;
    if (in.bits & kInputLeft)  it.walkWish -= walk;
    if (in.bits & kInputRight) it.walkWish += walk;

    it.jumpWish   = (in.bits & kInputUp)   != 0;
    it.crouchWish = (in.bits & kInputDown) != 0;
    it.frozen     = f.hitstop > 0;
    return it;
}

// Movement, gravity, the stun clocks and the freeze. Writes: the four clocks,
// the out-of-combo restore (comboHits/scaling/juggle), velX, velY, airborne,
// crouching, posX, posY, pushX. The clamps that need the OTHER fighter -- the
// invisible wall and the pushboxes -- are Resolve's.
void StepPhysics(Fighter& f, const Intent& it, const FighterData& data) {
    // A benched tag partner keeps its health and its meter and does nothing
    // else. Returning before the stun clocks is deliberate: a fighter tagged
    // out while stunned should still be stunned when it comes back, otherwise
    // tagging out is a free escape from every combo in the game.
    if (f.active == 0) return;

    // HITSTOP IS A FREEZE, AND IT IS THE FIRST THING BECAUSE IT FREEZES
    // EVERYTHING. Not movement only, and not agency only -- stun does not burn
    // down, moveFrame does not advance, gravity does not apply. That is what
    // keeps Combat.h's promise that hitstop "changes the meaning of every frame
    // number" only in wall-clock terms: startup 5 is still five ticks OF THE
    // MOVE, they simply take longer to arrive.
    if (it.frozen) {
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

    const bool canAct = Actionable(f);

    // COMMITTED: a move that was already running at the top of this tick owns
    // the fighter for it. No walking, no jumping, no change of posture.
    //
    // Asked for from play (2026-08-21): "we can move and attack and crouch and
    // move as well ... not possible in normal fighting games (with certain moves
    // being an exception - rather than a rule)." That is what a frame count
    // MEANS in the genre -- startup, active and recovery are the frames you gave
    // up on the press -- and a range measured against an attacker who can slide
    // forward during startup is not a range.
    //
    // READ BEFORE StepAttack RUNS, and that one tick is what keeps aerials
    // startable: Up+button takes off here, and StepAttack then finds an
    // airborne fighter asking for an air move. Up pressed one tick INTO a
    // grounded move is refused. THE EXCEPTION IS AUTHORED, NOT HERE: a lunge, a
    // slide or a hop kick authors its own motion -- MoveDef::motion since
    // M1.3(b2), applied below under ADR-011's rule that no mechanic is a
    // kernel constant.
    const bool committed = f.moveId != 0;
    const bool free      = canAct && !committed;

    // THE JUMP IS BALLISTIC. Horizontal velocity in the air was decided at
    // takeoff -- neutral, forward or back, from the direction held on the jump
    // tick -- and NOTHING in the air recomputes it. Not an attack: an air
    // normal rides the jump, and zeroing velX mid-arc (the first commitment
    // draft did) stops the fighter dead so the attack lands behind where the
    // jump was taking it. And not a held direction: where you land is chosen
    // when you leave the ground, which is what makes a jump a commitment and an
    // anti-air a read rather than a chase.
    //
    // Asked for from play (2026-08-21): "jumping normals should not block
    // movement ... mostly they keep their momentum during the entire jump." A
    // divekick that changes trajectory is an authored per-move motion (ROADMAP
    // M1.3(b)), never a rule here.
    //
    // Being hit still zeroes it: air hitstun replaces momentum, and the launch
    // vector that should replace it PROPERLY is M1.3(d)'s field.
    //
    // THE AUTHORED EXCEPTION the commitment comment above promised (ROADMAP
    // M1.3(b2), ADR-014): a committed fighter's velocity is zero UNLESS ITS
    // MOVE authors motion. The active key -- the last one whose fromTick is
    // at or before this tick's observed moveFrame -- owns BOTH velocity
    // components for the tick: `forward` resolves against facing by a branch
    // (never facing multiplied into a coordinate -- MirrorBox's rule), an
    // upward key lifts the fighter off the ground, and gravity does not
    // apply while a key owns the arc, because the keys are RESOLVED velocity
    // states (Combat.h::MotionKey) and the segment IS the trajectory. Stun
    // still wins: an interrupted fighter lost its move at the previous
    // Resolve, so `committed` is already false for it here.
    const MoveDef* const motionMove = committed ? MoveAt(data, f.moveId) : nullptr;
    const MotionKey*     motionKey  = nullptr;
    if (motionMove != nullptr) {
        const std::int32_t frame = static_cast<std::int32_t>(f.moveFrame);
        for (std::int32_t i = 0;
             i < motionMove->motionCount && i < kMaxMotionKeys; ++i)
            if (motionMove->motion[i].fromTick <= frame &&
                (motionKey == nullptr ||
                 motionMove->motion[i].fromTick >= motionKey->fromTick))
                motionKey = &motionMove->motion[i];
    }

    if (motionKey != nullptr && canAct) {
        f.velX = f.facing == 0 ? motionKey->velXSub : -motionKey->velXSub;
        f.velY = motionKey->velYSub;
        if (motionKey->velYSub > 0) f.airborne = 1;
    } else if (f.airborne) {
        // A HIT RE-DECIDES AN AIRBORNE ARC (M1.3(d)). A body merely hit out
        // of its jump drops straight -- velX zeroed, the behaviour the
        // crossplat golden has always pinned -- but a LAUNCHED body keeps the
        // arc, because the hit itself authored it (ResolveHits wrote the
        // launch velocity and marked Fighter::reaction); zeroing that on the
        // next tick would turn every juggle into a straight drop the tick
        // after the launcher connected. The first draft of (d) kept velX for
        // BOTH cases under the ballistic doctrine and the golden moved --
        // ticks 1000..2000, the hitstun window -- which is the golden doing
        // its job: air-reset-drops-straight is simulated behaviour somebody
        // recorded, not a free variable.
        if (!canAct && f.reaction != kReactionLaunched) f.velX = 0;
    } else if (free) {
        f.velX = it.walkWish;
        if (it.jumpWish) {
            f.velY = data.jumpImpulseSub != 0 ? data.jumpImpulseSub : kJumpImpulse;
            f.airborne = 1;
        }
    } else {
        f.velX = 0;
    }

    // Crouching is an INPUT posture and airborne is a POSITION fact, which is
    // why they are two fields; the impossible combination is closed here by
    // construction -- read off the LIVE airborne, so the jump above already
    // vetoes the crouch. A committed fighter KEEPS the posture the move started
    // in rather than dropping to standing: a crouching move's frame data was
    // authored against the crouching body, and a hurtbox that grew back to
    // 60 px on frame 2 of a slide is hittable by everything the slide ducks.
    if (free)
        f.crouching = (!f.airborne && it.crouchWish) ? 1u : 0u;
    else if (!canAct)
        f.crouching = 0;   // stunned or downed: nobody is holding a crouch

    if (f.airborne && motionKey == nullptr) {
        // The file's number when there is one; zero means unauthored and the
        // placeholder stands. Authorable since `engine.movement` (M1.3(b1)).
        // SKIPPED while an authored motion key owns the arc -- the key is a
        // resolved velocity state, and gravity on top of it would bend a
        // trajectory the transcription already integrated.
        f.velY -= data.gravitySub != 0 ? data.gravitySub : kGravity;
    }

    // Pushback rides its own field precisely so that this line survives the
    // `else` above zeroing velX -- being hit is exactly when a fighter cannot
    // act, so pushback stored in velX would be erased the tick it was applied.
    //
    // The decay is a HALVING WRITTEN AS `/ 2` AND NOT `>> 1`. D2 spells out the
    // difference and it is not stylistic: `>>` rounds toward minus infinity while
    // `/` truncates toward zero, so a shift would decay leftward pushback and
    // rightward pushback by different amounts and hand the mirror-asymmetry bug
    // this kernel is built to avoid a way back in.
    //
    // THE WALL STOPS THE BODY, NOT THE ORIGIN -- wallLimitFor above says why,
    // in the author's words.
    const std::int32_t limit = wallLimitFor(data, f);
    f.posX = clampInt(f.posX + f.velX + f.pushX, -limit, limit);
    f.pushX /= 2;

    f.posY += f.velY;

    if (f.posY <= 0) {
        f.posY     = 0;
        f.velY     = 0;
        f.airborne = 0;
        // The launch's arc ends where the ground begins: reaction is set by
        // ResolveHits and cleared HERE, the alreadyHitBits one-setter-known-
        // clearers shape, so a landed body is an ordinary grounded one and
        // the next launch starts clean.
        f.reaction = 0;
    }
}

// The ONE writer of the input-bookkeeping fields: bufferedButtons, bufferAge
// and prevButtons. The per-fighter head of the Resolve stage, and its position
// carries three rules at once:
//
// AFTER StepAttack, because whether the buffer was SPENT is derived here --
// `moveFrame == 0` with a move in progress, the same started-this-tick signal
// ComboWatcher and the drivers key on -- rather than written by StepAttack,
// which would make two stages writers of one field. A press that survived the
// start it triggered would fire the next window too.
//
// BEFORE ResolveHits, because an interrupt zeroes moveId AND moveFrame, which
// would read as "nothing started" and leave a spent press alive to fire again
// out of the hitstun it caused.
//
// AND ON FROZEN TICKS IT STILL RECORDS. Hitstop gates the fighter's ADVANCE,
// not the record of what the player asked for: a press made inside the freeze
// is captured (the tap-confirm modern games are built to accept), aging is
// suspended (frozen ticks are not time to the fighter, so not to the buffer),
// and the prevButtons latch is withheld so a release inside the freeze still
// reads as a falling edge on the first tick the fighter runs. Before ROADMAP
// M1.3f the freeze skipped this entirely -- two of those by accident, and the
// tap-confirm eaten. The three P3Input freeze tests pin all three, because the
// crossplat golden runs no moves and no hitstop and can police none of them.
void LatchInputs(Fighter& f, const Intent& it, const FighterData& data) {
    if (f.active == 0) return;

    if (it.frozen) {
        if (data.inputBufferFrames > 0 && it.buffable != 0) {
            f.bufferedButtons = it.buffable;
            f.bufferAge       = 0;
        }
        return;
    }

    if (data.inputBufferFrames > 0) {
        // A MOVE STARTED THIS TICK consumed whatever asked for it -- by press,
        // by buffer or by release -- so the buffer is spent. `canAct` would be
        // the wrong gate here: a fighter mid-move is actionable by that
        // measure, and most early presses arrive mid-move, exactly when a
        // player is trying to link.
        const bool startedThisTick = f.moveId != 0 && f.moveFrame == 0;
        if (startedThisTick) {
            f.bufferedButtons = 0;
            f.bufferAge       = 0;
        } else if (it.buffable != 0) {
            // A press that started nothing went unused and is worth keeping,
            // so that a link attempted a few frames early still comes out when
            // the fighter becomes actionable. The window is per-character
            // (docs/adr/ADR-011 decision 1); zero means no buffering, which is
            // the kernel that shipped before the field.
            f.bufferedButtons = it.buffable;
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

    // LAST of all: this tick's buttons become the next unfrozen tick's
    // `previous`. Every edge in this tick's Intent was computed against the
    // value from before this line ran.
    f.prevButtons = it.bits;
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

        // The SAME body-aware limit StepPhysics uses, or separation would shove
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
    // is the one already written beside the juggle restore in StepPhysics:
    // ResetMatch does not take the character data, and "restored from the
    // character data on the first tick, before anything can read it" is where
    // that rule already lives.
    //
    // It cannot join the per-combo restore beside juggle, and that is the whole
    // design point. Juggle is spent within one combo and restored when the
    // defender leaves hitstun; METER IS NOT -- `fighter_a` opens with 300 of it
    // and spends it across a round. A restore-every-idle-tick would hand the
    // meter back the instant the defender recovered, which is not a balance
    // choice, it is a resource that cannot be spent.
    //
    // Tick zero rather than a "primed" flag: GameState may not grow here, and
    // re-running tick zero after a rollback re-primes to exactly the same
    // numbers, which is what makes a tick-index condition safe at all.
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

    // The pipeline (docs/adr/ADR-012). No stage reads another fighter's fields,
    // so the stage loops are order-independent by construction -- and StepAttack
    // running after movement is what lets a move started this tick see the
    // position the fighter actually reached.
    Intent intents[kMaxFighters] = {};
    for (int i = 0; i < n; ++i)
        intents[i] = ReadIntent(state.p[i], inputs.p[i], data.p[i]);
    for (int i = 0; i < n; ++i)
        StepPhysics(state.p[i], intents[i], data.p[i]);
    for (int i = 0; i < n; ++i)
        StepAttack(state.p[i], data.p[i], intents[i]);

    // --- Resolve: everything that needs more than one fighter ----------------
    for (int i = 0; i < n; ++i)
        LatchInputs(state.p[i], intents[i], data.p[i]);

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
    // clamp StepPhysics already applied.
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
        // juggle is deliberately left at zero: StepPhysics restores it from the
        // character data on the first tick, before anything can read it, and
        // that is the ONE place the restore rule lives.
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
