// One match, advanced one 1/60 tick at a time.
//
// FightSession is the ONLY thing in this repository that calls
// cse::kernel::Simulate outside a test. Everything else -- the recorder, the
// combo judge, the training HUD, a future rollback host -- reads what it did.
// Concentrating the call is not tidiness: `Simulate` is a pure function of
// (state, inputs, data), and a second caller somewhere else is a second place
// where the tick can acquire a condition, a clock read, or an input that came
// from the wrong place. Keeping it to one function makes "nothing in a tick path
// reads the wall clock" a claim about forty lines rather than about a codebase.
//
// ---------------------------------------------------------------------------
// WHAT THIS OBJECT DOES NOT OWN, AND WHY THAT IS THE POINT
// ---------------------------------------------------------------------------
// NO CLOCK. NO FRAME RATE. NO RENDER LOOP. There is no Run(), no Update(dt), no
// accumulator and no <chrono> anywhere in this header or its implementation. The
// HOST decides when to call Tick(). Everything the training mode needs falls out
// of that for free and costs this file nothing:
//
//   normal play   the host ticks once per 1/60 s of real time
//   frame step    the host ticks once when a key is pressed
//   slow motion   the host ticks once every fourth display frame
//   fast rehearse BuildDemonstration ticks a private session as fast as it can
//   replay        the host ticks while pulling input from a ReplayInputSource
//
// A session that owned a timestep would have to be told which of those it was
// in, and Application's FixedTimestep is specifically disqualified from driving
// it -- it caps at 8 steps and then ZEROES the accumulator, dropping the backlog
// (ARCHITECTURE.md's rejected-ideas table). A dropped tick in a fight is a lost
// input; in lockstep it is an unrecoverable desync.
//
// NO RENDERER, NO ENGINE, NO GL. This library links CseKernel and CseData and
// nothing else, asserted at configure time in Game/CMakeLists.txt. That is what
// makes every claim below testable in a unit test with no window, which is how
// every other proof in this repository was earned.
//
// NO CHARACTER FILES. The session takes a built MatchData. Loading and building
// is CseData's job and has its own error vocabulary (LoadReport, BuildReport);
// a session that could also load would have three ways to fail at match start
// and one place to report it.
#pragma once

#include "cse/game/InputSource.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/kernel/Simulate.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cse::game {

// --- The initial conditions -------------------------------------------------

// Everything about the opening position that a replay has to write down.
//
// THIS STRUCT IS A WIRE FORMAT BY PROXY. Replay.h stores exactly these fields
// and nothing else, because "an input log plus the initial conditions IS the
// recording". So ADDING A FIELD HERE IS A REPLAY FORMAT VERSION BUMP -- there is
// no way to add one without every existing replay becoming a recording of a
// different opening, and a format that grew a field without bumping its version
// is a format that plays old files back wrong and says nothing.
//
// It is separated from FightSetup (which carries a borrowed pointer a file
// cannot hold) precisely so that this rule has an object to be about.
struct MatchStart {
    // Seeds GameState::rng through ResetMatch. Stored AS GIVEN: ResetMatch maps
    // 0 to 0x9E3779B9 because xorshift is absorbing at zero, and that mapping is
    // deterministic, so recording the seed the caller passed reproduces the same
    // stream as recording the substituted one. Recording the given value keeps
    // the file readable against the call site that produced it.
    std::uint32_t seed = 0;

    // Sub-units from stage centre, one per fighter slot, matching Fighter::posX.
    // ResetMatch's own opening is -100px / +100px, which is a 174 px body-to-body
    // gap -- further than anything either shipped character reaches, so a
    // training session that wants anything to connect must set these. See
    // tests/test_ground_truth.cpp, which puts the origins 34 px apart for
    // exactly this reason.
    //
    // posY is not here: fighters start grounded (ResetMatch zeroes it) and an
    // airborne opening is not a thing training mode has asked for. Adding it is
    // a format bump, per the note above.
    std::int32_t startPosX[2] = { -100 * cse::kernel::kSubUnitsPerPixel,
                                   100 * cse::kernel::kSubUnitsPerPixel };
};

// What Begin() needs: the initial conditions, plus the read-only character data
// the whole match is fought with.
struct FightSetup {
    MatchStart start{};

