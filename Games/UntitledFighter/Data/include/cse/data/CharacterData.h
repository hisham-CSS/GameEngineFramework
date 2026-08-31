// Character data: the immutable reference data a match is fought with.
//
// WHY THIS IS NOT GameState, AND WHY THE SPLIT IS EXACTLY HERE.
//
// docs/ARCHITECTURE.md D2/D4 make the SIMULATION state a fixed-size POD of
// integers, snapshotted by memcpy and restored by memcpy back. That rule buys
// rollback for free, and cse/kernel/GameState.h pays for it: no pointers, no
// heap, no std::string, nothing with a non-trivial copy.
//
// Character data is on the other side of that line, and the reason is one
// sentence: NO TICK EVER WRITES IT. A move's startup, its hitstun, its cancel
// list, the decay curve -- all of it is loaded once at match start from an
// authored file and is thereafter read-only for the whole match. It is not part
// of the state a rollback restores, because rolling back cannot change it.
// So it does not have to be POD, and it must NOT end up inside GameState: a
// std::vector<Move> reached by a memcpy snapshot would survive as a dangling
// pointer -- the failure mode test_kernel.cpp's static_asserts exist to prevent,
// and the worst kind, because it works locally and corrupts on the remote peer.
//
// THE SEAM IS THE HANDLE. Everything the POD state needs to name is named by a
// small integer index into a table here: Fighter::moveId is already a
// std::uint16_t "index into the character's move table" (GameState.h:56).
// Indices are memcpy-safe, wire-safe and checksum-safe; pointers and strings are
// none of those. So the rule for anything added later is: if a tick WRITES it,
// it is an integer field in GameState; if a tick only READS it, it lives here
// and the state holds an index.
//
// This is a straight vector-of-struct model rather than anything cleverer. It is
// loaded once, so load-time cost does not matter, and it is read by a tick, so
// layout and lookup cost do. There are no hash containers anywhere in it: this
// repository has been bitten twice by iteration order over a hash container
// leaking into a simulation (SimplePhysicsBackend, ScriptWorld), and a sorted
// vector plus a binary search has no such order to leak.
//
// UNITS. Every distance in this header is SUB-UNITS, 1 pixel = 256, matching
// cse::kernel::kSubUnitsPerPixel. Every duration is TICKS at 60 Hz. Damage is
// HUNDREDTHS of a damage point, meter is in units of 10 MUGEN power
// (ADR-001 section 3, gate 2), and the scaling/decay tables are PERMILLE. The
// authored files carry some of these as JSON floats; the loader converts once,
// at load, and every field this header exposes is an integer. No float survives
// into anything a tick reads.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cse::data {

// --- Handles ----------------------------------------------------------------
//
// Deliberately the same width as GameState's Fighter::moveId. If these ever
// disagree, a move index that round-trips through the state truncates.
using MoveIndex     = std::uint16_t;
using CancelIndex   = std::uint16_t;
using ResourceIndex = std::uint8_t;

// Returned by lookups that found nothing. Not 0: 0 is a perfectly good move
// index, and a sentinel that collides with a real value is how "not found"
// becomes "the first move in the file".
inline constexpr MoveIndex kInvalidMove = 0xFFFFu;

// A distance field that the file authored as null. Kung Fu Man's two projectile
// moves do this: a fireball's reach is a function of distance travelled, not a
// constant, so the file declines to invent one (ADR-001 section 4 group F).
// Negative, so it can never be mistaken for a reach of zero.
inline constexpr std::int32_t kNoReach = -1;

// A move that never leaves the ground, which is nearly all of them. Negative for
// exactly kNoReach's reason: tick 0 and tick 1 are both meaningful answers to
// "when does this move go airborne", so the sentinel must not be either of them.
inline constexpr std::int32_t kNeverAirborne = -1;

// --- Enumerations -----------------------------------------------------------

// A NOTE ON THE VERSION NUMBER BEFORE IT CONFUSES SOMEBODY. The comments below
// cite SCHEMA V3, and there is no file called schema.v3.json. The schema
// document lives beside the characters it describes, at
// Games/UntitledFighter/Assets/Characters/schema.v2.json (it was in the engine's
// asset root, Editor/src/Exported/Characters/, until this title took ownership
// of its own content; the STAGED path, Exported/Characters/, is unchanged), and
// declares itself v3 in its
// `$id`, its title and its engine.schema_version enum -- a JSON Schema
// document's identity is its `$id` rather than its filename, and nothing in this
// repository resolves that file by path. The schema says the same thing about
// itself in its own first key. Cite the version; open the v2 path.

// WHERE THE ATTACKER IS. NOT WHAT BLOCKS THE ATTACK -- see BlockHeight below.
//
// Schema v3, move.stance. There are five values rather than three because of
// a transcription loss that nothing in this project had recorded: MUGEN's own
// `statetype` is S/C/A/L, and every crouching move in the Phase-0 corpus -- 6 in
// Kung Fu Girl, 4 in Kung Fu Man -- arrived through the importer as `ground`.
// The crouching-ness survived only in the word "crouch" inside the move id,
// which is a string, which is not data: `crouch_mk` and `stand_mk` declared an
// IDENTICAL stance. ADR-001 never listed the collapse among its known losses,
// and meanwhile the same document names `p2statetype` three times as something
// Phase 5 must be able to read on the OPPONENT. The project knew state type
// mattered while having thrown its own away.
//
// `Ground` IS KEPT, AND IT IS NOT A FUDGE. It means GROUNDED, STANCE
// UNSPECIFIED: this move happens on the floor and the file does not say whether
// the character is standing or crouching. That is an honest description of what
// the MUGEN corpus actually is; it keeps the three shipped fixtures valid with
// no re-transcription; and it preserves the property ADR-001 records as worth
// keeping, that a file written against an older schema is a valid file under a
// newer one. A NEW character uses Standing or Crouching and never Ground, and
// the load-time roster check (A16) is what says so out loud rather than leaving
// it to a convention nobody reads.
//
// ORDER: APPEND ONLY. WHY THAT IS SAFE TODAY, AND WHAT WOULD END IT.
//
// Standing and Crouching are declared LAST rather than beside Ground where they
// read best, and the ugliness is deliberate: Any/Ground/Air keep the values
// 0/1/2 they have always had. Nothing observes those numbers today. A file
// authors the STRING "crouching" and the loader maps it, so no integer crosses
// the file boundary; and no integer crosses the wire either, because stance has
// no counterpart in cse::kernel::MoveDef -- MatchBuilder.cpp counts it as the
// `move.stance` loss instead of carrying it, and the connect handshake hashes
// MatchData, which is where a byte both peers must agree on lives.
//
// So appending was free. It will not always be: the day stance DOES cross into
// MoveDef -- which is what actually enforcing a low requires -- these values
// become wire-visible and a golden hash goes over them. Appending stays safe
// even then, because a new value at the end cannot change an existing one.
// REORDERING OR INSERTING IS NEVER SAFE: it silently re-labels every move in
// every replay and on every peer built from a different revision. Add at the
// end, accept that the list reads out of order, and leave this paragraph here.
enum class Stance : std::uint8_t { Any, Ground, Air, Standing, Crouching };

