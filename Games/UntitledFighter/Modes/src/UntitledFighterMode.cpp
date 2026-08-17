#include "UntitledFighterMode.h"

#include "cse/kernel/Combat.h"

#include <cstddef>
#include <string>
#include <utility>

namespace untitledfighter {

namespace {

    // --- Who plays, and who stands there ------------------------------------
    //
    // Slot 0 is the human and slot 1 is the dummy, fixed rather than
    // configurable. Everything downstream keys off it -- the ComboWatcher judges
    // ONE attacker (its header explains why judging both in one object makes
    // "the combo" ambiguous), the demonstration rehearses for ONE attacker, and
    // the corner setup below puts the OTHER one against the wall. A swap would
    // have to move all four of those together, and nothing has asked for it.
    constexpr int kPlayerSlot = 0;
    constexpr int kDummySlot  = 1;

    // --- The characters this mode can load -----------------------------------
    //
    // TWO, and the second one is not padding. The Demonstrate key performs the
    // decision procedure's PRINTED LOOP, and a character with no infinite has no
    // loop to print -- so on fighter_a alone the whole tool-assisted-player path
    // would ship unexercisable, with nothing on screen but an honest apology.
    // fighter_a_infinite exists precisely to be the other case: its entire
    // deliberate bug is `stand_lp -> stand_lp, delay 2, on hit`, which is a
    // witness one button long.
    //
    // Having both under one key is what makes the pair of honest answers
    // COMPARABLE by a playtester rather than only by a test: a certified-safe
    // character in which a combo can still run forever, and a certified-infinite
    // one whose printed loop the game performs perfectly on demand.
    //
    // Paths are relative to the host's content root and are resolved through
    // CseData's sandbox -- LoadCharacterFile refuses absolute paths, drive/UNC
    // roots and any ".." component lexically, before touching the filesystem.
    // They are constants here rather than something a player types, and they go
    // through the sandbox anyway, because the rule in docs/MAINTENANCE.md has no
    // exceptions and a constant today is a config field tomorrow.
    struct ShippedCharacter {
        const char* file;
    };
    const ShippedCharacter kCharacters[] = {
        { "Characters/fighter_a.json" },
        { "Characters/fighter_a_infinite.json" },
    };
    constexpr int kCharacterCount =
        static_cast<int>(sizeof(kCharacters) / sizeof(kCharacters[0]));

    // --- The binding table ---------------------------------------------------
    //
    // THE SCHEMA CARRIES NO INPUT NOTATION. MatchBuilder.h says so at length:
    // move ids like "stand_lp" carry the information only in the English of
    // their spelling, and reading a button out of a string suffix is exactly the
    // heuristic over a foreign format ARCHITECTURE.md D7 rejects. So the binding
    // comes from the caller, and this is the caller.
    //
    // THE KERNEL BIT IS AN IDENTIFIER AND NOTHING MORE. cse::kernel::kInputLP is
    // not "the light punch button" to the simulation -- StepAttack only ever asks
    // whether every bit of MoveDef::button is held (Combat.cpp), so which bit
    // pairs with which move is arbitrary and only DISTINCTNESS matters. They are
    // handed out in order below, and the names are not a claim.
    //
    // WHY SIX SINGLE BITS AND NO DIRECTION MODIFIERS. The obvious table is
    // "punch" and "down plus punch", and it cannot work here in the direction a
    // fighting game wants. StepAttack scans the move table in FILE ORDER and
    // takes the first move all of whose bits are held, so a plain mask that
    // appears EARLIER always beats a direction-modified superset that appears
    // later -- and fighter_a authors every standing normal before its crouching
    // counterpart. Binding `crouch_lp` to DOWN+LP would produce a key that
    // silently starts `stand_lp` forever. Six disjoint single bits is the table
    // that has no shadow in it at all, on either shipped character, and the
    // BindingRow::shadowed column exists so that a future table which does grow
    // one says so on screen instead of producing a dead key.
    //
    // WHICH SIX, and each is chosen for what the mode is for:
    //   stand_lp   the chain opener, 3f, and the infinite character's whole loop
    //   stand_mp   what a light chains into
    //   stand_hp   what a medium chains into, and a cancel source for the specials
    //   stand_lk   a second light, so a chain can be varied
    //   stand_hk   the LAUNCHER: its cancel into air_mp is one of the two edges
    //              into the air, and the only one reachable from the ground here
    //   air_mp     THE SELF-LOOP. `air_mp -> air_mp` is the one cycle in
    //              fighter_a's usable graph the cancel system performs end to
    //              end, and it is startable from neutral because the kernel does
    //              not gate a move on being airborne (MatchBuilder reports
    //              `move.stance` as a KernelPermits loss). Holding this one key
    //              is the shortest route a playtester has to a combo that does
    //              not stop on a character the tool certifies TERMINATING.
    // SIX BUTTONS, EIGHTEEN MOVES. Each key is one attack button, and which of
    // the three variants it starts is decided by the fighter's STATE rather than
    // by a different key -- airborne gives the air normal, holding down on the
    // ground gives the crouching one, otherwise the standing one.
    //
    // THIS IS ONLY POSSIBLE BECAUSE THE KERNEL NOW ENFORCES STANCE, and the
    // shape of the old table is the evidence for what it was like before. It
    // bound six keys to six moves, one of which was `air_mp` on the O key,
    // startable from the floor -- and the comment beside it said so, calling it
    // "the shortest route a playtester has to a combo". It was also the shortest
    // route to a nonsense one: holding O on the ground looped air_mp into itself
    // for 76 hits against a character the prover certifies TERMINATING with a
    // worst case of 21, which is the analysis and the game disagreeing in the
    // most visible way available.
    //
    // The old table also could not have been written this way. Its comment
    // explains at length why "punch" and "down plus punch" cannot work: StepAttack
    // scans in FILE ORDER and takes the first move whose bits are all held, so an
    // earlier plain mask always beats a later direction-modified superset, and
    // fighter_a authors every standing normal before its crouching counterpart.
    // That reasoning was correct and is now moot, because the disambiguation is
    // not a bigger mask -- it is StanceAllows refusing the two variants that do
    // not match, leaving exactly one. No most-specific-wins matcher was needed
    // after all; ADR-006 section 8 expected one.
    //
    // A character missing a variant simply gets no binding for it (Find returns
    // its documented 0 sentinel) rather than a wrong one.
    struct MoveKey {
        const char*   action;
        int           key;
        int           padButton;   // -1 for none
        const char*   label;
        const char*   name;        // what the button IS, for the HUD
        std::uint16_t button;
        const char*   standing;
        const char*   crouching;
        const char*   air;
    };