    // BORROWED. NOT OWNED. NOT COPIED.
    //
    // MatchData is 10800 bytes of pure integers that no tick writes (Combat.h
    // argues that at length), so copying it into the session would buy nothing
    // and cost a second source of truth for the frame data -- D8's "the engine
    // and the analysis disagree by a frame", one indirection down. The caller
    // owns the MatchBuild it came out of and MUST keep it alive for the whole
    // life of the session, including for every tick a rollback re-simulates.
    //
    // Null is refused by Begin() rather than crashing on the first tick.
    const cse::kernel::MatchData* data = nullptr;
};

// Refuse a setup that cannot produce a meaningful match, as data. Split out from
// Begin so a lobby can check before committing, and so the message is written
// once. Returns false and fills `error` for: a null data pointer; a start
// position whose magnitude exceeds cse::kernel::kMaxWorldCoord (the bound that
// makes PlaceBox total for any state).
//
// It does NOT refuse a position outside the playable stage. Simulate clamps posX
// to its own stage half-width on the first tick, that clamp is deterministic and
// identical on both peers, and refusing it here would be a second, disagreeing
// definition of where the stage ends.
bool ValidateSetup(const FightSetup& setup, std::string& error);

// --- Watching a tick from outside -------------------------------------------

// One tick, as an observer sees it. Everything is read off the simulation the
// kernel produced; nothing is inferred from what a script asked for.
struct TickView {
    // The index of the tick that JUST RAN. Note the off-by-one and note it once:
    // after this view is delivered, `state->tick` is `tick + 1`, because Simulate
    // increments at the bottom. An observer that keys a container by
    // `state->tick` is off by one on every entry.
    std::uint32_t tick = 0;

    // The bits that were actually fed in, whatever they came from -- a source, a
    // rollback event, or a caller passing them straight. A recorder must record
    // THESE and never re-ask the source, because re-asking is how a recorder and
    // a simulation come to disagree about what happened.
    cse::kernel::InputPair inputs{};

    // The state AFTER the tick, and the data it was simulated against. Both
    // borrowed and valid only for the duration of the OnTick call. An observer
    // that wants to keep something copies it.
    const cse::kernel::GameState* state = nullptr;
    const cse::kernel::MatchData* data  = nullptr;

    // True when this tick has been run before -- that is, when its index is at or
    // below the highest index this session has ever run. It is the session's own
    // bookkeeping, not a flag the caller passes, so no caller can forget to set
    // it: Restore() moves the tick index backwards, the next Tick() is therefore
    // a re-simulation, and the session knows that without being told.
    //
    // WHAT AN OBSERVER MUST DO WITH IT. A recorder OVERWRITES rather than
    // appends. A combo judge whose own history is now stale must say so rather
    // than report a verdict about a timeline that no longer happened. Anything
    // player-visible and non-deterministic -- a hit sound, a particle, a screen
    // shake, a haptic -- must be SUPPRESSED, which is the classic rollback bug
    // ISession.h names in the same words ("the same hit sound plays eight
    // times").
    bool resimulated = false;
};

// The observer seam.
//
// WHY OBSERVATION RATHER THAN THE SESSION OWNING A RECORDER. The session could
// hold a `ReplayRecorder*` and a `ComboWatcher*` and call them; it is fewer
// lines. It is refused for four reasons, and the first is the structural one:
//
//   1. The session would then KNOW ABOUT THE REPLAY FORMAT AND THE PROVER. Its
//      header would include Replay.h and ProverAdapter.h, the tick path would
//      link the analysis, and the one function that must stay a thin wrapper
//      around a pure kernel call would have acquired two dependencies with
//      opinions. As an observer list it knows about neither: the recorder and
//      the judge depend on the session, and the session depends on nothing.
//   2. NONE OF THE WATCHERS IS PRIVILEGED, and there are already four -- the
//      recorder, the combo judge, the replay verifier, and a test's tick log
//      (which tests/test_ground_truth.cpp and tests/test_gap_extent.cpp each
//      hand-rolled, because there was no seam). Owning a recorder makes one of
//      them special and the other three second-class.
//   3. RECORDING WOULD BECOME COMPULSORY. A session with a recorder field has a
//      null check in the tick path and a default answer to "am I recording",
//      and neither is what a determinism stress harness wants when it runs
//      1617 ticks (ADR-003) with nothing watching.
//   4. THE COST STAYS VISIBLE AT THE CALL SITE. `AddObserver(&recorder)` in the
//      host says that this session pays for a recorder; a constructor argument
//      hides it, and hidden per-tick costs are how a 60 Hz loop stops being one.
//
// CONTRACT:
//   * Observers are notified in REGISTRATION ORDER, after Simulate has returned
//     and after the session's own tick index has advanced. The order is part of
//     the contract so that two observers with a shared assumption can rely on
//     it, but no observer in this module depends on another.
//   * An observer MUST NOT call Tick(), Begin(), Restore(), AddObserver() or
//     RemoveObserver() from inside OnTick. The session does not guard against
//     re-entrancy and will not grow a guard: an observer that ticks the session
//     it is observing is a bug with an obvious fix, and a guard would turn it
//     into a silently reordered tick stream.
//   * OnTick must not throw. This is the 60 Hz path.
class ITickObserver {
public:
    virtual ~ITickObserver() = default;
    virtual void OnTick(const TickView& view) = 0;

protected:
    ITickObserver() = default;
};

