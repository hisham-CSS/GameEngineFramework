#include "cse/game/ComboWatcher.h"

// The live judge, executed.
//
// ComboWatcher.h states the signals in its six numbered sections, the trap inside
// each of them, and the exact matching rules for cycles, witnesses and dead
// edges. This file is where each of those happens ONCE. Nothing below re-argues a
// decision the header already made; where a comment appears here it is because
// the header's rule met a case the header does not name, and every one of those
// is marked THIS FILE DECIDED so a reader can tell it apart from the header
// speaking.
//
// ---------------------------------------------------------------------------
// THE THREE PROPERTIES THIS FILE HAS TO KEEP, ALL FOR ONE REASON
// ---------------------------------------------------------------------------
// This runs sixty times a second for as long as a playtester leaves the game
// open, and what it produces is shown to a human as a statement of fact.
//
//   NO CLOCK, NO rand(), NO FLOAT. Nothing here decides a simulation outcome, so
//   the letter of the tick-path rule does not bind -- but a verdict that depended
//   on when it was computed would make a REPLAY's judgement differ from the one
//   the player saw live, and the whole value of the replay is that the two are
//   the same event. Every number below comes out of GameState, MatchData or
//   ProverResult and out of nothing else.
//
//   NO ALLOCATION IN STEADY STATE. clearReport empties the live report field by
//   field rather than assigning a fresh one, so `current_` keeps the capacity the
//   first string grew and every later string reuses it. The one copy in the file
//   -- `previous_ = current_` on the tick a string ends -- reuses the
//   destination's capacity the same way, so it allocates while `previous_` is
//   still growing to the size a string reaches and not once it has. That is what
//   lets `edges` and `sequence` be std::vector at all rather than two more fixed
//   arrays.
//
//   NO THROW. FightSession.h's observer contract; the tick path has no handler.
//
// ---------------------------------------------------------------------------
// WHY THE TICK IS ONE FUNCTION AND THE HELPERS ARE FREE
// ---------------------------------------------------------------------------
// The class's member set is fixed by the header, which declares no private
// helper methods -- so anything factored out of OnTick has to be a free function
// taking what it touches as arguments. That turned out to be the better shape and
// not a compromise: `advanceCycle` and `advanceWitness` are pure functions of
// (analysis, one connecting move, the report, one cursor), and their signatures
// now say exactly that. What is left in OnTick is one linear pipeline over one
// tick, in the order the tick happened, which is the order a reader has to hold
// in their head anyway.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cse::game {

// At namespace scope rather than inside the unnamed namespace: these names are
// used by the public definitions further down as well as by the helpers, and a
// using-declaration tucked into an unnamed namespace that happens to be visible
// anyway is the kind of thing that works until somebody moves a function.
using cse::data::MoveIndex;
using cse::data::MoveIndexMap;
using cse::data::ProverDeadCancel;
using cse::data::ProverResult;
using cse::data::ProverStatus;
using cse::kernel::Fighter;
using cse::kernel::MoveDef;

