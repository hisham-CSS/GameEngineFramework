#include "cse/game/FightSession.h"

#include "cse/game/WitnessCursor.h"

// WHAT MAY NOT APPEAR IN THIS INCLUDE LIST, and it is not a style rule.
//
// No <chrono>, no <ctime>, no <random>, no <cstdlib> for rand(). This file
// contains the ONLY call to cse::kernel::Simulate outside a test, so "nothing in
// a tick path reads the wall clock" is a claim about ONE FILE rather than about a
// codebase -- and the cheapest way to keep it true is that the header which would
// let it be false is not here.
//
// No <algorithm> either, for Simulate.cpp's reason one directory over: a `min`
// written out is three lines and cannot pick up a floating-point overload by
// accident. Everything below is integer comparison, addition and assignment.
//
// <string> is fine and is the reason ValidateSetup takes a `std::string&`:
// refusals happen at match SETUP, never inside a tick.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cse::game {
namespace {

// A template rather than a single `num(std::int64_t)`. One signed parameter
// makes every std::size_t argument a narrowing conversion the compiler is
// entitled to warn about under /W4, and adding an unsigned overload beside it
// makes `num(someUint16)` ambiguous instead. std::to_string already has the
// overload set; this only spares the call sites the noise.
template <typename T>
std::string num(T v) {
    return std::to_string(v);
}

// Button masks are BIT PATTERNS -- kInputLP is 1 << 4 -- so a refusal that
// prints 16 makes its reader do the conversion in their head before they can
// tell which button was being asked for.
std::string hex16(std::uint16_t v) {
    static const char* const digits = "0123456789ABCDEF";
    std::string out = "0x";
    for (int shift = 12; shift >= 0; shift -= 4) {
        out += digits[(v >> shift) & 0xF];
    }
    return out;
}

// What the attacker was doing, for a stall message. Kernel move ids and not
// names: this function has the MatchData and not the CharacterData, so the
// names live one layer up in MoveIndexMap and a second, disagreeing name table
// here would be the "two sources of truth for the frame data" Combat.h refuses.
std::string describeAttacker(const cse::kernel::Fighter& f) {
    if (f.moveId == 0) return "idle";
    return "in kernel move " + num(f.moveId) + " at frame " + num(f.moveFrame);
}

} // namespace

// --- Setup -------------------------------------------------------------------

bool ValidateSetup(const FightSetup& setup, std::string& error) {
    // Cleared on ENTRY rather than written only on failure. A lobby that checks
    // two setups with one string would otherwise read the first one's complaint
    // beside the second one's `true`, and a stale error message is worse than
    // none: it names a problem somebody has already fixed.
    error.clear();

    if (setup.data == nullptr) {
        error = "the fight setup carries no MatchData. FightSession BORROWS the "
                "built character data (FightSetup::data) and never loads any -- "
                "building it is CseData's job and has its own error vocabulary "
                "(LoadReport, BuildReport). Refused here rather than crashing on "
                "the first tick.";
        return false;
    }

    for (int slot = 0; slot < 2; ++slot) {
        const std::int32_t x = setup.start.startPosX[slot];
        // Compared against the symmetric bound directly. Nothing is negated, so
        // INT32_MIN -- the one value with no positive counterpart -- is caught by
        // the comparison rather than by an overflow inside the check.
        if (x > cse::kernel::kMaxWorldCoord || x < -cse::kernel::kMaxWorldCoord) {
            error = "fighter " + num(slot) + " starts at " + num(x) +
                    " sub-units, outside the kernel's world bound of +/-" +
                    num(cse::kernel::kMaxWorldCoord) + ". That bound is what makes "
                    "PlaceBox total for ANY state (Combat.h), so a match opened "
                    "outside it has boxes nothing can reason about.";
            return false;
        }
    }

    // DELIBERATELY NOT REFUSED: a position outside the playable stage. Simulate
    // clamps posX to its own stage half-width on the first tick, that clamp is
    // deterministic and identical on both peers, and a check here would be a
    // second and disagreeing definition of where the stage ends. The header says
    // so; it is restated here because this is where somebody would add it.
    return true;
}

// --- The session -------------------------------------------------------------