// True for the three values that mean "on the floor", false for Air and for the
// Any that means "the file did not restrict this move". A named function rather
// than `s != Stance::Air && s != Stance::Any` written out at each call site,
// because that expression is right in four places and forgotten in the fifth --
// and it was already wrong the moment Standing and Crouching existed.
constexpr bool StanceIsGrounded(Stance s) {
    return s == Stance::Ground || s == Stance::Standing || s == Stance::Crouching;
}

// WHICH BLOCK STOPS THE ATTACK. NOT WHERE THE ATTACK VISIBLY LANDS.
//
// Schema v3, move.blocked_as. The value names THE BLOCK THAT DEFEATS THE
// MOVE, and the whole content of the field is this table:
//
//     a HIGH block (standing) stops  { High, Mid }
//     a LOW  block (crouching) stops { Low,  Mid }
//
// so a `High` attack goes through a low block -- it is an overhead -- and a
// `Low` attack goes through a high block. `Mid` is stopped by either, is the
// default, and is therefore what every move in the Phase-0 corpus is: the field
// did not exist when those files were written, so an absent value has to mean
// the case that changes nothing.
//
// IT IS NOT CALLED `guard`, AND THE NAME IS THE POINT. `Move::guard` already
// exists a few lines below and means something entirely different: a RESOURCE
// MINIMUM -- "this move costs 100 meter" -- read by the prover as a componentwise
// >= over the resource vector. Two unrelated ideas would have been sharing one
// word on one struct, and the resource one got there first.
enum class BlockHeight : std::uint8_t { High, Mid, Low };

// STANCE AND BLOCK HEIGHT ARE ORTHOGONAL. DO NOT DERIVE EITHER FROM THE OTHER.
//
// This paragraph exists to refuse, in advance, a simplification that will look
// obviously correct to whoever comes next. A crouching attack is USUALLY a low.
// An aerial is USUALLY an overhead. So `blockedAs` looks like a function of
// `stance`, one of the two fields looks redundant, and deleting it looks like
// tidying. It is not, and "usually" is the word that ends careers here:
//
//   * A crouching heavy punch is commonly a MID anti-air. Crouching, and not a
//     low. This single move is the counterexample that settles the question.
//   * A standing overhead is a Standing move blocked HIGH, in a cast where every
//     other standing normal is a Mid.
//   * An air attack blocked while the defender crouches is stopped by the low
//     block in most games -- an aerial is not automatically an overhead either.
//   * A sweep and a crouching jab are both Crouching and only one of them is Low.
//
// Every cell of the (stance, blockedAs) grid is legal and each is a move
// somebody has shipped. The loader therefore performs NO cross-check between the
// two fields -- not an error, and deliberately not even a warning. Any such
// check would have to encode "usually", and a load-time diagnostic that fires on
// correct data is precisely how a check gets deleted: see the A01 note in
// CharacterData.cpp, where an assertion authored from reasoning rather than from
// the corpus had to be corrected after it rejected every character in it.

// WHAT AN INCOMING ATTACK CAN BE, FOR THE PURPOSE OF BEING INVULNERABLE TO IT.
//
// Schema v3, move.invincibility[].attack_kinds. Six tokens, closed, and the
// first thing to understand about the list is that IT SPANS TWO DIFFERENT AXES
// OF THE INCOMING MOVE. That is convenient, it is correct, and it is exactly the
// sort of thing that reads as obvious today and confuses somebody in six months,
// so it is written down rather than left to be inferred from the names:
//
//   High, Mid, Low   the incoming attack's BlockHeight -- its `blocked_as`.
//   Aerial           the incoming ATTACKER's Stance being Air.
//   Throw, Projectile   RESERVED. See below.
//
// Two properties of two different fields of the other character's move, in one
// list, because "what can I not be hit by" is the sentence a designer writes and
// it does not respect the schema's field boundaries.
//
// THE MATCH IS AN INTERSECTION AND NOT A CONTAINMENT, which is the whole reason
// an anti-air works. An incoming attack carries one kind from EACH axis at once:
// fighter_a's `air_mp` is stance Air and blocked_as High, so it arrives as
// {Aerial, High}. A window naming {Aerial} stops it because ONE token matches.
// Under a containment rule -- "the window must name every kind the attack has"
// -- an anti-air would have to enumerate all three guard heights as well, and
// the field would be useless for the one case it was added for.
//
// THE COST OF THAT, STATED RATHER THAN DISCOVERED: a window naming {Mid} also
// stops an AERIAL mid, and there is no way to write "invincible to grounded mids
// only". That needs a conjunction across the two axes, a conjunction across axes
// is a predicate language, and ADR-002 CHOICE B's discipline -- a closed enum,
// not a grammar -- says six tokens and one bitwise AND beat an expression tree
// nobody has data for. If a character ever genuinely needs it, that is the
// change, and it is a schema conversation rather than a loader one.
//
// THROW AND PROJECTILE ARE RESERVED AND ARE LEGAL TO AUTHOR TODAY. The kernel
// can produce neither: it has no throw at all, and although engine.projectile is
// real transcribed data on four moves across two characters, nothing in
// cse::kernel spawns one. A window naming only reserved tokens is INERT rather
// than wrong, and nothing warns about it -- a warning there would fire on
// correct forward-looking data, which is the mistake A04 was corrected for.
// Reserving them costs one enumerator each now; adding them later costs a bit
// position inside a struct the connect handshake hashes.
//
// ORDER IS APPEND-ONLY, for Stance's reason exactly: each value becomes a BIT
// POSITION in AttackKindMask, the mask is what a kernel will compare and
// therefore what a golden hash will eventually cover, and inserting a token in
// the middle silently re-labels every window in every file and on every peer.
enum class AttackKind : std::uint8_t { High, Mid, Low, Aerial, Throw, Projectile };

