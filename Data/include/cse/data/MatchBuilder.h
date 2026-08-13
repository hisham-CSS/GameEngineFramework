// The bridge: a loaded character on one side, the bytes a tick reads on the
// other.
//
// Until this file existed, CseData could load Kung Fu Girl and CseKernel could
// fight, and no transcribed character had ever driven a tick. Everything here is
// in service of closing that: turn two CharacterData into one
// cse::kernel::MatchData, and hand back the index map the caller needs to go on
// talking about moves by name afterwards.
//
// ---------------------------------------------------------------------------
// WHY THIS LIVES IN CseData AND NOT IN CseKernel
// ---------------------------------------------------------------------------
// The kernel links NOTHING and Kernel/CMakeLists.txt makes that a configure-time
// failure to change. It therefore cannot see CharacterData, which is exactly the
// point: std::string and std::vector must not be reachable from a tick. The
// dependency runs one way only -- data reads a file and fills in a kernel-shaped
// POD; the kernel never reads a file.
//
// Note what that costs and what it does not. This translation unit includes
// cse/kernel/Combat.h for the STRUCT DEFINITIONS and calls no function declared
// in it. Not BoxIsValid, which would be the natural thing to reuse: calling it
// would put CseKernel into CseData's link line, and Data/CMakeLists.txt asserts
// that never happens. So the one bound check this file needs is written out
// again below, against the kernel's own kMaxBoxCoord constant. Duplicating four
// comparisons is the cheaper half of that trade.
//
// ---------------------------------------------------------------------------
// THE TWO REPRESENTATIONS DISAGREE. WHAT WAS DECIDED, AND WHY
// ---------------------------------------------------------------------------
//
// 1. VARIABLE-SIZED AND STRING-KEYED vs FIXED-CAPACITY AND INDEX-KEYED.
//    CharacterData holds however many moves the file authored, named by id.
//    FighterData holds 32 slots, of which slot 0 is the reserved idle slot, so
//    31 are usable. A character with more than 31 moves is REFUSED: BuildMatch
//    returns false and names the character, the count and the cap.
//
//    Truncating is the worst available option and it is worth saying why in more
//    than one word. A truncated build is not a smaller character, it is a
//    DIFFERENT one: the moves that fell off the end are exactly the ones a
//    designer added last, every cancel into them silently becomes a cancel into
//    nothing, and -- fatally -- the verdict ProverAdapter computed was computed
//    over the whole file. A truncating bridge would let the editor certify a
//    character that terminates and then ship a game playing a character that was
//    never analysed. That is D8's "the engine and the analysis disagree by a
//    frame", escalated to a whole move. Refusing turns it into a lobby error,
//    which is what Combat.h asks for: "a cap that is checked at load is a lobby
//    error, while a cap that is not is a buffer overrun."
//
//    Clamping to the cap and reporting a warning was considered and rejected for
//    the same reason -- a warning in a log is not a decision anybody made.
//
// 2. UNITS. Nothing is converted here that CseData did not already convert, with
//    exactly one exception, and that is deliberate: ARCHITECTURE.md D8 says a
//    quantization happens once, at load, by one documented rule.
//
//      frame data   ticks -> ticks              IDENTITY. schema v2 authors
//                                               startup/active/recovery/hitstun
//                                               in ticks at 60 Hz already.
//      distances    sub-units -> sub-units      IDENTITY. CharacterData.h's
//                                               units note: "every distance in
//                                               this header is SUB-UNITS, 1
//                                               pixel = 256". The float->integer
//                                               step already happened in the
//                                               loader (reach units are 100 px
//                                               each, and the shipped files
//                                               additionally carry an exact
//                                               pre-quantized reach_px).
//      damage       hundredths -> points        THE ONE CONVERSION. Combat.h:
//                                               "CseData stores hundredths and
//                                               the loader converts once, at
//                                               load". Round half away from
//                                               zero, matching D2's scaleBy, so
//                                               the rule is symmetric about
//                                               zero. Exact for all three
//                                               shipped characters.
//
//    ASSUMPTION, FLAGGED AS THE TASK ASKS. The schema's `reach` is authored in
//    reach units of 100 pixels, and the loader has already turned that into
//    sub-units; this file assumes nothing about the file format. What it DOES
//    assume is the SEMANTICS CharacterData.h states for the resulting number:
//    reachSub is "maximum gap at which the move connects", a gap being the space
//    between the two bodies. See the box construction below, which is built to
//    make that sentence literally true. The transcription derived those numbers
//    from a sprite box measured from the character's ORIGIN rather than from the
//    front of its body (each move's engine.derivation reads "Clsn1 <px>"), and
//    those two measurements differ by one body half-width. That discrepancy is
//    the file's, not this bridge's; it is reported as the `move.reach` loss
//    rather than silently split the difference.
//
// 3. WHAT HAS NO KERNEL COUNTERPART AT ALL. Cancels, gap actions, damage
//    scaling, hitstun decay, resources and their guards, pushback, stance, per
//    hit records, motion keys, hit conditions and walk speed. Every one of them
//    is listed in BuildReport::losses with a count and a direction, in the same
//    spirit as ProverAdapter's projection loss table, and the summary flag
//    BuildReport::playsAsAnalysed is false whenever any of them bites. It is
//    false for every shipped character today and the dominant reason is
//    `cancels`: the kernel has no cancel system, so the chains the prover
//    reasons about cannot be performed. A green build must not be allowed to
//    imply that the verdict describes the running game.
#pragma once

