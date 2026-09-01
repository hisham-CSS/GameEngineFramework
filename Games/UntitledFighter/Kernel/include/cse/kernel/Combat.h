// Hitboxes, hurtboxes, and the one hit a tick can resolve.
//
// This is the first thing in the kernel that can make one fighter hit another.
// Everything before it moved two independent characters around a stage and
// counted down a stun nobody could inflict.
//
// ---------------------------------------------------------------------------
// WHERE THE BOX DATA LIVES, AND WHY IT IS A PARAMETER RATHER THAN A FIELD
// ---------------------------------------------------------------------------
// The alternative was a fixed-capacity move table inside GameState. It is
// rejected, and the rule that rejects it is already written down in
// Games/UntitledFighter/Data/include/cse/data/CharacterData.h: "if a tick
// WRITES it, it is an integer field in GameState; if a tick only READS it, it
// lives [in character data] and the state holds an index."
//
// A hitbox is read-only for the whole match. No tick writes one. So putting the
// table in GameState would:
//
//   * memcpy ~2.6 KB of unchanging data 128 times a second, in and out of a ring
//     whose entire budget argument (D4) is that the snapshot is small;
//   * put character data inside the desync checksum, where a content mismatch
//     would surface as "desync at tick 3" instead of as the lobby error
//     §4.8 specifies -- the connect handshake hashes the loaded data ONCE, and
//     that is the right place for it;
//   * create a second copy of the move table, seeded by ResetMatch, that can
//     drift from the CseData one it was copied from. Two sources of truth for
//     the frame data is exactly the failure D8 warns about, where the engine and
//     the analysis disagree by a frame.
//
// So `Simulate` takes it. The data is a POD of integers owned by this header --
// the kernel still links nothing, and CseData (which has std::string and
// std::vector and must never be reachable from a tick) fills one of these in at
// match start. The parameter is a reference to immutable data, so `Simulate`
// stays a pure function of its arguments: two peers with the same bytes compute
// the same tick, and re-simulation after a rollback reads the same bytes it read
// the first time, because nothing can have written them in between.
//
// The one thing this trades away: the snapshot no longer describes the match on
// its own. A saved GameState is meaningless without the MatchData it was
// simulated against, and it is the connect handshake's job to prove both peers
// hold the same one. That is a property of the design either way -- a move-table
// copy inside GameState would only have moved the proof, not removed it.
//
// ---------------------------------------------------------------------------
// WHAT USED TO BE DELIBERATELY NOT HERE, AND NOW IS
// ---------------------------------------------------------------------------
// This block used to list hitstop, blocking, blockstun, pushback, juggle,
// proration, priority and hitstun decay as out of scope, each "Phase 3". They
// are ADR-005 section 4's P2, they landed as ONE state expansion for the reason
// that document gives -- GameState is a wire contract and seven separate passes
// pay for seven re-goldens -- and the fields for them are below.
//
// The old text's reasoning is preserved where it still binds. On hitstop it said
// it "is not a box question at all -- it is a rule about which fighters advance
// on which ticks, and it changes the meaning of every frame number in the
// character data". That is exactly right and it is why Fighter::hitstop is a
// FREEZE and not a subtraction: startup 5 is still five ticks OF THE MOVE.
//
// STILL NOT HERE, and each is still Phase 3+: throw boxes, push boxes, per-FRAME
// (rather than per-move) boxes, and projectiles. Per-move hurtbox overrides ARE
// here -- Combat.h predicted their shape before anyone asked for them, and
// ADR-006 section 3.3 took it up.
//
// ---------------------------------------------------------------------------
// CANCELS, AND THE ONE THING THEY ARE NOT ALLOWED TO COST
// ---------------------------------------------------------------------------
// A cancel edge is the sentence "move A, having connected, may be interrupted
// into move B after a delay". It arrived after the hit resolution above, and it
// arrived under a hard constraint: GameState's LAYOUT IS A WIRE CONTRACT. Its
// size, its per-field offsets and a golden checksum computed over it are
// asserted by tests/test_kernel.cpp and tests/test_determinism_crossplat.cpp,
// and the second of those is the evidence that gcc and MSVC agree byte for byte.
// Re-recording that golden is how the evidence gets destroyed, so the cancel
// rule below is built to need NO NEW FIELD IN GameState. It reads three things
// the state already carries -- moveId, moveFrame and alreadyHitBits -- and one
// table that lives here, next to the move table and for the same reason: no tick
// writes it.
//
// The consequence, stated rather than hidden. The state records THAT the current
// attack has connected (alreadyHitBits) and not WHEN. So "a delay of d ticks
// after contact" cannot be evaluated against the actual contact tick, and
// CancelEdge instead carries an absolute [earliestFrame, latestFrame] window in
// the SOURCE MOVE's own frame numbering, resolved once at load by the bridge
// that knows both the delay and the move's startup. The bridge measures from the
// first frame the move could possibly have connected on, which is the earliest
// contact and therefore the permissive end; MatchBuilder.cpp counts every edge
// that reading affects in its loss table, under `cancel.contact_frame`. The
// alternative -- a contactFrame byte in Fighter -- costs four bytes of state, a
// new golden hash and the crossplay proof that came with the old one, to buy
// back at most `active - 1` ticks of precision. That trade is refused here and
// the refusal is what this paragraph exists to record.
#pragma once

#include "GameState.h"

#include <cstdint>
#include <type_traits>