namespace {

// --- Counting something that does not stop ----------------------------------

// SATURATING, and that is not defensive noise. The case this module exists for is
// the string that never ends: fighter_a_infinite's printed loop lands a hit every
// six ticks for as long as anybody holds the button, and health clamps at zero
// rather than ending the match. A signed counter that wrapped would show a
// playtester a negative hit count at the exact moment the tool was right about
// something, and signed overflow is undefined besides. Pinning at the maximum is
// visibly wrong instead of subtly wrong.
constexpr std::int32_t kInt32Max = 2147483647;

void addSaturating(std::int32_t& counter, std::int32_t amount) {
    if (amount <= 0) return;
    counter = (counter > kInt32Max - amount) ? kInt32Max : counter + amount;
}

// --- Emptying a report without giving its buffers back -----------------------

// Field by field rather than `report = ComboReport{}`, and the difference is the
// whole of "never allocates in steady state after the first combo": assigning a
// fresh value frees the two vectors and the next string allocates again, while
// clear() keeps the capacity. Every scalar is named explicitly so that a field
// added to ComboReport shows up here as a field nobody reset, rather than being
// silently handled by an assignment that also undid the reuse.
void clearReport(ComboReport& report) {
    report.sequence.clear();
    report.edges.clear();

    report.open              = false;
    report.startTick         = 0;
    report.lastHitTick       = 0;
    report.endTick           = 0;
    report.hits              = 0;
    report.damage            = 0;
    report.sequenceTruncated = false;
    report.whiffedStarts     = 0;
    report.gapTicks          = 0;

    report.cycleRun           = 0;
    report.loopTurnsCompleted = 0;
    report.onWitness          = false;
    report.witnessIndex       = 0;

    report.completedProverLoop = false;
    report.performedDeadCancel = false;
    report.deadEdgeConnected   = false;
    report.witnessIncomplete   = false;
}

// --- Reading the witness ------------------------------------------------------
//
// The witness is `prefix` then `loop`, concatenated -- the sequence
// DescribeVerdict prints and the one tests/test_ground_truth.cpp's Driver walks.
// It is read on demand rather than flattened into a member, for two reasons: the
// member set is fixed by the header, and flattening would put a second copy of
// the analysis's own list inside an object whose first paragraph says it keeps no
// third source for anything.

std::size_t witnessLength(const ProverResult& analysis) {
    return analysis.prefix.size() + analysis.loop.size();
}

// The KERNEL move id the witness names at `i`, or 0 (idle, which no connecting
// move can ever be) when `i` is past the end. KernelMoveIdOf is the mapping as a
// function rather than as the "+1" everybody remembers; writing the arithmetic
// out at each use is how the two sides of a map drift apart.
std::uint16_t witnessKernelIdAt(const ProverResult& analysis, std::size_t i) {
    if (i < analysis.prefix.size())
        return MoveIndexMap::KernelMoveIdOf(analysis.prefix[i]);
    const std::size_t j = i - analysis.prefix.size();
    if (j < analysis.loop.size())
        return MoveIndexMap::KernelMoveIdOf(analysis.loop[j]);
    return 0;
}

std::uint16_t loopKernelIdAt(const ProverResult& analysis, std::size_t i) {
    if (i >= analysis.loop.size()) return 0;
    return MoveIndexMap::KernelMoveIdOf(analysis.loop[i]);
}

// --- The rotation-aware cycle matcher ----------------------------------------

// ComboWatcher.h's loop, verbatim, maintained over CONNECTING moves only: whiffs
// and links that never landed are not part of the chain the prover reasons about.
void advanceCycle(const ProverResult& analysis, std::uint16_t connecting,
                  ComboReport& report, std::size_t& cyclePos) {
    const std::size_t n = analysis.loop.size();
    if (n == 0) return;

    if (report.cycleRun > 0 && loopKernelIdAt(analysis, cyclePos) == connecting) {
        addSaturating(report.cycleRun, 1);
        cyclePos = (cyclePos + 1) % n;
    } else {
        // ENTER AT ANY ROTATION, which is required rather than a nicety: a player
        // who starts the loop from its second move is performing the same cycle,
        // and a matcher anchored at loop[0] would report nothing while an infinite
        // ran in front of it. Where several i satisfy loop[i] == m the FIRST wins
        // -- StepAttack's and FindCancel's tie-break, chosen here for their reason,
        // so that two machines watching the same fight agree.
        std::size_t entry = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (loopKernelIdAt(analysis, i) == connecting) { entry = i; break; }
        }
        if (entry < n) {
            report.cycleRun = 1;
            cyclePos        = (entry + 1) % n;
        } else {
            report.cycleRun = 0;
        }
    }

    report.loopTurnsCompleted =
        report.cycleRun / static_cast<std::int32_t>(n);

    // THE LOUD MARKER: the player performed, in the running game, the loop the
    // decision procedure printed out of the character file. ARCHITECTURE.md 5.5
    // item 4 happening in front of a playtester rather than in a test.
    //
    // STICKY -- THIS FILE DECIDED THAT. `loopTurnsCompleted` stays exactly the
    // derived number the header specifies and drops back to zero the moment the
    // player leaves the cycle, but the BOOLEAN is a statement about the STRING, in
    // the same family as performedDeadCancel and deadEdgeConnected. A marker a HUD
    // has to catch on precisely the right frame is a marker nobody ever sees, and
    // having performed the loop is not something a later mistake undoes.
    if (report.loopTurnsCompleted >= 1 && analysis.status == ProverStatus::Infinite)
        report.completedProverLoop = true;
}

// --- The witness cursor -------------------------------------------------------

void advanceWitness(const ProverResult& analysis, std::uint16_t connecting,
                    ComboReport& report) {
    if (!report.onWitness) return;   // never comes back true for the same string

    // Walks prefix ++ loop WITHOUT wrapping, and drops to false the first time a
    // connecting move is not the one the witness names -- including when the
    // witness has run out and names nothing at all. "You were on the combo and
    // then you were not" is the honest reading; re-acquiring mid-string would let
    // a HUD claim the player is on a witness they abandoned twelve hits ago, and
    // wrapping would make this a second and worse copy of the cycle matcher above.
    if (report.witnessIndex < witnessLength(analysis) &&
        witnessKernelIdAt(analysis, report.witnessIndex) == connecting) {
        ++report.witnessIndex;
    } else {
        report.onWitness = false;
    }
}

// --- Dead cancels, matched by endpoints ---------------------------------------

// The header's rule, and the reason it is endpoints: the kernel does not record
// WHICH CancelEdge FindCancel returned, several authored cancels can join the same
// pair of moves, and MoveIndexMap::fileCancelByEdge only maps the edges that
// survived the projection. from/to are what both sides unambiguously agree on.
//
// Returns the index into `list`, or list.size() for no match. First match wins,
// the same tie-break as everywhere else in this module and for the same reason.
std::size_t findDeadCancel(const std::vector<ProverDeadCancel>& list,
                           MoveIndex from, MoveIndex to) {
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].from == from && list[i].to == to) return i;
    return list.size();
}

// --- Names, for Describe() and for nothing else -------------------------------

