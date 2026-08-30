// The live judge: what the player just did, against what the analysis said.
//
// This is the object that makes the running game validate the tool rather than
// merely demonstrate it. A playtester performs a string; the watcher counts it,
// names its moves, and says whether the combo prover had anything to say about
// that exact sequence -- that it is a cycle the analysis found, that it closes a
// loop the analysis called INFINITE, or that it went through an edge the
// analysis called DEAD and therefore should not have connected at all.
//
// It reads GameState, MatchData and a ProverResult. It does NOT read
// CharacterData: everything it needs about the character is either in the built
// MatchData (frame data, durations, cancel windows) or in the analysis
// (verdict, witness, dead cancels), and a third source for the same facts is
// exactly the "two sources of truth for the frame data" Combat.h refuses.
//
// It is an ITickObserver. It never writes to the session, never allocates in
// steady state after the first combo, and never reads a clock.
//
// ===========================================================================
// THE THREE SIGNALS, AND THE TRAPS IN EACH. READ THIS SECTION BEFORE WRITING
// ANY OF THE IMPLEMENTATION. EVERY ONE OF THESE HAS ALREADY COST THIS PROJECT
// SOMETHING.
// ===========================================================================
//
// ---------------------------------------------------------------------------
// 1. A MOVE STARTED  ==  `moveId != 0 && moveFrame == 0`
// ---------------------------------------------------------------------------
// NOT a transition of moveId. NOT `moveId != previousMoveId`.
//
// A move that cancels into ITSELF never changes the id. It goes from
// (id=7, frame=9) to (id=7, frame=0) and a transition detector sees nothing
// happen at all. SELF-CANCELS ARE EXACTLY WHAT AN INFINITE COMBO IS MADE OF --
// fighter_a_infinite.json's entire deliberate bug is `stand_lp -> stand_lp,
// delay 2, on hit`, and the printed loop the ground-truth test executes is that
// one move, forever. A watcher built on id transitions would report one hit and
// then sit silently through the twenty-six that follow, which is precisely the
// combo it exists to catch.
//
// The frame counter is what resets, so it is what says a move began. Both halves
// are needed: `moveFrame == 0` alone is also true of an idle fighter, who has
// moveId 0 and moveFrame 0 forever.
//
// tests/test_ground_truth.cpp's Driver and tests/test_gap_extent.cpp both had to
// make this distinction, and both wrote it down. It is written down a third time
// here because the trap does not get less inviting.
//
// ---------------------------------------------------------------------------
// 2. THE COMBO ENDED  ==  THE DEFENDER LEFT HITSTUN, READ DIRECTLY
// ---------------------------------------------------------------------------
// Off Fighter::hitstun, and off nothing else. Not derived from the attacker's
// frame arithmetic, not from a move's authored hitstun, not from a count of
// ticks since the last hit.
//
// THE PRECISE RULE, and it is not `hitstun == 0` on the tick you are looking at:
//
//     the defender was ACTIONABLE at tick t  IFF  their hitstun as observed at
//     the END of tick t-1 was <= 1 (and their blockstun likewise)
//
// because StepPhysics decrements hitstun at the TOP of the tick before anything
// else looks at it, and Simulate.cpp's `actionable()` is
// `hitstun == 0 && blockstun == 0`. A defender sitting at hitstun 1 at the end of
// tick t-1 is free to act on tick t.
//
// WHY THE INDIRECT READINGS ARE WRONG, MEASURED. test_gap_extent.cpp compared
// this against the two detectors test_ground_truth.cpp uses:
//
//     `defStun == 0 && !hit` post-Simulate      misses it
//     `defMove != 0 && defFrame == 0`           misses it
//
// A defender whose stun expires on exactly the tick the next hit arrives is
// invisible to both: they ARE actionable at the top of the tick, StepAttack
// really does start their move, and then ResolveHits at the bottom of the SAME
// tick sets their moveId back to 0 and re-applies stun. The sample taken
// afterwards shows a defender who never moved, in hitstun, being hit. On
// fighter_a that hid four escapes out of 41 cycles and turned 33 into 37. THE
// DIRECT ONE WAS RIGHT.
//
// blockstun is in the rule even though nothing in the kernel writes it today
// (Fighter::blockstun exists and no line assigns it). Writing the rule the way
// `actionable()` is written means the day blocking lands, this file is already
// correct instead of quietly one term short.
//
// TWO DIFFERENT QUESTIONS COME OUT OF THIS ONE SIGNAL, and conflating them is
// how a training HUD lies:
//
//   the string ENDED       the defender was actionable at tick t AND no hit
//                          landed on tick t. They got out.
//   the string has a GAP   the defender was actionable at tick t and a hit
//                          landed anyway. The string continues -- and it is NOT
//                          A TRUE COMBO, because a human defender could have
//                          acted there. This is the case both indirect
//                          detectors miss, and it is the one a playtester most
//                          needs told.
//
// ---------------------------------------------------------------------------
// 3. A HIT CONNECTED  ==  THE ATTACKER'S alreadyHitBits GAINED THE DEFENDER'S BIT
// ---------------------------------------------------------------------------
// ResolveHits sets bit `d` on the ATTACKER when its box overlapped fighter d's
// body (Combat.cpp: `state.p[a].alreadyHitBits |= bitForSlot(d)`), and StepAttack
// clears the whole field whenever a move starts, cancels or ends. So the bit
// going from clear to set is exactly one connecting hit, once per active window,
// with the multi-hit guard already applied.
//
// THE ONE HOLE, AND THE DISJUNCT THAT CLOSES IT. A naive `was clear, now set`
// misses this sequence: move A has already hit (bit set), A ends this tick,
// B starts in the SAME StepAttack call (the button was held -- Combat.cpp does
// exactly this), B has startup 0 so its box is live on frame 0, and ResolveHits
// sets the bit again at the bottom of the same tick. Bit set before, bit set
// after, hit missed. So:
//
//     hit landed on the defender this tick  IFF
//         (attacker.alreadyHitBits & defenderBit) != 0
//         AND ( (previous attacker.alreadyHitBits & defenderBit) == 0
//               OR the attacker started a move this tick )
//
// The disjunct is sound because clearing happens in StepAttack, which runs
// BEFORE ResolveHits: if the attacker's move began this tick, the only thing
// that can have set that bit is this tick's ResolveHits.
//
// Damage is read as the DEFENDER'S HEALTH DELTA rather than as MoveDef::damage,
// because health clamps at zero (`health > dmg ? health - dmg : 0`) and the
// authored number would keep accruing damage against a fighter who is already
// at nothing.
//
// ---------------------------------------------------------------------------
// 4. Fighter::comboHits IS LIVE NOW. STILL DO NOT READ IT.
// ---------------------------------------------------------------------------
// It used to say "drives hitstun decay" while nothing in the kernel wrote it, so
// a watcher that read it reported zero hits forever and looked like it was
// working. ADR-005 P2 ended that: ResolveHits increments it and the decay rule it
// was named for now reads it.
//
// THE INSTRUCTION IS UNCHANGED AND THE REASON IS STRONGER THAN IT WAS. Two
// independent counts of the same thing are worth having precisely because they
// can be compared -- test_game_core asserts they agree, which is a real check
// only while they are derived separately. A watcher that read the field would
// turn that assertion into a tautology, and the tautology would still pass on the
// day the kernel's counter was wrong.
//
// They are also not the same quantity. Fighter::comboHits is a uint8 that
// SATURATES at 255 and resets the moment the defender leaves hitstun; this file's
// count is an int32 belonging to a STRING, which outlives the combo and moves to
// `previous` when it ends. They coincide on a short combo and are not the same
// number in general.
//
// ---------------------------------------------------------------------------
// 5. A CANCEL AND A LINK ARE DIFFERENT THINGS AND THE ANALYSIS ONLY KNOWS ONE
// ---------------------------------------------------------------------------
// When a move begins at tick t and the attacker was mid-move at tick t-1, two
// very different things can have happened, and only one of them is an edge in
// the graph the prover reasons about:
//
//   observed at end of tick t-1: (A, f), and MoveDuration(A) = D
//
//     f + 1 >= D    A ran out. StepAttack ended it and the button scan started
//                   the next move in the same call. This is a LINK, and the
//                   character's cancel table says nothing about it. The prover's
//                   graph does not contain it.
//     f + 1 <  D    A was interrupted mid-move. This is a CANCEL, and `f + 1` is
//                   the source frame the kernel matched CancelEdge's inclusive
//                   [earliestFrame, latestFrame] window against -- FindCancel
//                   runs AFTER the `++f.moveFrame` at the top of StepAttack.
//
// Being HIT also interrupts a move, but ResolveHits zeroes the victim's moveId,
// so an interrupted-by-a-hit attacker is observed at (0, 0) and cannot be
// confused with either case.
//
// THE DISTINCTION IS NOT PEDANTIC. tests/test_gap_extent.cpp's headline finding
// is that 32 of fighter_a's 41 cycles are performed by the LINK route -- the
// cancel edge resolves to an empty window and the held button starts the
// follow-up one tick later anyway. A watcher that reported those as cancels
// would be reporting that the player took edges the kernel demonstrably cannot
// take.
//
// ---------------------------------------------------------------------------
// 6. THIS OBJECT IS NOT SIMULATION STATE, SO A ROLLBACK INVALIDATES IT
// ---------------------------------------------------------------------------
// The watcher carries one tick of history plus the combo in progress. A
// GameState restore rolls back the fighters and does not roll back this, so on
// the first TickView with `resimulated` set AFTER IT HAS JUDGED A TICK the
// watcher marks itself STALE and stops reporting until Reset().
//
// That is the deliberate choice rather than the lazy one. The alternative --
// a per-tick ring of the watcher's own history, restored alongside the state --
// is real state that would need its own rollback correctness argument, in an
// object whose entire job is to tell a human something true. A verdict shown to
// a playtester that is silently about a timeline that no longer happened is
// worse than no verdict. Training mode is offline and never rolls back; replay
// playback runs forward from tick 0 and never rolls back; so nothing this module
// ships today is affected, and the day a netplay HUD wants one, `Stale()` is
// where the conversation starts.
//
// "AFTER IT HAS JUDGED A TICK" IS LOAD-BEARING, because a rollback window is many
// ticks long and Reset() is documented as the thing to call inside one. A
// re-simulated tick is never judged either way; it raises the flag only when there
// is history for it to invalidate, so a watcher that has just been Reset() sits
// out the rest of the window in silence and resumes by itself on the first tick
// the session has not run before. Without the qualifier a host that did exactly
// what Reset() says would have the flag back up one tick later, and could only
// clear it for good by tracking FightSession::HighWaterTick() itself and picking
// the one tick on which a second Reset() would stick.
#pragma once