#include "cse/data/CharacterData.h"

#include "cse/kernel/Combat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cse::data {

// --- Capacity ---------------------------------------------------------------

// Slot 0 of FighterData::moves is the reserved idle slot and must stay zeroed,
// so a character may fill 31 of the kernel's 32 slots. Derived from the kernel's
// own constant rather than written as 31, because the day that cap moves this
// number must move with it.
inline constexpr std::int32_t kMaxBuildableMoves =
    cse::kernel::kMaxMovesPerFighter - 1;

// --- The body ---------------------------------------------------------------

// The kernel wants one hurtbox per fighter. CharacterData has no body at all:
// the shipped files carry engine.constants.default_pushbox_sub, and
// CharacterData.h's closing note leaves engine.constants unloaded, so the number
// is not reachable from here. Rather than invent one silently, the caller
// supplies it -- and a caller who does not gets the documented default below
// plus a WARNING naming it, the same shape LoadOptions::expectedResources uses
// for a check no single file can perform.
//
// Both values are sub-units. The box is symmetric about the origin and stands on
// the floor: x in [-halfWidth, +halfWidth), y in [0, height).
struct BodySpec {
    std::int32_t halfWidthSub = 0;   // 0 means "not supplied"
    std::int32_t heightSub    = 0;   // 0 means "not supplied"
};

// MUGEN's ground.front/ground.back of 13 px and a height of 60 px -- Kung Fu
// Girl's own transcribed constants. Named rather than inlined so that a report
// can quote the number it fell back to, and so nobody has to guess where a
// mystery 3328 came from.
inline constexpr std::int32_t kDefaultBodyHalfWidthSub = 13 * cse::kernel::kSubUnitsPerPixel;
inline constexpr std::int32_t kDefaultBodyHeightSub    = 60 * cse::kernel::kSubUnitsPerPixel;

// --- Buttons ----------------------------------------------------------------

// Which held buttons start a move.
//
// THE SCHEMA HAS NO INPUT NOTATION. Not a command list, not a motion, not a
// button name -- move ids like "stand_lp" carry the information only in the
// English of their spelling, and reading a button out of a string suffix is
// precisely the "1123-line heuristic over a foreign format" that ARCHITECTURE.md
// D7 rejects. This is another of ADR-001's missing nouns, and until the schema
// grows one the binding comes from the caller, where it is visible.
//
// `button` is a mask of cse::kernel::kInput* bits and the kernel requires ALL of
// them to be held (Combat.h). Zero means the move is not startable from a
// button, which is also the default for every move nobody binds.
struct MoveBinding {
    std::string   moveId;
    std::uint16_t button = 0;
};

// --- Options ----------------------------------------------------------------

struct BuildOptions {
    BodySpec body;

    // First binding for a given move id wins; a duplicate is a warning, not a
    // silent overwrite, because "the last one in the list" is a rule nobody
    // reading the call site can see. A binding naming a move this character does
    // not have is also a warning rather than an error: one binding table shared
    // by two characters with different movesets is a reasonable thing to write.
    std::vector<MoveBinding> bindings;
};

// --- What was lost -----------------------------------------------------------

// WHICH WAY A DROP ERRS, in the vocabulary a kernel bridge can actually justify.
// ProverAdapter's Permissive/Conservative axis is about the analysis being wrong
// in a direction; this one is about the RUNNING GAME differing from the authored
// character in a direction.
//
//   Exact          the kernel reproduces what the file says.
//   KernelPermits  the kernel lets the character do something the file does not:
//                  a move with no stance restriction, an attack that costs meter
//                  the fighter never had to earn. More is possible in the game
//                  than in the file.
//   KernelOmits    the kernel does not do something the file authors: a cancel,
//                  a pushback, a scaling curve. Less is possible in the game
//                  than in the file.
//
// Both directions matter and neither is "the safe one". The prover's verdict was
// computed over the file; either direction means the verdict is about a
// character nobody is playing.
enum class BuildLossDirection : std::uint8_t { Exact, KernelPermits, KernelOmits };

// One named thing the projection to MatchData could not carry, scoped to THIS
// character. `count` is how many objects in this character it touches, so a
// panel can say "134 cancels" rather than "in principle". A loss with count 0 is
// still listed: knowing a check ran and found nothing is worth more than its
// absence, and it is what lets a reader tell "this character has no decay" apart
// from "nobody looked".
struct BuildLoss {
    std::string        field;
    BuildLossDirection direction = BuildLossDirection::Exact;
    std::int32_t       count     = 0;
    std::string        note;
};