namespace cse::kernel {

// --- Boxes ------------------------------------------------------------------

// An axis-aligned integer rectangle in SUB-UNITS (1 pixel = 256), authored
// RELATIVE TO THE FIGHTER'S ORIGIN and as if the fighter faced +X. +X is stage
// right and +Y is up, matching Fighter::posX/posY.
//
// HALF-OPEN on both axes: the rectangle covers x in [x0, x1) and y in [y0, y1).
// Half-open removes the off-by-one that inclusive bounds smuggle into every
// width calculation (x1 - x0 is the width, full stop), and it makes two boxes
// that merely touch NOT overlap -- so a hit at exactly maximum range is a clean
// miss rather than a value that depends on which end of the box you measured.
struct Box {
    std::int32_t x0;
    std::int32_t y0;
    std::int32_t x1;
    std::int32_t y1;
};

// The largest magnitude a box coordinate may have: 4096 pixels, which is eight
// stage widths. This bound is not decoration -- it is what makes the arithmetic
// below provably overflow-free (see PlaceBox) and what makes negation total:
// INT32_MIN has no positive counterpart, and a mirror that cannot negate one
// value is a mirror with a hole in it.
inline constexpr std::int32_t kMaxBoxCoord = 1 << 20;

// The largest magnitude a fighter origin may have when a box is placed. Far
// beyond the stage clamp (480 px), because this is a safety bound rather than a
// gameplay one: it makes PlaceBox total for ANY GameState, including one a test
// or a corrupt packet built by hand.
inline constexpr std::int32_t kMaxWorldCoord = 1 << 24;

// True if a box is a well-formed, non-empty rectangle within the bounds above.
// Exposed for the data loader, which is where a bad box should be rejected --
// the simulation itself is total and never needs to ask.
bool BoxIsValid(const Box& b);

// Reflect a box across x = 0, for a fighter that faces -X.
//
// NEGATE AND SWAP, AND IT IS EXACT.
//
//     { x0, y0, x1, y1 }  ->  { -x1, y0, -x0, y1 }
//
// Each vertical edge is negated, and the two exchange roles, because negation
// reverses the order of the number line and x0 must remain the smaller edge.
//
// Why this construction and not a multiply. Integer negation is exact for every
// value in [-kMaxBoxCoord, kMaxBoxCoord], it is its own inverse, and it
// introduces no rounding decision at all -- mirroring a box twice returns the
// original bytes, and the mirrored box is the same width as the original by
// construction rather than by luck. Any construction that scales instead --
// reflecting about a pivot, multiplying by a signed facing, or halving a width
// to find a centre -- goes through a division, and D2 spells out what a division
// costs here: `>>` rounds toward minus infinity while `/` truncates toward zero,
// so a left-facing box can lose a least-significant sub-unit that the identical
// right-facing box keeps. One sub-unit of reach is 1/256th of a pixel, and it
// decides whether a combo connects, which is the difference between Terminating
// and Infinite in the proof this engine is the case study for.
//
// Note that multiplying each edge by -1 WITHOUT the swap is also exact, and also
// wrong: it produces x0 > x1, an inside-out rectangle that every overlap test
// below reports as empty, so a mirrored character would simply never hit
// anything. The swap is not an optimisation of the multiply. It is the half of
// the operation the multiply forgets.
//
// Y is not touched. Mirroring is horizontal; a fighter turning around does not
// turn upside down.
Box MirrorBox(const Box& local);

// Put a fighter-relative box into stage coordinates: mirror it if the fighter
// faces -X (facing != 0, per Fighter::facing), then translate by the origin.
//
// Both steps are exact, and their composition is exact, which is the property
// the mirror test rests on:
//
//     PlaceBox(b, -px, py, facing^1)  ==  MirrorBox(PlaceBox(b, px, py, facing))
//
// Inputs are clamped to the bounds above before use. The clamps are SYMMETRIC
// about zero (-kMaxBoxCoord..+kMaxBoxCoord), so clamping commutes with negation
// -- clamp(-v) == -clamp(v) -- and the guard that exists to prevent overflow
// cannot itself introduce the asymmetry this whole file is about.
Box PlaceBox(const Box& local, std::int32_t posX, std::int32_t posY,
             std::uint8_t facing);

// Half-open overlap. Boxes that share an edge do not overlap.
//
// This predicate is invariant under mirroring BOTH boxes, which is why the
// half-open convention is safe here even though the mirror of the point set
// [a, b) is (-b, -a]: the test only ever compares one box's edges against the
// other's, both endpoints move together under negation, and each of the four
// comparisons turns into the comparison it is paired with. Written out:
// a.x0 < b.x1 becomes -a.x1 < -b.x0, i.e. b.x0 < a.x1, which is the third
// comparison; and vice versa.
bool BoxesOverlap(const Box& a, const Box& b);

// --- The read-only character data a tick may look at ------------------------

// --- The vocabularies ADR-006 decided ---------------------------------------

// MoveDef::stance -- what the ATTACKER must be in to START this move.
//
// APPEND-ONLY, and ADR-006 section 3.1 says why in one sentence: these integers
// become wire-visible under the connect handshake's hash, so inserting a value
// beside the one it reads better next to silently re-labels every move in every
// replay and on every peer.
//
// kStanceGround continues to mean GROUNDED, STANCE UNSPECIFIED -- an honest
// description of the MUGEN corpus, whose importer collapsed S and C into one
// value (ADR-006 section 1.3). A NEW character uses standing or crouching and
// never ground.
inline constexpr std::uint8_t kStanceAny       = 0;  // zero-init = no restriction
inline constexpr std::uint8_t kStanceGround    = 1;
inline constexpr std::uint8_t kStanceStanding  = 2;
inline constexpr std::uint8_t kStanceCrouching = 3;
inline constexpr std::uint8_t kStanceAir       = 4;

// MoveDef::blockedAs -- which block stops it.
//
//     a HIGH block (standing)  stops { high, mid }
//     a LOW  block (crouching) stops { low,  mid }
//
// So a high attack goes through a low block (an OVERHEAD) and a low attack goes
// through a high block. kBlockedAsMid is ZERO because it is the default, and the
// default has to be the value that changes nothing: every move in the Phase-0
// corpus was authored before this field existed.
//
// It is `blockedAs` and never `guard`. ADR-006 section 1.5: `move.guard` already
// exists in the schema and is a RESOURCE MINIMUM ("this super costs a bar"),
// with nothing whatever to do with blocking, and it got there first.
inline constexpr std::uint8_t kBlockedAsMid  = 0;
inline constexpr std::uint8_t kBlockedAsHigh = 1;
inline constexpr std::uint8_t kBlockedAsLow  = 2;

// InvincibilityWindow::kinds -- what an attack COUNTS AS, for the purpose of
// deciding what cannot be hit by it.
//
// THE LIST SPANS TWO AXES ON PURPOSE, and ADR-006 section 3.7 insists this be
// written down rather than inferred from the names: high/mid/low come from the
// incoming attack's `blockedAs`, while `aerial` comes from the incoming
// ATTACKER's `stance`. They sit in one list because "what can I not be hit by"
// is the sentence a designer writes, and it does not respect field boundaries.
//
// THE MATCH IS AN INTERSECTION, NOT A CONTAINMENT, and that is the whole reason
// an anti-air works. An incoming attack carries one token from EACH axis: an
// aerial medium punch arrives as {aerial, high}, and a window naming {aerial}
// stops it because ONE token matches. Under a containment rule an anti-air would
// have to enumerate all three guard heights as well, and the field would be
// useless for the case it was added for.
//
// APPEND-ONLY, same reason as kStance*.
inline constexpr std::uint16_t kAttackHigh       = 1u << 0;
inline constexpr std::uint16_t kAttackMid        = 1u << 1;
inline constexpr std::uint16_t kAttackLow        = 1u << 2;
inline constexpr std::uint16_t kAttackAerial     = 1u << 3;
// RESERVED: legal to author, inert today. The kernel can produce neither a throw
// nor a projectile, so a window naming only these is inert rather than wrong and
// nothing warns about it -- a warning there would fire on correct forward-looking
// data. Reserving them costs one bit each now; adding them later costs a bit
// position inside a struct the connect handshake hashes.
inline constexpr std::uint16_t kAttackThrow      = 1u << 4;
inline constexpr std::uint16_t kAttackProjectile = 1u << 5;

// One span of ticks during which a move cannot be hit by certain kinds.
//
// AN EMPTY `kinds` MEANS INVINCIBLE TO EVERYTHING, because this list only ever
// NARROWS a window and the identity element of a narrowing is everything. That
// is also why there is no whitelist form and must never be one: windows may
// overlap, the meaning of an overlap is the UNION, and union is commutative and
// idempotent only while every window subtracts. Two overlapping whitelists have
// no order-independent reading. See ADR-006 section 3.6.
struct InvincibilityWindow {
    // Ticks from the start of the move, in MoveDef::startup's own base.
    std::int32_t fromTick;