#include "cse/game/FightSession.h"

#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cse::game {

// How many connecting moves of one string are remembered for display and for the
// prefix comparison. An INFINITE combo does not stop -- health clamps at zero and
// the hits keep landing -- so this cannot be unbounded, and the case that would
// overflow it is exactly the case the tool is for.
//
// The FIRST kMaxComboSequence are kept and later ones are dropped, not the other
// way round, because the prefix is what a witness comparison starts from.
inline constexpr std::size_t kMaxComboSequence = 256;

// The tail kept for cycle matching, separately from the above so that a long
// combo can still be recognised as looping after its display sequence filled up.
// A simple cycle in the cancel graph visits distinct moves, so it cannot be
// longer than the character's move table.
inline constexpr std::size_t kMaxCycleLength = cse::kernel::kMaxMovesPerFighter;

// --- One transition the player performed ------------------------------------

// A move beginning, with what it began FROM and by which route. Recorded for
// every move start inside a string, so a HUD can draw the chain and a test can
// assert which edges were taken.
struct PerformedEdge {
    std::uint32_t tick = 0;

    // Kernel move ids (direct indices into FighterData::moves).
    // MoveIndexMap::IdOf turns one into a name; MoveIndexMap::CharacterMoveOf
    // turns one into the cse::data::MoveIndex the analysis speaks.
    std::uint16_t from = 0;      // 0 when the move started from idle
    std::uint16_t to   = 0;

