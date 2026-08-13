// The combo-prover adapter: CharacterData in, a verdict out.
//
// ARCHITECTURE.md contribution #9 is "an integration of the analysis into a
// working engine's editor" -- a designer authoring a character is told, live,
// whether what they just typed has an infinite combo. This header is the seam
// that makes that possible: it takes the character the game actually loads and
// hands back the decision procedure's answer in this project's own vocabulary.
//
// THREE THINGS ABOUT THIS SEAM, AND THEY ARE ALL DELIBERATE.
//
// 1. `comboprover.hpp` DOES NOT APPEAR IN THIS HEADER. Not in a member, not in
//    a signature, not in a forward declaration. The prover is an implementation
//    detail of ProverAdapter.cpp, so the editor panel that draws these results
//    needs no include path to it, and the day the midscreen model arrives (it
//    lives in Python today -- ADR-001 section 3, gate 4) it can be swapped in
//    behind this same declaration. Everything below is spelled in MoveIndex,
//    CancelIndex and ResourceIndex, which is what the rest of CseData speaks.
//
// 2. THE VERDICT ANSWERS THE CORNER, WHATEVER THE FILE SAYS. `comboprover.hpp`
//    :15-24 states its own scope: a defender pinned against the wall, with no
//    distance between the players and therefore no walking forward to stay in
//    range. All three shipped characters declare `"stage": "midscreen"`, and
//    ADR-001 section 2 measures Kung Fu Man as TERMINATING midscreen and
//    INFINITE in the corner -- BOTH CORRECT, two different questions. So
//    ProverResult carries the stage it answered AND the stage the file claims,
//    and a panel that shows one without the other is showing a coin flip
//    (ARCHITECTURE.md section 5.3, as amended).
//
// 3. THE PROJECTION IS LOSSY, AND EVERY LOSS IS REPORTED WITH ITS DIRECTION.
//    See ProjectionLoss below. This is not documentation-in-a-struct for its own
//    sake: the direction of a loss is the difference between a tool that is
//    merely noisy and a tool that lies, and ADR-001 spends a whole section
//    (section 4, groups A-I) establishing which of this projection's losses are
//    closable and which are not.
#pragma once

#include "cse/data/CharacterData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cse::data {

// The three outcomes. UNKNOWN is a real answer, not an error: the search has a
// budget, and an unfinished search dressed up as a clean bill of health is worse
// than no answer at all (comboprover.hpp:309-313).
enum class ProverStatus : std::uint8_t { Terminating, Infinite, Unknown };

// Only one member, and that is the point. The in-engine decision procedure is
// corner-only; a second member appears the day the midscreen model is ported,
// and every switch over this enum will then fail to compile until it is handled.
enum class ProverStage : std::uint8_t { Corner };

// WHICH WAY A LOSS ERRS. The whole soundness argument is this enum.
//
//   PERMISSIVE   the model can say INFINITE where the game is safe. A designer
//                investigates a combo that turns out not to exist, is annoyed,
//                and ships a correct character. SAFE.
//   CONSERVATIVE the model can say TERMINATING where the game has a real
//                infinite. The designer is told the character is fine and it is
//                not. This is a SOUNDNESS BUG, and any loss carrying this
//                direction with a nonzero count raises ProverResult::
//                soundnessAlarm and must be shown to the user in those words.
//   EXACT        the projection loses nothing that this question can observe.
//
// The asymmetry is not a preference. A tool whose failure mode is "you have work
// to do that you do not have" costs an afternoon; a tool whose failure mode is
// "you are finished" costs the shipped game.
enum class LossDirection : std::uint8_t { Exact, Permissive, Conservative };