    // Length. A zero-tick window is not a short window, it is the empty set --
    // it sits in the file looking like protection while providing none -- so the
    // LOADER refuses it. The kernel treats it as inert rather than trusting that.
    std::int32_t ticks;

    // Bitwise OR of kAttack*. Zero means everything (see above).
    std::uint16_t kinds;

    // Explicit, for the reason MoveDef's own trailing byte was explicit before
    // negativeEdge took it: a hashed POD may not carry indeterminate bytes.
    std::uint16_t pad_;
};

// Four windows per move. The motivating case from ADR-006 section 3.6 --
// "invincible 1-6, then throw-invincible through recovery" -- is two, so this is
// comfortable headroom, and it is a fixed bound because D4 forbids unbounded
// growth in anything the simulation reads.
inline constexpr std::int32_t kMaxInvulnWindows = 4;

// One velocity state in a move's authored motion (ROADMAP M1.3(b2), ADR-014).
//
// RESOLVED, NOT SYMBOLIC: each key is a complete velocity the fighter flies
// from `fromTick` until the next key or the move's end -- friction and any
// authored curve were pre-evaluated into these numbers at transcription, which
// is what lets a rollback resume mid-move from state alone. While a key owns
// the fighter, gravity does NOT apply (the segment IS the trajectory); a move
// that wants ballistics back before it ends authors a final key saying so.
//
// `velXSub` is TOWARD THE FIGHTER'S FACING, applied by a branch and never by
// multiplying facing into a coordinate (MirrorBox's rule). `velYSub` is +Y UP,
// the kernel's convention -- the bridge flips the file's MUGEN Y-down sign, in
// MatchBuilder, once. `fromTick` compares against Fighter::moveFrame as
// observed at the TOP of the tick, so a key at 0 first moves the fighter on
// the tick after the press -- the press tick's physics ran before the move
// existed, the same ordering that makes commitment readable.
struct MotionKey {
    std::int32_t fromTick;
    std::int32_t velXSub;
    std::int32_t velYSub;
};

// Eight keys per move. Measured against the corpus rather than picked: the
// largest authored motion is five keys (fighter_a_infinite's
// special_dash_punch), so this is comfortable headroom and still the fixed
// bound D4 demands of anything the simulation reads.
inline constexpr std::int32_t kMaxMotionKeys = 8;

// One attack. Frame numbers are ticks from the start of the move, where the tick
// the move starts is frame 0. The hitbox is live on frames
// [startup, startup + active) and the move ends after
// startup + active + recovery ticks.
struct MoveDef {
    std::int32_t startup;
    std::int32_t active;
    std::int32_t recovery;

    // Damage, in the SAME UNITS as Fighter::health. Not hundredths: CseData
    // stores hundredths (CharacterData.h) and the loader converts once, at load,
    // which is D8's rule -- exactly one documented quantization, applied
    // identically on both peers. A second conversion here would be a second
    // rounding rule in a file whose whole point is that there are none.
    std::int32_t damage;

    // Ticks of hitstun inflicted on hit. Clamped into Fighter::hitstun's uint16
    // range when applied; a negative value means no stun.
    std::int32_t hitstun;

    // The box, authored facing +X. Mirrored by MirrorBox when the fighter faces
    // the other way, never by scaling it.
    Box hitbox;

    // The attacker's body WHILE THIS MOVE RUNS, replacing FighterData::hurtbox,
    // and used only when hasHurtboxOverride is set.
    //
    // THIS IS WHY THERE IS NO `low_profiles` BOOLEAN, and ADR-006 section 3.3
    // makes the argument: a crouching attack ducks a high attack because ITS BODY
    // IS SHORTER for those frames, not because somebody wrote a flag. Not every
    // crouching attack low-profiles, and a file that states a SHAPE never has to
    // claim that it does. A boolean answers exactly one question -- the one its
    // author happened to think of. A box answers all of them: which specific
    // attacks clear it, whether a sweep still catches it, how much is exposed.
    Box hurtboxOverride;

    // Ticks of blockstun inflicted when this is blocked. Distinct from hitstun
    // because it always is in real frame data -- blockstun being shorter than
    // hitstun is what makes a blocked string punishable and a connected one not.
    std::int16_t blockstun;

    // Damage dealt THROUGH a block, in Fighter::health units.
    std::int16_t chipDamage;

    // Pushback applied to the defender, in sub-units per tick, AWAY from the
    // attacker. Signed for authoring convenience (a negative value pulls, which
    // is a real move) rather than because the sign encodes direction -- direction
    // comes from the position difference at contact, never from facing
    // multiplied into a coordinate.
    std::int16_t pushbackHit;
    std::int16_t pushbackBlock;

    // Who wins when both attacks land on the same tick. HIGHER WINS OUTRIGHT and
    // the loser takes nothing; EQUAL IS A TRADE and both land.
    //
    // THE DEFAULT IS THE GAME THAT WAS ALREADY RUNNING. Every character authored
    // before this field leaves it absent, so every move compares 0 against 0, so
    // every meeting is a tie, so every meeting is a trade -- which is precisely
    // what ResolveHits did before priority existed. A file that does not author
    // this field describes the shipped game exactly, and
    // test_combat.cpp's ASimultaneousTradeIsSymmetric is the test that proves it.
    //
    // SIGNED, because "this move loses to everything" is a real thing to author
    // -- a committal heavy, a taunt -- and it is naturally -1; with an unsigned
    // type the only way to say "below the default" is to renumber the whole cast.
    //
    // SCOPE, stated narrowly because the word invites more: it is not a measure
    // of how good a move is, it does not gate STARTING a move, it interrupts
    // nothing, and it is consulted only in the one instant two hitboxes both
    // connect. A move that never meets another on that tick never reads it.
    std::int16_t priority;

    // Juggle budget this move spends from the DEFENDER when it connects. A hit
    // that would take the defender's remaining budget below zero does not land.
    std::int16_t juggleCost;