// A set of AttackKind, one bit each. A MASK RATHER THAN A vector<AttackKind>,
// and the three reasons are the three things that would otherwise need rules:
// it de-duplicates by construction, so ["mid", "mid"] cannot produce two
// entries; it is order-independent by construction, which is what makes
// overlapping windows a union rather than a precedence question; and it is one
// byte a kernel can AND against an incoming attack's kinds with no loop.
using AttackKindMask = std::uint8_t;

constexpr AttackKindMask AttackKindBit(AttackKind k) {
    return static_cast<AttackKindMask>(1u << static_cast<std::uint8_t>(k));
}

// Every kind, which is what an ABSENT or EMPTY `attack_kinds` list means. The
// loader normalises to this at load rather than storing "empty" and making every
// reader remember the special case -- the same rule this file applies to every
// other quantity: convert once, at load, and expose one representation.
//
// A CONSEQUENCE WORTH RELYING ON: after a successful load no window's mask is
// ever zero. Zero would mean "invulnerable to nothing", which is a window that
// does nothing while looking like protection, and there is no way to author it
// -- an empty list means everything and an unknown token is refused outright
// (A19). A reader may treat a zero mask as impossible.
inline constexpr AttackKindMask kAllAttackKinds =
    static_cast<AttackKindMask>(AttackKindBit(AttackKind::High) |
                                AttackKindBit(AttackKind::Mid) |
                                AttackKindBit(AttackKind::Low) |
                                AttackKindBit(AttackKind::Aerial) |
                                AttackKindBit(AttackKind::Throw) |
                                AttackKindBit(AttackKind::Projectile));

// schema.v2.json $defs.cancel.on. The prover's C++ header carries a single bool
// `onHit`, and the mapping is {Hit, Always} -> true, {Block, Whiff} -> false.
// Kept as four values here rather than a bool, because collapsing at load
// destroys the distinction the adapter has to make and this file may not.
enum class Contact : std::uint8_t { Hit, Block, Whiff, Always };

// schema.v2.json properties.decay.kind. There is deliberately no Multiplicative
// member: ADR-001 section 8 item 3 removed it from the enum, and assertion A02
// below rejects a file that still authors it. See the comment on A02.
enum class DecayKind : std::uint8_t { None, Linear, Table };

// How a move's [P] scalars relate to its engine.hits[] array, when it has one.
enum class HitsProjection : std::uint8_t { Single, First, FirstDamageSummed };

// --- Resources --------------------------------------------------------------

// ORDER IS A BUILD-WIDE CONTRACT, not a per-file choice. The prover keys its
// resource vector POSITIONALLY (comboprover.hpp:56, :128-129, :152), so index 0
// must mean the same resource in every file a build loads or a comparison of
// meter against juggle succeeds silently. ADR-001 section 8 item 7 promotes this
// to a decision; assertion A03 below is where it is enforced.
struct ResourceDef {
    std::string  name;
    std::int32_t initial = 0;
    std::int32_t floor   = 0;
    std::int32_t ceiling = 0;
    bool         hasCeiling = false;   // the file may author null, meaning none
};

// One entry of an `effect` (a delta) or a `guard` (a componentwise minimum).
// A sorted vector rather than a map from name to value: the name is resolved to
// an index at load, iteration order is then the resource order, and the resource
// order is the contract above.
struct ResourceAmount {
    ResourceIndex resource = 0;
    std::int32_t  value    = 0;
};

// --- The engine-only namespace ----------------------------------------------

// One HitDef of a move that registers several. ADR-001 section 4 group A: "the
// schema's Move is a single (startup, active, hitstun, damage) tuple; real
// states register several HitDefs at different animelems."
//
// `isAlternative` is the distinction that assertion A06 turns on, and it is not
// cosmetic. A record carrying a `condition` is an ALTERNATIVE profile, not a
// sequel: Kung Fu Girl's chop registers its second HitDef only when the first
// WHIFFED, so the two can never both land. Treating it as a sequel would credit
// the attacker with 198 damage from one button.
struct HitRecord {
    std::int32_t tick             = 0;   // ticks from move start, same base as Move::startup
    std::int32_t animElem         = -1;  // provenance only; -1 when the file did not say
    std::int32_t damageHundredths = 0;
    std::int32_t hitstunTicks     = 0;
    std::int32_t blockstunTicks   = -1;  // -1 for the authored null (Kung Fu Man has no blockstun)
    std::int32_t slideTicks       = 0;
    std::int32_t pushbackVelSub   = 0;   // a VELOCITY, sub-units per tick -- not Move::pushbackSub
    std::int32_t meterGain        = 0;
    std::int32_t reachSub         = kNoReach;  // this hit's own box, which need not equal Move::reachSub
    bool         isAlternative    = false;     // true iff the record carried a `condition`
    std::string  conditionProse;               // unparsed; see the note on predicates below
};