// Failures and non-fatal notes as DATA, exactly like LoadReport and
// ProverReport. Authored content is untrusted and a match-start path must not
// throw.
struct BuildReport {
    std::string              error;      // empty if and only if the build succeeded
    std::vector<std::string> warnings;
    std::vector<BuildLoss>   losses;

    // How many entries in `losses` have a nonzero count for this character. The
    // number a panel should show next to a green build, because a build that
    // succeeded otherwise reads as a build that is faithful.
    std::int32_t lossesThatBite = 0;

    // True only when lossesThatBite is 0 -- when the kernel plays the character
    // the file describes, and therefore the character ProverAdapter analysed.
    //
    // IT IS FALSE FOR EVERY CHARACTER THE SCHEMA CAN CURRENTLY EXPRESS, and
    // saying so here is more useful than pretending otherwise. The dominant
    // reason is `cancels`: the kernel has no cancel system, so the chains the
    // verdict is about cannot be performed. Two further entries are nonzero for
    // structural reasons that no file can avoid -- the schema authors no
    // vertical extent for an attack (`move.hitbox.y`) and no body at all
    // (`hurtbox`). So this is a goal-post rather than a discriminator today. It
    // is COMPUTED rather than hardcoded false, so the day the kernel grows
    // cancels and the schema grows boxes it flips on its own instead of being
    // remembered.
    bool playsAsAnalysed = false;
};

// --- Talking about moves after the build ------------------------------------

// The mapping between a character's move ids and the kernel's move slots.
//
// The mapping itself is one addition: character move i becomes kernel slot i+1,
// because slot 0 is reserved for idle. File order is preserved rather than
// sorted or compacted, which keeps this arithmetic total and keeps a kernel
// moveId one subtraction away from the MoveIndex ProverAdapter reports -- so a
// dead-cancel list and a live match can name the same move.
//
// Lookup is a binary search over a sorted vector. No hash container, for the
// reason CharacterData.h gives: this repository has twice had iteration order
// over a hash container leak into a simulation.
struct MoveIndexMap {
    std::string characterId;

    // Slot count including the reserved idle slot, i.e. exactly the
    // FighterData::moveCount that was built.
    std::int32_t moveCount = 0;

    // Index by kernel moveId. Element 0 is empty: the idle slot names no move.
    std::vector<std::string> idByMoveId;

    // Sorted by id, for lookup.
    std::vector<std::pair<std::string, std::uint16_t>> byId;

    // The kernel moveId for an id, or 0 when this character has no such move.
    //
    // 0 IS A SAFE SENTINEL HERE, and it is worth being explicit about that
    // because CharacterData::FindMove deliberately returns 0xFFFF for the
    // opposite reason. There, 0 is a perfectly good move index. Here, slot 0 is
    // reserved and can never name a move, so "0" and "idle" and "no such move"
    // are the same statement -- and a caller who forgets to check gets a fighter
    // that stays idle rather than one that performs the first move in the file.
    std::uint16_t Find(std::string_view moveId) const;

    // The id a kernel moveId names, or "" for idle and for anything out of
    // range. Exists for error messages and for tests, which is why it is here
    // and not in a panel.
    std::string_view IdOf(std::uint16_t kernelMoveId) const;

    // The mapping, both ways, as functions rather than as a rule people
    // remember. CharacterMoveOf(0) is kInvalidMove: idle is not a move.
    static std::uint16_t KernelMoveIdOf(MoveIndex characterMove);
    static MoveIndex     CharacterMoveOf(std::uint16_t kernelMoveId);
};

// --- Building ---------------------------------------------------------------

// One side. Public because both a match and an editor panel want exactly one of
// these, and because it is the unit the over-capacity decision applies to.
bool BuildFighterData(const CharacterData&      character,
                      const BuildOptions&       options,
                      cse::kernel::FighterData& out,
                      MoveIndexMap&             moves,
                      BuildReport&              report);

// Both sides, plus everything a caller needs afterwards.
//
// The reports live INSIDE the result rather than beside it, which departs from
// LoadCharacterFile's out-parameter shape. The reason is that there are two of
// them and they are per-side: a `BuildReport&` covering both would have to merge
// two loss tables, and a merged count of 268 cancels across two characters is a
// number about nothing.
struct MatchBuild {
    cse::kernel::MatchData data{};
    MoveIndexMap           moves[2];
    BuildReport            report[2];
};

bool BuildMatchData(const CharacterData& p0, const BuildOptions& options0,
                    const CharacterData& p1, const BuildOptions& options1,
                    MatchBuild& out);

// Human-readable names, for a panel and for test failure messages.
const char* BuildLossDirectionName(BuildLossDirection direction);

} // namespace cse::data
