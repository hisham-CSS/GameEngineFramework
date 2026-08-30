// The authoritative fighting-game simulation state.
//
// WHY THIS LIVES OUTSIDE Engine/. This header and its target deliberately link
// nothing -- no Jolt, no EnTT, no Lua, no GLM, no libm, not even the engine.
// That is docs/ARCHITECTURE.md D2 made structural: the standing temptation the
// plan names is "just ask the physics world for the hitbox", and against a
// separate target that is not a bad idea someone has to talk you out of, it is
// an unresolved symbol. See docs/adr/ADR-002-open-decisions.md CHOICE D. The same
// trick is why the fast-math CI gate works: a violation is a build failure
// rather than a review miss.
//
// THE RULES THIS FILE OBEYS, all from ARCHITECTURE.md D2/D4/D8:
//   * Integers only. No float, no double, anywhere in this target. Cross-platform
//     bit-identity is then a property of integer arithmetic rather than of a
//     compiler flag, which is what makes Windows<->Linux crossplay (NORTHSTAR Q1)
//     cost nothing to guarantee.
//   * Positions and velocities are SUB-UNITS: 1 pixel = 256 sub-units. Not a
//     general Fixed type with operator overloads -- D2 rejects that explicitly,
//     because this game almost never multiplies two positions and the overload
//     set buys an overflow surface and a mirror-asymmetry bug for nothing.
//   * Fixed size, no pointers, no heap, trivially copyable. Snapshot is memcpy
//     and restore is memcpy back. test_kernel.cpp asserts all of this, so the
//     property cannot rot silently.
//   * No hidden state. Anything the simulation reads must live in GameState,
//     including the PRNG. A static counter or a cached global is a desync that
//     only appears when someone rolls back.
#pragma once

#include <cstdint>
#include <type_traits>   // the layout assertions at the bottom of this file