// One keyframe of the attacker's own displacement. ADR-001 section 4 group B:
// schema v1 had `pushback` for the defender and nothing at all for the
// attacker's travel, so the forward motion that decides whether a special
// connects at range existed nowhere in the file.
//
// posAdd is MUGEN's PosAdd, an instantaneous teleport applied at this tick in
// addition to the velocity. It needs its own slot because it is not a velocity
// and integrating it as one would spread one frame's displacement over many.
struct MotionKey {
    std::int32_t tick       = 0;
    std::int32_t velXSub    = 0;   // forward-positive, sub-units per tick
    std::int32_t velYSub    = 0;   // down-positive, matching MUGEN's sign convention
    std::int32_t posAddXSub = 0;
    std::int32_t posAddYSub = 0;
};

// The body a move presents WHILE IT RUNS, replacing the single box the character
// otherwise carries for its whole life. cse::kernel::Combat.h already predicted
// this field and its shape: "per-frame hurtboxes (a crouch is a different shape)
// are Phase 3, and they land as a per-move override here, not as a field of
// GameState."
//
// THIS IS WHY LOW-PROFILING IS GEOMETRY AND NOT A FLAG, and that is a design
// decision rather than an implementation detail. A crouching attack that ducks
// under a high attack does so because ITS BODY IS SHORTER for those frames --
// not because somebody wrote `low_profiles: true` on it. A boolean would answer
// exactly one question, the one its author happened to think of, and would have
// to be kept in sync with the shape by hand. A box answers all of them with no
// second rule: which specific attacks go over it, whether an opponent's sweep
// still catches it, whether a hop-over clears it, and how much of the character
// is left exposed. Not every crouching attack low-profiles, and a file that
// states a SHAPE never has to claim that it does -- the behaviour falls out.
//
// UNITS AND ORIENTATION ARE cse::kernel::Box's, exactly, because that is what
// this becomes: sub-units (1 pixel = 256), authored RELATIVE TO THE FIGHTER'S
// ORIGIN and as if the fighter faced +X, with +X stage right and +Y up. The
// field names and their order are the kernel's too, so the projection is four
// assignments a reader can check by eye rather than a mapping to get wrong.
// Half-open on both axes, per Combat.h: the box covers x in [x0, x1) and
// y in [y0, y1), which is why assertion A15 requires x0 < x1 and y0 < y1 rather
// than <= -- under a half-open convention an equal pair is not a thin box, it is
// the empty set.
//
// +Y IS UP HERE AND DOWN IN THE CHARACTER FILES, WHICH IS A LIVE TRAP AND NOT A
// HYPOTHETICAL. fighter_a.json's engine.constants.default_pushbox_sub is
// [-3328, -15360, 3328, 0]: MUGEN's convention, origin at the feet, head at
// -60 px. The identical body written for this struct is
// [-3328, 0, 3328, 15360], which is what MatchBuilder.cpp builds from the
// half-width and the height. Both are well-formed rectangles of the same size,
// so A15's inversion, emptiness and range clauses all pass either one -- and the
// author reaching for a hurtbox override will reach for the pushbox they already
// have. A15 therefore WARNS on a negative y0 and names the constant, because
// this is the one bad box that would otherwise ship.
//
// NAMED FOR THE SHAPE, NOT FOR TODAY'S ONLY USER. The author's brief specifies
// the hurtbox override as "the same shape as its hitbox", and that is the
// design: a low profile is two boxes failing to overlap, so the attack's box and
// the body's box must be the same kind of thing. The attack half does not exist
// yet -- the schema still authors no vertical extent for an attack, which
// MatchBuilder.cpp's loss table already reports -- and when it lands it reuses
// this struct instead of growing a parallel HitBox to drift from it.
//
// IT IS NOT NAMED `Box`. MatchBuilder.cpp opens with `using cse::kernel::Box;`
// at namespace scope inside cse::data, so a cse::data::Box declared here would
// collide with it outright. The name is a consequence of that file and is worth
// knowing before somebody "tidies" it.
struct BoxSub {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
};

// --- Moves, cancels, gap actions --------------------------------------------

// One span of ticks on which a move CANNOT BE HIT, and by what.
//
// Schema v3, move.invincibility[]. Half-open is NOT the convention here and the
// difference from BoxSub is deliberate: a box edge is a position on a line, so
// [x0, x1) makes x1 - x0 the width with no off-by-one, while a tick is a COUNT,
// and "six frames of invincibility" is the sentence a designer writes. So the
// window covers fromTick .. fromTick + ticks - 1 INCLUSIVE, matching the
// cancel-window bounds in cse::kernel::CancelEdge, which are inclusive for the
// identical reason and say so.
//
// A MOVE MAY CARRY SEVERAL AND THEY MAY OVERLAP. The meaning is the UNION: on a
// given tick you are invulnerable to every kind named by any window covering it.
// Nothing refuses an overlap, nothing warns about one, and the loader does not
// merge them -- the loaded list is the authored list, because the file is what a
// designer reads and a loader that turned two authored windows into three
// normalised ones would make the data stop matching the document.
//
// THE SCOPE CHECK THAT ALMOST WENT WRONG HERE, recorded because getting it right
// was not obvious. A07 requires hits[].tick strictly increasing and A08 requires
// the same of motion[].tick, so requiring it of these looks like consistency.
// IT IS NOT: a hit record and a motion keyframe are APPLIED IN SEQUENCE and the
// result depends on which came last, which is the entire content of those two
// assertions. A window contributes to a set union, and a union is commutative
// and idempotent, so order and disjointness cannot change the answer. Copying
// A07's rule here would have been a rule inherited from a neighbour's shape
// instead of derived from this field's meaning -- which is precisely what A04's
// scope_correction records going wrong once already.
//
// AND THE REASON THE UNION IS SOUND AT ALL is that there is no inverted form.
// The schema's older engine.invuln[] carries `kind: not_hit_by | hit_by`,
// because MUGEN has both controllers and one is a whitelist; two overlapping
// whitelists have no order-independent reading. This struct has no such field
// and must not grow one.
struct InvincibilityWindow {
    std::int32_t   fromTick = 1;                 // >= 1, same tick base as Move::startup
    std::int32_t   ticks    = 1;                 // >= 1; A18 refuses 0 -- see the loader
    AttackKindMask kinds    = kAllAttackKinds;   // never 0 after a successful load
};