    const MoveKey kMoveKeys[] = {
        { "Fight.LP", GLFW_KEY_U, GLFW_GAMEPAD_BUTTON_X,
          "U", "LP", cse::kernel::kInputLP, "stand_lp", "crouch_lp", "air_lp" },
        { "Fight.MP", GLFW_KEY_I, GLFW_GAMEPAD_BUTTON_Y,
          "I", "MP", cse::kernel::kInputMP, "stand_mp", "crouch_mp", "air_mp" },
        { "Fight.HP", GLFW_KEY_O, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
          "O", "HP", cse::kernel::kInputHP, "stand_hp", "crouch_hp", "air_hp" },
        { "Fight.LK", GLFW_KEY_J, GLFW_GAMEPAD_BUTTON_A,
          "J", "LK", cse::kernel::kInputLK, "stand_lk", "crouch_lk", "air_lk" },
        { "Fight.MK", GLFW_KEY_K, GLFW_GAMEPAD_BUTTON_B,
          "K", "MK", cse::kernel::kInputMK, "stand_mk", "crouch_mk", "air_mk" },
        { "Fight.HK", GLFW_KEY_L, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,
          "L", "HK", cse::kernel::kInputHK, "stand_hk", "crouch_hk", "air_hk" },
    };

    // The FOUR direction bits stepFighter reads. kInputLeft and kInputRight set
    // velX and kInputUp jumps (Simulate.cpp), as they always did.
    //
    // kInputDown USED TO BE DELIBERATELY UNBOUND, and the comment here said why:
    // "nothing in the kernel reads it and no move in either shipped character
    // asks for it, so a key for it would be a key that does nothing, which is the
    // failure mode this project keeps naming." That was true and is no longer.
    // The P2 expansion made Down do two things a player can see -- it sets
    // Fighter::crouching, which selects the crouching variant of every attack
    // button, and combined with holding back it makes the guard LOW, which is the
    // only way to block a sweep. Binding it is now required rather than allowed.
    const MoveKey kDirectionKeys[] = {
        { "Fight.Left",  GLFW_KEY_A, GLFW_GAMEPAD_BUTTON_DPAD_LEFT,
          "A", "", cse::kernel::kInputLeft,  "", "", "" },
        { "Fight.Right", GLFW_KEY_D, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
          "D", "", cse::kernel::kInputRight, "", "", "" },
        { "Fight.Up",    GLFW_KEY_W, GLFW_GAMEPAD_BUTTON_DPAD_UP,
          "W", "", cse::kernel::kInputUp,    "", "", "" },
        { "Fight.Down",  GLFW_KEY_S, GLFW_GAMEPAD_BUTTON_DPAD_DOWN,
          "S", "", cse::kernel::kInputDown,  "", "", "" },
    };

    // --- The training controls -----------------------------------------------
    constexpr const char* kActPause = "Fight.Pause";
    constexpr const char* kActStep  = "Fight.Step";
    constexpr const char* kActSlow  = "Fight.Slow";
    constexpr const char* kActReset = "Fight.Reset";
    constexpr const char* kActDemo  = "Fight.Demonstrate";
    constexpr const char* kActSwap  = "Fight.NextCharacter";

    struct ControlKey {
        const char* action;
        int         key;
        int         padButton;   // -1 for none
    };
    const ControlKey kControlKeys[] = {
        { kActPause, GLFW_KEY_SPACE,  GLFW_GAMEPAD_BUTTON_START },
        { kActStep,  GLFW_KEY_PERIOD, -1 },
        { kActSlow,  GLFW_KEY_COMMA,  -1 },
        { kActReset, GLFW_KEY_R,      -1 },
        { kActDemo,  GLFW_KEY_TAB,    -1 },
        { kActSwap,  GLFW_KEY_C,      -1 },
    };

    // 1 -> 1/2 -> 1/4 -> 1/8 -> 1. Integer division of the host's fixed steps,
    // so slow motion changes NOTHING about a tick: the same ticks run, in the
    // same order, with the same inputs, just further apart in wall time.
    int nextSlowDivisor(int current) {
        return current >= 8 ? 1 : current * 2;
    }

    // How many turns of the loop a demonstration performs.
    //
    // MEASURED AGAINST WALL TIME AND AGAINST THE TWO BUDGETS IT SPENDS, because
    // the number that was here (3) was none of those. fighter_a_infinite's
    // printed loop is `stand_lp -> stand_lp, delay 2` against a 3-frame startup,
    // so the kernel takes that cancel at moveFrame 5 and ONE TURN IS FIVE TICKS.
    // Three turns is 15 ticks -- a quarter of a second, over before a playtester
    // has finished reading the chip that says it started, and the mode's own
    // instruction to watch it repeat reads as a flicker.
    //
    // 24 is chosen against the two things that bound it, and it is comfortably
    // inside both:
    //
    //   THE REHEARSAL BUDGET. DemonstrationRequest::maxTicks defaults to 600 and
    //   this mode does not override it. 24 turns is ~120 ticks plus the witness's
    //   prefix -- a fifth of the budget, so a character whose loop is four times
    //   this one's period still rehearses to completion.
    //
    //   THE HEALTH BAR. The loop move deals 30 and ResetMatch opens the defender
    //   at 1000, so 24 turns is 720 damage: the demonstration finishes with the
    //   bar still able to express a hit. Past ~33 turns the damage readout would
    //   silently stop climbing against Fighter::health's clamp at zero while the
    //   fight went on, which is the one thing a demonstration must not teach.
    //
    // Two seconds of screen time, and the repetition is the claim being made.
    constexpr std::uint32_t kDemoTurns = 24;

    // The body-to-body gap the match opens at, in the kernel's pixels.
    //
    // ResetMatch's own opening is -100 / +100 px, a 174 px gap further than
    // anything either character reaches -- correct as a neutral default and
    // useless as a training position, because nothing can connect from it. Eight
    // pixels is comfortably inside the shortest authored reach in either file
    // (0.24 reach units, i.e. 24 px), so every bound move connects and no verdict
    // on screen can turn on spacing the playtester did not choose.
    constexpr std::int32_t kTrainingGapPx = 8;

    const glm::vec4 kBackdrop{ 0.045f, 0.05f, 0.065f, 1.0f };