namespace cse::kernel {

// 1 pixel = 256 sub-units. Chosen so a pixel divides evenly by 2, 4, 8 and 16 --
// halving a velocity is exact, which matters because friction and knockback are
// authored as halvings and a rounding difference is a desync.
inline constexpr std::int32_t kSubUnitsPerPixel = 256;

// --- The stage ---------------------------------------------------------------
//
// PUBLIC BECAUSE THREE THINGS HAVE TO AGREE ABOUT THEM, and until ROADMAP M1.1g
// only one of the three could see them: Simulate clamped against a file-local
// constant, the training mode WALKED A FIGHTER INTO THE CLAMP to find out where
// it was, and four test files wrote `480` down again by hand. That is the shape
// MAINTENANCE.md's rule is about -- if a comment says "must match X", make it
// call X.
//
// Both are stage properties rather than character ones, and a stage has no data
// model yet, so they are kernel constants for now and M1.2 is where they become
// authored. That is a known debt, written down rather than hidden: nothing here
// may be a number a level designer cannot set.
inline constexpr std::int32_t kStageHalfWidthSub = 480 * kSubUnitsPerPixel;

// THE INVISIBLE WALL, and it is the Street Fighter rule rather than a camera
// convenience. Two fighters may never be more than this far apart: a player
// backing away stops at it, and it MOVES WITH THE OPPONENT, so retreating stays
// possible for exactly as long as the opponent keeps advancing. Once they stop,
// so does the retreat -- and the only thing that lets anyone gain more ground
// than this is the opponent's own forward walk, or an absolute corner behind
// them.
//
// It is simulation and not presentation: it decides POSITION, so it decides
// whether a move reaches, and a camera that merely declined to show the gap
// would be a camera lying about a fight the kernel had already resolved.
//
// 374 px is 400 -- the width the view shows -- less a 13 px half-body at each
// edge, so two fighters at maximum separation are both fully on screen rather
// than half of each hanging off it. FightView derives its framing from THIS
// number for that reason; see kViewHalfWidthPx.
inline constexpr std::int32_t kMaxSeparationSub = 374 * kSubUnitsPerPixel;

// Ticks per second. The simulation is tick-driven, never delta-time driven:
// Application's FixedTimestep is explicitly NOT allowed to drive this (it caps
// at 8 steps then ZEROES the accumulator, dropping the backlog -- see
// ARCHITECTURE.md's rejected-ideas table -- and a dropped tick in lockstep is an
// unrecoverable desync).
inline constexpr std::int32_t kTicksPerSecond = 60;

// --- How many fighters a match has (docs/adr/ADR-009) ---------------------------

// EIGHT, AND THE NUMBER IS MEASURED RATHER THAN CHOSEN.
//
// Fighter::alreadyHitBits is a uint8 indexed as `1u << slot` (Combat.cpp). One
// bit per slot in eight bits, so eight is the capacity the multi-hit guard
// ALREADY HAS. A ninth slot would shift a bit out of the mask silently: an
// attack that has already connected would read as one that has not, which is
// exactly the infinite-combo bug that guard exists to prevent, reintroduced by
// a constant. The static_assert below ties the two together so neither can move
// alone.
//
// It is also enough for every shape asked for: 1v1 is 2, Tekken-style tag is 4,
// KOF/MvC 3v3 is 6, and 4v4 is 8. A larger ROSTER is not this number -- a 5v5
// with three benched per side is ten characters and never more than a few active
// at once, and what this counts is slots in the state, not names on a select
// screen. ADR-009 §7 records that distinction because it is the likeliest way
// somebody talks themselves into widening this.
inline constexpr std::int32_t kMaxFighters = 8;

// Two sides. This constant exists so `roundsWon` has a name rather than a 2, and
// so the one place that genuinely assumes two sides is greppable.
//
// NOTE WHAT DOES *NOT* USE IT: the "can a hit b" test is `a.team != b.team`, an
// inequality rather than `b.team == 1 - a.team`. Both are correct for two sides
// and only the first survives a third, and the difference cost one operator. A
// free-for-all is not in scope and is not being built; that operator is the
// entire allowance made for one. See ADR-009 §3.
inline constexpr std::int32_t kMaxTeams = 2;

// --- Resources, positionally (docs/adr/ADR-011 decision 1) ------------------

// HOW MANY RESOURCE SLOTS A FIGHTER HAS. Meter, juggle, and whatever else a
// character file declares -- all one array, indexed, never named.
//
// POSITIONAL IS THE WHOLE POINT, and it is a contract across three programs.
// comboprover's ResourceVec is indexed and nothing in it names a resource, so
// index i in the character file must be index i here and index i in the prover
// or the analysis compares meter against juggle points and says nothing about
// either. Load assertion A03 checks it; this array is what makes it true by
// construction rather than by agreement.
//
// FOUR because that is what the shipped files need (meter, juggle) with room
// for two a character invents. Widening it is a GameState expansion -- batch it
// with the next one (ADR-005 section 3) rather than spending a re-golden on it.
inline constexpr std::int32_t kMaxResources = 4;

// --- The per-tick event ring (ROADMAP M3.1) --------------------------------

// One thing the simulation wants to tell presentation about. RESERVED HERE AND
// NOT YET WRITTEN: M3.1 fills it, and it lands in this expansion so the wire
// format changes once rather than twice.
//
// It is IN THE STATE, which is the whole design. A callback fired from inside a
// tick fires again on every re-simulation, so a seven-frame rollback plays the
// hit sound eight times. An event appended to the state rolls back with the
// state, and presentation drains only confirmed ticks (docs/DETERMINISM.md K11).
struct Event {
    std::uint8_t slot;   // which fighter it happened to
    std::uint8_t kind;   // hit, block, land, ... -- M3.1 names them
    std::int16_t a;      // kind-specific payload
    std::int16_t b;
};

// Per TICK, not per second: the ring is drained every tick it is written.
inline constexpr std::int32_t kMaxEventsPerTick = 16;

// What a fighter is currently guarding, in Fighter::guard.
//
// A LEVEL rather than an edge: it is recomputed from held input every tick, the
// way every other input read in this kernel works (MoveDef::button is "all of
// them held"). It lives in the state rather than being recomputed inside hit
// resolution because ResolveHits does not receive inputs -- and giving it inputs
// would let it read a button the fighter's own step had already declined to act
// on, which is two readings of one tick's input.
inline constexpr std::uint8_t kGuardNone = 0;
inline constexpr std::uint8_t kGuardHigh = 1;  // standing block: stops high + mid
inline constexpr std::uint8_t kGuardLow  = 2;  // crouching block: stops low + mid

// GameState::roundState.
inline constexpr std::uint8_t kRoundFighting = 0;
inline constexpr std::uint8_t kRoundOver     = 1;  // this round is decided
inline constexpr std::uint8_t kMatchOver     = 2;  // a team has taken the set

// Full damage. Fighter::scaling is a PERCENT rather than a fraction because the
// kernel has no division that rounds the way a designer expects, and a percent
// of an integer damage value is one multiply and one divide by a constant whose
// rounding is stated once, here, rather than at each site.
inline constexpr std::uint16_t kScalingFull = 100;

// One player's authoritative state. Field order is chosen for packing, not for
// reading pleasure: the whole struct is memcpy'd 128 times a second in the worst
// rollback case, and it is hashed for the desync checksum.
struct Fighter {
    std::int32_t posX;      // sub-units from stage centre, +X is stage right
    std::int32_t posY;      // sub-units above the floor; 0 is grounded
    std::int32_t velX;      // sub-units per tick
    std::int32_t velY;      // sub-units per tick
    std::int32_t health;
    // Positional resources. res[i] is whatever slot i of the character file
    // declares -- see kMaxResources. This replaced a named `meter` field that
    // no file in Games/UntitledFighter/Kernel/src/ ever wrote: it was declared,
    // hashed, and dead, which is worse than absent because it read as support
    // for a mechanic the kernel did not have. ROADMAP M1.1b writes it.
    std::int32_t res[kMaxResources];