    // Fighter::moveFrame of the SOURCE at the moment the transition was taken,
    // i.e. the number CancelEdge's inclusive window was compared against. Only
    // meaningful when `cancel` is true.
    std::int32_t sourceFrame = 0;

    // See signal 5. `cancel` false means the source ran out and the follow-up
    // started from the button scan -- a LINK, which is not an edge in the
    // analysis's graph at all.
    bool cancel = false;

    // --- What the analysis said about this edge, when there is an analysis ---

    // The edge appears in ProverResult::deadCancels: by the time the follow-up
    // becomes dangerous, the defender is already free. The player performed it
    // anyway, which is legal -- the kernel takes edges the analysis calls dead.
    bool dead = false;

    // The edge is dead ONLY in the with-decay list and not in
    // deadCancelsPreDecay. ADR-001 section 5 D8: 128 of Kung Fu Girl's 134 dead
    // cancels are the decay curve talking, so a HUD that shows all of them
    // identically is showing mostly noise. Meaningless when `dead` is false.
    bool deadOnlyUnderDecay = false;

    // THE LOUD ONE. The edge was dead AND the follow-up then CONNECTED while the
    // string was still open. The analysis said the defender would be free before
    // this could land, and they were not. That is a measured disagreement
    // between the model and the running game -- ARCHITECTURE.md D8 -- and it is
    // a finding, not a glitch. Set retroactively, on the tick the follow-up
    // lands, so it is false on the tick the edge is taken.
    bool deadEdgeConnected = false;