    // Whether a bound move can be started FROM NEUTRAL at all.
    //
    // The rule is StepAttack's, restated over the BUILT data rather than over the
    // authoring intent: the scan runs slot 1 upward and takes the first move all
    // of whose bits are held, so an earlier slot whose mask is a SUBSET of this
    // one's wins every time this one's bits are held. The move remains reachable
    // by a CANCEL -- FindCancel scans the cancel table, not the move table -- so
    // "shadowed" means cancel-only and not unreachable, and BindingRow says so.
    // The fighter states a stance condition admits, as a 3-bit set.
    //
    // This exists because shadowing is no longer a question about buttons alone.
    // Since the P2 expansion the kernel enforces stance (Combat.cpp StanceAllows),
    // so `stand_lp` and `crouch_lp` can share one button WITHOUT either shadowing
    // the other -- a fighter is in exactly one stance and only one of them can
    // ever match. Comparing masks alone would report six false shadows on a
    // correctly authored 6/6/6 character, and a diagnostic that fires on correct
    // data is how a diagnostic gets deleted; this repository has already recorded
    // that happening once, to assertion A04.
    constexpr std::uint8_t kStateStanding  = 1u << 0;
    constexpr std::uint8_t kStateCrouching = 1u << 1;
    constexpr std::uint8_t kStateAir       = 1u << 2;

    std::uint8_t statesAdmittedBy(std::uint8_t stance) {
        switch (stance) {
            case cse::kernel::kStanceStanding:  return kStateStanding;
            case cse::kernel::kStanceCrouching: return kStateCrouching;
            case cse::kernel::kStanceAir:       return kStateAir;
            case cse::kernel::kStanceGround:    return kStateStanding | kStateCrouching;
            default: break;   // kStanceAny, and anything unrecognised
        }
        return kStateStanding | kStateCrouching | kStateAir;
    }