    // The tick on which a GROUNDED move leaves the floor. Only meaningful when
    // hasAirborneFrom is set -- see that field, which exists because ZERO IS A
    // LEGAL VALUE HERE and therefore cannot also be the sentinel.
    //
    // This is the other half of ADR-006 section 3.3: a hop kick passes over a low
    // for as long as it is off the ground. In startup's tick base, so a move can
    // leave the ground BEFORE its hitbox appears, which is what a hop kick
    // actually does. Note that v2 already modelled the END of an airborne phase
    // (transition.kind "on_land") and not its start.
    std::int16_t airborneFromTick;

    // The input bits that start this move. ALL of them must be held, and zero
    // means "not startable from a button" (a move only reachable from a cancel).
    // Held, not pressed: edge detection needs the previous tick's buttons inside
    // GameState, because a rollback restores state and hands Simulate only the
    // current tick's input -- that field is a deliberate omission, not an
    // oversight. See the note in Combat.cpp.
    std::uint16_t button;

    // Ticks BOTH fighters freeze for when this connects. A freeze, not a
    // subtraction: see Fighter::hitstop.
    std::uint16_t hitstop;

    // Ticks the defender spends on the floor when this connects, or 0 for no
    // knockdown. ADR-006 section 9 records that this fact is already authored
    // three times in the shipping character (a tag, an engine.reaction field and
    // an English label) and read by nothing. This is the field that reads it.
    std::uint16_t knockdownTicks;

    // How much this move REDUCES the defender's damage scaling by, in percent,
    // applied multiplicatively: 0 takes nothing off, 100 makes everything after
    // it worthless.
    //
    // IT IS A REDUCTION AND NOT THE RESULTING SCALE, and that inversion is the
    // whole point. The rule this struct obeys everywhere -- kStanceAny is 0,
    // kBlockedAsMid is 0 -- is that THE ZERO VALUE MUST BE THE ONE THAT CHANGES
    // NOTHING, because zero is what every character authored before the field
    // existed has and what every value-initialised MoveDef in a test has.
    // Storing the resulting scale instead would make an unauthored move prorate
    // the combo to 0%, so the first hit lands and every hit after it deals no
    // damage -- which is not a hypothesis: it is what this field did on its first
    // build, and five behavioural tests caught it.
    std::uint16_t scalingReduction;

    std::uint8_t stance;     // kStance*
    std::uint8_t blockedAs;  // kBlockedAs*
    std::uint8_t hasHurtboxOverride;
    std::uint8_t invulnCount;  // used entries in invuln[]

    // Whether airborneFromTick means anything.
    //
    // A SEPARATE FLAG RATHER THAN A -1 SENTINEL, for the reason above: a move
    // that leaves the ground on frame 0 is a real move, so 0 cannot also mean
    // "never". With a sentinel, a value-initialised MoveDef would claim EVERY
    // move goes airborne on its first frame, which classifies every attack in
    // the game as aerial and hands every anti-air a free win. Same shape as
    // hasHurtboxOverride, deliberately, so there is one idiom here and not two.
    std::uint8_t hasAirborneFrom;

    // Fires on button RELEASE as well as on press: hold the button, input the
    // motion, let go, and the special comes out. Zero is off, which is what a
    // file that authors nothing gets.
    //
    // Per move rather than per character, because it is a property of the move
    // in every game that has it -- a special accepts negative edge and a jab
    // does not. It took the byte that used to be explicit padding here, so
    // MoveDef is still 128 bytes and no MatchData layout moved for it.
    std::uint8_t negativeEdge;

    InvincibilityWindow invuln[kMaxInvulnWindows];

    // --- Resources (ROADMAP M1.1b) ------------------------------------------
    //
    // POSITIONAL, and that is a build-wide contract rather than a convenience.
    // The prover keys its resource vector by index (ADR-001 section 8 item 7),
    // so slot i means the same resource in every file a build loads; the loader
    // resolves each authored name to an index once, and from here down there are
    // no names at all. Assertion A03 is where the contract is enforced.
    //
    // `effect` is a DELTA applied when this move connects: +1 juggle spent, +25
    // meter gained. Zero is no effect, which is what an unauthored slot holds
    // and what every character built before this field had.
    std::int32_t effect[kMaxResources];

    // `guard` is a MINIMUM checked before the move starts: a super that costs
    // meter refuses below its cost. Guarded slots are named by the mask rather
    // than by a sentinel, because ZERO IS A LEGAL MINIMUM -- a resource with a
    // negative floor can be guarded at zero, and a sentinel would silently make
    // that mean "unguarded" and hand the move away for free.
    std::int32_t guard[kMaxResources];
    std::uint8_t guardMask;      // bit i set = guard[i] is checked

    // Explicit, because a hashed POD may not carry indeterminate bytes: the
    // connect handshake compares these bytes across two machines. No longer
    // the tail -- M1.3(b2) appended below it -- but a hole is a hole wherever
    // it sits.
    std::uint8_t pad2_[3];

    // --- Authored motion (ROADMAP M1.3(b2), ADR-014 step two) ---------------
    //
    // THE EXCEPTION COMMITMENT PROMISED: a committed fighter's velocity is
    // zero unless ITS MOVE says otherwise, and this is where a move says so --
    // the lunge that carries the fighter, the hop kick that leaves the ground
    // mid-move (velYSub > 0 sets `airborne`), the divekick that rewrites an
    // arc it is already flying. Zero keys means the move does not move, which
    // is every move authored before this field and every hand-built bench.
    // StepPhysics::ActiveMotion is the one reader.
    MotionKey    motion[kMaxMotionKeys];
    std::int32_t motionCount;

    // --- M1.3(c) counter-hit, LIVE; (d) launch fields still RESERVED --------
    //
    // Reserved in the SAME growth as the motion block so the hashed contract
    // paid its re-hash once (ADR-005 section 3, ADR-014). The counter pair
    // stopped being reserved when ADR-015 was accepted (option 3, per-opening
    // verdicts) and M1.3(c) landed: ResolveHits adds both bonuses when the
    // defender is caught MID-STARTUP -- startup only, a trade is a trade and
    // a punish is its own reward -- and zero stays inert, which is every move
    // authored before the field. The launch pair remains bytes only for (d):
    // nothing reads them, and Simulate.cpp's air-hit velX zeroing stands
    // until they land.
    std::int32_t counterHitstunBonus;
    std::int32_t counterDamageBonus;
    std::int32_t launchVelXSub;
    std::int32_t launchVelYSub;