// Fixed capacity, so registration allocates nothing and the tick path iterates a
// small array rather than chasing a vector. Four is the number of observers this
// module actually defines; eight is headroom for a host's own.
inline constexpr int kMaxTickObservers = 8;

// --- The session ------------------------------------------------------------

class FightSession {
public:
    FightSession();

    FightSession(const FightSession&)            = delete;
    FightSession& operator=(const FightSession&) = delete;

    // --- Starting and restarting ---------------------------------------------

    // Put the match at its opening position and set the tick index to 0.
    //
    // Calls ResetMatch (which memsets the state, so no field survives a Begin)
    // and then applies MatchStart::startPosX. Returns false with `error` filled
    // in when ValidateSetup refuses; the session is then NOT started and Tick()
    // does nothing.
    //
    // Begin on an already-started session is a legal RESTART: the state, the
    // tick index and the re-simulation high-water mark all reset. OBSERVERS ARE
    // KEPT, deliberately -- a training host that rebinds its recorder and its
    // combo judge on every reset would drop one of them eventually, and every
    // observer in this module has its own Begin/Reset for its own state. A
    // restart is not a teardown.
    bool Begin(const FightSetup& setup, std::string& error);

    bool Started() const;

    // --- Advancing ------------------------------------------------------------

    // One tick, with inputs PULLED from the bound sources.
    //
    // Each player's source is asked At(CurrentTick()) -- the same question a
    // seek or a rollback re-simulation would ask, which is the entire reason
    // IInputSource is indexed by tick. A player with no source bound, or whose
    // source authors nothing at this tick, contributes NEUTRAL (bits 0).
    //
    // NEUTRAL, NEVER "REPEAT THE LAST TICK". Repeating would make the input a
    // function of the path taken through the match rather than of the tick
    // index, so re-simulating tick 900 after a different history would feed it
    // different bits -- the exact failure the tick-indexed interface exists to
    // rule out. It also has a gameplay meaning nobody would choose: a
    // demonstration that ends mid-combo would leave the button jammed down
    // rather than handing control back.
    //
    // Does nothing when !Started().
    void Tick();

    // One tick, with inputs supplied directly. This is what Tick() calls, and it
    // is the entry point for a rollback host driving from ISession's Advance
    // event, where the inputs arrive in the event rather than from a local
    // source. Both overloads produce the same TickView, so a recorder records
    // the bits that actually went in either way.
    void Tick(const cse::kernel::InputPair& inputs);

    // --- Reading -------------------------------------------------------------

    // The authoritative state. Const on purpose and const forever: the one-way
    // flow ARCHITECTURE.md 4.8 asks for ("enforce it with the type system") is
    // worth more here than any convenience a mutable accessor would buy.
    const cse::kernel::GameState& State() const;

    // The data this match is being fought with. Valid only while the caller's
    // MatchBuild is alive -- the session borrows it (see FightSetup::data).
    const cse::kernel::MatchData& Data() const;

    // The index of the tick that will run NEXT. Equal to State().tick, restated
    // as a method because that equality is a fact worth being able to assert.
    // After Begin it is 0; after one Tick it is 1 and the tick that ran was 0.
    std::uint32_t CurrentTick() const;