std::string moveName(const MoveIndexMap* moves, std::uint16_t kernelMoveId) {
    if (kernelMoveId == 0) return "idle";
    if (moves != nullptr) {
        const std::string_view id = moves->IdOf(kernelMoveId);
        if (!id.empty()) return std::string(id);
    }
    // A null map costs the HUD its labels and costs the judgement nothing -- the
    // header says so, and it is true because CharacterMoveOf is static and the
    // dead-cancel match above needs no map at all. Naming the slot keeps a log
    // readable rather than printing an empty string that reads as a missing move.
    return "slot" + std::to_string(static_cast<int>(kernelMoveId));
}

std::string num(std::int64_t v) { return std::to_string(v); }

// How much of a string Describe() spells out before it stops. A 26-hit loop is
// worth printing in full; the one this tool exists for is not, and a log line
// nobody scrolls to the end of is a log line nobody reads.
constexpr std::size_t kDescribeMoves = 24;
constexpr std::size_t kDescribeEdges = 12;

} // namespace

// --- Construction and reset ---------------------------------------------------

ComboWatcher::ComboWatcher(int attackerSlot, const MoveIndexMap* moves,
                           const ProverResult* analysis)
    // CLAMPED rather than asserted. Every read below indexes GameState::p with
    // this and with `1 - this`, and there are two slots, so a caller who passes 2
    // gets fighter 0 judged rather than a read off the end of the state. The
    // header says a host wanting both directions creates two watchers, so 0 and 1
    // are the only values with a meaning.
    : attackerSlot_(attackerSlot == 1 ? 1 : 0),
      moves_(moves),
      analysis_(analysis) {}

void ComboWatcher::Reset() {
    clearReport(current_);
    clearReport(previous_);
    completed_ = 0;
    stale_     = false;

    havePrevious_     = false;
    prevAtkMove_      = 0;
    prevAtkFrame_     = 0;
    prevAtkHitBits_   = 0;
    prevDefHitstun_   = 0;
    prevDefBlockstun_ = 0;
    prevDefHealth_    = 0;

    recentCount_ = 0;
    cyclePos_    = 0;

    // The borrowed pointers are deliberately untouched -- the header says so, and
    // a Reset that dropped the analysis would silently turn the judge into a
    // counter at the exact moment a host was restarting a round to try again.
}

const ComboReport& ComboWatcher::Current() const { return current_; }
const ComboReport& ComboWatcher::Previous() const { return previous_; }
std::int32_t       ComboWatcher::CompletedCombos() const { return completed_; }
bool               ComboWatcher::Stale() const { return stale_; }

// --- The tick -----------------------------------------------------------------