    // CORNER PUSH (M1.6's microwalk slice): recoil applied to the ATTACKER
    // when a hit lands on a defender the wall already stops. Not a (c)/(d)
    // field -- it moves nobody's stun and is ADR-015-free, a displacement
    // like pushback -- and it took two of the tail pad bytes the (b2) growth
    // left, so no layout moved for it. BEFORE the reaction byte, because an
    // int16 after a uint8 opens the implicit hole the static_assert below
    // exists to catch (it did, in this slice's first build). Saturated at
    // int16 like pushbackHit. Zero is every move authored before the wire,
    // including all 22 of fighter_a's (the file authors the key at 0).
    std::int16_t cornerPushHit;
    std::uint8_t onHitReaction;

    // Explicit tail padding, hashed like everything else here.
    std::uint8_t pad3_[1];
};

// 32 moves per fighter. A hard cap, deliberately: D4 forbids unbounded growth in
// anything the simulation reads, and a cap that is checked at load is a lobby
// error, while a cap that is not is a buffer overrun.
inline constexpr std::int32_t kMaxMovesPerFighter = 32;

// --- Cancels ----------------------------------------------------------------

// One directed cancel edge: "while performing `from`, start `to` instead".
//
// WHY THE WINDOW IS ABSOLUTE AND NOT A DELAY. The authored datum is a delay in
// ticks measured from the moment the source move CONNECTED (see
// Games/UntitledFighter/Data/include/cse/data/CharacterData.h, Cancel::delay).
// Evaluating that at tick time needs the contact tick, which GameState does not
// carry and -- per the long note at the top of this file -- is not going to
// start carrying. So the
// bridge resolves the delay against the source move's frame numbering ONCE, at
// load, and hands the kernel two frame numbers it can compare against
// Fighter::moveFrame with no arithmetic at all. Both bounds are INCLUSIVE:
// unlike a box edge, a frame number is a count of ticks rather than a position
// on a line, there is no half-open convention to preserve, and "the last frame
// the cancel works on" is the sentence a designer writes.
//
// An edge with earliestFrame > latestFrame is INERT rather than malformed. It is
// what an authored delay longer than the whole source move resolves to, and that
// is a real thing in the shipped data: every one of the AOF2 character's 26
// edges is a LINK (press the next button after the move has fully recovered)
// wearing the schema's cancel shape. The kernel simply never takes it, which is
// the correct behaviour, and MatchBuilder counts it rather than pretending the
// character has 26 cancels it can perform.
// The contact outcomes a cancel edge may name (ROADMAP M1.3 slice (a)). Bit
// values, so an edge can name any set of them.
//
// kContactHit is 1 ON PURPOSE: the pre-mask kernel collapsed the schema's four
// Contact values into one bit whose value for an `on: hit` edge was 1, and
// every shipped fighter_a edge authors `on: hit` -- so the mask keeps those
// bytes (and the character's MatchData hash, which the replay format and the
// connect handshake compare) exactly where they were.
inline constexpr std::uint8_t kContactHit   = 1;  // a guard did NOT stop it
inline constexpr std::uint8_t kContactBlock = 2;  // contact a guard stopped
inline constexpr std::uint8_t kContactWhiff = 4;  // no contact (yet)

struct CancelEdge {
    // Kernel move ids, i.e. direct indices into FighterData::moves. An edge
    // naming slot 0 on either end can never fire: slot 0 is idle, a fighter with
    // moveId 0 has no move to cancel out of, and MoveAt refuses to describe it.
    std::uint16_t from;
    std::uint16_t to;

    // Fighter::moveFrame bounds on the SOURCE move, inclusive both ends.
    std::int32_t earliestFrame;
    std::int32_t latestFrame;

    // Which contact outcomes open this edge: a mask of kContactHit /
    // kContactBlock / kContactWhiff, or 0 for UNGATED (the schema's
    // `on: always`, and the hand-built harness default -- a raw zero must keep
    // meaning "no contact condition", not "no outcome allowed").
    //
    // This byte used to be `onHit`, one bit collapsing the schema's four
    // Contact values because the kernel could not tell a hit from a blocked
    // contact -- both set alreadyHitBits and nothing else reached the
    // attacker. That stopped being true when blocking landed (ResolveHits has
    // written Fighter::blockstun since 41ea6e5), and since M1.3(a) the
    // attacker also keeps the BLOCKED mirror of alreadyHitBits in the low
    // byte of Fighter::flags -- so all three outcomes are attacker-observable
    // and the file's `on` crosses whole: hit fires only on a clean hit (a
    // blocked contact no longer opens an `on: hit` chain), block only on a
    // stopped one, whiff only while nothing has connected. CancelIsOpen is
    // the one reader.
    std::uint8_t contactMask;

    // Explicit, for the same reason: the connect handshake hashes these
    // bytes, and an indeterminate byte is a byte two peers can disagree about.
    std::uint8_t pad_[3];
};

// 256 cancel edges per fighter. Chosen against the shipped data rather than
// picked round: Kung Fu Girl authors 134, Kung Fu Man 87, the AOF2 character 26,
// so this is comfortable headroom over the largest real character and still a
// fixed bound, which is what D4 asks of anything the simulation reads. Over the
// cap is a REFUSAL in the bridge, never a truncation, for the reason
// MatchBuilder.h gives about moves: a character missing its last cancels is a
// different character from the one the prover analysed.
inline constexpr std::int32_t kMaxCancelsPerFighter = 256;

// Everything one fighter's simulation reads and never writes.
// One resource slot, as the kernel sees it. NO NAME: the loader resolved every
// authored name to an index once, and the index is the contract from there down
// (ADR-001 section 8 item 7, assertion A03). A name here would be a second
// spelling of the same fact and the first thing to drift.
//
// `hasCeiling` is a flag rather than a sentinel for the reason MoveDef's
// hasAirborneFrom is: zero is a legal ceiling -- a resource that may never rise
// above its starting value is a real design -- so no single int can mean both
// "capped at zero" and "uncapped".
struct ResourceDef {
    std::int32_t initial;
    std::int32_t floor;
    std::int32_t ceiling;
    std::uint8_t hasCeiling;
    std::uint8_t pad_[3];
};

static_assert(std::is_trivially_copyable_v<ResourceDef>,
              "MatchData is hashed and compared as bytes");
static_assert(sizeof(ResourceDef) == 3 * sizeof(std::int32_t) + 4,
              "ResourceDef grew, shrank, or acquired implicit padding. Same "
              "connect handshake, same hazard as MoveDef.");

struct FighterData {
    // The body, authored facing +X. This is the DEFAULT body; a move may replace
    // it for the ticks it runs via MoveDef::hurtboxOverride, which is the
    // per-move override this comment used to predict as Phase 3 and which
    // ADR-006 section 3.3 took up. Per-FRAME boxes are still later.
    Box hurtbox;