    // The highest tick index this session has ever run, plus one. Only ever
    // moves forward, including across a Restore, which is what makes
    // TickView::resimulated computable without the caller saying anything.
    std::uint32_t HighWaterTick() const;

    // FNV-1a over the state's bytes: cse::kernel::Checksum, forwarded. The
    // desync checksum from ADR-002 CHOICE C and the replay checkpoint value are
    // THE SAME NUMBER, and they must stay the same number -- a replay that
    // verified against a different hash than the network compares would be a
    // second definition of "the same state".
    std::uint32_t Checksum() const;

    // --- Snapshot and restore, for a rollback host ---------------------------
    //
    // Not needed by training mode and present anyway, because the module must not
    // be a dead end for Net/ISession.h: an Advance/Save/Load event stream needs
    // exactly these two. Both are a memcpy of an 80-byte POD (D4), so neither
    // allocates and neither can fail.
    //
    // Restore moves the tick index BACKWARDS to the restored state's own tick.
    // It does not touch HighWaterTick, so every tick run afterwards up to that
    // mark is correctly reported as resimulated. Restoring a state that was
    // produced by a DIFFERENT MatchData is undefined and is the caller's
    // problem -- the handshake (ARCHITECTURE.md 4.8) is what proves it did not
    // happen, and re-checking it here every 8 ticks would be the wrong place.
    void Snapshot(cse::kernel::GameState& out) const;
    void Restore(const cse::kernel::GameState& in);

    // --- Input sources -------------------------------------------------------

    // BORROWED. The session never owns, copies or deletes a source. A source
    // must outlive the session or be unbound first; null means neutral.
    // `player` outside [0, 2) is ignored.
    void                SetInputSource(int player, const IInputSource* source);
    const IInputSource* InputSourceFor(int player) const;

    // --- Observers ------------------------------------------------------------

    // False when the list is full (kMaxTickObservers) or `observer` is null or
    // already registered. Registering twice is refused rather than allowed,
    // because an observer notified twice per tick double-counts every hit.
    bool AddObserver(ITickObserver* observer);
    // False when it was not registered. Remaining observers keep their relative
    // order, so removal cannot silently reorder the notification sequence.
    bool RemoveObserver(ITickObserver* observer);
    void ClearObservers();
    int  ObserverCount() const;

private:
    cse::kernel::GameState        state_{};
    const cse::kernel::MatchData* data_          = nullptr;
    bool                          started_       = false;
    std::uint32_t                 highWaterTick_ = 0;

    const IInputSource* sources_[2]                 = { nullptr, nullptr };
    ITickObserver*      observers_[kMaxTickObservers] = {};
    int                 observerCount_             = 0;
};

// --- The tool-assisted player's rehearsal -----------------------------------
//
// THE TWO-PHASE SHAPE, AND WHY IT IS TWO PHASES.
//
// tests/test_ground_truth.cpp performs the prover's printed loop with a
// CLOSED-LOOP driver: it presses the button of whatever move the witness says
// comes next, and advances its cursor when the attacker actually ENTERS that
// move. That works, and it is the right way to turn a witness into inputs --
// but a closed-loop driver is not a pure function of a tick index. Its cursor
// depends on what the simulation did, so a rollback that re-ran a tick would ask
// it a question it had already answered and get a different reply, and a replay
// of the demonstration would need the driver rather than the inputs.
//
// So the demonstration is REHEARSED and then PERFORMED:
//
//   REHEARSE  BuildDemonstration runs a PRIVATE FightSession, headlessly, with
//             the closed-loop driver, starting from a copy of the state handed
//             in. It costs microseconds (a 160-tick loop is 160 pure integer
//             ticks) and reads no clock, so it is safe to run on the frame the
//             playtester presses the button.
//   PERFORM   what comes back is a FIXED LIST of per-tick bits. Wrapped in a
//             ScriptedInputSource it is a pure function of tick index, so the
//             performance is replayable, rollback-safe, and recordable by the
//             same recorder that records a human.
//
// The closed loop is thereby confined to a rehearsal that is thrown away, and
// the thing the match actually consumes is data.

struct DemonstrationRequest {
    // The state to rehearse FROM -- normally FightSession::State() at the tick
    // the playtester pressed Demonstrate. Copied, never modified. Null is an
    // error. It is a GameState rather than a MatchStart because "show me this
    // combo from where I am standing" is the request; a host that wants it from
    // neutral calls Begin() first and passes the state that produces.
    const cse::kernel::GameState* from = nullptr;