    // Pushback velocity, in sub-units per tick, decaying toward zero.
    //
    // SEPARATE FROM velX ON PURPOSE, and the reason is a line in StepPhysics:
    // a fighter who cannot act has `velX = 0` written every tick. Pushback that
    // lived in velX would therefore be erased on the very tick it was applied,
    // because being hit is precisely when you cannot act. Adding a "unless we
    // were just pushed" exception to that line would make one field mean two
    // things depending on a condition, which is the ADR-006 §2 mistake.
    //
    // Signed, and in stage coordinates rather than relative to the attacker, so
    // that two fighters pushing each other in opposite directions compose by
    // addition and need no arbitration.
    std::int32_t pushX;

    std::uint16_t moveId;   // index into the character's move table; 0 = idle.
                            // The table is Combat.h's FighterData::moves, which
                            // is passed to Simulate rather than stored here --
                            // no tick writes it, and D4's snapshot is for what
                            // ticks write. See the long note at the top of
                            // Combat.h for why that split is exactly there.
    std::uint16_t moveFrame;// ticks since this move started; the tick it starts
                            // on is frame 0
    std::uint16_t hitstun;  // ticks remaining; 0 = actionable
    std::uint16_t blockstun;

    // Ticks this fighter does not advance. Hitstop is NOT a stun: a fighter in
    // hitstop is frozen mid-move and its moveFrame does not tick, which is what
    // makes a heavy hit feel heavy without changing what frame the move is on
    // when it resumes.
    //
    // Combat.h's old note said hitstop "changes the meaning of every frame number
    // in the character data", and that is why it is a FREEZE rather than a
    // subtraction: startup 5 is still five ticks OF THE MOVE. What changes is how
    // many wall-clock ticks those five occupy, which is a thing a player sees and
    // not a thing the frame data claims.
    std::uint16_t hitstop;

    // Ticks remaining on the floor after a knockdown, counting down to wakeup.
    // Zero means standing (or airborne -- knockdown is a grounded state).
    //
    // This is MUGEN's statetype L, which ADR-006 §1.3 records being destroyed in
    // transcription by the same expression that destroyed C, and §6(b) records as
    // the reason "a reversal beats a meaty" could not be built: a meaty is an
    // attack timed against a state that did not exist. It exists now.
    std::uint16_t knockdown;

    // Remaining juggle budget, as a DEFENDER. Spent by moves that author a
    // juggle cost, refilled when the combo ends.
    //
    // Signed because the prover's certificate is a `nonNegative` condition over
    // this quantity (docs/manual/fighting-core.md), and a budget that cannot go
    // negative cannot express the check that refuses the hit which would have
    // taken it there. The kernel refuses the hit instead, so it never actually
    // goes below zero -- but the type is the one the proof is written in, and
    // matching it is free.
    std::int16_t juggle;

    // Damage scaling for the combo in progress, as a percent of authored damage.
    // kScalingFull (100) is undiminished. Applies to this fighter AS A DEFENDER,
    // because proration is a property of the combo being received.
    std::uint16_t scaling;

    std::uint8_t facing;    // 0 = facing +X, 1 = facing -X. Not a sign multiplier:
                            // a sign would invite `pos * facing` and the
                            // mirror-asymmetry bug D2 warns about.
    std::uint8_t airborne;
    std::uint8_t comboHits; // drives hitstun decay; see ADR-001 on decay.floor