void ComboWatcher::OnTick(const TickView& view) {
    // Section 6 of the header. A re-simulated tick means the fighters were rolled
    // back and this object was not, so its one tick of history is about a timeline
    // that no longer happened. It stops reporting rather than report about it. The
    // flag is tested FIRST so that a session which keeps re-simulating cannot walk
    // the watcher forward one signal at a time after it has given up.
    if (stale_) return;
    if (view.resimulated) {
        // A RE-SIMULATED TICK IS NEVER JUDGED -- both paths below return -- and it
        // raises the flag ONLY WHEN THERE IS HISTORY FOR IT TO INVALIDATE. THIS
        // FILE DECIDED the second half, because without it the header's own
        // instruction (Reset(): "call it on Begin() and after a rollback") is one
        // the very next tick undoes.
        //
        // `havePrevious_` is exactly "a tick has been judged since the last
        // Reset()": it is set at the bottom of every judged tick and cleared by
        // Reset() and by nothing else. A watcher that has judged nothing carries no
        // verdict about the old timeline, so there is nothing for the flag to
        // protect a reader from -- and raising it anyway would mean this:
        //
        //   a host restores tick 15 while the session's high-water mark is 30 and
        //   calls Reset(), as the header tells it to. Ticks 15 through 29 all
        //   report themselves re-simulated (FightSession.cpp: the flag is `ran <
        //   highWaterTick_`), so an unconditional stale_ comes straight back up on
        //   the next tick and the judge stays silent for the rest of the window --
        //   unless the host tracks FightSession::HighWaterTick() itself and calls
        //   Reset() a second time on exactly the right tick. A reset whose effect
        //   is undone one tick later is not a reset.
        //
        // SKIPPING rather than judging is the conservative half, and it is what
        // keeps the seam honest: `havePrevious_` stays false across the whole
        // window, so the first tick the watcher does judge is treated as its first
        // tick and signal 3's guard stops an already-connected hit bit from being
        // read as a fresh hit. It also keeps the property the paragraph above is
        // about -- a session that keeps re-simulating cannot walk the watcher
        // forward one signal at a time, whether the flag went up or not.
        if (havePrevious_) stale_ = true;
        return;
    }

    // A view with no state is a host wiring bug rather than a tick. Refusing it
    // here instead of dereferencing keeps OnTick total for any TickView, which is
    // the standard IInputSource::At is held to one header over.
    if (view.state == nullptr || view.data == nullptr) return;

    const int      a   = attackerSlot_;
    const int      d   = 1 - a;
    const Fighter& atk = view.state->p[a];
    const Fighter& def = view.state->p[d];

    // "bit i is set once this attack has connected on slot i" (GameState.h).
    // Combat.cpp's bitForSlot is the same shift.
    const std::uint8_t defBit = static_cast<std::uint8_t>(1u << d);

    // === SIGNAL 1: A MOVE STARTED ==========================================
    //
    // `moveId != 0 && moveFrame == 0`, and NOT a change of moveId. StepAttack
    // increments moveFrame at the top of every tick a move is already running, so
    // frame 0 at the END of a tick means the move began ON that tick -- whether it
    // began from idle, from a cancel, or from the button scan that runs in the
    // same call as the previous move ending. A self-cancel keeps the id and resets
    // the frame, which is why the frame is the signal: fighter_a_infinite's whole
    // deliberate bug is `stand_lp -> stand_lp`, and a transition detector watching
    // that fight would report one hit and sit silently through the other 25.
    const bool started = atk.moveId != 0 && atk.moveFrame == 0;

    // === SIGNAL 3: A HIT CONNECTED =========================================
    const bool bitNow    = (atk.alreadyHitBits & defBit) != 0;
    const bool bitBefore = (prevAtkHitBits_ & defBit) != 0;

    // The header's rule, with `havePrevious_` added to the transition half and to
    // that half only. THIS FILE DECIDED that guard, and it is a small decision: on
    // the very first tick a watcher sees, `prevAtkHitBits_` is zero because
    // nothing has ever been observed and not because the bit was clear -- so a
    // watcher attached to a session mid-active-window would read an
    // already-connected bit as a fresh hit and open a string that began before it
    // was watching. The `started` disjunct needs no such guard and keeps its full
    // strength: if the attacker's move began on this tick then StepAttack cleared
    // the whole field on this tick, so anything set in it can only have come from
    // this tick's ResolveHits. A genuine hit on a watcher's first tick therefore
    // still counts, because a hit that early is a startup-0 move connecting on the
    // frame it started -- which is exactly the hole the disjunct exists to close.
    const bool hit = bitNow && (started || (havePrevious_ && !bitBefore));

    // === SIGNAL 2: THE DEFENDER WAS ACTIONABLE AT THE TOP OF THIS TICK =====
    //
    // Read DIRECTLY off Fighter::hitstun as observed at the END of the previous
    // tick, and off nothing else. StepPhysics decrements stun before anything else
    // looks at it and Simulate.cpp's actionable() is `hitstun == 0 && blockstun ==
    // 0`, so a defender sitting at 1 at the end of tick t-1 is free to act on tick
    // t. blockstun is in the test even though nothing in the kernel writes it, so
    // that the day blocking lands this line is already right rather than quietly
    // one term short.
    //
    // The two indirect readings tests/test_ground_truth.cpp uses -- `stun == 0 &&
    // !hit` after the tick, and `defMove != 0 && defFrame == 0` -- both miss the
    // defender whose stun expires on exactly the tick the next hit arrives, and on
    // fighter_a that hid four escapes and turned 33 cycles into 37.
    //
    // With no previous tick there is nothing holding anybody still, so the answer
    // is yes. That branch is unreachable while a string is open (a string needs a
    // prior hit, which needs a prior tick) and is written anyway, because a
    // predicate that is only defined on the paths somebody checked is a predicate
    // waiting for the path nobody checked.
    const bool actionable =
        havePrevious_ ? (prevDefHitstun_ <= 1 && prevDefBlockstun_ <= 1) : true;

    // Taken before anything below can close the string: every judgement made on
    // this tick is a judgement about the string as it stood when the tick began.
    const bool wasOpen = current_.open;

    // === A MOVE THAT STARTED INSIDE THE STRING AND NEVER CONNECTED =========
    //
    // Counted when it CONCLUDES rather than when it starts, because that is the
    // first moment the answer exists: a move at frame 3 of a 5-frame startup has
    // not whiffed, it has not happened yet. The previous move has concluded when a
    // new one began this tick (StepAttack ended or cancelled it) or when the
    // attacker is now idle -- it ran out, or ResolveHits zeroed it because the
    // attacker was hit. In every one of those cases the record of what it hit was
    // cleared THIS tick, so `bitBefore`, taken at the end of the previous tick, is
    // that move's final verdict on itself.
    //
    // It cannot double-count: after a conclusion `prevAtkMove_` is either 0 (the
    // guard fails next tick) or the freshly started move, which is at frame 1 next
    // tick and so is neither `started` nor idle.
    if (wasOpen && havePrevious_ && prevAtkMove_ != 0 &&
        (started || atk.moveId == 0) && !bitBefore) {
        addSaturating(current_.whiffedStarts, 1);
    }

    // === THE STRING ENDED, OR IT HAS A GAP IN IT ===========================
    //
    // THE TWO QUESTIONS THAT COME OUT OF SIGNAL 2, kept apart because conflating
    // them is how a training HUD lies. Both are asked only while a string is
    // already open, which is also what excludes the opening hit's own tick: the
    // defender was legitimately actionable then, and that is what being hit out of
    // neutral means.
    if (wasOpen && actionable) {
        if (hit) {
            // The string CONTINUES and is NOT a true combo: a human defender could
            // have acted on this tick and the next hit landed anyway. This is the
            // case both indirect detectors miss and the one a playtester most
            // needs told.
            addSaturating(current_.gapTicks, 1);
        } else {
            // They got out. Close it where it ended, AND HAND THE FINISHED STRING
            // TO `previous_` ON THIS TICK rather than on the next string's opening
            // hit.
            //
            // That is what makes both accessors' documented sentences true at the
            // same time. Previous() is "the string that most recently ENDED", and
            // from here until the next hit there is one; filling it at the next
            // open would leave it holding the string BEFORE last for that whole
            // window -- for a first combo, a freshly cleared report -- so a HUD
            // asked to go on showing "37 hits, 480 damage, TRUE COMBO" after the
            // defender got out would show zeroes at exactly the moment it is meant
            // to speak. Current() is "the string in progress, or the last one that
            // finished": nothing clears it here, and the opening hit below is where
            // it starts over.
            //
            // COPIED, NOT SWAPPED, and the allocation budget survives it.
            // vector::operator= from an lvalue reuses the destination's capacity
            // when it is large enough, so this allocates while `previous_` is still
            // growing to the size a string reaches and not once it has -- the
            // header's "never allocates in steady state after the first combo",
            // intact. What it costs instead is one copy of at most
            // kMaxComboSequence entries ONCE PER STRING END, which is not a
            // per-tick cost at all. A swap cannot be had here for any price: it
            // would hand the ended string's contents AWAY from Current(), which is
            // the accessor documented to keep them.
            current_.open    = false;
            current_.endTick = view.tick;
            addSaturating(completed_, 1);
            previous_ = current_;
        }
    }

    // === OPEN A NEW STRING ON THE HIT THAT STARTS IT =======================
    //
    // The finished string is already in `previous_` -- it went there on the tick it
    // ended -- so all that happens here is that the live report starts over. From
    // this tick Current() is the new string and Previous() goes on showing the
    // finished one for as long as the new one runs, which is the window a HUD draws
    // both of them in; before this tick they are the same string, the one that
    // finished, which is the other thing a HUD needs to be able to say.
    //
    // clearReport rather than `current_ = ComboReport{}`, for the reason
    // clearReport is written out field by field: it keeps the two vectors'
    // capacity, so every string after the first reuses the buffers the first grew.
    if (hit && !current_.open) {
        clearReport(current_);

        current_.open      = true;
        current_.startTick = view.tick;

        if (analysis_ != nullptr) {
            // A string begins ON the witness by definition -- nothing has diverged
            // from it yet -- but only if there is a witness to be on. A Terminating
            // verdict has an empty prefix and an empty loop, and reporting a player
            // as "on the witness" for a character that has none would be a progress
            // bar with no destination.
            current_.onWitness = witnessLength(*analysis_) > 0;

            // The adapter's own caveat, refused the chance to wear a verdict's
            // clothes. When comboprover did not enter the loop on the very first
            // move it omits the opening move from BOTH lists (ProverAdapter.h
            // states this), so the witness comparison would be run against a
            // sequence missing its head. Reported as "the analysis could not say"
            // rather than as a mismatch the player caused.
            //
            // Gated on there BEING a loop, which the header does not spell out:
            // loopEntryKnown is false by default on every terminating character,
            // and raising this flag on all of those would be a caveat with no
            // witness behind it.
            current_.witnessIncomplete =
                !analysis_->loop.empty() && !analysis_->loopEntryKnown;
        }

        // The cycle matcher and its tail start over with the string. A cycle that
        // spanned two strings would be a cycle the defender got out of the middle
        // of, which is not a cycle anybody performed.
        recentCount_ = 0;
        cyclePos_    = 0;
    }

    // === THE CHAIN, ONE TRANSITION AT A TIME ===============================
    //
    // Recorded BEFORE the hit is counted, and the order matters for exactly one
    // case: a move with startup 0 begins and connects on the same tick, and the
    // retroactive `deadEdgeConnected` below looks for the edge that started the
    // move now landing. Recording afterwards would leave that edge invisible on
    // the one tick it mattered.
    //
    // A move that started while no string was open is not recorded, which is what
    // "every move start INSIDE the string" says. It has one visible consequence
    // worth stating: an ordinary opener -- move starts at tick 3, connects at tick
    // 6 -- contributes to `sequence` and not to `edges`, because the string did
    // not exist when it began.
    if (started && current_.open) {
        PerformedEdge edge{};
        edge.tick = view.tick;
        edge.from = prevAtkMove_;      // 0 when the move started from idle
        edge.to   = atk.moveId;

        // SIGNAL 5: A CANCEL AND A LINK ARE DIFFERENT THINGS AND THE ANALYSIS
        // ONLY KNOWS ONE OF THEM.
        //
        // The source was observed at (A, f) at the end of the previous tick, and
        // MoveDuration(A) = D. StepAttack runs `++moveFrame` and THEN asks whether
        // the move has run out, so `f + 1` is the frame the source reached on this
        // tick:
        //
        //   f + 1 >= D   A ran out. StepAttack ended it and the button scan
        //                started the follow-up in the same call. This is a LINK,
        //                the character's cancel table says nothing about it, and
        //                the prover's graph does not contain it at all. 32 of
        //                fighter_a's 41 cycles run by this route, and reporting
        //                them as cancels would claim the player took edges the
        //                kernel demonstrably cannot take.
        //   f + 1 <  D   A was interrupted mid-move, which only FindCancel does,
        //                and `f + 1` is the number CancelEdge's inclusive window
        //                was compared against -- FindCancel runs after the
        //                increment.
        //
        // Being hit also interrupts a move, but ResolveHits zeroes the victim's
        // moveId, so an interrupted-by-a-hit attacker was observed at (0, 0) and
        // lands in the from-idle case rather than being mistaken for either.
        if (prevAtkMove_ != 0) {
            const MoveDef* source =
                cse::kernel::MoveAt(view.data->p[a], prevAtkMove_);
            // A moveId this character's table does not describe is INERT in the
            // kernel -- MoveAt returns null, FindCancel returns null for it, and
            // StepAttack only advances its frame -- so no cancel can have been
            // taken out of it and there is no duration to compare against. Left as
            // a link, which is what the button scan that started the follow-up
            // actually did.
            if (source != nullptr) {
                const std::int32_t reached =
                    static_cast<std::int32_t>(prevAtkFrame_) + 1;
                if (reached < cse::kernel::MoveDuration(*source)) {
                    edge.cancel      = true;
                    edge.sourceFrame = reached;
                }
            }
        }

        // --- What the analysis said about this pair of moves -----------------
        //
        // MATCHED FOR LINKS AS WELL AS FOR CANCELS, and THIS FILE DECIDED that:
        // the header states the matching rule and is silent on which routes it
        // applies to. The reason is that a dead cancel is a claim about TIMING --
        // "by the time the follow-up becomes dangerous, the defender is already
        // free" -- and the link route reaches the follow-up one tick LATER than
        // the cancel would. If the analysis says a pair cannot connect in time and
        // the player connects it, the disagreement is the same disagreement
        // whichever route the kernel took, and it is more surprising by the slower
        // one rather than less. `cancel` sits on the same struct and says which
        // route it was, so a HUD that wants the distinction has it; withholding
        // the analysis from links instead would have hidden the D8 finding on the
        // exact character it was measured from, where 32 of 41 cycles are links.
        //
        // A move started FROM IDLE is excluded, and not merely as a bounds guard:
        // a cancel is an edge between two moves, so a start with no source is not
        // a transition the analysis has any opinion about. The guard also keeps
        // CharacterMoveOf(0)'s kInvalidMove out of the comparison, where it would
        // match a default-constructed ProverDeadCancel -- whose `from` and `to`
        // are both kInvalidMove -- and report a player who pressed a button out of
        // neutral as having taken a dead edge.
        if (analysis_ != nullptr && edge.from != 0) {
            const MoveIndex from = MoveIndexMap::CharacterMoveOf(edge.from);
            const MoveIndex to   = MoveIndexMap::CharacterMoveOf(edge.to);

            const std::size_t index =
                findDeadCancel(analysis_->deadCancels, from, to);
            if (index < analysis_->deadCancels.size()) {
                edge.dead            = true;
                edge.deadCancelIndex = index;

                // ADR-001 section 5, D8: 128 of Kung Fu Girl's 134 dead cancels
                // are the decay curve talking rather than the character. An edge
                // dead only once decay is switched on is a different finding from
                // one dead at the authored numbers, and a panel that draws them
                // identically is mostly drawing noise.
                edge.deadOnlyUnderDecay =
                    findDeadCancel(analysis_->deadCancelsPreDecay, from, to) >=
                    analysis_->deadCancelsPreDecay.size();
            }
        }

        if (edge.dead) current_.performedDeadCancel = true;

        // Capped, and DROPPED at the cap rather than rotated, for the reason
        // ComboReport gives about `sequence`: the prefix is what a witness
        // comparison starts from, so the first entries are the ones worth keeping.
        // Nothing that matters goes with the dropped ones -- the counters, the
        // loud flags, the cycle matcher and the witness cursor all keep running
        // past the cap, because none of them reads this list.
        if (current_.edges.size() < kMaxComboSequence)
            current_.edges.push_back(edge);
    }

    // === THE HIT ITSELF ====================================================
    if (hit) {
        addSaturating(current_.hits, 1);
        current_.lastHitTick = view.tick;

        // DAMAGE IS THE DEFENDER'S HEALTH DELTA, never MoveDef::damage.
        // ResolveHits applies `health > dmg ? health - dmg : 0`, so the authored
        // number would go on accruing against a fighter already at nothing -- and
        // a fighter at nothing is the normal end state of the infinite this tool
        // is for. With no previous tick there is no delta and the hit contributes
        // zero rather than the defender's whole health bar.
        if (havePrevious_)
            addSaturating(current_.damage, prevDefHealth_ - def.health);

        // The connecting move is the attacker's move as it stands at the end of
        // the tick. ResolveHits runs last and does not touch the ATTACKER's
        // moveId; it zeroes only a fighter that was hit, and an attacker who was
        // hit had alreadyHitBits cleared in the same pass, so no hit would have
        // been counted here at all.
        //
        // THAT IS ALSO THIS SIGNAL'S ONE BLIND SPOT, named rather than papered
        // over: in a genuine trade both fighters' bits are set by ResolveHits and
        // then both are cleared, so a hit traded on the same tick is invisible
        // here and contributes neither a hit nor its damage. The two numbers stay
        // consistent with each other, which is the property worth keeping.
        // Fighter::comboHits is where the kernel could say otherwise, and since
        // ADR-005 P2 it does say something -- but it is still not read here, and
        // the header's section 4 gives the reason: two counts derived separately
        // can be cross-checked, and one derived from the other cannot.
        const std::uint16_t connecting = atk.moveId;

        if (current_.sequence.size() < kMaxComboSequence)
            current_.sequence.push_back(connecting);
        else
            current_.sequenceTruncated = true;

        // The tail of connecting moves, kept so a string whose display sequence
        // has already filled can still be spelled out. Written with a running
        // total and a modulus rather than a head/tail pair because the member set
        // is fixed and one counter is what there is; it is exact, since the last
        // min(recentCount_, kMaxCycleLength) entries end at
        // (recentCount_ - 1) % kMaxCycleLength.
        recent_[recentCount_ % kMaxCycleLength] = connecting;
        ++recentCount_;

        // --- THE RETROACTIVE ONE, and it is the loudest thing this file says ---
        //
        // The move now landing was started by some edge, and if that edge is one
        // the analysis called DEAD then the analysis said the defender would be
        // free before this could connect, and they were not. That is a measured
        // disagreement between the model and the running game (ARCHITECTURE.md
        // D8) and it is a finding, not a glitch.
        //
        // WHICH edge, with no ambiguity and no scan: the move in progress began at
        // `view.tick - moveFrame`, and no two starts share a tick, so the last
        // recorded edge is the right one IF AND ONLY IF its tick is that number.
        // The comparison earns three things at once. It picks the correct edge
        // rather than the nearest one with a matching target, so a repeated move
        // cannot retroact onto an older repetition. It makes the cap above safe --
        // once `edges` is full its last entry has a strictly older tick than any
        // new start, so a truncated list cannot retroact onto a stale entry. And
        // it correctly declines when the connecting move began BEFORE the string
        // opened, which is the ordinary case for a string's very first hit.
        const std::uint32_t beganAt =
            view.tick - static_cast<std::uint32_t>(atk.moveFrame);
        if (!current_.edges.empty()) {
            PerformedEdge& last = current_.edges.back();
            if (last.tick == beganAt && last.to == connecting && last.dead) {
                last.deadEdgeConnected     = true;
                current_.deadEdgeConnected = true;
            }
        }

        if (analysis_ != nullptr) {
            advanceCycle(*analysis_, connecting, current_, cyclePos_);
            advanceWitness(*analysis_, connecting, current_);
        }
    }

    // === ONE TICK OF HISTORY, AND ONLY ONE ==================================
    //
    // Five scalars off the two fighters plus the defender's health, which is all
    // three signals need. Held as scalars rather than as a GameState copy so that
    // what this object depends on is visible at the one place it is written down.
    havePrevious_     = true;
    prevAtkMove_      = atk.moveId;
    prevAtkFrame_     = atk.moveFrame;
    prevAtkHitBits_   = atk.alreadyHitBits;
    prevDefHitstun_   = def.hitstun;
    prevDefBlockstun_ = def.blockstun;
    prevDefHealth_    = def.health;
}