// Declared in the header and therefore defined here rather than left implicit:
// `FightSession(const FightSession&) = delete` is a user-declared constructor,
// which suppresses the implicit default one. Every member carries its own
// initializer, so there is nothing for this to do beyond existing.
FightSession::FightSession() = default;

bool FightSession::Begin(const FightSetup& setup, std::string& error) {
    if (!ValidateSetup(setup, error)) {
        // NOT started, and a session that was already running is left EXACTLY as
        // it was -- state, tick index, high-water mark and all. A failed restart
        // that had already wiped the match would throw away a recording in
        // progress on behalf of a caller who never got a `true` back.
        return false;
    }

    // ResetMatch memsets, so no field survives a Begin: tick, rng, both
    // fighters, and the high-water mark below. The seed is passed AS GIVEN --
    // ResetMatch is the one place that maps 0 to 0x9E3779B9, so a replay that
    // records the caller's seed reproduces this same stream.
    cse::kernel::ResetMatch(state_, setup.start.seed);
    state_.p[0].posX = setup.start.startPosX[0];
    state_.p[1].posX = setup.start.startPosX[1];

    data_          = setup.data;
    started_       = true;
    highWaterTick_ = 0;

    // OBSERVERS AND SOURCES BOTH SURVIVE. The header argues the observer half:
    // a training host that rebinds its recorder and its combo judge on every
    // reset drops one of them eventually, and every observer in this module has
    // its own Begin/Reset for its own state. The input sources are kept for the
    // same reason and it is the more obvious one -- the pad the human is holding
    // did not change because the round did.
    return true;
}

bool FightSession::Started() const { return started_; }

void FightSession::Tick() {
    if (!started_) return;

    // ASKED AT THE TICK THAT IS ABOUT TO RUN, spelled as the accessor so the
    // equality CurrentTick() == State().tick is visible at the one call site
    // that depends on it. This is the same question a seek or a rollback
    // re-simulation asks, which is the entire reason IInputSource is indexed by
    // tick rather than pulled from.
    const std::uint32_t tick = CurrentTick();

    cse::kernel::InputPair inputs{};
    for (int player = 0; player < 2; ++player) {
        const IInputSource* const source = sources_[player];
        if (source == nullptr) continue;   // no source bound: NEUTRAL

        const InputSample sample = source->At(tick);
        // The FLAG is the contract; the zeroed `input` an unauthored sample is
        // required to carry is the belt and braces (InputSource.h). Reading the
        // flag rather than trusting the zero is what keeps a source that
        // violates the second half from putting stale bits into a match, and it
        // costs one branch per player per tick.
        //
        // NEUTRAL, NEVER "REPEAT THE LAST TICK": repeating would make the input
        // a function of the path taken through the match rather than of the tick
        // index, so re-simulating tick 900 after a different history would feed
        // it different bits. It would also jam the button down the moment a
        // demonstration ended, which is the one tick the playtester is supposed
        // to be handed control on.
        if (sample.authored) inputs.p[player] = sample.input;
    }

    Tick(inputs);
}

