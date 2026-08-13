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

// --- Enumerations -----------------------------------------------------------

enum class Stance : std::uint8_t { Any, Ground, Air };

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

// --- Moves, cancels, gap actions --------------------------------------------

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

    std::vector<ResourceAmount> effect;   // sorted by resource index
    std::vector<ResourceAmount> guard;    // sorted by resource index

    // Engine-only, all optional in the file.
    std::int32_t stateId = -1;
    std::int32_t animId  = -1;
    std::int32_t variant = -1;            // the sub-character this move belongs to, or -1

    std::vector<HitRecord> hits;
    HitsProjection         hitsProjection = HitsProjection::Single;
    std::vector<MotionKey> motion;

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
    std::vector<std::int32_t> scalingPermille;  // damage scaling by combo depth
    Decay decay;

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
    std::string rule;                   // "A01".."A04" when an ADR-001 load assertion failed
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

} // namespace cse::data