    // Starting and maximum health for this fighter in THIS match.
    //
    // IT LIVES HERE AND NOT IN Fighter BECAUSE A TICK ONLY READS IT, which is
    // the rule quoted at the top of this file. It is also the right side of the
    // wire on the merits: two peers fighting a World Tour battle against an
    // opponent with 1400 health must AGREE that it has 1400 health, and the
    // connect handshake hashing this array is what proves it. A per-fighter
    // maximum is what makes RPG-style progression expressible at all -- see
    // ADR-009 section 5.
    std::int32_t maxHealth;

    // Juggle budget this fighter starts every combo with, as a DEFENDER.
    //
    // Per-character rather than a kernel constant because it is a balance number
    // and the kernel holds none of those -- Simulate.cpp's own tuning block calls
    // its contents "the SHAPE, not the balance". The prover's ranking certificate
    // for fighter_a is written against a starting budget of 4
    // (docs/manual/fighting-core.md), so that is what the loader defaults to when
    // a file does not say; a zero here would mean no move with a juggle cost can
    // ever connect, which is a legal but almost certainly unintended character.
    std::int32_t juggleMax;

    // How many ticks a press survives while this fighter cannot act, before it
    // is discarded. ZERO MEANS NO BUFFERING, which is what a file that authors
    // nothing gets and what the kernel did before this field existed.
    //
    // A field rather than a constant because buffering changes which links are
    // performable, and "which links are performable" is the question the whole
    // project exists to answer -- a number the kernel chose for every character
    // would be the kernel deciding the answer (docs/adr/ADR-011 decision 1).
    std::int32_t inputBufferFrames;

    // Hitstun decay, as suffered BY THIS FIGHTER as a defender: each hit already
    // taken in the current combo shortens the next one's hitstun by `step` ticks,
    // never below `floor`.
    //
    // STEP 0 IS NO DECAY AND IS THE DEFAULT, which is the same rule every field
    // in MoveDef follows: the zero value must be the one that changes nothing.
    //
    // THE FLOOR IS NOT DECORATION AND THE NUMBERS ARE NOT PICKED. ADR-005 P2
    // item 6 records that this project's own draft rule -- linear, step 2, floor
    // 10 -- FABRICATED AN INFINITE on Kung Fu Girl: decay that bottoms out too
    // low turns a string the character cannot actually loop into one it can, and
    // the analysis then certifies a combo that does not exist. That is why the
    // rule is per-character authored data rather than a constant in this kernel,
    // and why assertion A01 guards it at load.
    std::int32_t hitstunDecayStep;
    std::int32_t hitstunDecayFloor;

    // --- Resources (ROADMAP M1.1b) ------------------------------------------
    //
    // Slot i here is slot i of Fighter::res, of MoveDef::effect and of the
    // prover's own resource vector. `resourceCount` is how many the file
    // declared; slots past it are zeroed and never read.
    ResourceDef  resources[kMaxResources];
    std::int32_t resourceCount;

    // The body while CROUCHING, and it is a separate box rather than a scale
    // factor because a crouch is not a shorter standing pose -- the head comes
    // forward as it comes down, and a character whose crouch is merely `height *
    // 0.6` ducks nothing a designer aimed at.
    //
    // A DEGENERATE BOX MEANS UNAUTHORED: x1 <= x0 or y1 <= y0 and the standing
    // body is used, which is what every character built before this field
    // existed gets and is why wiring it changes nobody who does not use it. A
    // flag would be the idiom elsewhere in this header (hasHurtboxOverride,
    // hasAirborneFrom, hasCeiling), and it is not used here for a reason: those
    // guard SCALARS where zero is a legal value, and there is no legal
    // zero-area body. An empty box is already the impossible value.
    //
    // Asked for from play (ROADMAP M1.3d): a crouch that changes no box is a
    // crouch nobody can see, and it is the state a low attack is aimed at.
    Box crouchHurtbox;

    // THE BODY YOU CANNOT WALK THROUGH. Separate from the hurtbox because the
    // two answer different questions -- the hurtbox is where you can be HIT and
    // this is where you can BE -- and in the genre they routinely differ: a
    // sweep's hurtbox stretches far past the pushbox it keeps.
    //
    // A DEGENERATE BOX MEANS UNAUTHORED and nobody is separated, which is the
    // behaviour every character had before ROADMAP M1.2 and is why wiring this
    // changes nobody who does not carry one.
    //
    // Asked for from play (2026-08-20): "the enemy collider should be blocking
    // collisions rather than trigger ... this prevents players and enemies
    // overlapping hurtboxes and missing attacks because of that."
    Box pushbox;

    // Ground walk speed in sub-units per tick. ZERO MEANS UNAUTHORED, and the
    // kernel then uses the placeholder it used before this field existed --
    // which is what keeps every harness that builds a synthetic FighterData
    // walking at the speed its expectations were written against.
    //
    // A field rather than Simulate.cpp's `kWalkSpeed` because walk speed decides
    // whether a MICROWALK LOOP exists: the attacker steps forward between two
    // hits to stay in range, and one pixel per tick is the difference between a
    // string that drops and one that repeats forever. ADR-011 section 4 makes
    // the microwalk variant `walk_speed` +1 px/tick from the base, so the base
    // has to be a number the file owns. The corner-only prover cannot see this
    // loop at all, which is exactly why the engine has to.
    //
    // Note the shipped `fighter_a.json` authors 3 px/tick where this kernel's
    // placeholder is 2, so honouring the file is a BEHAVIOUR change and not a
    // no-op -- see ROADMAP M1.1b, which measured it before writing the field.
    std::int32_t walkSpeedSub;

    // Downward acceleration and jump impulse, sub-units. ZERO MEANS UNAUTHORED
    // on both, exactly as above.
    //
    // These arrived AHEAD of anything that could set them (M1.1b), because
    // MatchData is hashed by the connect handshake and ADR-005 section 3's
    // rule is to batch a contract change and review it once -- and the bet
    // paid: `engine.movement` (ROADMAP M1.3(b1), ADR-014) made them
    // authorable with no byte of this struct moving. The loader enforces the
    // kernel's own convention at the boundary (+Y up, positive, an explicit
    // zero refused as this sentinel), so the carry is a copy. A silent file
    // still takes Simulate.cpp's placeholders, which is every character
    // authored before the key existed.
    std::int32_t gravitySub;
    std::int32_t jumpImpulseSub;

    // Number of USED slots in moves[], INCLUDING the reserved idle slot 0.
    // A moveId names a real move if and only if 0 < moveId && moveId < moveCount.
    //
    // Slot 0 is reserved and must stay zeroed. It exists so that Fighter::moveId
    // is a DIRECT index with no arithmetic anywhere -- GameState.h already
    // documents moveId 0 as idle, and a 1-based table would put a "-1" at every
    // lookup site, which is the kind of correction that is right in four places
    // and forgotten in the fifth.
    std::int32_t moveCount;