void FightSession::Tick(const cse::kernel::InputPair& inputs) {
    // data_ is non-null whenever started_ is true (Begin refuses a null one), so
    // the second half of this condition can never fire. It is written anyway:
    // the alternative is dereferencing a pointer on the strength of a bool.
    if (!started_ || data_ == nullptr) return;

    // READ BEFORE Simulate, because Simulate increments it at the bottom. This
    // is TickView's documented off-by-one and it is computed once, here, so that
    // no observer has to compute it and no observer can get it wrong.
    const std::uint32_t ran = state_.tick;

    // `ran` has been run before exactly when it is at or below the highest index
    // ever run -- and the high-water mark is that index PLUS ONE, so the test is
    // a strict `<`. Computed from the session's own bookkeeping rather than
    // passed in, so no caller can forget to set it: Restore moves the tick index
    // backwards, the next Tick is therefore a re-simulation, and the session
    // knows that without being told.
    const bool resimulated = ran < highWaterTick_;

    cse::kernel::Simulate(state_, inputs, *data_);

    // Only ever forward, INCLUDING across a Restore. That is what makes the line
    // above true for every tick a rollback re-runs.
    //
    // NO CAP ON THE TICK INDEX HERE, and that is a decision rather than an
    // omission. kMaxMatchTicks bounds the things that ALLOCATE per tick -- a
    // latched source, a scripted trace, a recorder -- and a session that refused
    // to advance at some number would be a second, disagreeing definition of how
    // long a match may be, in the one object that must stay a thin wrapper around
    // a pure kernel call. The uint32 wrap is 2.27 years of continuous ticking
    // away and would degrade to "every tick reports itself resimulated", which is
    // conservative rather than wrong.
    if (state_.tick > highWaterTick_) highWaterTick_ = state_.tick;

    TickView view{};
    view.tick        = ran;
    view.inputs      = inputs;   // the bits that WENT IN, never re-asked of a
                                 // source -- re-asking is how a recorder and a
                                 // simulation come to disagree about what
                                 // happened
    view.state       = &state_;
    view.data        = data_;
    view.resimulated = resimulated;

    // Registration order, after Simulate has returned and after the tick index
    // has advanced. NO RE-ENTRANCY GUARD, deliberately: FightSession.h states
    // that an observer which ticks, begins or restores the session it is
    // observing is a caller bug with an obvious fix, and a guard would turn that
    // bug into a silently reordered tick stream -- which is the same failure
    // wearing a green test.
    for (int i = 0; i < observerCount_; ++i) {
        observers_[i]->OnTick(view);
    }
}

const cse::kernel::GameState& FightSession::State() const { return state_; }

const cse::kernel::MatchData& FightSession::Data() const {
    // A session that has never begun has no data to lend, and the header
    // promises a reference rather than a pointer. kNoMoves is the kernel's own
    // answer to "no characters loaded" -- moveCount 0 and a degenerate hurtbox,
    // so nothing can start, nothing can be live and nothing can hit. Returning
    // it makes this accessor total, and it makes a caller who forgot to Begin
    // read an empty match rather than a dangling reference.
    return data_ != nullptr ? *data_ : cse::kernel::kNoMoves;
}

std::uint32_t FightSession::CurrentTick() const { return state_.tick; }

std::uint32_t FightSession::HighWaterTick() const { return highWaterTick_; }

std::uint32_t FightSession::Checksum() const {
    // QUALIFIED, and it has to be: unqualified `Checksum(state_)` finds this
    // member function first, class scope beating the enclosing namespace, and
    // fails on the argument count rather than reaching the kernel's.
    return cse::kernel::Checksum(state_);
}

void FightSession::Snapshot(cse::kernel::GameState& out) const {
    // A copy of an 80-byte trivially-copyable POD. No allocation, nothing that
    // can fail, which is what D4's snapshot budget rests on.
    out = state_;
}

void FightSession::Restore(const cse::kernel::GameState& in) {
    // The tick index moves with the state, because it IS part of the state --
    // there is no separate cursor to forget to move. highWaterTick_ is
    // deliberately untouched, so every tick re-run up to that mark reports
    // itself as resimulated.
    //
    // Legal before Begin, and inert afterwards: started_ stays false, so Tick
    // still does nothing. A state without the MatchData it was simulated against
    // is not a match (Combat.h), and this is not the place to invent one.
    state_ = in;
}

void FightSession::SetInputSource(int player, const IInputSource* source) {
    if (player < 0 || player > 1) return;   // ignored, per the header
    sources_[player] = source;
}

const IInputSource* FightSession::InputSourceFor(int player) const {
    if (player < 0 || player > 1) return nullptr;
    return sources_[player];
}

bool FightSession::AddObserver(ITickObserver* observer) {
    if (observer == nullptr) return false;
    if (observerCount_ >= kMaxTickObservers) return false;

    // Registering twice is REFUSED rather than allowed. An observer notified
    // twice per tick double-counts every hit, and a combo judge that reports 52
    // hits for a 26-hit loop is a bug that looks like a finding.
    for (int i = 0; i < observerCount_; ++i) {
        if (observers_[i] == observer) return false;
    }

    observers_[observerCount_] = observer;
    ++observerCount_;
    return true;
}