    // Which fighters the CURRENT active window has already hit: bit i is set
    // once this attack has connected on slot i. Cleared whenever a move starts
    // or ends, so it describes one active window and never a whole match.
    //
    // This is D4's `alreadyHitBits`. It is the guard against the oldest bug in
    // the genre: a hitbox that stays live for three frames deals its damage on
    // all three, and every string in the game becomes an infinite for a reason
    // that has nothing to do with the character.
    //
    // ITS WIDTH IS WHY kMaxFighters IS 8 rather than the other way round -- see
    // that constant. It STAYS a uint8: widening it would buy nothing, because
    // the projectiles D4 anticipates need 32 more bits and 8 + 32 does not fit a
    // uint32 either. A projectile system needs its own mask at any width, so
    // paying three bytes here to postpone that is paying for nothing.
    std::uint8_t alreadyHitBits;

    // Which side. The opponent test is `a.team != d.team && d.active`; see
    // kMaxTeams for why that is an inequality.
    std::uint8_t team;

    // 0 for a benched tag partner: no position, no boxes, no turn, and unhittable
    // in both directions -- but its health and meter persist, which is the whole
    // point of benching rather than removing.
    //
    // State rather than data because a tag swap is a tick writing it, and
    // "anything a tick writes lives here" is this file's rule.
    std::uint8_t active;

    // Input posture. SEPARATE FROM airborne, and the two are not a collapsed
    // three-value enum by deliberate choice: `airborne` is derived from POSITION
    // (posY <= 0 lands you) and `crouching` from INPUT (holding down). They have
    // different sources of truth and different writers, which is what makes them
    // two facts rather than ADR-006 §2's one-field-two-ideas mistake.
    //
    // The impossible combination is closed by construction rather than by
    // convention: StepPhysics forces crouching to 0 whenever airborne is 1.
    std::uint8_t crouching;

    // kGuardNone / kGuardHigh / kGuardLow, recomputed from held input each tick.
    std::uint8_t guard;

    // --- Reserved for the pass-1 mechanics (ROADMAP M1.3) ------------------
    //
    // Nothing writes these yet, and they are here anyway: M1.3 adds wall
    // bounce, wall splat, ground bounce and counter-hit as per-hit reactions,
    // and every one of them needs state on the DEFENDER. Reserving them costs
    // three bytes now; adding them later costs a second wire-format change and
    // a second cross-toolchain re-golden (ADR-005 section 3).
    std::uint8_t reaction;   // which on_hit reaction is playing out; 0 = none
    std::uint8_t bounces;    // bounces spent, so a loop cannot bounce forever
    std::uint16_t flags;     // per-fighter reaction bits, defined by M1.3

    // --- Input edges and buffering (ROADMAP M1.1d) -------------------------
    //
    // WHY THESE ARE IN THE STATE and not in whatever produced the input. D6 puts
    // the input ring OUTSIDE GameState on purpose, and Simulate is handed only
    // the CURRENT tick's bits -- so an edge computed anywhere else is recomputed
    // wrongly on every re-simulation, and a rollback would replay a press as a
    // hold. Anything the simulation reads lives here; that is the whole rule.
    //
    // Last tick's buttons, which is where both edges come from: a press is
    // `bits & ~prevButtons` and a release is `~bits & prevButtons`. One field,
    // two mechanics, which is why positive and negative edge are one change.
    std::uint16_t prevButtons;

    // A press that arrived while this fighter could not act, kept for a few
    // ticks and CONSUMED -- not merely aged out -- the tick they become
    // actionable. Consumed, because a buffered press that is only aged would
    // start a move and then start it again on the next actionable tick.
    //
    // The window is a per-character field (FighterData::inputBufferFrames), not
    // a constant here: buffering is a mechanic, and mechanics are authored
    // (docs/adr/ADR-011 decision 1). Zero means no buffering, which is the
    // behaviour a file that says nothing gets.
    std::uint16_t bufferedButtons;
    std::uint8_t  bufferAge;      // ticks the buffered press has been waiting

    // THREE, not one, and the assertion is what said so. Fighter is 40 bytes of
    // int32 plus 22 of 16-bit plus its uint8 group, and the struct has to end on
    // a multiple of its 4-byte alignment -- one pad byte left it at 74 and the
    // compiler quietly added two more. has_unique_object_representations_v
    // turned that into a build error instead of two machines hashing bytes
    // neither of them wrote.
    std::uint8_t  pad_[3];
};

// The complete authoritative state. Everything the simulation may read.
struct GameState {
    std::uint32_t tick;
    std::uint32_t rng;      // snapshot-OWNED PRNG. Rolling back the state rolls
                            // back the random stream, which is the only way a
                            // re-simulated tick can reproduce its first run.