// --- The string in words -------------------------------------------------------

std::string ComboWatcher::Describe() const {
    // Allocates, and is called on demand rather than per tick -- the header says
    // so. It exists for the reason cse::data::DescribeVerdict exists: the sentence
    // has to be written once rather than once per panel, so that a test failure
    // message, a log line and the editor's copy-to-clipboard cannot drift into
    // saying three different things about the same string.
    std::string out = "combo judge -- attacker slot " + num(attackerSlot_);
    if (moves_ != nullptr && !moves_->characterId.empty())
        out += " (" + moves_->characterId + ")";

    if (stale_) {
        out += "\n  STALE      a re-simulated tick rolled the fighters back and did "
               "not roll this object back, so it stopped reporting rather than "
               "describe a timeline that no longer happened. Reset() clears it.";
        return out;
    }

    const ComboReport& r = current_;
    if (!r.open && r.hits == 0) {
        out += "\n  state      nothing yet; " + num(completed_) +
               " string(s) have ended since the last reset";
        return out;
    }

    out += "\n  state      ";
    out += r.open ? std::string("OPEN -- the defender has not got out yet")
                  : ("ended on tick " + num(r.endTick));
    out += "\n  hits       " + num(r.hits) + " for " + num(r.damage) +
           " damage; first on tick " + num(r.startTick) + ", last on tick " +
           num(r.lastHitTick);

    out += "\n  combo      ";
    if (r.TrueCombo()) {
        out += "TRUE COMBO -- the defender was never actionable inside it";
    } else {
        out += "NOT A TRUE COMBO: the defender was actionable on " + num(r.gapTicks) +
               " tick(s) inside the string and was hit anyway, so a human could "
               "have acted there. That is a blockable string, not a combo.";
    }
    if (r.whiffedStarts > 0)
        out += "\n  whiffs     " + num(r.whiffedStarts) +
               " move(s) started inside the string and never connected";

    // --- what was performed ---------------------------------------------------
    out += "\n  moves      ";
    const std::size_t shown =
        r.sequence.size() < kDescribeMoves ? r.sequence.size() : kDescribeMoves;
    for (std::size_t i = 0; i < shown; ++i) {
        if (i) out += " > ";
        out += moveName(moves_, r.sequence[i]);
    }
    if (r.sequence.size() > shown)
        out += " > ... and " +
               num(static_cast<std::int64_t>(r.sequence.size() - shown)) + " more";
    if (r.sequenceTruncated) {
        out += "\n             the string outran the " +
               num(static_cast<std::int64_t>(kMaxComboSequence)) +
               "-move display cap, which is itself the finding. Most recent: ";
        const std::size_t have =
            recentCount_ < kMaxCycleLength ? recentCount_ : kMaxCycleLength;
        const std::size_t first = recentCount_ - have;
        for (std::size_t i = 0; i < have; ++i) {
            if (i) out += " > ";
            out += moveName(moves_, recent_[(first + i) % kMaxCycleLength]);
        }
    }

    if (!r.edges.empty()) {
        out += "\n  chain      ";
        const std::size_t edgesShown =
            r.edges.size() < kDescribeEdges ? r.edges.size() : kDescribeEdges;
        for (std::size_t i = 0; i < edgesShown; ++i) {
            const PerformedEdge& e = r.edges[i];
            if (i) out += "\n             ";
            out += "tick " + num(e.tick) + "  " + moveName(moves_, e.from) + " -> " +
                   moveName(moves_, e.to);
            // The distinction test_gap_extent.cpp's headline rests on: 32 of
            // fighter_a's 41 cycles are performed by the LINK route, which is not
            // an edge in the prover's graph at all.
            if (e.cancel)
                out += "  by CANCEL, from source frame " + num(e.sourceFrame);
            else if (e.from == 0)
                out += "  from idle";
            else
                out += "  by LINK (the source ran out and the held button started "
                       "the follow-up)";
            if (e.dead) {
                out += "  [the analysis calls this pair DEAD";
                if (e.deadOnlyUnderDecay)
                    out += ", but only once hitstun decay is switched on";
                if (e.deadEdgeConnected) out += " AND IT CONNECTED";
                out += "]";
            }
        }
        if (r.edges.size() > edgesShown)
            out += "\n             ... and " +
                   num(static_cast<std::int64_t>(r.edges.size() - edgesShown)) +
                   " more";
    }

    // --- what the analysis says about it --------------------------------------
    if (analysis_ == nullptr) {
        out += "\n  analysis   none supplied: this is a plain combo counter, which "
               "is a useful thing on its own";
        return out;
    }

    out += "\n  analysis   ";
    out += cse::data::ProverStatusName(analysis_->status);
    if (r.witnessIncomplete)
        out += "\n  caveat     the analysis omitted the loop's opening move "
               "(ProverResult::loopEntryKnown is false), so the witness comparison "
               "below is unreliable and says so, rather than reporting the "
               "adapter's caveat as a mismatch the player caused";

    if (!analysis_->loop.empty()) {
        out += "\n  cycle      " + num(r.cycleRun) +
               " connecting move(s) of the printed loop in a row, " +
               num(r.loopTurnsCompleted) + " complete turn(s)";
        out += "\n  witness    ";
        if (r.onWitness)
            out += "on the witness, " +
                   num(static_cast<std::int64_t>(r.witnessIndex)) + " of " +
                   num(static_cast<std::int64_t>(witnessLength(*analysis_))) +
                   " move(s) matched";
        else
            out += "not on the witness";
    }

    // THE LOUD ONES, last, because they are the point of the file.
    if (r.completedProverLoop)
        out += "\n  ** THE PLAYER PERFORMED THE LOOP THE DECISION PROCEDURE "
               "PRINTED. The analysis called this character INFINITE and named "
               "this cycle out of the character file; it has now been executed in "
               "the running game by a person.";
    if (r.deadEdgeConnected)
        out += "\n  ** A TRANSITION THE ANALYSIS CALLS DEAD CONNECTED. The model "
               "said the defender would be free before the follow-up became "
               "dangerous, and they were not. That is a measured disagreement "
               "between the model and the game, not a glitch.";
    else if (r.performedDeadCancel)
        out += "\n  note       the player took a transition the analysis calls "
               "dead; it did not connect inside this string, so the analysis and "
               "the game agree about it";

    return out;
}

} // namespace cse::game