// Why a terminating character has no ranking certificate.
//
// ADR-001 section 3, gate 3 is emphatic that this must not be one bit: the two
// terminating Phase-0 characters BOTH have `hasRanking == false` and they have
// it for different reasons, and "no certificate" is bad news in one case and the
// best possible news in the other. NothingLoops is the strongest termination
// result the tool can produce -- not one cancel in the character can connect --
// and rendering it identically to CharacterGainsAResource throws that away.
enum class RankingAbsence : std::uint8_t {
    Present,                 // rankingOrder is populated
    NoResources,             // the character declares no resources at all
    CharacterGainsAResource, // `spendOnly` cleared: some reachable edge ADDS.
                             // ADR-001 section 3: THIS IS THE COMMON CASE. Any
                             // character that builds meter on hit lands here, so
                             // do not build the panel around the certificate.
    NothingLoops,            // no usable cancel between two reachable moves.
                             // The best news available: nothing can loop at all.
    NoDescendingOrder        // spend-only, edges exist, but no resource strictly
                             // runs down across all of them
};

// One named thing the projection could not carry, scoped to THIS character.
//
// `count` is how many objects in this character the loss actually touches, so a
// panel can say "17 moves" rather than "in principle". A loss with count 0 is
// still listed when it is worth knowing it was checked and found inert -- that
// is exactly what ADR-001 records about the projectile contact frame, and
// ProverAdapter.cpp re-derives that inertness rather than quoting it.
struct ProjectionLoss {
    std::string   field;
    LossDirection direction = LossDirection::Exact;
    std::int32_t  count     = 0;
    std::string   note;
};

// A cancel the designer authored that can never connect: by the time the
// follow-up becomes dangerous, the defender is already free.
struct ProverDeadCancel {
    CancelIndex  cancel    = 0;
    MoveIndex    from      = kInvalidMove;
    MoveIndex    to        = kInvalidMove;
    std::int32_t advantage = 0;   // frames of stun left after the cancel's delay
    std::int32_t startup   = 0;   // frames the follow-up needs
    std::int32_t shortfall() const { return startup - advantage; }
};

struct ProverResult {
    ProverStatus status = ProverStatus::Unknown;
    std::string  reason;

    // What was answered, and what the file thinks it describes. See note 2 at
    // the top of this file: these two disagree on all three shipped characters.
    ProverStage stage = ProverStage::Corner;
    std::string fileStage;

    // --- Status::Infinite ---------------------------------------------------
    //
    // Move indices, ready to be turned into ids and made clickable. The full
    // sequence a designer should read is `prefix` then `loop`, and `loop` ends
    // on the move it returns to.
    //
    // CAVEAT, and it is the prover's, not this adapter's: when the loop is not
    // entered on the very first move, comboprover.hpp:511-517 omits the opening
    // move from both lists. `loopEntryKnown` is false in exactly that case, and
    // it is also the case in which the ceiling replay below cannot run.
    std::vector<MoveIndex> prefix;
    std::vector<MoveIndex> loop;
    bool loopEntryKnown = false;

    // The ceiling clamp, which is the adapter's own correctness decision and the
    // first one Phase 1 had to make (ADR-001 section 6.1). comboprover.hpp
    // carries no resource ceilings at all, so it searches a state space in which
    // meter grows past three bars forever. That over-approximates the attacker,
    // which is the PERMISSIVE direction and therefore safe -- see the simulation
    // lemma written out in ProverAdapter.cpp -- but it means an INFINITE verdict
    // may rest on meter the game would never have handed out.
    //
    // So the adapter replays the reported loop through its own clamped loop. If
    // the cycle still returns to a position no worse than it started, having had
    // its resources capped at every step, the infinite is real under the real
    // ceilings and this is true. If it is false, the verdict stands (we cannot
    // conclude TERMINATING from a failed replay) but the panel should say the
    // loop was not reproducible under the character's declared ceilings.
    bool loopHoldsUnderCeilings = false;
    bool ceilingReplayRan       = false;

    // --- Status::Terminating ------------------------------------------------
    std::vector<ResourceIndex> rankingOrder;
    bool           hasRanking     = false;
    RankingAbsence rankingAbsence = RankingAbsence::Present;

    std::int32_t maxHits  = 0;
    std::int32_t maxFrames = 0;
    // HUNDREDTHS, matching Move::damageHundredths, so nothing in this struct is
    // a float. The prover sums damage in float and this is the rounded result;
    // ARCHITECTURE.md section 5.4 is the reason it must never enter a checksum.
    std::int32_t maxDamageHundredths = 0;