bool FightSession::RemoveObserver(ITickObserver* observer) {
    for (int i = 0; i < observerCount_; ++i) {
        if (observers_[i] != observer) continue;

        // Shift the tail down rather than swapping the last entry into the hole.
        // The swap is one assignment and it silently reorders the notification
        // sequence, which the header makes part of the contract precisely so
        // that two observers with a shared assumption may rely on it.
        for (int j = i + 1; j < observerCount_; ++j) {
            observers_[j - 1] = observers_[j];
        }
        --observerCount_;
        observers_[observerCount_] = nullptr;
        return true;
    }
    return false;   // includes RemoveObserver(nullptr): never registered
}

void FightSession::ClearObservers() {
    for (int i = 0; i < observerCount_; ++i) observers_[i] = nullptr;
    observerCount_ = 0;
}

int FightSession::ObserverCount() const { return observerCount_; }

// --- The tool-assisted player's rehearsal ------------------------------------
//
// WHAT THIS IS A REUSABLE VERSION OF, AND WHERE IT DIFFERS.
//
// tests/test_ground_truth.cpp already performs a printed witness, with a class
// called Driver and a free function called drive(). That pair is the working
// reference and this is not a reinvention of it -- the cursor rule below is
// theirs, verbatim, including the part that matters most (advance on
// `moveFrame == 0`, never on a change of `moveId`, because the witness for an
// infinite is a move cancelling into ITSELF and the id never changes).
//
// FIVE DELIBERATE DIFFERENCES, each of which is the reason this exists:
//
//   1. THE DRIVER IS THROWN AWAY. There, the closed loop IS the input source:
//      it is consulted live, every tick, forever. Here it runs once against a
//      private session and what comes back is a fixed list. A closed loop is not
//      a pure function of a tick index -- its cursor depends on what the
//      simulation did -- so a rollback would ask it a question it had already
//      answered and get a different reply. The list is replayable, seekable and
//      recordable; the loop that produced it is not.
//   2. THE BUTTON COMES FROM MoveDef::button. There, a MoveBinding table maps
//      move ID STRINGS to buttons and the driver looks each one up. This
//      function holds the MatchData and not the CharacterData, so it reads the
//      button the bridge already wrote into the built move -- the same number by
//      construction, one lookup shorter, and with no second binding table to
//      disagree with the first.
//   3. IT STOPS. There, a stalled driver holds one button forever and the test's
//      per-tick table is what tells you why. Here there is a budget, a stall is
//      DATA (`stalledAt`, `error`), and the message names the move it was
//      waiting for and what the attacker was doing instead -- because "the
//      demonstration did not work" is not something a playtester can act on.
//   4. THE WITNESS IS CHECKED BEFORE THE FIRST TICK, which is what Driver::Usable
//      does and for the same reason: a move nothing can ask for must be a
//      refusal that names it, not a trace that silently stops following the
//      witness.
//   5. IT GOES THROUGH FightSession. There, drive() calls Simulate directly on a
//      hand-built GameState, because there was no session to call. Keeping the
//      rehearsal on the same path as the live match is what makes "this file is
//      the only caller of Simulate" true, and it means the demonstration is
//      rehearsed by exactly the object that will perform it.