struct Move {
    std::string  id;
    std::string  label;

    // The prover-read subset. Ticks.
    std::int32_t startup  = 0;
    std::int32_t active   = 1;
    std::int32_t recovery = 0;
    std::int32_t hitstun  = 0;

    std::int32_t damageHundredths = 0;
    std::int32_t reachSub    = kNoReach;  // maximum gap at which the move connects
    std::int32_t pushbackSub = 0;         // DEFENDER displacement on hit. ADR-001 section 6.3
                                          // item 1: this number is ESTIMATED in every shipped
                                          // character -- MUGEN records a velocity and a
                                          // friction and never a displacement -- and the
                                          // midscreen verdict turns on it.
    Stance       stance = Stance::Any;

    // --- engine.reaction, the block that says what a HIT DOES ----------------
    //
    // Authored on every move of `fighter_a` since the file was written and read
    // by nothing until ROADMAP M1.3d. These are the fields that make a defender
    // visibly react rather than merely lose health.
    //
    // Zero is "the file did not say" on all three, which is what a move authored
    // before this block existed gets and is why wiring them changes no character
    // that does not use them.
    std::int32_t hitstopTicks    = 0;   // impact freeze, BOTH fighters
    std::int32_t airHitstunTicks = 0;   // hitstun when the defender is AIRBORNE;
                                        // it is what makes a juggle last
    std::int32_t fallRecoverTicks = 0;  // ticks on the floor after a knockdown
    bool         causesKnockdown  = false;

    // WHICH BLOCK STOPS THIS MOVE. Mid by default, which is what an absent field
    // means and what every move written before this field existed is.
    //
    // Sitting here beside `stance` on purpose: they are the two halves of the
    // sentence a designer writes about an attack, and they are ORTHOGONAL. The
    // long note on BlockHeight above is the argument, and it is worth reading
    // before touching either field, because the collapse it refuses is one this
    // struct's layout would otherwise seem to invite.
    BlockHeight  blockedAs = BlockHeight::Mid;

    // WHO WINS WHEN BOTH ATTACKS WOULD LAND ON THE SAME TICK, and nothing else.
    // Higher wins outright and the loser takes nothing; EQUAL IS A TRADE and
    // both land.
    //
    // THE DEFAULT OF 0 IS THE VALUE THAT CHANGES NOTHING, exactly as
    // BlockHeight::Mid is. Every character ever authored leaves this field
    // absent, so every move compares 0 against 0, so every meeting is a tie, so
    // every meeting is a trade -- which is precisely what Combat.cpp's
    // ResolveHits does today ("the rule here is the symmetric one, and both
    // fighters land"). A file that does not author this field describes the
    // game that is already running, and the day the kernel starts reading it
    // every existing character must play identically. That is a regression test
    // rather than a hope.
    //
    // SCOPE, NARROWLY, BECAUSE THE WORD INVITES MORE. It is not a measure of how
    // good a move is. It does not gate STARTING a move, it interrupts nothing,
    // and it is consulted only in the one instant two hitboxes both connect. A
    // move that never meets another move on the same tick never consults it.
    //
    // WHY int16_t, AND THE WIDTH IS NOT A FREE CHOICE. This field's destination
    // is cse::kernel::MoveDef, whose loaded arrays the connect handshake hashes.
    // MoveDef already carries an explicit 16-bit `pad_`, placed there because
    // "hashing a struct with indeterminate padding compares two machines'
    // uninitialised bytes" -- so a 16-bit signed priority is exactly the slot
    // that already exists, and MoveDef neither grows nor acquires new implicit
    // padding. Whether the kernel spends that slot on this is ADR-005 P2's call;
    // what is decided HERE is that the authored range cannot exceed it, because
    // narrowing later is not a clamp, it is a SIGN FLIP AT THE BOUNDARY: 32768
    // truncated to 16 bits is -32768, and the strongest move in the file
    // silently becomes the weakest. Assertion A17 refuses the value instead.
    //
    // SIGNED, because "this move loses to everything" is a real thing to author
    // -- a committal heavy, a taunt, a move whose whole point is that it trades
    // badly -- and it is naturally -1. With an unsigned type the only way to
    // express "below the default" is to renumber every other move in the cast
    // upward, which is a data migration bought for one bit.
    //
    // THE NAME IS ALREADY IN THE SCHEMA TWICE AND NEITHER IS THIS FIELD.
    // engine.projectile.priority is MUGEN's ProjPriority; engine.reaction
    // .priority is MUGEN's HitDef parameter, and the AOF2 thug authors 2 on its
    // normals. Both are TRANSCRIPTIONS of what a source file said; this is the
    // DESIGNED rule -- the same relationship blockedAs already has with
    // engine.reaction.attack_type, and deliberately reused rather than renamed,
    // because the collision is two nesting levels away rather than on one object
    // the way `guard` was. THE HAZARD IS THE SCALE, NOT THE NAME: MUGEN's
    // parameter has its default in the middle of a small band, so a transcribed
    // 2 means BELOW NORMAL while a 2 here means ABOVE the default of 0. The two
    // numbers look identical and produce opposite outcomes, and NO ASSERTION CAN
    // TELL THEM APART because both are legal integers. Do not copy one into the
    // other; re-derive against this field's default.
    std::int16_t priority = 0;