    MoveDef moves[kMaxMovesPerFighter];

    // Number of USED entries in cancels[]. There is no reserved slot here and no
    // off-by-one to remember: entry 0 is a real edge, because nothing indexes
    // this array out of GameState. Fighter::moveId indexes moves[]; nothing
    // indexes cancels[] except the scan below.
    std::int32_t cancelCount;

    // File order, preserved. The scan takes the FIRST edge it can, so the order
    // is a tie-break rule that two peers must agree on -- and preserving the
    // authored order makes that rule one a designer can see in their own file,
    // rather than one that emerged from a sort nobody wrote down.
    CancelEdge cancels[kMaxCancelsPerFighter];
};

// The read-only data for every slot in a match, indexed by the same slot as
// GameState::p. Passing them as one object keeps the tick from having to be told
// which fighter it is looking at data for.
//
// SIZE, STATED BECAUSE IT IS NOT SMALL: kMaxFighters entries of ~8 KB each is
// about 64 KB. That is fine and it is fine for a specific reason -- unlike
// GameState this is NOT snapshotted, not memcpy'd 128 times a second, and not in
// the rollback ring. It is built once at match start and hashed once by the
// connect handshake. What it should NOT be is a casually copied local; prefer a
// reference or a single owned instance.
struct MatchData {
    FighterData p[kMaxFighters];
};

// A match with no moves and no bodies: nothing can start, nothing can be live,
// and a degenerate hurtbox overlaps nothing. This is what the two-argument
// Simulate uses, so that a caller who has not loaded a character yet gets
// exactly the pre-hitbox kernel rather than a subtly different one.
inline constexpr MatchData kNoMoves{};

// The data is on the wire's side of the handshake, so its layout has to be as
// disciplined as GameState's.
static_assert(std::is_trivially_copyable_v<MoveDef>, "MatchData is hashed and compared as bytes");
static_assert(std::is_standard_layout_v<MatchData>, "MatchData is hashed and compared as bytes");
static_assert(std::is_trivially_copyable_v<InvincibilityWindow>,
              "MatchData is hashed and compared as bytes");
static_assert(sizeof(InvincibilityWindow) == 12,
              "InvincibilityWindow grew, shrank, or acquired implicit padding. "
              "Same handshake, same hazard as MoveDef below.");
static_assert(std::is_trivially_copyable_v<MotionKey>,
              "MatchData is hashed and compared as bytes");
static_assert(sizeof(MotionKey) == 12,
              "MotionKey grew, shrank, or acquired implicit padding. Same "
              "handshake, same hazard as MoveDef below.");
static_assert(sizeof(MoveDef) == 128 + 2 * kMaxResources * sizeof(std::int32_t) + 4 +
                                     kMaxMotionKeys * sizeof(MotionKey) + 4 +
                                     4 * sizeof(std::int32_t) + 4,
              "MoveDef grew, shrank, or acquired implicit padding. The connect "
              "handshake hashes these bytes (ARCHITECTURE.md 4.8), so a padding "
              "hole would make two peers with identical characters disagree. "
              "This was 40 before ADR-005 P2, 128 before M1.1b and 164 before "
              "M1.3(b2); the growth is priority, blockstun, chip, pushback, "
              "juggle, hitstop, knockdown, scaling, stance, blockedAs, the "
              "hurtbox override, the invincibility windows, the resource effect "
              "and guard vectors with their mask, and then the authored motion "
              "block with the reserved (c)/(d) fields -- one batched re-hash, "
              "per ADR-005 section 3 and ADR-014. Written as the OLD SIZE PLUS "
              "THE NEW MEMBERS rather than as a fresh round number, so it still "
              "asks 'did padding appear' and not 'is this what I last wrote "
              "down'.");
static_assert(std::is_trivially_copyable_v<CancelEdge>, "MatchData is hashed and compared as bytes");
static_assert(sizeof(CancelEdge) == 16,
              "CancelEdge grew, shrank, or acquired implicit padding. Same "
              "handshake, same hazard as MoveDef above.");

// --- Reading the state through the data -------------------------------------

// Where the wall stops THIS fighter's origin: the stage half-width minus the
// placed pushbox's reach past the origin (the body may not hang into the
// void; Simulate.cpp's wall clamp essay is the one home of the argument). In
// this header because TWO files ask it: StepPhysics clamps positions with it,
// and ResolveHits' corner push asks whether the defender is already standing
// at the answer.
std::int32_t WallLimitFor(const FighterData& data, const Fighter& f);

// The MoveDef a fighter's moveId names, or null if it names none -- either
// because the fighter is idle (moveId 0) or because the id is outside this
// character's table.
//
// Callers branch on null, which is not the pointer branch §4.3 forbids: that
// rule is about behaviour that depends on an ADDRESS, and null-ness is a value
// every peer computes identically from the same integers.
//
// An id the table does not describe is INERT rather than an error: its frame
// counter advances and it has no boxes. That is deliberately identical to what
// the kernel did before hitboxes existed, so a harness that sets moveId by hand
// (tests/test_determinism_crossplat.cpp does exactly that) keeps behaving as it
// did, and a character file that was loaded with fewer moves than the state
// refers to cannot index off the end of the array.
const MoveDef* MoveAt(const FighterData& data, std::uint16_t moveId);

// Total ticks a move occupies.
std::int32_t MoveDuration(const MoveDef& m);

// The fighter's hitbox in stage coordinates, if one is live this tick.
// Returns false when the fighter is idle, when the id is undescribed, or when
// the current frame is outside [startup, startup + active).
bool ActiveHitbox(const FighterData& data, const Fighter& f, Box& out);

// The fighter's body in stage coordinates. Always exists; a character whose
// hurtbox is degenerate simply cannot be hit, which is what kNoMoves relies on.
//
// Uses the CURRENT move's hurtboxOverride when it has one, which is what makes a
// crouching attack low-profile a high one without any move naming any other move.
Box Hurtbox(const FighterData& data, const Fighter& f);

// --- Stance, guard, priority and invincibility (ADR-006) ---------------------

// Whether this fighter is off the ground RIGHT NOW, accounting both for the
// physical jump (Fighter::airborne) and for a grounded move that takes off
// partway through (MoveDef::airborneFromTick).
//
// The second is what makes a hop kick pass over a low, and it is a mid-move
// state change rather than an entry condition -- ADR-006 section 3.3: "a
// standing move that goes airborne on frame 9 is still a standing move, because
// stance says what you must be in to START it."
bool AirborneNow(const FighterData& data, const Fighter& f);

// Whether a fighter in this state may START this move, given what the input is
// asking for RIGHT NOW.
//
// kStanceAny permits everything, which is what every character authored before
// the field existed gets from a zero-init. kStanceGround means grounded with the
// standing/crouching distinction unstated, so it permits both.
//
// THE STANDING/CROUCHING AXIS READS `crouchHeld` -- the INPUT -- and never
// Fighter::crouching, because commitment freezes input-driven posture while a
// move runs and a selection that read the frozen posture refused every
// cross-posture cancel: stand_mp -> crouch_hp with Down held, the ordinary
// gatling, collapsed 120 of 121 measured cycles when stance was first wired
// (ROADMAP M1.3e). The posture then FOLLOWS THE MOVE: StepAttack sets
// `crouching` from the started move's stance, so selection asks the player and
// the body obeys the move. `airborne` stays the fighter's own -- a jump is
// position history, not a request.
bool StanceAllows(const MoveDef& m, const Fighter& f, bool crouchHeld);

// What an incoming attack COUNTS AS: a bitwise OR of kAttack*, drawn from the
// move's blockedAs and from the attacker's current airborne-ness.
//
// One token from each axis, which is what makes the intersection rule in
// InvincibilityWindow work.
std::uint16_t AttackKinds(const FighterData& attackerData, const Fighter& attacker,
                          const MoveDef& m);

// Whether the defender's held guard stops an attack blocked as `blockedAs`.
//
//     high guard stops { high, mid };  low guard stops { low, mid }
//
// kGuardNone stops nothing. Note this asks ONLY about height -- whether the
// defender is guarding at all, and is facing the right way, and is not in
// hitstun, are decided by the caller.
bool GuardStops(std::uint8_t guard, std::uint8_t blockedAs);

// Whether the defender's CURRENT move makes it immune to an attack of these
// kinds, this tick.
//
// The match is an INTERSECTION: the attack's kinds and the window's kinds need
// share only one bit. A window with no kinds at all is immune to everything,
// because the list only narrows and the identity of a narrowing is everything.
// Overlapping windows are a union, which needs no arbitration because union is
// commutative and idempotent.
bool InvulnerableTo(const FighterData& data, const Fighter& f, std::uint16_t kinds);

// --- Cancels, read out of the state -----------------------------------------

// Whether the fighter's CURRENT move satisfies this edge's window and contact
// requirement, ignoring buttons entirely.
//
// Separated from the button test so that the two halves of "can I cancel" can be
// asserted apart. A test that only ever asks the combined question cannot tell a
// window that is off by one from a binding that is missing, and those are the
// two ways this feature fails silently.
//
// "Has connected" is Fighter::alreadyHitBits being nonzero. That field is
// cleared whenever a move starts or ends (StepAttack) and set by ResolveHits on
// the ATTACKER when its box overlapped a body, so a nonzero value means exactly
// "the attack currently in progress has landed on somebody". It is the multi-hit
// guard doing a second job, and the two uses do not conflict: both want the same
// sentence to be true.
bool CancelIsOpen(const Fighter& f, const CancelEdge& edge);

// The edge this fighter may take THIS TICK given these inputs, or null.
//
// Scans FighterData::cancels in index order and returns the first edge that
// matches. First-wins over a fixed dense array is the same tie-break rule
// StepAttack already uses for buttons, and it is chosen for the same reason: two
// peers holding the same bytes must pick the same edge, and "the first one in
// the file" is a rule that survives being written down.
//
// Returns null when the fighter is idle, when its moveId names no move, when no
// edge's window is open, or when the target of every open edge is a move whose
// buttons are not held. A target with no button at all (MoveDef::button == 0) is
// unreachable by definition and is skipped rather than taken for free.
const CancelEdge* FindCancel(const FighterData& data, const Fighter& f, Input in);

// --- The tick as a pipeline (docs/adr/ADR-012) -------------------------------

// What one fighter's input MEANS this tick, read once at the top of the tick
// and passed by value to every stage after it. Transient: never part of
// GameState, never hashed, rebuilt from (state, input, data) on every
// re-simulation -- which is exactly why the stages that consume it cannot
// disagree about what was pressed.
//
// Edges are computed against Fighter::prevButtons AS LATCHED, and the latch is
// written only at the end of an unfrozen tick -- so during hitstop the edges
// keep describing "since the fighter last ran", which is what lets a release
// made inside the freeze still read as a falling edge on thaw.
struct Intent {
    std::uint16_t bits;       // this tick's raw buttons
    std::uint16_t pressed;    // rising edges since the fighter last ran
    std::uint16_t released;   // falling edges since the fighter last ran
    std::uint16_t buffable;   // pressed bits some move in the table can use
    std::int32_t  walkWish;   // signed sub-units of walk the input asks for
    bool          jumpWish;   // Up is held
    bool          crouchWish; // Down is held
    bool          frozen;     // hitstop held this fighter at the top of the tick
};

// A fighter is actionable when nothing is holding them still: no hitstun, no
// blockstun, no knockdown. ONE function decides it, shared by the physics and
// attack stages, because "cannot act" split across two files is how one gate
// ends up enforced and the other does not. Hitstop is deliberately NOT in this
// list: a frozen fighter does not act because it does not ADVANCE at all, which
// is the stages' own gate -- folding it in here would freeze the fighter's
// agency while still ticking its move frames.
bool Actionable(const Fighter& f);

// Advance one fighter's attack: end a move that has run out, CANCEL a move that
// is still running into a follow-up the fighter is asking for, or start one if
// the fighter is idle and pressing a move's buttons. The attack stage of the
// tick pipeline; called once per fighter, in slot order, and it returns
// immediately for a benched or frozen fighter.
//
// THE ORDERING FACT A CANCEL RULE HAS TO LIVE WITH. This runs BEFORE
// ResolveHits -- so a hit landing on tick N is not visible to a cancel test
// until tick N+1. The fastest cancel the kernel can express is therefore one
// tick after contact, never zero, and an edge whose authored delay is 0 fires
// on the tick after the hit. That is a property of the tick's shape, not a
// fudge: on the tick the hit resolves, the hit has not happened yet as far as
// the first half of the tick is concerned, and moving the cancel test after
// ResolveHits would instead let a fighter cancel a move on the same tick it
// started, which is worse.
void StepAttack(Fighter& f, const FighterData& data, const Intent& it);

// Test every active fighter's live hitbox against every ACTIVE OPPOSING
// fighter's body, and apply at most one hit per attacker. Called once per tick,
// after every fighter has moved and after facing has been resolved -- boxes are
// mirrored by facing, so facing must be settled before any box is built.
//
// "Opposing" is `a.team != d.team`, an inequality rather than `1 - a`; ADR-009
// section 3 says why that shape was chosen over the arithmetic one.
void ResolveHits(GameState& state, const MatchData& data);

} // namespace cse::kernel