bool BuildDemonstration(const DemonstrationRequest& request, Demonstration& out) {
    // The caller may be reusing `out`; a rehearsal that appended to somebody
    // else's trace would perform two combos back to back and blame the second.
    out = Demonstration{};
    out.firstTick = request.firstTick;

    // --- Refusals, all of them before anything is simulated ------------------

    if (request.from == nullptr) {
        out.error = "the demonstration request has no state to rehearse from. It "
                    "is a GameState and not a MatchStart because `show me this "
                    "combo from where I am standing` is the request; a host that "
                    "wants it from neutral calls Begin() first and passes what "
                    "that produces.";
        return false;
    }
    if (request.data == nullptr) {
        out.error = "the demonstration request has no MatchData. The button each "
                    "move is asked for with is read straight out of "
                    "MoveDef::button, so without the built data there is nothing "
                    "to press.";
        return false;
    }
    if (request.attackerSlot < 0 || request.attackerSlot > 1) {
        out.error = "attackerSlot is " + num(request.attackerSlot) +
                    "; there are two fighter slots, 0 and 1.";
        return false;
    }
    if (request.moveIds.empty()) {
        out.error = "the witness is empty. A demonstration of nothing is not a "
                    "demonstration, and an empty trace handed to a "
                    "ScriptedInputSource would hand control back on the tick it "
                    "was pressed.";
        return false;
    }
    if (request.loopStart >= request.moveIds.size()) {
        out.error = "loopStart is " + num(request.loopStart) + " and the witness "
                    "has " + num(request.moveIds.size()) + " entries. Past the "
                    "end the cursor returns to loopStart, so a loopStart at or "
                    "beyond the end would step off the witness on the first wrap.";
        return false;
    }
    if (request.turns == 0 && request.loopStart == 0) {
        // THE ONE REQUEST THAT IS SATISFIED BEFORE THE FIRST TICK, and it is
        // refused rather than granted. `turns == 0` is documented as "perform the
        // prefix and stop"; the prefix is moveIds[0, loopStart), so a loop that
        // starts at index 0 has no prefix and this pair asks for no ticks at all.
        //
        // Granting it returns complete == true with an EMPTY trace, which
        // contradicts the refusal fifteen lines up on exactly the ground that
        // refusal states: an empty trace handed to a ScriptedInputSource hands
        // control back on the tick it was pressed. `complete` is what a caller
        // checks before performing a demonstration, and a demonstration that
        // reports success and then visibly does nothing is the failure that costs
        // an afternoon -- there is nothing to look at and nothing said.
        //
        // Refused rather than repaired by performing one entry anyway: the
        // request would then be answered with work nobody asked for. Header note
        // for whoever reconciles the two sentences later -- Demonstration::complete
        // says "every move in the witness was entered AND `turns` full turns",
        // and that sentence already calls this case incomplete.
        out.error = "the request asks for 0 turns of a witness whose loop starts "
                    "at index 0, which is a request for no ticks at all: `turns "
                    "== 0` means perform the PREFIX and stop, and a loop starting "
                    "at 0 leaves no prefix to perform. Refused for the reason an "
                    "empty witness is refused -- an empty trace handed to a "
                    "ScriptedInputSource would hand control back on the tick it "
                    "was pressed -- rather than handed back as a complete "
                    "demonstration of nothing. Ask for at least one turn, or give "
                    "the witness a prefix to perform.";
        return false;
    }

    const cse::kernel::FighterData& attacker = request.data->p[request.attackerSlot];

    // EVERY ENTRY, BEFORE THE FIRST TICK. Checking lazily as the cursor arrives
    // would spend forty ticks performing the first half of a witness whose sixth
    // move nothing can ask for, and would hand the caller a partial trace it had
    // no reason to distrust. tests/test_ground_truth.cpp's Driver::Usable makes
    // exactly this check, in exactly this order, for exactly this reason.
    for (std::size_t i = 0; i < request.moveIds.size(); ++i) {
        const std::uint16_t id = request.moveIds[i];
        const cse::kernel::MoveDef* const move = cse::kernel::MoveAt(attacker, id);
        if (move == nullptr) {
            out.error = "witness entry " + num(i) + " names kernel move " +
                        num(id) + ", which fighter " + num(request.attackerSlot) +
                        " does not describe (its table has " +
                        num(attacker.moveCount) + " slot(s), of which slot 0 is "
                        "the reserved idle one). These are KERNEL ids -- "
                        "MoveIndexMap::KernelMoveIdOf turns a prover MoveIndex "
                        "into one.";
            return false;
        }
        if (move->button == 0) {
            // REFUSED BY NAME rather than pressed as nothing. A move with no
            // button is reachable only from a cancel; asking for it by holding
            // zero bits would start whatever the attacker's first bound move is,
            // which is a different combo performed under the witness's name.
            out.error = "witness entry " + num(i) + " names kernel move " +
                        num(id) + ", whose MoveDef::button is 0. That move is "
                        "reachable only from a cancel and nothing can ASK for "
                        "it, so the witness cannot be performed as inputs.";
            return false;
        }
    }

    // --- The budget ----------------------------------------------------------
    //
    // TWO BOUNDS, AND THE SECOND IS NOT THE CALLER'S. `maxTicks` is the caller's
    // hard budget; kMaxMatchTicks is this module's bound on every per-tick
    // container (InputSource.h), and the produced trace becomes exactly one of
    // those the moment it is wrapped in a ScriptedInputSource -- whose
    // precondition is `firstTick + inputs.size() <= kMaxMatchTicks`. Clamping
    // here is what keeps that source from having to truncate a trace it was
    // handed, which it would report but which nobody would be watching for.
    const std::uint32_t room = request.firstTick >= kMaxMatchTicks
                                   ? 0u
                                   : kMaxMatchTicks - request.firstTick;
    if (room == 0) {
        out.error = "the demonstration would begin at tick " +
                    num(request.firstTick) + ", at or past this module's tick cap "
                    "of " + num(kMaxMatchTicks) + " (one hour at 60 Hz). There is "
                    "no room left in the match to perform it.";
        return false;
    }
    const std::uint32_t budget = request.maxTicks < room ? request.maxTicks : room;

    // --- The private session -------------------------------------------------
    //
    // Begin puts it at ResetMatch's opening; Restore then installs the state the
    // playtester is actually standing in, which is the whole point of rehearsing
    // "from here". Restore is the public seam for that and using it is
    // deliberate -- a second `begin from this state` entry point would be a
    // second definition of what starting a match means, and this one is already
    // documented as a memcpy that cannot fail.
    //
    // MatchStart is left at its default because nothing it writes survives the
    // Restore. Its start positions are inside kMaxWorldCoord, so ValidateSetup
    // cannot refuse it and the failure branch below is defensive rather than
    // reachable today.
    //
    // NO OBSERVERS ARE REGISTERED. The closed loop never touches the live
    // session, so nothing the playtester's recorder or combo judge sees comes
    // from a rehearsal.
    FightSetup setup{};
    setup.data = request.data;

    FightSession session;
    std::string beginError;
    if (!session.Begin(setup, beginError)) {
        out.error = "the rehearsal could not start a private session: " + beginError;
        return false;
    }
    // Copied, never modified: the caller's state is a const reference and this
    // is the copy the rehearsal burns through. The absolute tick number inside
    // it is irrelevant to the outcome -- nothing in a tick reads GameState::tick
    // except the increment at the bottom of Simulate -- so a rehearsal that
    // starts from a state numbered 900 and a trace numbered from firstTick do
    // not have to agree.
    session.Restore(*request.from);

    // --- The rehearsal -------------------------------------------------------

    const std::size_t witnessSize  = request.moveIds.size();
    const int         defenderSlot = 1 - request.attackerSlot;

    // Bounded by kMaxMatchTicks by construction, which is 216000 Input = 432 KB
    // in the worst case a caller can ask for and 1.2 KB at the default budget of
    // 600. D4's rule about unbounded growth is satisfied by the clamp above, not
    // by hoping the witness is short.
    out.inputs.reserve(budget);

    // THE cursor -- WitnessCursor.h is the one home of every trace rule (the
    // release between repeats, the stance hold riding through it, the two-tick
    // re-press, start-before-release). The seam test compares this rehearsal
    // against a WitnessDriver over the SAME step function, so the two cannot
    // drift the way the five copies this replaced did (ROADMAP M1.3g).
    const WitnessCursor cursor =
        WitnessCursor::FromSlots(request.moveIds, request.loopStart, attacker);
    WitnessCursor::State cur{};

    std::uint32_t ticksRun        = 0;
    bool          everAdvanced    = false;
    std::uint32_t lastAdvanceTick = 0;

    // WHAT "DONE" MEANS, and the two sentences in the header that have to be
    // reconciled. `complete` is documented as "every move in the witness was
    // entered AND `turns` full turns were performed"; `turns` is documented as
    // "zero means perform the prefix and stop". For turns >= 1 those agree
    // exactly -- a turn is only counted on a wrap past the end, and you cannot
    // wrap without having entered every entry -- so the general rule is written
    // as the prefix requirement plus the turn count, which is the more specific
    // of the two sentences in the one case they differ.
    //
    // This is TRUE ON ENTRY for exactly one request -- loopStart 0 with turns 0,
    // since reachedIndex and turnsDone both start at zero -- and that request is
    // refused above. So the loop always runs at least once when the budget allows
    // it, and `complete` implies a trace with at least one tick in it.
    const auto satisfied = [&out, &request]() {
        return out.reachedIndex >= request.loopStart &&
               out.turnsDone >= request.turns;
    };

    while (ticksRun < budget && !satisfied()) {
        const std::size_t   at   = cur.cursor;
        const std::uint16_t bits = cursor.Bits(cur);

        cse::kernel::InputPair in{};
        in.p[request.attackerSlot].bits = bits;
        // The other slot gets the SAME bits on every tick -- the silent training
        // dummy the ground-truth file's own recipe prescribes ("input: zero on
        // every tick"), or whatever the caller chose. It is a constant and is
        // not stored in the result; storing 600 copies of it would invite a
        // caller to believe it varies.
        in.p[defenderSlot] = request.defenderInput;

        session.Tick(in);
        // Recorded BEFORE the cursor is examined, so that the loop can break the
        // instant the demonstration is done without dropping the tick that
        // finished it. `inputs[i]` is what was fed to relative tick i, always.
        out.inputs.push_back(cse::kernel::Input{ bits });
        ++ticksRun;

        const cse::kernel::Fighter& f = session.State().p[request.attackerSlot];
        const WitnessCursor::StepResult step = cursor.Step(cur, f.moveId, f.moveFrame);
        cur = step.next;
        if (step.advanced) {
            // How far along the witness this got: the number of LEADING entries
            // entered at least once, which reaches witnessSize exactly on the
            // wrap and stays there. On a stall during the first pass it is also
            // the index of the entry the rehearsal was waiting for, which is
            // what makes it the number a progress bar and a failure message can
            // both quote. The wrap is what counts a turn.
            if (at + 1 > out.reachedIndex) out.reachedIndex = at + 1;
            if (step.wrapped) ++out.turnsDone;
            everAdvanced    = true;
            lastAdvanceTick = ticksRun - 1;
        }
    }

    out.complete = satisfied();
    if (out.complete) {
        // `error` empty if and only if complete, and `stalledAt` left at zero
        // because the header says it is meaningless here.
        //
        // The trace ends on the tick the last required move STARTED, not when it
        // finishes. That is deliberate and it is the hand-back: the kernel needs
        // no button held to play a move out, so the final move completes under
        // the neutral input a source produces when it runs out, and the
        // playtester has the pad back before the animation is over.
        return true;
    }

    // --- The useful failure --------------------------------------------------
    //
    // A witness the engine cannot perform is the OTHER publishable outcome
    // (ARCHITECTURE.md 5.5 item 4), not a bug to retry, so this says which of
    // the two things happened and quotes the numbers rather than reporting that
    // the demonstration did not work.
    //
    // STALLED and RAN OUT are different findings. A cursor that was still
    // advancing on the final tick did not stall -- the budget simply ended, and
    // pointing a playtester at a stall that never happened would send them
    // looking for a broken cancel window instead of raising maxTicks.
    const bool ranOutMidProgress = everAdvanced && lastAdvanceTick + 1u == ticksRun;
    out.stalledAt = everAdvanced ? lastAdvanceTick + 1u : 0u;

    const std::uint16_t wanted = request.moveIds[cur.cursor];
    const std::uint16_t bits   = cse::kernel::MoveAt(attacker, wanted)->button;

    out.error = ranOutMidProgress
                    ? "the rehearsal ran out of budget after " + num(ticksRun) +
                          " tick(s) while it was still making progress"
                    : "the rehearsal stalled at tick " + num(out.stalledAt) +
                          " of " + num(ticksRun) + " (ticks relative to "
                          "firstTick " + num(request.firstTick) + ")";
    out.error += ": it was waiting for kernel move " + num(wanted) + " (button " +
                 hex16(bits) + ") and the attacker was " +
                 describeAttacker(session.State().p[request.attackerSlot]) +
                 ". Entered " + num(out.reachedIndex) + " of " + num(witnessSize) +
                 " witness move(s) and completed " + num(out.turnsDone) + " of " +
                 num(request.turns) + " turn(s).";
    return false;
}

} // namespace cse::game