    // THE FRAMES ON WHICH THIS MOVE CANNOT BE HIT AT ALL, and by which kinds.
    // Empty on nearly every move, which is what an absent field means and what
    // every character in the repository currently is.
    //
    // A LIST, NOT A RANGE, because more than one window per move is ordinary:
    // "invincible 1-6 and then throw-invincible through recovery" is two windows
    // with different kind sets, not one window with a caveat.
    //
    // THIS IS THE GENERAL MECHANISM AND IT SUBSUMES THE ANTI-AIR. An anti-air is
    // not a category of move needing its own rule and its own field -- it is a
    // move invincible to AttackKind::Aerial during its startup, which is one
    // window and no condition. A reversal is the same field with a wider kind
    // set. That is why the request for "a priority system toggled depending on
    // attack state" became two orthogonal fields rather than a priority with a
    // condition table: see the schema's x-v3-changes.why_two_mechanisms_and_not
    // _one, which records that this was a reading of the request and not a
    // transcription of it.
    //
    // IT RESOLVES BEFORE `priority`, AND THE ORDER IS WHY THE PAIR IS TWO
    // FIELDS. Invincibility removes an incoming hit from the set of hits that
    // landed at all, so there is nothing left for priority to arbitrate. An
    // anti-air invincible to aerials does not WIN the exchange with a jump-in;
    // it never has one.
    //
    // NOT THE SAME FIELD AS engine.invuln[], WHICH IS STILL NOT LOADED. That one
    // is the MUGEN transcription -- NotHitBy/HitBy, state types S/C/A, attribute
    // classes NA/SP/HT -- kept so a transcription can be audited line by line
    // against a .cns. This one is the designed rule in this schema's own nouns.
    // Neither can replace the other, and the reason is measurable rather than
    // stylistic: MUGEN's attribute vocabulary has NO GUARD-HEIGHT AXIS AT ALL
    // (MUGEN carries that in a different HitDef parameter entirely), so half of
    // what this field says was never sayable in the other one.
    std::vector<InvincibilityWindow> invincibility;

    std::vector<ResourceAmount> effect;   // sorted by resource index
    std::vector<ResourceAmount> guard;    // sorted by resource index

    // Engine-only, all optional in the file.
    std::int32_t stateId = -1;
    std::int32_t animId  = -1;
    std::int32_t variant = -1;            // the sub-character this move belongs to, or -1

    std::vector<HitRecord> hits;
    HitsProjection         hitsProjection = HitsProjection::Single;
    std::vector<MotionKey> motion;

    // The tick on which this move takes the character OFF THE GROUND, or
    // kNeverAirborne for the overwhelming majority that never do.
    //
    // THIS IS WHY HOP-OVERS ARE GEOMETRY TOO. A grounded attack that becomes
    // airborne partway through its animation passes over a low sweep for as long
    // as it is off the floor -- Ryu's f+HK is the standard example -- and that is
    // a consequence of where the body is, not a property anybody declares about
    // the sweep. This one tick number plus the hurtbox override below is enough
    // to answer it, for every pair of moves, without either move naming the
    // other. A `hops_over_lows: true` boolean would again answer only the
    // question its author thought of, and would be wrong the first time two
    // sweeps had different heights.
    //
    // Same tick base as Move::startup, so a move can go airborne BEFORE its
    // hitbox appears, which is what a hop kick actually does. Assertion A14
    // refuses a tick that falls outside the move, because a move cannot make you
    // airborne before it starts or after it has ended.
    //
    // MEANINGLESS ON AN `air` MOVE, which was airborne before it began. The field
    // is for the Ground/Standing/Crouching cases. It is not an error to author it
    // on an air move -- it says nothing new rather than something false -- so
    // nothing rejects it.
    std::int32_t airborneFromTick = kNeverAirborne;

    // The body this move presents while it runs. Absent on nearly every move, and
    // the EMPTY case is not expressible on purpose: assertion A15 refuses a
    // malformed or zero-area box, because a body with no area is total,
    // permanent, unattributed invulnerability -- and this schema already has a
    // field for invulnerability (engine.invuln[]) that says WHICH attacks and for
    // HOW LONG. Saying it by authoring a degenerate rectangle instead would hide
    // the strongest property a move can have inside four coordinates.
    bool         hasHurtboxOverride = false;
    BoxSub       hurtboxOverride{};

    // [open, close] in ticks, or absent. The EMPTY case is load-bearing and was
    // found by validating the schema against its own characters: the AOF2 thug's
    // moves correctly author [] because they have no cancel window, and a schema
    // that cannot say "none" forces an author to invent a value.
    bool         hasCancelWindow  = false;
    std::int32_t cancelWindowOpen = 0;
    std::int32_t cancelWindowClose = 0;

    // The Phase-0 gate is computed from this field and nothing else.
    bool        escapeHatchNeeded = false;
    std::string escapeHatchKind;

    // A predicate over the DEFENDER gating whether the move connects at all --
    // ADR-001 section 4 group G, "the finding". Kept as the prose the file
    // authors, not as a parsed predicate: every one of Kung Fu Girl's 17 normals
    // carries `!var(16) && var(15) < 1`, which counts hits since the defender was
    // last grounded, and evaluating that needs the opponent namespace Phase 5
    // owns and has not built. Preserved rather than dropped so that the day the
    // parser exists it has the strings in front of it.
    std::string hitConditionProse;
};

struct Cancel {
    MoveIndex    from  = kInvalidMove;   // resolved at load; a dangling id is a load error
    MoveIndex    to    = kInvalidMove;
    std::int32_t delay = 0;              // ticks between the source CONNECTING and the
                                         // follow-up being allowed to start
    Contact      on    = Contact::Hit;

    std::vector<ResourceAmount> effect;
    std::vector<ResourceAmount> guard;

    std::string label;
    std::string caveat;

    // False records that the authored edge is gated on a runtime condition no
    // importer can evaluate. Measured across the three Phase-0 characters:
    // 200 of 247 edges (KFG 103/134, KFM 71/87, AOF2 26/26).
    //
    // Do not confuse this with ADR-001:49's "26 cancels needing an opponent
    // predicate". That is a narrower group -- edges gated specifically on the
    // DEFENDER, which ADR-001 calls "the finding" -- and the field that carries
    // it is `engine.condition`, present on 5 edges in total. An earlier draft of
    // this comment quoted 26 here, which was wrong by 8x and attached the right
    // number to the wrong concept.
    bool        certain = true;
    std::string conditionProse;          // unparsed; see the note below
    std::string family;                  // engine.family / engine.rule, provenance only
};