    bool shadowedFromNeutral(const cse::kernel::FighterData& data,
                             std::uint16_t slot) {
        const std::int32_t index = static_cast<std::int32_t>(slot);
        if (index <= 0 || index >= data.moveCount) return false;
        const std::uint16_t mine = data.moves[index].button;
        if (mine == 0) return false;
        const std::uint8_t mineStates = statesAdmittedBy(data.moves[index].stance);
        for (std::int32_t i = 1; i < index; ++i) {
            const cse::kernel::MoveDef& other = data.moves[i];
            if (other.button == 0) continue;
            if ((mine & other.button) != other.button) continue;
            // A subset mask earlier in the file only wins where BOTH moves could
            // start, which is where their stances overlap.
            if ((statesAdmittedBy(other.stance) & mineStates) != 0) return true;
        }
        return false;
    }

} // namespace

// --- Entering and leaving ------------------------------------------------------

bool UntitledFighterMode::Enter(const MyCoreEngine::GameModeContext& ctx,
                                std::string& error) {
    ctx_ = ctx;
    modeTicks_    = 0;
    paused_       = false;
    slowDivisor_  = 1;
    slowCounter_  = 0;
    pendingSteps_ = 0;

    bindActions_();

    // characterIndex_ is NOT reset: the registry constructs a mode once and
    // enters it as many times as the player likes (GameMode.h), so coming back
    // returns to the character you were looking at rather than to the first one.

    // ENTER SUCCEEDS EVEN WHEN THE CONTENT DOES NOT LOAD, and that is a
    // deliberate reading of IGameMode::Enter's contract rather than laziness.
    // Returning false would bounce the player back to a menu with one truncated
    // line of status, and would make the whole chain this mode exercises --
    // menu verb, registry, fixed tick, UI pass -- unexercisable on any machine
    // where the character file did not stage. A mode that CAN still show
    // something honest should show it, and what is honest here is the loader's
    // own error, full width, next to a tick counter that proves the host is
    // calling us regardless.
    //
    // `error` is therefore left empty and this returns true on every path.
    (void)startCharacter_(characterIndex_);
    error.clear();
    return true;
}

void UntitledFighterMode::Exit() {
    // Safe after a FAILED Enter, which IGameMode::Exit requires: every member
    // touched here is default-constructible and was default-constructed, and
    // nothing below cares how far Enter got.
    //
    // OBSERVERS AND SOURCES ARE DROPPED BEFORE THE OBJECTS THEY POINT AT. The
    // session BORROWS both (FightSession.h), and although nothing dereferences
    // them outside Tick, a session left holding pointers into freed unique_ptrs
    // is a trap for the next person who adds a call between here and the
    // destructor.
    session_.ClearObservers();
    session_.SetInputSource(kPlayerSlot, nullptr);
    session_.SetInputSource(kDummySlot, nullptr);
    playerSource_.reset();
    demo_.reset();
    watcher_.reset();

    // The mode's own action names go with it. They live in the host's shared
    // InputMap, so leaving them bound would put "Fight.Attack1" on the J key for
    // the rest of the process, in a menu that has never heard of it.
    clearActions_();

    // The session is NOT re-Begun here. FightSession::Begin is itself the
    // restart (it memsets the state and resets the tick index), so leaving and
    // re-entering starts a fresh match through one code path rather than two.
    matchReady_    = false;
    analysisReady_ = false;
    setupError_.clear();
    analysisError_.clear();
    demoNote_.clear();
    fatal_.clear();
    bindings_.clear();
    hitAdvantage_ = LatchedAdvantage{};
    modeTicks_ = 0;
    ctx_ = MyCoreEngine::GameModeContext{};
}

// --- Bringing a character up ---------------------------------------------------

bool UntitledFighterMode::startCharacter_(int index) {
    // Everything the previous character owned goes first, in dependency order:
    // the session stops pointing at the watcher and the sources, then those are
    // freed, then the data they borrowed is replaced. A build_ reassigned while
    // the session still held a pointer into it would be a dangling MatchData on
    // the very next tick.
    session_.ClearObservers();
    session_.SetInputSource(kPlayerSlot, nullptr);
    session_.SetInputSource(kDummySlot, nullptr);
    playerSource_.reset();
    demo_.reset();
    watcher_.reset();

    matchReady_    = false;
    analysisReady_ = false;
    setupError_.clear();
    analysisError_.clear();
    demoNote_.clear();
    fatal_.clear();
    bindings_.clear();
    hitAdvantage_ = LatchedAdvantage{};
    paused_       = false;
    slowDivisor_  = 1;
    slowCounter_  = 0;
    pendingSteps_ = 0;

    character_ = cse::data::CharacterData{};
    build_     = cse::data::MatchBuild{};
    analysis_  = cse::data::ProverResult{};
    setup_     = cse::game::FightSetup{};

    // Wraps in both directions, so one key can walk the list forwards and a
    // negative index cannot land outside it.
    characterIndex_ = ((index % kCharacterCount) + kCharacterCount) % kCharacterCount;
    const std::string file = kCharacters[characterIndex_].file;

    // --- load ---------------------------------------------------------------
    cse::data::LoadReport  loadReport{};
    cse::data::LoadOptions loadOptions{};
    // expectedResources deliberately empty: assertion A03 is a CROSS-FILE rule
    // about a whole build's resource ORDER, and this mode loads one file at a
    // time. Naming an order here would be this file inventing a build-wide
    // contract. The check is SKIPPED rather than passed, and CharacterData.h
    // records that as a warning for exactly this reason.
    if (!cse::data::LoadCharacterFile(ctx_.contentRoot, file, loadOptions,
                                      character_, loadReport)) {
        setupError_ = "load " + file + ": " +
                      (loadReport.rule.empty() ? "" : loadReport.rule + ": ") +
                      loadReport.error;
        return false;
    }

    // --- build ---------------------------------------------------------------
    cse::data::BuildOptions options{};
    // The documented defaults, PASSED BY NAME rather than left at zero. Omitting
    // them gets the same numbers plus a warning nobody would draw; naming them
    // puts the decision where the next person can see there is one to make. They
    // are also fighter_a's own transcribed constants (13 px and 60 px), so the
    // body on screen is the body the file describes.
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    for (const MoveKey& key : kMoveKeys) {
        // All three variants get the SAME bit. StanceAllows is what picks one at
        // tick time; a character that has only some of them binds only those.
        for (const char* moveId : { key.standing, key.crouching, key.air }) {
            cse::data::MoveBinding binding{};
            binding.moveId = moveId;
            binding.button = key.button;
            options.bindings.push_back(binding);
        }
    }
    // BOTH SIDES GET THE SAME TABLE. A mirror match is the setup in which nothing
    // that happens can be blamed on the two sides having different data, and the
    // dummy having the same bindings is what makes "the dummy never acted" a fact
    // about the combo rather than about the binding table -- the same reason
    // tests/test_gap_extent.cpp's harness mirrors. It presses nothing today; that
    // is a decision about its INPUT and not about its data.
    if (!cse::data::BuildMatchData(character_, options, character_, options,
                                   build_)) {
        // Both sides are the same character, so a per-side report cannot
        // disagree; take whichever one actually says something.
        const std::string& first = build_.report[0].error;
        setupError_ = "build match: " +
                      (first.empty() ? build_.report[1].error : first);
        return false;
    }

    // The binding table AS BUILT, which is not the same thing as the table asked
    // for: a move this character does not have got no slot (Find returns 0, its
    // documented sentinel), and a mask an earlier slot shadows can never start
    // from neutral. Both are computed here, once, and drawn.
    // ONE ROW PER VARIANT, not per key, because `slot` and `shadowed` are facts
    // about a MOVE. Collapsing the three into one row would have to pick which
    // move's slot to show, and the one it did not pick is the one that would fail
    // silently. A variant the character does not have is skipped rather than
    // listed as slot 0, which reads as "idle".
    for (const MoveKey& key : kMoveKeys) {
        const char* const ids[3]    = { key.standing, key.crouching, key.air };
        const char* const states[3] = { "stand", "crouch", "air" };
        for (int v = 0; v < 3; ++v) {
            const std::uint16_t slot = build_.moves[kPlayerSlot].Find(ids[v]);
            if (slot == 0) continue;
            BindingRow row{};
            row.keyLabel = std::string(key.label) + " " + states[v];
            row.moveId   = ids[v];
            row.button   = key.button;
            row.slot     = slot;
            row.shadowed = shadowedFromNeutral(build_.data.p[kPlayerSlot], slot);
            bindings_.push_back(row);
        }
    }

    // --- analyse -------------------------------------------------------------
    //
    // RUN HERE AND NOT IN THE EDITOR'S PANEL, because the thing on screen has to
    // be the verdict for the character this match is being fought with. The
    // ComboWatcher borrows the result for its whole life and judges every string
    // against it, so a verdict computed somewhere else and pasted in would be the
    // "two sources of truth" this title spends its whole architecture avoiding.
    //
    // It costs a search of a few dozen configurations on these characters
    // (ADR-001 measured 63 / 79 / 10 against a limit of 200000), so it is
    // affordable at match start and is not attempted anywhere else.
    cse::data::ProverOptions proverOptions{};
    // expectedResources empty for the reason the loader's is, one seam over.
    cse::data::ProverReport proverReport{};
    analysisReady_ = cse::data::AnalyseCharacter(character_, proverOptions,
                                                 analysis_, proverReport);
    if (!analysisReady_) {
        analysis_      = cse::data::ProverResult{};
        analysisError_ = (proverReport.rule.empty() ? "" : proverReport.rule + ": ") +
                         proverReport.error;
    }

    // --- the judge -----------------------------------------------------------
    //
    // ONE watcher, on the player's slot. The dummy presses nothing and cannot
    // combo, and a second watcher would be a second panel with nothing in it.
    // Both pointers are borrowed and both outlive it: build_ and analysis_ are
    // members and this object is destroyed before either is replaced (see the
    // teardown at the top of this function).
    watcher_ = std::make_unique<cse::game::ComboWatcher>(
        kPlayerSlot, &build_.moves[kPlayerSlot],
        analysisReady_ ? &analysis_ : nullptr);

    // --- the opening position -------------------------------------------------
    //
    // THE DUMMY GOES IN THE CORNER, and that is not a flourish. The in-engine
    // decision procedure is corner-only by construction (ProverAdapter.h note 2):
    // it answers for a defender pinned against the wall with no room to walk
    // away. Every verdict this mode puts on screen is about that position, so the
    // match opens in it -- a training mode that started midscreen would be
    // quoting a corner verdict at a player standing somewhere the verdict says
    // nothing about.
    //
    // The corner itself is MEASURED off the kernel rather than typed in here; see
    // ProbeStageHalfWidthSub.
    stageHalfWidthSub_ = ProbeStageHalfWidthSub();
    setup_             = cse::game::FightSetup{};
    setup_.data        = &build_.data;   // BORROWED for the session's whole life
    setup_.start.startPosX[kDummySlot] = stageHalfWidthSub_;
    setup_.start.startPosX[kPlayerSlot] =
        stageHalfWidthSub_ - (2 * options.body.halfWidthSub +
                              kTrainingGapPx * cse::kernel::kSubUnitsPerPixel);

    std::string beginError;
    if (!session_.Begin(setup_, beginError)) {
        setupError_ = "begin match: " + beginError;
        watcher_.reset();
        return false;
    }

    session_.AddObserver(watcher_.get());
    local_.Reset(session_.CurrentTick());
    bindPlayerSource_();
    // Explicit, though it is also the default. "The dummy is fed neutral" is a
    // decision about how this mode trains, and a decision made by omission is a
    // decision nobody can find.
    session_.SetInputSource(kDummySlot, nullptr);

    matchReady_ = true;
    refreshDemoNote_();
    return true;
}

void UntitledFighterMode::resetMatch_() {
    if (!matchReady_) return;

    // Begin IS the restart: it memsets the state and resets the tick index, and
    // it KEEPS the observers on purpose (FightSession.h), so the watcher stays
    // registered and only its own history is cleared below.
    std::string beginError;
    if (!session_.Begin(setup_, beginError)) {
        setupError_ = "restart match: " + beginError;
        matchReady_ = false;
        return;
    }

    // THE DEMONSTRATION DOES NOT SURVIVE A RESET, and it must not: a
    // ScriptedInputSource authors an ABSOLUTE tick range, so a trace built for
    // ticks 400..560 would sit silently in front of the pad forever after the
    // tick index went back to 0, and then wake up 400 ticks later.
    demo_.reset();
    local_.Reset(session_.CurrentTick());
    bindPlayerSource_();
    if (watcher_) watcher_->Reset();
    // AND THE LATCHED MEASUREMENT, for the reason the demonstration goes: it
    // names an ABSOLUTE TICK, and Begin has just put the tick index back to zero.
    // Kept on screen it would be an answer about tick 431 of a match that no
    // longer has one, which is the exact failure the tick stamp exists to make
    // impossible -- so the stamp is not enough on its own and this line is the
    // rest of it.
    hitAdvantage_ = LatchedAdvantage{};

    paused_       = false;
    slowCounter_  = 0;
    pendingSteps_ = 0;
    fatal_.clear();
    refreshDemoNote_();
}

// --- Input ---------------------------------------------------------------------

void UntitledFighterMode::bindActions_() {
    if (!ctx_.app) return;
    MyCoreEngine::InputMap& map = ctx_.app->input();

    // clearAction FIRST on every name. bindKey APPENDS to a list, so entering
    // this mode a second time would otherwise bind J twice -- harmless today,
    // because bindings are OR'd, and exactly the kind of quiet growth that makes
    // a rebinding feature behave oddly later.
    for (const MoveKey& key : kMoveKeys) {
        map.clearAction(key.action);
        map.bindKey(key.action, key.key);
        if (key.padButton >= 0) map.bindGamepadButton(key.action, key.padButton);
    }
    for (const MoveKey& key : kDirectionKeys) {
        map.clearAction(key.action);
        map.bindKey(key.action, key.key);
        if (key.padButton >= 0) map.bindGamepadButton(key.action, key.padButton);
    }
    for (const ControlKey& key : kControlKeys) {
        map.clearAction(key.action);
        map.bindKey(key.action, key.key);
        if (key.padButton >= 0) map.bindGamepadButton(key.action, key.padButton);
    }
}

void UntitledFighterMode::clearActions_() {
    if (!ctx_.app) return;
    MyCoreEngine::InputMap& map = ctx_.app->input();
    for (const MoveKey& key : kMoveKeys)      map.clearAction(key.action);
    for (const MoveKey& key : kDirectionKeys) map.clearAction(key.action);
    for (const ControlKey& key : kControlKeys) map.clearAction(key.action);
}

cse::kernel::Input UntitledFighterMode::readPad_() const {
    cse::kernel::Input input{};
    if (!ctx_.app) return input;
    MyCoreEngine::InputMap& map = ctx_.app->input();

    // isDown, NOT consumePressed. The kernel takes buttons HELD -- StepAttack
    // scans for a move all of whose bits are held and starts it, with no notion
    // of an edge, because honest edge detection would need the previous tick's
    // buttons inside GameState and a rollback hands Simulate only the current
    // tick's (Combat.cpp). So the right read here is a LEVEL, and a level read is
    // phase-independent: it cannot be consumed by somebody else and cannot be
    // missed by a frame that ran no tick.
    //
    // Holding a button therefore REPEATS a move as soon as the last one recovers.
    // That is the kernel's documented gap, and on this character it is also the
    // route 32 of the 33 runaway cycles actually take (tests/test_gap_extent.cpp),
    // so a playtester holding one key is doing the most interesting thing
    // available to them.
    for (const MoveKey& key : kMoveKeys)
        if (map.isDown(key.action)) input.bits |= key.button;
    for (const MoveKey& key : kDirectionKeys)
        if (map.isDown(key.action)) input.bits |= key.button;
    return input;
}

void UntitledFighterMode::readControls_() {
    if (!ctx_.app) return;
    MyCoreEngine::InputMap& map = ctx_.app->input();

    // consumePressed, and it is read from the FIXED phase because every one of
    // these changes what the simulation does. wasPressed is scoped to a rendered
    // frame: above the fixed rate most frames run zero ticks and a stalled frame
    // runs several, so a fixed-tick reader of wasPressed misses most presses and
    // multiplies the rest (InputMap.h). consumePressed latches the press and
    // serves it to exactly one phase, which is what makes "step exactly one tick"
    // mean one tick at 144 Hz and at 30.

    // The character swap first, and it RETURNS: it tears the match down and
    // builds another, so every control below it would be acting on an object
    // that no longer exists. The presses it skips are dropped rather than
    // queued, which is the right answer for a frame on which the player changed
    // the subject.
    //
    // It loads a file and runs the decision procedure inside a fixed tick, which
    // is a hitch and is deliberate: it happens because a key was pressed, which
    // is a moment a player understands, and hiding it behind a loading state
    // would be a state machine bought with nothing.
    if (map.consumePressed(kActSwap)) {
        (void)startCharacter_(characterIndex_ + 1);
        return;
    }

    if (map.consumePressed(kActReset)) resetMatch_();

    // --- FROM HERE DOWN, EVERY CONTROL ACTS ON A MATCH THAT IS RUNNING --------
    //
    // C and R are above this line because they are the two that BRING ONE BACK:
    // the swap rebuilds from a character file and the reset re-Begins the
    // session, so both are meaningful with no match loaded and with a match that
    // has stopped. Everything below acts on a session that will not tick again.
    //
    // Pause, step and slow motion are decisions about WHEN Tick() is called, and
    // FixedTick returns before that call while `fatal_` is set -- so they change
    // a variable nothing reads and make a stopped match feel alive. TAB is worse
    // than cosmetic: it would rehearse a demonstration and install a
    // ScriptedInputSource authoring an absolute tick range into a session that
    // will never reach it, and the chip would read PERFORMING over a fight that
    // has stopped.
    //
    // The presses are DROPPED rather than queued, which is right for a frame on
    // which the player asked a stopped match to do something, and it is what
    // happens by construction: InputMap::clearPressLatches drops any latch
    // nothing consumed, so returning without consuming IS dropping.
    if (!matchReady_ || !fatal_.empty()) return;

    if (map.consumePressed(kActPause)) {
        paused_      = !paused_;
        slowCounter_ = 0;
    }
    if (map.consumePressed(kActStep)) {
        // A step is a request for ONE tick, and it implies pause: stepping while
        // the match is running is indistinguishable from not stepping at all.
        ++pendingSteps_;
        paused_ = true;
    }
    if (map.consumePressed(kActSlow)) {
        slowDivisor_ = nextSlowDivisor(slowDivisor_);
        slowCounter_ = 0;
        paused_      = false;
    }
    if (map.consumePressed(kActDemo)) startDemonstration_();
}

// --- The tool-assisted player ---------------------------------------------------

bool UntitledFighterMode::demoArmed_() const {
    return analysisReady_ &&
           analysis_.status == cse::data::ProverStatus::Infinite &&
           !analysis_.loop.empty();
}

void UntitledFighterMode::refreshDemoNote_() {
    if (!matchReady_) { demoNote_.clear(); return; }

    if (demoArmed_()) {
        demoNote_ = "performs the loop the decision procedure printed out of "
                    "this character file, " + std::to_string(kDemoTurns) +
                    " turns of it, and then HANDS CONTROL BACK mid-string so you "
                    "can try it yourself.";
        return;
    }

    // THE HONEST BRANCH, AND IT IS THE ONE fighter_a TAKES.
    //
    // A dead key with no explanation would be the worst available outcome here:
    // the playtester would conclude the feature is broken, when what has actually
    // happened is that the tool looked at this character and found nothing to
    // demonstrate. So the key says what the analysis said, in the analysis's own
    // vocabulary, and points at the character that does have a loop.
    if (!analysisReady_) {
        demoNote_ = "nothing to demonstrate: the analysis did not run for this "
                    "character. " + analysisError_;
        return;
    }

    demoNote_ = std::string("nothing to demonstrate: this character is ") +
                cse::data::ProverStatusName(analysis_.status) +
                ", so the decision procedure printed no loop to perform.";
    if (analysis_.status == cse::data::ProverStatus::Terminating) {
        if (analysis_.hasRanking) {
            demoNote_ += " It terminates on a ranking certificate -- see above "
                         "for what that certificate rests on, and whether this "
                         "kernel carries it.";
        } else {
            demoNote_ += std::string(" No ranking certificate: ") +
                         cse::data::RankingAbsenceName(analysis_.rankingAbsence) + ".";
        }
    }
    demoNote_ += "  [C] loads the character that does have one.";
}

bool UntitledFighterMode::demoInFlight_() const {
    if (!demo_) return false;
    return session_.CurrentTick() < demo_->FirstTick() + demo_->TickCount();
}

void UntitledFighterMode::startDemonstration_() {
    if (!matchReady_) return;

    // ONE DEMONSTRATION AT A TIME, AND THE SECOND PRESS IS REFUSED OUT LOUD.
    //
    // Pressing TAB again while one is running is the most natural thing in the
    // world -- nothing on screen said not to -- and every outcome of it was
    // wrong. The rehearsal would start from the MIDDLE of the combo the first one
    // is performing, which is not the position the witness was proved from; if it
    // then failed to rehearse, the early return below would overwrite demoNote_
    // with THE WITNESS DID NOT REHEARSE while the first demonstration was still
    // playing and the chip still read PERFORMING; and if it succeeded, it would
    // replace a ScriptedInputSource that the session is at that moment reading
    // through a raw pointer.
    //
    // So the press is refused and the refusal says why, which is the same
    // treatment every other honest "no" in this mode gets. R restarts the match
    // and clears the demonstration with it, which is the way out.
    if (demoInFlight_()) {
        const std::uint32_t end = demo_->FirstTick() + demo_->TickCount();
        demoNote_ = "ALREADY PERFORMING: " +
                    std::to_string(end - session_.CurrentTick()) +
                    " scripted tick(s) left. A second demonstration would be "
                    "rehearsed from the middle of this one, which is not the "
                    "position the witness was proved from -- so this press was "
                    "refused. It hands the pad back on its own; R stops it now.";
        return;
    }

    if (!demoArmed_()) { refreshDemoNote_(); return; }

    // THE REQUEST. The witness is `prefix` then `loop` (ProverAdapter.h), turned
    // from prover MoveIndex into kernel move ids through MoveIndexMap's own
    // function rather than by adding one at the call site.
    cse::game::DemonstrationRequest request{};
    // FROM WHERE THE PLAYER IS STANDING, which is what "show me this combo"
    // means. BuildDemonstration copies this state into a private session and
    // never modifies it.
    request.from         = &session_.State();
    request.data         = &session_.Data();
    request.attackerSlot = kPlayerSlot;
    // Zero on every tick: the silent dummy the ground-truth recipe prescribes,
    // and the same thing the dummy's absent input source produces during live
    // play -- so the rehearsal and the performance meet the same defender.
    request.defenderInput = cse::kernel::Input{};
    request.turns         = kDemoTurns;
    // THE TICK THE PLAYER PRESSED THE KEY. Carried through to the source so that
    // At(tick) stays a pure function of the session's own tick index and no
    // offset has to be stored beside it.
    request.firstTick = session_.CurrentTick();

    for (const cse::data::MoveIndex move : analysis_.prefix)
        request.moveIds.push_back(cse::data::MoveIndexMap::KernelMoveIdOf(move));
    request.loopStart = request.moveIds.size();
    for (const cse::data::MoveIndex move : analysis_.loop)
        request.moveIds.push_back(cse::data::MoveIndexMap::KernelMoveIdOf(move));

    cse::game::Demonstration rehearsal{};
    const bool complete = cse::game::BuildDemonstration(request, rehearsal);

    if (!complete) {
        // A WITNESS THE ENGINE CANNOT PERFORM IS A RESULT, NOT A BUG TO RETRY.
        // It is the other publishable outcome this whole apparatus exists to
        // produce, so it is reported with the numbers that make it actionable --
        // how far along the witness the rehearsal got and which tick it stopped
        // advancing on -- rather than swallowed and tried again.
        //
        // The partial trace is NOT installed. It is real data and performing it
        // would show the attacker holding one button for the whole budget, which
        // reads as the feature being broken rather than as the finding it is.
        demoNote_ = "THE WITNESS DID NOT REHEARSE. Reached move " +
                    std::to_string(rehearsal.reachedIndex + 1) + " of " +
                    std::to_string(request.moveIds.size()) + " after " +
                    std::to_string(rehearsal.turnsDone) + " turn(s); the cursor "
                    "stopped advancing " + std::to_string(rehearsal.stalledAt) +
                    " tick(s) in. " + rehearsal.error;
        return;
    }

    // PERFORMED AS DATA. What comes back is a fixed list of per-tick bits, so the
    // performance is a pure function of the tick index -- replayable, recordable
    // by the same recorder that records a human, and safe to re-simulate. The
    // closed-loop driver that produced it lived and died inside the rehearsal.
    //
    // UNBOUND BEFORE IT IS REPLACED, AND THAT ORDER IS THE POINT OF THESE FOUR
    // LINES. The session holds playerSource_, and playerSource_ holds a RAW
    // POINTER to *demo_ (FallbackInputSource borrows both children; InputSource.h
    // says so). Assigning to demo_ destroys whatever it held, so an assignment
    // with either of those still pointing at the old object leaves a dangling
    // primary behind a live binding. Nothing ticks between here and the rebind
    // today -- this runs inside readControls_, above FixedTick's own Tick() call
    // -- and the whole value of dropping the pointers first is that it stays true
    // when somebody adds a line, or an early return, in between.
    session_.SetInputSource(kPlayerSlot, nullptr);
    playerSource_.reset();
    demo_ = std::make_unique<cse::game::ScriptedInputSource>(
        std::move(rehearsal.inputs), rehearsal.firstTick, "DEMO");
    bindPlayerSource_();

    // Nothing to watch while paused, and the player just asked to watch.
    paused_       = false;
    pendingSteps_ = 0;

    demoNote_ = "PERFORMING: " + std::to_string(demo_->TickCount()) +
                " scripted tick(s) from tick " + std::to_string(demo_->FirstTick()) +
                ", " + std::to_string(rehearsal.turnsDone) +
                " turn(s) of the printed loop. When it runs out the pad takes "
                "over on the very next tick with nothing to reset.";
    if (demo_->Truncated())
        demoNote_ += "  (the trace was clamped at this module's tick cap.)";
}

void UntitledFighterMode::bindPlayerSource_() {
    // The composition that IS the Demonstrate feature: primary if it authors this
    // tick, otherwise the pad. A null primary means the pad answers everything,
    // which is the state before any demonstration and after every one -- so the
    // player's slot is wired exactly once and never rewired for gameplay reasons.
    playerSource_ =
        std::make_unique<cse::game::FallbackInputSource>(demo_.get(), &local_);
    session_.SetInputSource(kPlayerSlot, playerSource_.get());
}

// --- The tick -------------------------------------------------------------------

void UntitledFighterMode::FixedTick(float dt) {
    (void)dt;   // the session takes no dt at all: it is a tick index, not a clock
    ++modeTicks_;

    // BEFORE the match check, so the character key still works on a machine where
    // the content did not stage. A screen whose only escape is Escape teaches the
    // playtester nothing about the file that is missing. readControls_ carries
    // the same check INSIDE it, at the line below which every control needs a
    // running match -- being called early buys C and R their reach and buys the
    // rest of them nothing.
    readControls_();

    if (!matchReady_ || !fatal_.empty()) return;

    // --- whether a tick runs at all ------------------------------------------
    //
    // This is the whole of pause, slow motion and frame step. FightSession owns
    // no clock, so all three are decided here and none of them is visible to the
    // simulation: the same ticks run, in the same order, with the same inputs.
    bool run = false;
    if (pendingSteps_ > 0) {
        --pendingSteps_;
        run = true;
    } else if (!paused_) {
        if (++slowCounter_ >= slowDivisor_) {
            slowCounter_ = 0;
            run = true;
        }
    }
    if (!run) return;

    // --- LATCH, THEN TICK -----------------------------------------------------
    //
    // The pad is read and written down for tick T before the session runs tick T.
    // From here on `local_.At(T)` returns those bytes forever, so re-simulating
    // this tick -- for a rollback, a replay, a desync investigation -- feeds it
    // what the player actually pressed rather than what they are pressing at the
    // moment of the re-run. That property is the entire reason IInputSource is
    // indexed by an absolute tick, and this ordering is the only place a host can
    // get it wrong.
    //
    // Latching happens only when a tick actually runs, because latching is
    // MONOTONIC: Latch(t) is legal only for t == NextTick(), and a tick that does
    // not run does not advance either counter. Latching on a paused step would
    // put a value in the log for a tick nobody simulated.
    const std::uint32_t tick = session_.CurrentTick();
    if (!local_.Latch(tick, readPad_())) {
        // The header is explicit: a caller that gets false back has a sequencing
        // bug and must STOP THE MATCH rather than continue with a hole in the
        // input log. Continuing would leave a tick nobody can answer for, and
        // every downstream guarantee -- replay, rollback, the desync checksum --
        // rests on there being none.
        fatal_ = "input latch refused tick " + std::to_string(tick) +
                 " (the source expects " + std::to_string(local_.NextTick()) +
                 "). LatchedInputSource is monotonic and never rewrites the past, "
                 "so this mode stopped the match rather than run a tick whose "
                 "input nothing recorded. Press R to restart.";
        return;
    }

    // ONE TICK. The host's FixedTimestep caps at 8 steps per frame and then
    // ZEROES the accumulator, so a stalled frame silently DROPS the backlog
    // (FightSession.h disqualifies it from driving a session in general). For a
    // local training session with no opponent and no recording that is a hitch:
    // a dropped step is a step on which no tick ran, which is indistinguishable
    // from a frame of slow motion and cannot desync anything.
    //
    // IT STOPS BEING ACCEPTABLE the moment a replay is being recorded (the file
    // would claim a tick count the inputs do not account for), verified, or
    // driven over a network. At that point this call must drive its own
    // accumulator or take its ticks from ISession's Advance events -- and
    // nothing about this seam changes for it, only what this function does with
    // the call.
    session_.Tick();

    // The one thing measured off a tick rather than read off the state.
    latchHitAdvantage_();
}

void UntitledFighterMode::latchHitAdvantage_() {
    if (!watcher_ || watcher_->Stale()) return;
    // Tick() has run, so the tick that ran is one below the next one. Guarded
    // rather than assumed: an unsigned 0 - 1 is 4294967295, and a match that
    // somehow reached here without ticking would compare against it and latch.
    const std::uint32_t next = session_.CurrentTick();
    if (next == 0u) return;
    const std::uint32_t ran = next - 1u;

    // DID A HIT LAND ON THAT TICK -- ASKED OF THE JUDGE, NOT ANSWERED HERE.
    // ComboReport::lastHitTick is set by ComboWatcher signal 3, which is the
    // alreadyHitBits rule WITH the disjunct that covers a move ending and its
    // successor starting inside one StepAttack call. Re-deriving it here would be
    // a second implementation of the one signal this mode's whole verdict rests
    // on, and it is the implementation with the documented hole in it.
    //
    // Current() rather than Previous(): a tick on which a hit lands is a tick on
    // which the string is open, by ComboWatcher's own reading of signal 2 -- a
    // defender who became actionable and was hit anyway continues the string as a
    // GAP rather than ending it. `hits > 0` keeps a fresh report's zeroed
    // lastHitTick from matching tick 0.
    const cse::game::ComboReport& report = watcher_->Current();
    if (report.hits == 0 || report.lastHitTick != ran) return;

    hitAdvantage_.measured  = true;
    hitAdvantage_.tick      = ran;
    // The attacker's move as it stands at the END of the contact tick, which is
    // the move whose box connected: StepAttack runs before ResolveHits, so a move
    // that ended on this tick had no box for ResolveHits to test and cannot be
    // the one that landed.
    hitAdvantage_.moveId    = session_.State().p[kPlayerSlot].moveId;
    hitAdvantage_.advantage =
        FrameAdvantage(session_.Data(), session_.State(), kPlayerSlot);
}

void UntitledFighterMode::Update(float dt) {
    (void)dt;
    if (!ctx_.app) return;

    // Escape (and gamepad BACK) leaves the mode.
    //
    // THE MODE CONSUMES THIS, NOT THE HOST, and the host must be suppressing its
    // own handling of it while a mode owns the screen -- "Quit" is the same
    // action RunLoop closes the window on, so a mode that owned the screen
    // without the host standing down would find Back-to-menu quitting the game.
    // IGameMode::OwnsScreen documents that pairing; this is the half of it that
    // lives in the mode.
    //
    // It is read here rather than in FixedTick because leaving is presentation,
    // not simulation: it must not be able to land on a different tick depending
    // on how many fixed steps a frame happened to run. It is also the ONE action
    // this mode reads from the variable phase, which is what keeps it clear of
    // the fixed-phase controls -- a press is served to one phase only.
    if (ctx_.app->input().wasPressed("Quit")) requestExit();
}

// --- Drawing --------------------------------------------------------------------

FightHudModel UntitledFighterMode::hudModel_() const {
    FightHudModel model{};

    model.matchReady = matchReady_;
    model.character  = &character_;
    model.analysis   = analysisReady_ ? &analysis_ : nullptr;
    model.watcher    = watcher_.get();
    model.bindings   = &bindings_;

    model.setupError    = &setupError_;
    model.analysisError = &analysisError_;
    model.demoNote      = &demoNote_;
    model.fatal         = &fatal_;

    model.modeTicks   = modeTicks_;
    model.hostHz      = ctx_.app ? ctx_.app->fixedTimestepHz() : 0.0f;
    model.paused      = paused_;
    model.slowDivisor = slowDivisor_;
    model.playerSlot  = kPlayerSlot;
    model.demoArmed   = demoArmed_();
    // BY VALUE, and it is the only field here that is a measurement rather than a
    // reading. See FightHudModel::hitAdvantage and latchHitAdvantage_.
    model.hitAdvantage = hitAdvantage_;
    model.stageHalfWidthSub = stageHalfWidthSub_;

    if (matchReady_) {
        model.state    = &session_.State();
        model.data     = &session_.Data();
        model.names[0] = &build_.moves[0];
        model.names[1] = &build_.moves[1];
        model.build    = &build_.report[kPlayerSlot];
        model.tick      = session_.CurrentTick();
        model.highWater = session_.HighWaterTick();
        model.checksum  = session_.Checksum();

        // WHICH SOURCE IS SPEAKING, asked as the pure question the composition
        // exposes for exactly this. FallbackInputSource::Name() is always
        // "FALLBACK" on purpose -- returning whichever child spoke last would
        // need a mutable member written from a const method -- so a HUD that
        // wants DEMO or YOU asks Active(tick), which is pure and answers for any
        // tick including ones that have not run.
        if (playerSource_) {
            const cse::game::IInputSource* const active =
                playerSource_->Active(model.tick);
            model.speaking = active != nullptr ? active->Name() : nullptr;
        }
        // Gated on demoInFlight_() rather than on its own comparison, so the chip
        // that says how much script is left and the key that refuses a second
        // demonstration are answering one question. The subtraction cannot
        // underflow: being in flight IS `CurrentTick() < end`.
        if (demoInFlight_())
            model.demoRemaining =
                demo_->FirstTick() + demo_->TickCount() - model.tick;
    }
    return model;
}

void UntitledFighterMode::Draw(MyCoreEngine::Renderer2D& r2d, int widthPx,
                               int heightPx, float dt) {
    (void)dt;

    // Opaque, full screen. This mode owns the screen (OwnsScreen), and the 3D
    // pass still ran over whatever scene the host had loaded, so without this the
    // fight would be painted over a menu backdrop it has nothing to do with.
    r2d.DrawQuad({ 0.0f, 0.0f },
                 { static_cast<float>(widthPx), static_cast<float>(heightPx) },
                 kBackdrop, 0);

    if (matchReady_) {
        // --- THE WORLD PASS, BRACKETED BY HAND ------------------------------
        //
        // UIPass hands this callback a Renderer2D already in SCREEN mode
        // (BeginScreen around IGameMode::Draw, UIPass.cpp), which is right for a
        // HUD and is the wrong space for a fight: the boxes are in world units
        // with +Y up. So the screen frame is ENDED, a world frame is run, and a
        // fresh screen frame is begun for the readouts.
        //
        // This is safe and is not a trick. End() flushes and restores exactly the
        // GL state its Begin captured, and each Begin captures the state it finds
        // -- which, after the End above it, is the state UIPass's own Begin
        // captured. UIPass's closing End() therefore restores the same bits it
        // would have restored anyway. The cost is two extra flushes per frame.
        r2d.End();
        r2d.BeginWorld(FightCamera(session_.State(), widthPx, heightPx), widthPx,
                       heightPx);
        DrawFightWorld(r2d, session_.State(), session_.Data(), stageHalfWidthSub_);
        r2d.End();
        r2d.BeginScreen(widthPx, heightPx);
    }

    // A font that failed to bake leaves the host drawing nothing at all rather
    // than crashing -- Font is documented as safe-but-inert in that state, but
    // dereferencing a null one is not, and the host's own font load is allowed to
    // fail (it prints and carries on). The fight above is still drawn, so "the
    // mode is up but the font is missing" looks different from "nothing
    // happened".
    if (!ctx_.font) return;

    DrawFightHud(r2d, *ctx_.font, widthPx, heightPx, hudModel_());
}

} // namespace untitledfighter