    // Index into ProverResult::deadCancels when `dead`; otherwise unused. Carried
    // so a panel can quote the analysis's own advantage/startup/shortfall numbers
    // rather than recompute them.
    std::size_t deadCancelIndex = 0;
};

// --- One string, judged ------------------------------------------------------

struct ComboReport {
    // --- What happened ------------------------------------------------------

    bool          open      = false;  // the defender has not got out yet
    std::uint32_t startTick = 0;      // the tick the first hit landed
    std::uint32_t lastHitTick = 0;
    std::uint32_t endTick   = 0;      // the tick the defender became actionable;
                                      // meaningful only when !open && hits > 0

    std::int32_t hits   = 0;
    std::int32_t damage = 0;          // the defender's health delta, summed

    // The connecting moves, in order, as KERNEL move ids. Capped at
    // kMaxComboSequence; `sequenceTruncated` says the cap bit.
    std::vector<std::uint16_t> sequence;
    bool                       sequenceTruncated = false;

    // Every move start inside the string, cancels and links alike. Capped at
    // kMaxComboSequence for the same reason.
    //
    // INSIDE THE STRING IS THE WHOLE OF THE RULE, and the consequence is worth
    // stating because it looks like an omission: a string opens on the tick its
    // first hit LANDS, and an opener with any startup at all began before that, so
    // an ordinary opener appears in `sequence` and not here. It appears here only
    // when it started on the very tick it connected -- which is also the only case
    // in which one tick of history can name its source truthfully.
    std::vector<PerformedEdge> edges;

    // Moves that STARTED and never connected, counted rather than listed. A
    // string with whiffs in it is still a string, but it is not the clean chain
    // the analysis describes.
    std::int32_t whiffedStarts = 0;

    // --- Whether it is really a combo ---------------------------------------

    // Ticks inside the string at which the defender was ACTIONABLE and a hit
    // landed anyway (signal 2). Zero is a true combo.
    std::int32_t gapTicks = 0;
    bool TrueCombo() const { return hits > 0 && gapTicks == 0; }

    // --- What the analysis says about it ------------------------------------
    //
    // All of these stay at their defaults when no ProverResult was supplied. A
    // watcher with no analysis is still a combo counter, and that is a useful
    // thing on its own.