struct GapAction {
    std::string  id;
    std::string  label;
    std::int32_t frames   = 0;
    std::int32_t maxUses  = 0;           // 0 means unlimited

    std::vector<ResourceAmount> effect;
    std::vector<ResourceAmount> guard;

    // `__space` is RESERVED on moves and cancels (assertion A04) and REQUIRED to
    // be authorable here: on a gap action it is the only way to express
    // displacement, and every shipped character authors it. Authored in prover
    // space units, where one unit is exactly one pixel (model.py:466); scaled to
    // sub-units on load so that every distance in this header is one unit.
    bool         hasSpaceEffect = false;
    std::int32_t spaceEffectSub = 0;
    bool         hasSpaceGuard  = false;
    std::int32_t spaceGuardSub  = 0;

    bool enabled = true;                 // engine.enabled: false means "transcribed as inert"
};

struct Decay {
    DecayKind    kind  = DecayKind::None;
    std::int32_t step  = 0;
    std::int32_t floor = 0;
    std::vector<std::int32_t> tablePermille;
};

// --- The character ----------------------------------------------------------

struct CharacterData {
    std::string id;      // the file's stem, e.g. "kung_fu_girl" -- stable across renames of `name`
    std::string name;    // the authored display name
    std::string stage;   // "midscreen" or "corner": which model the file's verdict answers

    std::int32_t walkSpeedSub = 0;              // sub-units per tick

    // Jump takeoff velocity and gravity, sub-units per tick(^2), KERNEL
    // semantics: +Y up, both positive, gravity a magnitude the kernel
    // subtracts. From `engine.movement` (ROADMAP M1.3(b1), ADR-014). ZERO
    // MEANS UNAUTHORED -- the kernel's `!= 0` fallback to its placeholder --
    // so the loader refuses an explicit 0 by name rather than letting it
    // silently mean "the default". NOT read from `engine.constants`, whose
    // MUGEN-provenance numbers are Y-down and cited-not-loaded.
    std::int32_t jumpImpulseSub = 0;
    std::int32_t gravitySub     = 0;

    // The modern input buffer's window, in ticks; zero-or-absent is no
    // buffering. Character-global (the schema note says why that matches the
    // genre), loaded with a hard 255 bound because the kernel ages the buffer
    // in a uint8. A window of N accepts a press on the actionable tick plus
    // the N before it -- N+1 ticks in all, so the modern "3-frame feel" is
    // authored as 2. ROADMAP M1.1e.
    std::int32_t inputBufferFrames = 0;
    std::vector<std::int32_t> scalingPermille;  // damage scaling by combo depth
    Decay decay;

    // Crouching body height in sub-units, from `engine.constants.crouch_height_px`.
    // ZERO MEANS UNAUTHORED and the standing body is used while crouching, which
    // is what every character written before this field gets.
    //
    // A HEIGHT AND NOT A BOX, unlike the kernel's crouchHurtbox: the file already
    // authors `height_px` beside this and the crouch shares the standing box's
    // width, so asking a designer to restate the width would be asking them to
    // keep two numbers in step for no gain. MatchBuilder turns the pair into the
    // box the kernel wants.
    std::int32_t crouchHeightSub = 0;

    std::vector<ResourceDef> resources;         // ORDER IS THE CONTRACT -- see ResourceDef
    std::vector<GapAction>   gapActions;
    std::vector<Move>        moves;
    std::vector<Cancel>      cancels;
    std::vector<MoveIndex>   starters;

    // Outgoing cancel edges per move, in file order. Built at load because a tick
    // asks "what can this move cancel into" far more often than the load runs,
    // and because building it once removes the temptation to scan `cancels`
    // linearly inside the simulation.
    std::vector<std::vector<CancelIndex>> cancelsFrom;

    // Sorted by id. Public, and everything in this struct stays public, because
    // CharacterData must remain an aggregate: a private member would make it one
    // and stop `CharacterData c{};` from meaning what it reads as.
    std::vector<std::pair<std::string, MoveIndex>> moveIndexById;

    // O(log n) id lookup with no hash container.
    MoveIndex FindMove(std::string_view moveId) const;

    // Rebuilds moveIndexById and cancelsFrom from moves/cancels. The loader
    // calls it; a caller that assembled a character by hand must call it too.
    void RebuildIndices();
};

// NOTE ON WHAT IS DELIBERATELY NOT LOADED.
//
// The structured predicate objects -- move.hit_condition,
// move.engine.hits[].condition and cancel.engine.condition in their
// predicateList form -- are NOT decoded into typed predicates here. They are
// schema v2's fields 7 and 8, and evaluating one needs the opponent namespace
// that ARCHITECTURE.md Phase 5 owns and has not yet been built. All three
// shipped characters author these as PROSE STRINGS anyway (the schema's `oneOf`
// permits both), so decoding the object form today would be code with no data
// behind it. The prose is preserved verbatim in `conditionProse` so nothing is
// lost, and the day Phase 5 lands, the parser has both forms in front of it.
//
// Also not loaded, for the same "no data behind it" reason:
// engine.projectile, engine.transitions (the on_land kind), engine.invuln,
// engine.freeze, engine.min_reach_sub, engine.proximity_variant,
// engine.motion_physics, engine.reaction, engine.anim, engine.fx.
//
// TWO ENGINE FIELDS ARE LOADED, AND THE EXCEPTION IS THE POINT.
// engine.airborne_from_tick and engine.hurtbox_sub cross into this header while
// their neighbours do not, because they are not waiting on Phase 5 or on a
// hitbox editor: they are the entire mechanism behind two behaviours the design
// asks for by name -- a crouching attack low-profiling a high one, and a
// grounded attack hopping over a low. Both fall out of a shape and a tick, both
// are authorable today with a tape measure, and neither means anything if it
// stops at the file. Leaving them unloaded would have left the two headline
// features of this schema revision as documentation.
//
// AND engine.invuln STAYS ON THE UNLOADED LIST WHILE Move::invincibility IS
// LOADED, WHICH LOOKS LIKE AN INCONSISTENCY AND IS NOT. They are two fields
// about one subject in two vocabularies. engine.invuln is a TRANSCRIPTION:
// MUGEN's NotHitBy/HitBy controllers, its S/C/A state types and its NA/SP/HT
// attribute classes, stored verbatim so a transcription can be audited against
// the .cns line it came from. Decoding it here would mean teaching this loader
// MUGEN's attribute algebra -- including the `hit_by` WHITELIST form, which is
// the inversion that would destroy the union semantics InvincibilityWindow
// depends on -- in exchange for data no kernel is asking for.
//
// The two are also not translations of each other, which is what stops one being
// deleted for the other. MEASURED: five engine.invuln windows exist across the
// two Editor characters and none in the three MUGEN fixtures; MUGEN's attribute
// strings have NO GUARD-HEIGHT AXIS AT ALL, so High/Mid/Low cannot be expressed
// there however it is stretched; and the one genuinely shared idea --
// state_types ["A"] against AttackKind::Aerial -- is not a clean mapping either,
// because engine.invuln inherits the S/C collapse ADR-006 exists to record while
// Stance no longer does.