    // Borrowed for the duration of the call only. Null is an error.
    const cse::kernel::MatchData* data = nullptr;

    // Which slot performs the combo. The other slot is fed `defenderInput` on
    // every tick -- zero for the silent training dummy the ground-truth file's
    // own recipe prescribes ("input: zero on every tick").
    int                attackerSlot  = 0;
    cse::kernel::Input defenderInput{};

    // The witness, as KERNEL move ids (direct indices into
    // FighterData::moves -- MoveIndexMap::KernelMoveIdOf turns a prover
    // MoveIndex into one). `prefix` then `loop`, concatenated, with `loopStart`
    // the index at which the loop begins; past the end the cursor returns to
    // `loopStart`, which is what makes the loop repeat.
    //
    // Kernel ids and not character move ids, because this function has the
    // MatchData and not the CharacterData, and because the button it presses is
    // read straight out of MoveDef::button. A move whose button is 0 cannot be
    // asked for at all and is refused by name rather than pressed as nothing.
    std::vector<std::uint16_t> moveIds;
    std::size_t                loopStart = 0;

    // How many full turns of the loop to perform before handing control back.
    // One turn is enough to show the shape; a playtester validating an infinite
    // wants several. Zero means "perform the prefix and stop".
    std::uint32_t turns = 1;

    // Hard budget. The rehearsal stops here whether or not the witness finished,
    // and reports where it stalled -- which is the useful failure: a witness the
    // engine cannot perform is the OTHER publishable outcome
    // (ARCHITECTURE.md 5.5 item 4), not a bug to retry.
    std::uint32_t maxTicks = 600;

    // The absolute tick the produced trace begins on, i.e. the session's
    // CurrentTick() at the moment Demonstrate was pressed. Carried through to
    // Demonstration::firstTick so the caller can build the ScriptedInputSource
    // without re-deriving it.
    std::uint32_t firstTick = 0;
};

struct Demonstration {
    // One entry per tick, for the ATTACKER only. The defender's bits are the
    // request's `defenderInput` and are not stored -- a training dummy's input
    // is a constant, and storing 600 copies of it would invite a caller to think
    // it varies.
    std::vector<cse::kernel::Input> inputs;
    std::uint32_t                   firstTick = 0;

    // True when every move in the witness was entered and `turns` full turns of
    // the loop were performed inside the budget.
    bool complete = false;

    // How far along `moveIds` the rehearsal got, and how many full turns it
    // completed. Both are what a HUD shows and what a failure message quotes.
    std::size_t   reachedIndex = 0;
    std::uint32_t turnsDone    = 0;

    // The tick, relative to firstTick, at which the cursor stopped advancing.
    // Meaningful only when !complete.
    std::uint32_t stalledAt = 0;

    // Empty if and only if `complete`. Names the move the rehearsal was waiting
    // for and what the attacker was doing instead, because "the demonstration
    // did not work" is not a thing a playtester can act on.
    std::string error;
};

// Rehearse. Returns `out.complete`.
//
// THE ALGORITHM, WRITTEN DOWN BECAUSE THREE PEOPLE WOULD OTHERWISE WRITE THREE:
//
//   cursor = 0
//   each tick, up to maxTicks:
//     bits    = MoveAt(data.p[attackerSlot], moveIds[cursor])->button
//     run one tick with (bits, defenderInput)
//     if attacker.moveId == moveIds[cursor] AND attacker.moveFrame == 0:
//         cursor = (cursor + 1 < moveIds.size()) ? cursor + 1 : loopStart
//         (a wrap past the end counts one completed turn)
//     record bits
//
// `moveFrame == 0` AND NOT A CHANGE OF moveId. The witness for an infinite is a
// move cancelling into ITSELF, so the id never changes and a transition detector
// would see nothing happen at all and hold one button forever. The frame counter
// is what resets, so it is what says a move began. This is the same trap
// ComboWatcher.h documents at length and the same one test_ground_truth.cpp's
// Driver had to avoid; it has already been fallen into on this project.
//
// A stall is therefore visible rather than silent: the cursor stops advancing,
// the same button is held, and `stalledAt` names the tick.
bool BuildDemonstration(const DemonstrationRequest& request, Demonstration& out);

} // namespace cse::game