    // Consecutive connecting moves at the tail that follow ProverResult::loop in
    // cyclic order, and how many complete turns of it that is. The player may
    // enter a cycle at any rotation, so this is a rotation-aware match rather
    // than a prefix comparison against loop[0].
    std::int32_t cycleRun            = 0;
    std::int32_t loopTurnsCompleted  = 0;

    // The string so far is a prefix of ProverResult::prefix followed by
    // ProverResult::loop -- the player is on the witness. What a "Demonstrate,
    // now you try" HUD draws a progress bar from.
    bool         onWitness    = false;
    std::size_t  witnessIndex = 0;

    // THE LOUD MARKERS, and they are the point of the file.
    //
    // completedProverLoop: loopTurnsCompleted >= 1 while the analysis's status is
    // Infinite. The player performed, in the running game, the loop the decision
    // procedure printed out of the character file. This is
    // ARCHITECTURE.md 5.5 item 4 happening in front of a playtester rather than
    // in a test.
    bool completedProverLoop = false;

    // performedDeadCancel: at least one edge in `edges` has `dead`.
    // deadEdgeConnected: at least one of those then LANDED inside the string.
    // The second is the one worth shouting about; the first alone only means the
    // player pressed something the analysis did not expect to work.
    bool performedDeadCancel = false;
    bool deadEdgeConnected   = false;

    // The analysis was consulted and had nothing to say about this loop, because
    // ProverResult::loopEntryKnown is false -- comboprover omits the opening move
    // from both lists when the loop is not entered on the very first move
    // (ProverAdapter.h states the caveat). The witness comparison is then
    // unreliable and this flag says so, rather than reporting a mismatch that is
    // the adapter's caveat wearing a verdict's clothes.
    bool witnessIncomplete = false;
};

// --- The watcher -------------------------------------------------------------

class ComboWatcher final : public ITickObserver {
public:
    // `attackerSlot` is the fighter whose combos are being judged; the other slot
    // is the defender. A host that wants both directions creates two watchers.
    // Judging both in one object would need two of every field and would make
    // "the combo" ambiguous in exactly the situation where two players trade.
    //
    // `moves` and `analysis` are BORROWED and may be null; both must outlive the
    // watcher. `moves` is the ATTACKER's MoveIndexMap (from
    // MatchBuild::moves[attackerSlot]) and is used only for names -- a null one
    // costs the HUD its labels and costs the judgement nothing. `analysis` is the
    // ProverResult for the attacker's character; a null one leaves every
    // judgement field at its default and the watcher is a plain combo counter.
    ComboWatcher(int                          attackerSlot,
                 const cse::data::MoveIndexMap* moves,
                 const cse::data::ProverResult* analysis);

    void OnTick(const TickView& view) override;

    // Forget the combo in progress and the one tick of history, and clear
    // Stale(). Call it on Begin() and after a rollback -- INCLUDING INSIDE ONE:
    // the re-simulated ticks that follow are skipped rather than judged, and they
    // do not raise Stale() again, so judging resumes by itself on the first tick
    // the session has not run before. Does not clear the borrowed pointers.
    void Reset();

    // The string in progress, or the last one that finished. `open` distinguishes
    // them. A host draws from this every frame; it is not recomputed per read.
    const ComboReport& Current() const;

    // The string that most recently ENDED, kept separately so a HUD can go on
    // showing "37 hits, 480 damage, TRUE COMBO" after the defender got out
    // while `Current()` has already reset for the next one.
    //
    // FILLED ON THE TICK THE STRING ENDS, not on the next string's opening hit, so
    // there is no window in which "most recently ended" names something else. In
    // the gap between one string ending and the next beginning both accessors
    // therefore describe that same ended string; they part company on the opening
    // hit, which is the tick Current() starts over and this one does not.
    const ComboReport& Previous() const;

    // How many strings have ended since Reset(). Zero with Current().open false
    // means nothing has happened yet, which a HUD needs to tell apart from a
    // combo that ended with no hits.
    std::int32_t CompletedCombos() const;