// NOTE FOR WHOEVER ADDS THE LOSS-TABLE ROWS, WHICH IS MatchBuilder.cpp'S JOB AND
// NOT THIS FILE'S.
//
// Both new fields are DATA WITH NO READER: MatchData carries neither, so a
// character that authors them is projected into a kernel that ignores them, and
// saying so per character is MatchBuilder's whole job. Both rows are
// KernelPermits -- "the kernel lets the character do something the file does
// not" -- for the reason `move.stance` is: the file states a restriction, the
// kernel does not apply it, so more is possible in the game than in the file.
// Worded to match that row rather than inventing a second house style:
//
//   move.priority       KernelPermits, count = moves with a non-zero priority.
//     "Which move wins when both would land on the same tick. ResolveHits
//      decides every overlap from the same pre-hit state and then applies both,
//      so a move that should have beaten another outright trades with it
//      instead and the loser lands a hit the file says it never gets. More is
//      possible in the game than in the file."
//
//   move.invincibility  KernelPermits, count = moves with at least one window.
//     "Frames on which a move cannot be hit, by named attack kinds. The kernel
//      connects whenever the boxes overlap, so an anti-air's startup and a
//      reversal's frames 1-6 are both hittable and the move the file says
//      cannot be touched takes the hit. More is possible in the game than in
//      the file."
//
// A ZERO COUNT IS STILL LISTED, per MatchBuilder's own rule that "knowing a
// check ran and found nothing is worth more than its absence" -- and that
// matters more than usual here, because the count IS zero on every shipped file
// today, so a missing row would read as "nobody looked" rather than as "nobody
// authors it yet".

// --- Loading ----------------------------------------------------------------

struct LoadOptions {
    // Assertion A03 is a CROSS-FILE rule -- "every character in a build declares
    // the same resources in the same order" -- so no single file can check it
    // and the caller has to say what the build's order is. Empty means the check
    // is skipped, which is recorded as a warning rather than passing silently:
    // a build that never sets this has no A03 at all, and that should be visible.
    std::vector<std::string> expectedResources;

    // Refuse a file larger than this before reading it. Authored content is
    // untrusted (docs/MAINTENANCE.md), and a 4 GB "character" is a denial of
    // service that costs nothing to author.
    std::size_t maxFileBytes = 64u * 1024u * 1024u;
};

// The result of a load, as DATA. A rejected file is a normal outcome -- authored
// content is untrusted, and a hostile or merely broken character must not be
// able to throw through a match-start path or call exit(). There is no throw and
// no abort anywhere in the loader.
struct LoadReport {
    std::string error;                  // empty if and only if the load succeeded
    // The failing assertion's id -- "A01", "A15" -- when a load assertion is what
    // rejected the file, and empty when an ordinary type or range check did.
    // Deliberately NOT a list of the implemented ids: this comment said
    // "A01".."A04" for as long as it took A05-A08 to be added without anybody
    // updating it, and a range that has to be maintained is a range that goes
    // stale. The schema's own x-load-assertions block is the register.
    std::string rule;
    std::vector<std::string> warnings;  // non-fatal; the load still produced a character
};

// Load from a file. `relPath` is UNTRUSTED and is resolved through
// MyCoreEngine::PathIsContained against `baseDir` before the file is opened --
// docs/MAINTENANCE.md, "authored paths are untrusted", and that rule has no
// exceptions. Absolute paths, drive/UNC roots and any ".." component are
// refused lexically, before any filesystem access.
bool LoadCharacterFile(const std::string& baseDir,
                       const std::string& relPath,
                       const LoadOptions& options,
                       CharacterData& out,
                       LoadReport& report);

// Load from JSON already in memory. `sourceName` is used only to name the file
// in error messages. This exists so a test can construct a character that
// violates a load assertion without writing a hostile file to disk -- an
// assertion nobody has watched fail is not an assertion.
bool LoadCharacterJson(const std::string& sourceName,
                       const std::string& jsonText,
                       const LoadOptions& options,
                       CharacterData& out,
                       LoadReport& report);

// Load a base character with a VARIANT patch applied -- the showcase's
// one-fighter-many-patches mechanism (docs/adr/ADR-011 section 4, ROADMAP
// M1.6). The variant file is `{ "description": "<one line>", "patch": {...} }`;
// the description is REQUIRED and is returned through `description` when the
// caller wants it. The patch is RFC 7386 at the top level, EXCEPT that
// `patch.moves` is an OBJECT keyed by move id, each value merged onto that one
// array element -- merge patch treats arrays as atomic, and a variant that had
// to restate every move would stop being the diff it exhibits. A patch naming
// a move the base does not author is refused rather than silently inert. Both
// paths go through the same containment rule as LoadCharacterFile.
bool LoadCharacterVariant(const std::string& baseDir,
                          const std::string& baseRelPath,
                          const std::string& variantRelPath,
                          const LoadOptions& options,
                          CharacterData& out,
                          LoadReport& report,
                          std::string* description = nullptr);

} // namespace cse::data