    // Ticks left in the round, counting down. Zero with roundState == fighting
    // means time out, which is decided by the round rule rather than here.
    std::uint32_t roundTimer;

    std::uint16_t roundNumber;

    std::uint8_t roundState;   // kRoundFighting / kRoundOver / kMatchOver

    // Slots in p[] that are part of this match. Slots at or above it are not
    // simulated and are zeroed by ResetMatch, so hashing the whole array is
    // stable regardless of how many fighters a mode uses.
    std::uint8_t fighterCount;

    // Rounds taken, per team. THIS is the field that assumes two sides, and it
    // is the only one -- see kMaxTeams.
    std::uint8_t roundsWon[kMaxTeams];

    // Rounds a team needs to take the set. Zero means the set never ends, which
    // is what training mode wants and what every test predating round flow gets.
    //
    // It is in the STATE rather than in MatchSetup alone because the round rule
    // reads it every tick, and ADR-002's rule is that anything the simulation
    // reads lives here. A copy in the host that the kernel consulted would be
    // exactly the hidden state this file's header forbids.
    std::uint8_t roundsToWin;

    // Explicit, for Fighter's reason: hashing a struct with indeterminate
    // padding compares two machines' uninitialised bytes. This pads the header
    // to 20 bytes, which is a multiple of Fighter's 4-byte alignment, so the
    // array that follows starts where the arithmetic in
    // test_determinism_crossplat.cpp says it does.
    std::uint8_t pad_;

    // The per-tick event ring, reserved by ROADMAP M1.1a and filled by M3.1.
    // Sized and counted rather than terminated: a fixed-capacity array with an
    // explicit count is the only shape a memcpy snapshot can carry
    // (docs/DETERMINISM.md S4).
    Event ev[kMaxEventsPerTick];
    std::uint8_t evCount;

    // Three bytes, explicit, for the reason pad_ above is explicit: ev is
    // 2-aligned and Fighter is 4-aligned, so without these the compiler would
    // insert padding here and two machines would hash their uninitialised
    // bytes. has_unique_object_representations_v below is what catches it.
    std::uint8_t pad2_[3];