    // --- Reported whatever the verdict, and the part designers use daily -----
    //
    // ADR-001 section 5.3: "These are the features a designer will actually use
    // daily, and they land before anyone cares about the theorem."
    std::vector<ProverDeadCancel> deadCancels;
    // The same list computed with decay switched off. ADR-001 section 5, D8:
    // both implementations evaluate every edge at the SETTLED hitstun, so a
    // decay rule reports real cancels as dead -- 128 of Kung Fu Girl's 134 under
    // the project's own draft house rule. A designer looking at a dead-cancel
    // list needs to know which entries are the decay curve talking.
    std::vector<ProverDeadCancel> deadCancelsPreDecay;
    std::vector<MoveIndex>        unreachableMoves;

    std::int32_t settlingIndex = 0;
    std::int32_t usableCancels = 0;
    std::int32_t explored      = 0;
    bool         capped        = false;
    bool         spendOnly     = true;

    // --- The projection's own honesty ---------------------------------------
    std::vector<ProjectionLoss> losses;
    // True if any loss with direction CONSERVATIVE has a nonzero count -- that
    // is, if this character contains data whose projection could HIDE a real
    // infinite. Show it in those words. On all three Phase-0 characters this is
    // false, and ProverAdapter.cpp says exactly why for each check.
    bool soundnessAlarm = false;
};

struct ProverOptions {
    // Assertion A03 again, at a second seam. The loader already checks it
    // (CharacterData.cpp), but a CharacterData assembled in memory never went
    // through the loader, and the failure this protects against is silent:
    // comboprover keys its ResourceVec POSITIONALLY, so a character whose
    // index 0 is `juggle` compares juggle against meter and says nothing.
    // Empty skips the check and records a warning, never a silent pass.
    std::vector<std::string> expectedResources;

    // Positions the search may visit before giving up and answering Unknown.
    // The default is comboprover.hpp's own. ADR-001 measured 63 / 79 / 10
    // configurations on the three real characters, so this is three orders of
    // magnitude of headroom and Unknown has never been produced.
    std::int32_t limit = 200000;

    // How many times the ceiling replay may go round the reported loop before
    // giving up. Bounded because a failed replay must cost a fixed amount of
    // time in an editor that re-runs this on every keystroke.
    std::int32_t ceilingReplayRounds = 64;

    // How many distinct clamped resource vectors the replay may track at once.
    // Several authored cancels can join the same pair of moves with different
    // costs, and the replay follows all of them; this bounds that fan-out.
    std::int32_t ceilingReplayFrontier = 64;
};

// Failures and non-fatal notes, as DATA, exactly like LoadReport. A character
// that cannot be projected is a normal outcome and must not throw through an
// editor's paint path.
struct ProverReport {
    std::string              error;     // empty iff the analysis ran
    std::string              rule;      // "A03" when a load assertion failed here
    std::vector<std::string> warnings;
};

// Project `character` into the decision procedure and run it.
//
// Returns false only when the character cannot be projected at all (an empty
// move list, a resource index that does not exist, a resource order that
// contradicts the build). A projected character that the search cannot decide
// returns TRUE with status == Unknown, because "I ran and could not tell you" is
// an answer and "I could not run" is not.
bool AnalyseCharacter(const CharacterData&  character,
                      const ProverOptions&  options,
                      ProverResult&         out,
                      ProverReport&         report);

// Human-readable names, for the panel and for test failure messages.
const char* ProverStatusName(ProverStatus status);
const char* RankingAbsenceName(RankingAbsence absence);
const char* LossDirectionName(LossDirection direction);

// The whole verdict as text, in the shape of comboprover::describe but spelled
// with this character's own move ids. Used by the editor's copy-to-clipboard and
// by tests, which is why it is here rather than in the panel.
std::string DescribeVerdict(const CharacterData& character, const ProverResult& result);

} // namespace cse::data