    // See section 6 of the header comment: a re-simulated tick invalidates this
    // object's history, so it stops reporting rather than reporting about a
    // timeline that no longer happened. Reset() clears it, and a re-simulated tick
    // arriving with no history to invalidate -- the ticks after a Reset(), inside
    // the rollback window -- is skipped without raising it again.
    bool Stale() const;

    // The current string in words, for a log, a test failure message and the
    // editor's copy-to-clipboard. Same role as
    // cse::data::DescribeVerdict, and here for the same reason: the sentence
    // must be written once, not once per panel. Allocates; call it on demand,
    // never per tick.
    std::string Describe() const;

private:
    int                            attackerSlot_ = 0;
    const cse::data::MoveIndexMap* moves_        = nullptr;
    const cse::data::ProverResult* analysis_     = nullptr;

    ComboReport  current_{};
    ComboReport  previous_{};
    std::int32_t completed_ = 0;
    bool         stale_     = false;

    // Exactly one tick of history, which is all three signals need. Held as
    // scalars rather than as a GameState copy so that what this object depends
    // on is visible: two fields of the attacker, two of the defender, and the
    // defender's health.
    bool          havePrevious_    = false;
    std::uint16_t prevAtkMove_     = 0;
    std::uint16_t prevAtkFrame_    = 0;
    std::uint8_t  prevAtkHitBits_  = 0;
    std::uint16_t prevDefHitstun_  = 0;
    std::uint16_t prevDefBlockstun_= 0;
    std::int32_t  prevDefHealth_   = 0;

    // The rotation-aware cycle matcher's state (see ComboReport::cycleRun). The
    // ring holds the last kMaxCycleLength connecting moves so that a string whose
    // display sequence has already truncated can still be recognised as looping.
    std::uint16_t recent_[kMaxCycleLength] = {};
    std::size_t   recentCount_             = 0;
    std::size_t   cyclePos_                = 0;
};

// --- The judgement rules, stated so they are implemented once ---------------
//
// THE CYCLE MATCHER. Maintained over the sequence of CONNECTING moves only --
// whiffs and links that never landed are not part of the chain the prover
// reasons about. On each connecting move `m`, with `loop` = ProverResult::loop
// converted to kernel move ids (MoveIndexMap::KernelMoveIdOf) and `n` its size:
//
//     if cycleRun > 0 and loop[cyclePos] == m:
//         cycleRun += 1;  cyclePos = (cyclePos + 1) % n
//     else if some i has loop[i] == m:
//         cycleRun  = 1;  cyclePos = (i + 1) % n        // enter at any rotation
//     else:
//         cycleRun  = 0
//     loopTurnsCompleted = cycleRun / n
//
// Entering at any rotation is required, not a nicety: a player who starts the
// loop from its second move is performing the same cycle, and a matcher anchored
// at loop[0] would report nothing while an infinite ran in front of it. Where
// several i satisfy loop[i] == m, take the FIRST -- the same first-wins tie-break
// StepAttack and FindCancel use, chosen here for the same reason, so that two
// machines watching the same fight agree.
//
// THE WITNESS CURSOR (`onWitness`, `witnessIndex`) walks
// prefix ++ loop the same way, without wrapping, and drops to false the first
// time a connecting move is not the one the witness names. It never comes back
// true for the same string: "you were on the combo and then you were not" is the
// honest reading, and re-acquiring mid-string would let a HUD claim the player
// is on a witness they abandoned twelve hits ago.
//
// A DEAD EDGE IS MATCHED BY ITS ENDPOINTS, NOT BY ITS INDEX. PerformedEdge's
// from/to are converted with MoveIndexMap::CharacterMoveOf and compared against
// ProverDeadCancel::from/to. Matching on CancelIndex would be wrong: the kernel
// does not record WHICH CancelEdge FindCancel returned, several authored cancels
// can join the same pair of moves, and MoveIndexMap::fileCancelByEdge only maps
// the edges that survived the projection. Endpoints are what both sides
// unambiguously agree on.
//
// `deadOnlyUnderDecay` is `dead && the same (from,to) does NOT appear in
// ProverResult::deadCancelsPreDecay`.

} // namespace cse::game