    Fighter p[kMaxFighters];
};

// The mask must be exactly wide enough for the slots, in both directions. Too
// narrow silently breaks the multi-hit guard; too wide is a byte nobody reads
// that still lands in the wire hash.
static_assert(kMaxFighters == 8,
              "kMaxFighters is pinned to the width of Fighter::alreadyHitBits. "
              "Changing it means changing that field's type, re-deriving "
              "Fighter's padding, and re-recording the cross-toolchain golden -- "
              "see docs/adr/ADR-009 section 7's 'Reversed if'.");
// Written without naming std::size_t, because this header includes <cstdint> and
// not <cstddef> -- and the kernel's include list is short on purpose.
static_assert(kMaxFighters <= static_cast<std::int32_t>(sizeof(std::uint8_t) * 8u),
              "Fighter::alreadyHitBits cannot address every fighter slot. "
              "bitForSlot() would shift out of range and an attack that had "
              "already connected would read as one that had not.");

// --- THE LAYOUT CONTRACT ---------------------------------------------------
//
// These lived in tests/test_kernel.cpp, which is the wrong place for them by
// one link in the chain: a test asserts a property of a build that already
// compiled, and these are properties a change to the struct above should not be
// able to compile past. Here, editing Fighter or GameState meets them.
//
// The one that was MISSING is has_unique_object_representations_v, and it is
// the one the whole scheme rests on. Checksum() hashes the object
// representation -- the raw bytes -- so a byte the compiler inserted and never
// wrote is compared between two machines that never agreed on it. Nothing else
// in this repository would notice: the build succeeds, every local test passes,
// and two peers desync. docs/DETERMINISM.md S3.

static_assert(std::is_trivially_copyable_v<GameState>,
              "GameState must be trivially copyable: the rollback snapshot is a "
              "memcpy (docs/ARCHITECTURE.md D4). Adding a std::string, a "
              "std::vector, or any type with a user-provided copy constructor "
              "breaks rollback without breaking the build anywhere else.");

static_assert(std::is_standard_layout_v<GameState>,
              "GameState must be standard-layout: Checksum() hashes its object "
              "representation, and the desync detector compares those hashes "
              "across two machines built by two different compilers.");

static_assert(std::is_trivially_default_constructible_v<GameState>,
              "GameState must stay an aggregate -- adding a constructor is what "
              "would make it non-trivially-copyable.");

static_assert(std::is_trivially_copyable_v<Fighter>, "see GameState");

static_assert(std::has_unique_object_representations_v<GameState>,
              "GameState has padding, so two bytes of it are indeterminate and "
              "Checksum() is hashing whatever the compiler happened to leave "
              "there. Add an explicit padN_ member where the hole is -- see "
              "pad_ and pad2_ -- rather than reordering fields, which changes "
              "the wire format for every peer.");

// Written as a SUM OF THE MEMBERS rather than a byte count, so it keeps asking
// "did padding appear" instead of "is this the number I last wrote down".
static_assert(sizeof(Fighter) == sizeof(std::int32_t) * (6 + kMaxResources) +
                                     sizeof(std::uint16_t) * 11 +
                                     sizeof(std::uint8_t) * 14,
              "Fighter has grown a member that is not accounted for, or the "
              "compiler inserted padding into it.");

// The header is tick, rng and roundTimer (uint32); roundNumber (uint16); six
// bytes -- roundState, fighterCount, roundsWon[2], roundsToWin, pad_; then the
// event ring, evCount and three bytes of pad2_.
static_assert(sizeof(GameState) == sizeof(std::uint32_t) * 3 +
                                       sizeof(std::uint16_t) +
                                       sizeof(std::uint8_t) * 6 +
                                       sizeof(Event) * kMaxEventsPerTick +
                                       sizeof(std::uint8_t) * 4 +
                                       sizeof(Fighter) * kMaxFighters,
              "GameState has grown a member that is not accounted for, or the "
              "compiler inserted padding between the header and the fighters. "
              "Either way the byte layout changed and the checksum is no longer "
              "comparable to a peer built before the change.");

// One player's inputs for one tick. A bitfield rather than an enum set, because
// this is the thing sent over the wire every tick and re-read on every rollback.
struct Input {
    std::uint16_t bits;
};
inline constexpr std::uint16_t kInputUp     = 1u << 0;
inline constexpr std::uint16_t kInputDown   = 1u << 1;
inline constexpr std::uint16_t kInputLeft   = 1u << 2;
inline constexpr std::uint16_t kInputRight  = 1u << 3;
inline constexpr std::uint16_t kInputLP     = 1u << 4;
inline constexpr std::uint16_t kInputMP     = 1u << 5;
inline constexpr std::uint16_t kInputHP     = 1u << 6;
inline constexpr std::uint16_t kInputLK     = 1u << 7;
inline constexpr std::uint16_t kInputMK     = 1u << 8;
inline constexpr std::uint16_t kInputHK     = 1u << 9;

// Inputs for every slot, indexed the same way GameState::p is. The name is now
// a mild lie -- it was a pair when there were two fighters -- and it is KEPT
// because renaming it would touch every host, every replay reader and every
// test for no behavioural gain. ADR-009 §1 measures that indexed access is
// exactly what survives a widening; the name is the one thing that does not.
struct InputPair {
    Input p[kMaxFighters];
};

// --- Starting a match (docs/adr/ADR-009 section 5) ------------------------------

// One slot's opening conditions.
//
// This is what makes a fight startable from something other than "both fighters
// at +/-100 pixels with 1000 health", which was true of every fight in the game
// because the only way to start one said so. An RPG progression that raises a
// character's health, a story fight that opens cornered, a handicap, or a 3v3
// with three start positions are all this struct and no new system.
struct FighterSetup {
    std::int32_t startPosX;
    std::int32_t startHealth;
    std::uint8_t team;
    std::uint8_t active;
    std::uint8_t facing;
    std::uint8_t pad_;
};

// Everything ResetMatch needs that is not character data.
//
// NOT hashed and not on the wire: it is an ARGUMENT to ResetMatch, and what
// goes on the wire is the GameState it produces. Two peers must agree on the
// setup, but they prove that by agreeing on the resulting state's checksum,
// which they already exchange every 8 ticks.
struct MatchSetup {
    std::uint32_t seed;

    // Ticks in a round. Zero means untimed, which is what training mode wants
    // and what every test that predates round flow gets by default.
    std::uint32_t roundTicks;

    // Rounds a team must take to win the set. Zero means "never ends", the
    // training-mode default, for the same reason.
    std::uint8_t roundsToWin;

    std::uint8_t fighterCount;
    std::uint8_t pad_[2];

    FighterSetup p[kMaxFighters];
};

} // namespace cse::kernel
