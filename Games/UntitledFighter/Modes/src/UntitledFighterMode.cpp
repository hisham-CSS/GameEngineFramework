#include "UntitledFighterMode.h"

#include "../src/core/PathSandbox.h"

#include "cse/game/Replay.h"   // HashMatchData: the content hash the offer carries (A4)
#include "cse/kernel/Combat.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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
    // The two of them: the session every match here is created with
    // (SessionDriver::WireConfig) and the bound AttachSession holds a padSlot
    // to. Not cse::kernel::kMaxFighters -- that is the state's capacity (8),
    // and a pad offered to slot 2 of a two-player session is a keyboard
    // offered to nobody.
    constexpr int kMatchPlayers = 2;

    // The word after the title on the HUD, per intent (ADR-022 D1). Here and
    // not in FightHud.cpp because the HUD reads and never decides: the mode
    // knows what it is, the screen says it.
    const char* ModeWord(ModeIntent intent) {
        switch (intent) {
            case ModeIntent::Replay:   return "REPLAY";
            case ModeIntent::Versus:   return "VERSUS";
            case ModeIntent::Training: break;
        }
        return "TRAINING";
    }

    // The control strip's line, per intent, and in the mode's words for the
    // same reason: the mode binds the keys and knows which are inert this
    // visit, so the screen reads the sentence and never decides it. Versus
    // lists the two keys a peer cannot feel; every other one restarts,
    // pauses or re-sources the match, which is a desync under a peer
    // (DETERMINISM.md T3), and readControls_ drops it. Replay runs the
    // training clock (ADR-022 D4) but both slots are the file's, so its
    // line keeps the clock keys and names the two that would re-source a
    // recording (TAB, V) and the one that changes meaning (C: the next
    // replay, not the next character).
    const char* ModeControls(ModeIntent intent) {
        switch (intent) {
            case ModeIntent::Versus:
                return "B overlay     ESC menu     --  pause, step, slow motion, reset, "
                       "the character swap, the stage position and Demonstrate are inert "
                       "while a peer runs the same match";
            case ModeIntent::Replay:
                return "SPACE pause     . step one tick     , slow motion     R restart the replay"
                       "     C next replay     B overlay     ESC menu     --  TAB and V are inert: "
                       "both slots are the recording's";
            case ModeIntent::Training: break;
        }
        return "SPACE pause     . step one tick     , slow motion     R reset"
               "     TAB demonstrate     C next character     V corner/midscreen"
               "     B overlay     ESC menu";
    }

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

    // --- The replays this mode can play (ROADMAP M2.5; ADR-022 D3) ------------
    //
    // ONE, and it is the catalogue's own `base` row: fighter_a against itself,
    // the verdict's combo performed from the corner bench, cooked by
    // UntitledFighterCatalogue and committed under the title's asset root
    // (Assets/UntitledFighter/Replays/, with its CREDITS.md). Same sandbox and
    // same constant-today-config-tomorrow reasoning as kCharacters above.
    //
    // A REPLAY NAMES EXACTLY ONE MatchData BY HASH (DETERMINISM.md S9), and
    // this mode reads the file against the hash of the match it just built --
    // so the shipped file is stale the moment fighter_a's frame data, the
    // binding table or the kernel changes, and the honest-error screen says so
    // with the command below in the sentence. The test that decodes it
    // (FightMode.AReplayDrivesBothSlotsAndTheTrainingClockStillWorks) goes red
    // first, with the same command.
    struct ShippedReplay {
        const char* file;
    };
    const ShippedReplay kReplays[] = {
        { "UntitledFighter/Replays/base.csrp" },
    };
    constexpr int kReplayCount =
        static_cast<int>(sizeof(kReplays) / sizeof(kReplays[0]));
    constexpr const char* kReplayRegenerate =
        " -- regenerate it: from build/bin/<Config>/ run `UntitledFighterCatalogue "
        "Exported/Characters <scratch dir>` and copy <scratch dir>/base.csrp to "
        "Games/UntitledFighter/Assets/UntitledFighter/Replays/ (its CREDITS.md says the same)";

    // The Versus lobby's file (ADR-022 D2), beside the title's menu and its
    // look, resolved through the same sandbox as the characters. The command
    // line overrides it (cse::data::ApplyVersusOverrides); the two peers on one
    // machine differ by exactly those overrides.
    constexpr const char* kVersusFile = "UntitledFighter/versus.json";

    // How long the lobby waits for the peer's offer, in seconds of the host's
    // fixed steps. tests/online_peer.cpp's wall-clock figure, kept so the two
    // reference flows give up at the same moment.
    constexpr float kLobbyTimeoutSeconds = 30.0f;

    // --- The desync tail's clocks, in fixed steps (ROADMAP M2.5) --------------
    //
    // tests/online_peer.cpp's three numbers, kept so the two reference flows
    // behave alike. The grace is how long the session keeps pumping after the
    // report so the peer receives the checksums that let it detect too; the
    // exchange grace is how many more pumps send our state after theirs has
    // arrived (the peer may have begun its tail a whole grace later); the cap
    // is when a peer that never answers stops holding the screen.
    constexpr int           kDesyncGraceSteps   = 30;
    constexpr int           kExchangeGraceSteps = 12;
    constexpr std::uint32_t kExchangeCapSteps   = 600;

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
    // SIX BUTTONS, NOT SIX MOVES. Each key is a STRENGTH, and the character
    // file's `stance` decides which move that strength starts from where the
    // fighter happens to be -- standing, crouching or airborne.
    //
    // IT USED TO BE ONE KEY PER MOVE, and the comment here justified it with a
    // premise the kernel has since made false: "air_mp is startable from
    // neutral because the kernel does not gate a move on being airborne".
    // ADR-006 added `MoveDef::stance` and StanceAllows gates exactly that, in
    // both the button scan and the cancel scan. So the old table spent its six
    // buttons on stand_lp/mp/hp, stand_lk, stand_hk-on-the-MK-bit and
    // air_mp-on-the-HK-bit: the button you pressed had no relationship to the
    // strength you got, twelve of fighter_a's eighteen normals were unreachable,
    // and there was no button left for a crouching attack. Found by playing it
    // (ROADMAP.md review point R0), not by a test -- every test passed, because
    // the kernel did exactly what this table told it to.
    //
    // A move id that the character does not have is skipped with a warning by
    // MatchBuilder, so naming all three stances per strength is safe for a
    // character that authors fewer.
    struct MoveKey {
        const char*   action;
        int           key;
        int           padButton;   // -1 for none
        const char*   label;
        std::uint16_t button;
        // Standing, crouching, air. Nullptr where the character has none.
        // ORDER IS NOT PRECEDENCE -- the kernel picks by stance, and two moves
        // on one button with overlapping stances is refused by the build rather
        // than resolved here.
        const char*   moves[3];
    };

    // PUNCHES ON THE TOP ROW, KICKS ON THE HOME ROW. Reported from play
    // (2026-08-21): "the keys u/i/o are supposed to be punches with j/k/l being
    // kicks - it is stated opposite currently". It was. The convention is the
    // arcade six-button layout read off a keyboard: the upper row is the upper
    // row of the cabinet, and a hand resting on the home row finds the kicks.
    //
    // The pad follows the same shape: the four face buttons are light and medium
    // of each, and the two heavies live on the bumpers, which is where every
    // six-button fighting game on a standard pad puts them.
    const MoveKey kMoveKeys[] = {
        { "Fight.Attack1", GLFW_KEY_U, GLFW_GAMEPAD_BUTTON_X,
          "U", cse::kernel::kInputLP, { "stand_lp", "crouch_lp", "air_lp" } },
        { "Fight.Attack2", GLFW_KEY_I, GLFW_GAMEPAD_BUTTON_Y,
          "I", cse::kernel::kInputMP, { "stand_mp", "crouch_mp", "air_mp" } },
        { "Fight.Attack3", GLFW_KEY_O, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
          "O", cse::kernel::kInputHP, { "stand_hp", "crouch_hp", "air_hp" } },
        { "Fight.Attack4", GLFW_KEY_J, GLFW_GAMEPAD_BUTTON_A,
          "J", cse::kernel::kInputLK, { "stand_lk", "crouch_lk", "air_lk" } },
        { "Fight.Attack5", GLFW_KEY_K, GLFW_GAMEPAD_BUTTON_B,
          "K", cse::kernel::kInputMK, { "stand_mk", "crouch_mk", "air_mk" } },
        { "Fight.Attack6", GLFW_KEY_L, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,
          "L", cse::kernel::kInputHK, { "stand_hk", "crouch_hk", "air_hk" } },
    };

    // The four direction bits ReadIntent reads: kInputLeft and kInputRight set
    // velX, kInputUp jumps, and kInputDown sets `crouching` (Simulate.cpp).
    //
    // DOWN USED TO BE UNBOUND, on the stated grounds that "nothing in the kernel
    // reads it and no move in either shipped character asks for it". Both halves
    // are now false: Simulate.cpp writes f.crouching from this bit, and
    // fighter_a authors six crouching normals. Leaving it unbound was what made
    // kStanceCrouching unreachable and a third of the moveset dead.
    const MoveKey kDirectionKeys[] = {
        { "Fight.Left",  GLFW_KEY_A, GLFW_GAMEPAD_BUTTON_DPAD_LEFT,
          "A", cse::kernel::kInputLeft,  { nullptr, nullptr, nullptr } },
        { "Fight.Right", GLFW_KEY_D, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
          "D", cse::kernel::kInputRight, { nullptr, nullptr, nullptr } },
        { "Fight.Up",    GLFW_KEY_W, GLFW_GAMEPAD_BUTTON_DPAD_UP,
          "W", cse::kernel::kInputUp,    { nullptr, nullptr, nullptr } },
        { "Fight.Down",  GLFW_KEY_S, GLFW_GAMEPAD_BUTTON_DPAD_DOWN,
          "S", cse::kernel::kInputDown,  { nullptr, nullptr, nullptr } },
    };

    // --- The training controls -----------------------------------------------
    constexpr const char* kActPause = "Fight.Pause";
    constexpr const char* kActStep  = "Fight.Step";
    constexpr const char* kActSlow  = "Fight.Slow";
    constexpr const char* kActReset = "Fight.Reset";
    constexpr const char* kActDemo  = "Fight.Demonstrate";
    constexpr const char* kActSwap  = "Fight.NextCharacter";
    constexpr const char* kActStage = "Fight.StagePosition";
    constexpr const char* kActOverlay = "Fight.Overlay";

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
        { kActStage, GLFW_KEY_V,      -1 },
        { kActOverlay, GLFW_KEY_B,    -1 },
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
    bool shadowedFromNeutral(const cse::kernel::FighterData& data,
                             std::uint16_t slot) {
        const std::int32_t index = static_cast<std::int32_t>(slot);
        if (index <= 0 || index >= data.moveCount) return false;
        const std::uint16_t mine = data.moves[index].button;
        if (mine == 0) return false;
        for (std::int32_t i = 1; i < index; ++i) {
            const std::uint16_t other = data.moves[i].button;
            if (other != 0 && (mine & other) == other) return true;
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
    //
    // VERSUS OPENS IN THE CORNER, ALWAYS. The offer carries no start position
    // (Handshake.h), so the two peers must Begin the identical MatchStart or
    // the first checksum disagrees before anyone has pressed a key; [V] is
    // inert for the whole visit (readControls_), so this is the one place the
    // toggle could have leaked in from an earlier visit.
    if (intent_ == ModeIntent::Versus) stageMidscreen_ = false;
    (void)startCharacter_(characterIndex_);
    if (intent_ == ModeIntent::Versus) enterLobby_();
    error.clear();
    return true;
}

UntitledFighterMode::~UntitledFighterMode() {
    // Not endVersus_: DetachSession tells ctx_.app, and a destructor that
    // runs after the Application would dereference it. What this owes the
    // process is the bridge slot and a GekkoNet static that no longer points
    // at this object's Handshake; the host's flag is Exit's, which every host
    // calls.
    driver_.Unbind();
    liveSession_ = nullptr;
    dropNet_();
}

void UntitledFighterMode::Exit() {
    // Safe after a FAILED Enter, which IGameMode::Exit requires: every member
    // touched here is default-constructible and was default-constructed, and
    // nothing below cares how far Enter got.
    //
    // The session first, while ctx_.app is still ours to tell: a mode that
    // left with the Application's session flag up would leave the host's
    // pause dead for the next mode. And the wire with it: a Versus visit
    // owns its session, and the bridge slot it holds is freed here or never.
    endVersus_();
    versusNote_.clear();
    versus_    = cse::data::VersusConfig{};
    localSlot_ = kPlayerSlot;
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
    history_.reset();
    // The replay's sources and verifier borrow replay_: they go first, it
    // goes last (teardownMatch_ keeps the same order).
    replaySrc_[0].reset();
    replaySrc_[1].reset();
    verifier_.reset();
    replay_     = cse::game::ReplayData{};
    replayOver_ = false;
    replayNote_.clear();

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
    // The fighters and the look must not outlive the mode in the host's scene
    // (M3.4c): Exit did not call teardownMatch_, so this is done here too.
    destroyScene3d_();
    ctx_ = MyCoreEngine::GameModeContext{};
}

// --- Bringing a character up ---------------------------------------------------

// The options every match this mode starts is built with. One function rather
// than a block inside the start path, because prepare and adopt both need the
// SAME answer -- two assemblies would be two binding tables one edit apart.
//
// AND IT HAS A TWIN IT MUST AGREE WITH: Catalogue.cpp's normalBindings builds
// the same six (button x stance) rows for the cook, and the shipped
// base.csrp carries the hash of THAT build; this mode reads the file against
// the hash of THIS one (loadReplay_). MoveDef::button is in the hashed bytes,
// so a row moved here and not there is a replay refused as "character
// changed" about a character nobody changed.
// FightMode.TheCatalogueAndTheModeBuildTheSameMatchData holds the two equal
// until one function owns both (ROADMAP M2.5's follow-up).
cse::data::BuildOptions UntitledFighterMode::MatchBuildOptions() {
    cse::data::BuildOptions options{};
    // The documented defaults, PASSED BY NAME rather than left at zero. Omitting
    // them gets the same numbers plus a warning nobody would draw; naming them
    // puts the decision where the next person can see there is one to make. They
    // are also fighter_a's own transcribed constants (13 px and 60 px), so the
    // body on screen is the body the file describes.
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    for (const MoveKey& key : kMoveKeys) {
        // One binding per stance variant, all on the SAME bit. The kernel's
        // StanceAllows is what makes that unambiguous rather than a race
        // between slots; MatchBuilder refuses two moves on one button whose
        // stances overlap.
        for (const char* moveId : key.moves) {
            if (moveId == nullptr) continue;
            cse::data::MoveBinding binding{};
            binding.moveId = moveId;
            binding.button = key.button;
            options.bindings.push_back(binding);
        }
    }
    return options;
}

bool UntitledFighterMode::startCharacter_(int index) {
    // Wraps in both directions, so one key can walk the list forwards and a
    // negative index cannot land outside it.
    characterIndex_ = ((index % kCharacterCount) + kCharacterCount) % kCharacterCount;
    const std::string file = kCharacters[characterIndex_].file;

    // The watch is bound BEFORE the load and regardless of its outcome
    // (ADR-016): the file that failed to load is exactly the file whose next
    // save must be noticed, or the honest-error screen can only be revived by
    // C -- which advances to the NEXT character. A containment refusal here is
    // impossible for the constants above, and ignored the same way the load's
    // own refusal would report it one line later.
    {
        std::string watchError;
        (void)reloadWatch_.Bind(ctx_.contentRoot, file, watchError);
    }

    cse::data::CharacterData character{};
    cse::data::MatchBuild    build{};
    std::string              error;
    const bool prepared = prepareCharacter_(file, character, build, error);
    if (!prepared) {
        // A swap is a SWAP: the old match goes even when the new file is
        // broken, and the honest-error screen says why. (A hot reload of the
        // SAME file makes the other choice -- keep-last-good -- and
        // pollHotReload_ says why there.)
        teardownMatch_();
        setupError_ = error;
        return false;
    }
    return adoptPrepared_(std::move(character), std::move(build));
}

bool UntitledFighterMode::prepareCharacter_(const std::string& file,
                                            cse::data::CharacterData& outCharacter,
                                            cse::data::MatchBuild& outBuild,
                                            std::string& error) {
    // --- load ---------------------------------------------------------------
    cse::data::LoadReport  loadReport{};
    cse::data::LoadOptions loadOptions{};
    // expectedResources deliberately empty: assertion A03 is a CROSS-FILE rule
    // about a whole build's resource ORDER, and this mode loads one file at a
    // time. Naming an order here would be this file inventing a build-wide
    // contract. The check is SKIPPED rather than passed, and CharacterData.h
    // records that as a warning for exactly this reason.
    if (!cse::data::LoadCharacterFile(ctx_.contentRoot, file, loadOptions,
                                      outCharacter, loadReport)) {
        error = "load " + file + ": " +
                (loadReport.rule.empty() ? "" : loadReport.rule + ": ") +
                loadReport.error;
        return false;
    }

    // --- build ---------------------------------------------------------------
    const cse::data::BuildOptions options = MatchBuildOptions();
    // BOTH SIDES GET THE SAME TABLE. A mirror match is the setup in which nothing
    // that happens can be blamed on the two sides having different data, and the
    // dummy having the same bindings is what makes "the dummy never acted" a fact
    // about the combo rather than about the binding table -- the same reason
    // tests/test_gap_extent.cpp's harness mirrors. It presses nothing today; that
    // is a decision about its INPUT and not about its data.
    if (!cse::data::BuildMatchData(outCharacter, options, outCharacter, options,
                                   outBuild)) {
        // Both sides are the same character, so a per-side report cannot
        // disagree; take whichever one actually says something.
        const std::string& first = outBuild.report[0].error;
        error = "build match: " +
                (first.empty() ? outBuild.report[1].error : first);
        return false;
    }
    return true;
}

void UntitledFighterMode::teardownMatch_() {
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
    history_.reset();
    // The replay's two sources and its verifier hold `const ReplayData*` into
    // replay_ (Replay.h): both go BEFORE the data they borrow, or the reset
    // of replay_ below would leave two dangling sources behind bindings the
    // session has already dropped -- harmless today, a trap for the next
    // line added between here and the adopt.
    replaySrc_[0].reset();
    replaySrc_[1].reset();
    verifier_.reset();
    replay_     = cse::game::ReplayData{};
    replayOver_ = false;
    replayNote_.clear();

    matchReady_    = false;
    analysisReady_ = false;
    setupError_.clear();
    analysisError_.clear();
    demoNote_.clear();
    reloadNote_.clear();
    reloadFailed_  = false;
    fatal_.clear();
    bindings_.clear();
    hitAdvantage_ = LatchedAdvantage{};
    paused_       = false;
    slowDivisor_  = 1;
    slowCounter_  = 0;
    pendingSteps_ = 0;
    // A pending tap was aimed at the match that is going away; delivered later
    // it would start a move on tick 0 of a match nobody pressed anything in.
    // resetMatch_ has always cleared these; the swap path silently keeping
    // them was the stale-press leak the M1.5 seam mapping found.
    taps_.Clear();

    character_ = cse::data::CharacterData{};
    build_     = cse::data::MatchBuild{};
    analysis_  = cse::data::ProverResult{};
    setup_     = cse::game::FightSetup{};
    clips_        = cse::presentation::FighterClips{};
    modelWatch_   = cse::data::CharacterFileWatch{};
    sidecarWatch_ = cse::data::CharacterFileWatch{};
    destroyScene3d_();
}

bool UntitledFighterMode::adoptPrepared_(cse::data::CharacterData&& character,
                                         cse::data::MatchBuild&& build) {
    // Teardown BEFORE the members move: the session must not hold a pointer
    // into a build_ that is being replaced, even for the length of this call.
    teardownMatch_();
    character_ = std::move(character);
    build_     = std::move(build);

    // THE REPLAY IS READ HERE, before the bindings, the look, the scene and
    // the analysis are built for a match that may never begin: a refused file
    // is the honest-error screen (setupError_), with nothing standing in the
    // host's scene behind it. It needs build_ in place -- the read is against
    // the hash of the data just built -- and nothing else below.
    if (intent_ == ModeIntent::Replay && !loadReplay_()) return false;

    const cse::data::BuildOptions options = MatchBuildOptions();

    // The binding table AS BUILT, which is not the same thing as the table asked
    // for: a move this character does not have got no slot (Find returns 0, its
    // documented sentinel), and a mask an earlier slot shadows can never start
    // from neutral. Both are computed here, once, and drawn.
    for (const MoveKey& key : kMoveKeys) {
        // ONE ROW PER STANCE VARIANT, because the readout's job is to say what
        // the button will actually start, and that now depends on where the
        // fighter is standing. A single row per key would have to pick one of
        // three to name, which is the readout lying about two thirds of the
        // time -- the failure this whole WP came from.
        for (const char* moveId : key.moves) {
            if (moveId == nullptr) continue;
            const std::uint16_t slot = build_.moves[kPlayerSlot].Find(moveId);
            if (slot == 0) continue;   // this character does not have it
            BindingRow row{};
            row.keyLabel = key.label;
            row.moveId   = moveId;
            row.button   = key.button;
            row.slot     = slot;
            row.shadowed = shadowedFromNeutral(build_.data.p[kPlayerSlot], slot);
            bindings_.push_back(row);
        }
    }

    // The clip table (ROADMAP M3.4b), bound by id right beside the binding
    // table and for the same reason: this build's slot numbers are this
    // build's. A character with no model leaves it empty. The model and its
    // sidecar get their (mtime, size) watches here -- like reloadWatch_,
    // bound whether or not the files exist yet, so a first export lands.
    {
        std::vector<cse::presentation::MoveSlot> slots;
        slots.reserve(character_.moves.size());
        for (const cse::data::Move& mv : character_.moves)
            slots.push_back({ build_.moves[kPlayerSlot].Find(mv.id), mv.id });
        clips_.Rebuild(character_, slots);
        if (!character_.anim3dModel.empty()) {
            std::string watchError;
            (void)modelWatch_.Bind(ctx_.contentRoot, character_.anim3dModel, watchError);
            std::filesystem::path sidecar(character_.anim3dModel);
            sidecar.replace_extension(".clips.json");
            (void)sidecarWatch_.Bind(ctx_.contentRoot, sidecar.generic_string(), watchError);
        }
    }

    // The 3D presentation (M3.4c): the look, the model, the entities. Every
    // step that fails leaves the 2D placeholders on and says why on the HUD;
    // none of them can fail the match, which is the kernel's and is ready.
    loadLook_();
    createScene3d_();

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
    // THE DUMMY OPENS IN THE CORNER, and that is not a flourish. The in-engine
    // decision procedure is corner-only by construction (ProverAdapter.h note 2):
    // it answers for a defender pinned against the wall with no room to walk
    // away. Every verdict this mode puts on screen is about that position, so the
    // match opens in it -- a training mode that started midscreen would be
    // quoting a corner verdict at a player standing somewhere the verdict says
    // nothing about.
    //
    // AND [V] MOVES IT TO MIDSCREEN, which is not a softening of that argument
    // but the other half of it. Asked for from play (2026-08-20): "the training
    // mode seems to keep the enemy in the corner so I can't really tell if
    // pushback or anything like that is working." Both statements are true at
    // once -- the corner is where the verdicts mean something, and it is also
    // the one place on the stage where knockback has nowhere to put anybody, so
    // every spacing mechanic this engine has is invisible there.
    //
    // Rather than choose, the mode says which position it is in and what that
    // costs. The HUD carries the warning: midscreen, the verdict above the
    // fighters is about a position they are not standing in.
    bodyHalfWidthSub_  = options.body.halfWidthSub;
    stageHalfWidthSub_ = ProbeStageHalfWidthSub();
    setup_             = cse::game::FightSetup{};
    setup_.data        = &build_.data;   // BORROWED for the session's whole life
    applyStagePosition_();
    // A REPLAY OPENS WHERE IT WAS RECORDED, not where training would put the
    // dummy: the file carries exactly FightSession's MatchStart (Replay.h),
    // the recorder checkpoints tick 0 unconditionally, and a match begun from
    // any other seed or position disagrees with the first checkpoint before a
    // key is pressed. AFTER applyStagePosition_, which rewrites startPosX, and
    // before Begin -- assigned earlier it would be overwritten; later, unused.
    if (intent_ == ModeIntent::Replay) setup_.start = replay_.start;

    std::string beginError;
    if (!session_.Begin(setup_, beginError)) {
        setupError_ = "begin match: " + beginError;
        watcher_.reset();
        return false;
    }

    session_.AddObserver(watcher_.get());
    // THE STATE HISTORY (ROADMAP M2.5), registered for every intent: the
    // desync post-mortem needs the state at frame + 1 and a session can be
    // attached to any intent. A FRESH ring with every match -- StateHistory
    // has no Clear, and a ring that outlived the match would answer Find(t)
    // with the old match's tick t until this one overwrote the slot. The
    // table holds eight observers and this mode registers two, so the
    // refusal cannot happen today; if it ever does, the tail says "no longer
    // held" rather than dereferencing an observer nobody notifies.
    history_ = std::make_unique<HistoryObserver>();
    if (!session_.AddObserver(history_.get())) history_.reset();

    if (intent_ == ModeIntent::Replay) {
        // BOTH SLOTS FROM THE FILE (ADR-022 D3): one source per slot over the
        // one ReplayData, the pattern every replay consumer in the tree uses
        // (the catalogue's verify pass, test_catalogue.cpp). No pad, no
        // dummy, no Fallback wrapper -- the HUD reads the bound source's own
        // Name() for its chip. And the verifier BESIDE the watcher, comparing
        // the file's checkpoints against the kernel as it runs today; its
        // verdict is read every tick in replayTick_, and a verifier the table
        // would not hold is refused out loud rather than left to read as "no
        // divergence" -- a verifier that compared nothing must not pass for
        // one that agreed with everything (Replay.h).
        replaySrc_[0] = std::make_unique<cse::game::ReplayInputSource>(replay_, 0);
        replaySrc_[1] = std::make_unique<cse::game::ReplayInputSource>(replay_, 1);
        session_.SetInputSource(0, replaySrc_[0].get());
        session_.SetInputSource(1, replaySrc_[1].get());
        verifier_ = std::make_unique<cse::game::ReplayVerifier>(replay_);
        if (!session_.AddObserver(verifier_.get())) {
            setupError_ = "replay " + replayRel_ +
                          ": no observer slot left for the verifier, so its checkpoints "
                          "could not be checked; the match was not started";
            session_.SetInputSource(0, nullptr);
            session_.SetInputSource(1, nullptr);
            replaySrc_[0].reset();
            replaySrc_[1].reset();
            verifier_.reset();
            watcher_.reset();
            return false;
        }
    } else {
        local_.Reset(session_.CurrentTick());
        bindPlayerSource_();
        // Explicit, though it is also the default. "The dummy is fed neutral" is a
        // decision about how this mode trains, and a decision made by omission is a
        // decision nobody can find.
        session_.SetInputSource(kDummySlot, nullptr);
    }

    // The camera opens ON THE PAIR rather than holding a framing from a match
    // that no longer exists. Without this a restart, or the [V] toggle, would
    // leave the deadzone anchored where the last fight ended and scroll back
    // across the stage on the first tick of the new one.
    cameraCentrePx_ =
        static_cast<float>((setup_.start.startPosX[kPlayerSlot] +
                            setup_.start.startPosX[kDummySlot]) / 2) /
        static_cast<float>(cse::kernel::kSubUnitsPerPixel);

    matchReady_ = true;
    refreshDemoNote_();
    return true;
}

// --- The 3D presentation (ROADMAP M3.4c; ADR-019 D3, D4, D5, D9) ----------------

void UntitledFighterMode::loadLook_() {
    look_ = cse::presentation::FightLook{};
    presentationNote_.clear();
    // The committed fight_look.json, staged beside the title's menu, through
    // the same containment gate as every authored read. Missing or refused: the
    // defaults (a consistent look by construction) and a note.
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(ctx_.contentRoot, "UntitledFighter/fight_look.json", full)) {
        presentationNote_ = "fight_look.json: path refused; using the default look";
        return;
    }
    std::ifstream in(full, std::ios::binary);
    if (!in) {
        presentationNote_ = "fight_look.json not staged; using the default look";
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    if (!cse::presentation::ParseFightLook(text, look_, error)) {
        look_ = cse::presentation::FightLook{};
        presentationNote_ = error + "; using the default look";
    }
}

void UntitledFighterMode::applyLook_() {
    if (!ctx_.scene) return;
    // Snapshot ONCE per applied look: a second apply over an applied look
    // would snapshot the fight's own values and restore the wrong ones.
    if (!lookSnapshot_.taken)
        lookSnapshot_ = ApplyFightLook(*ctx_.scene, ctx_.app ? &ctx_.app->renderer() : nullptr, look_);
    else
        (void)ApplyFightLook(*ctx_.scene, ctx_.app ? &ctx_.app->renderer() : nullptr, look_);
}

void UntitledFighterMode::createScene3d_() {
    destroyScene3d_();
    if (character_.anim3dModel.empty()) return;            // off by default
    if (!ctx_.scene || !ctx_.assets) {
        presentationNote_ = "no scene or asset cache in this host; the 2D placeholders stand in";
        return;
    }
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(ctx_.contentRoot, character_.anim3dModel, full)) {
        presentationNote_ = "engine.anim3d.model: path refused";
        return;
    }
    // Through the host's cache, so the editor's asset panel and this mode hold
    // one Model, and the cache's reload door is the one hot reload uses.
    model_ = ctx_.assets->GetModel(full.generic_string());
    std::string error;
    if (!model_ || !scene3d_.Create(*ctx_.scene, model_, look_, error)) {
        presentationNote_ = "engine.anim3d.model `" + character_.anim3dModel + "`: " +
                            (error.empty() ? std::string("did not load") : error) +
                            "; the 2D placeholders stand in";
        model_.reset();
        return;
    }
    applyLook_();
    reconcile_();
}

void UntitledFighterMode::destroyScene3d_() {
    if (ctx_.scene) {
        scene3d_.Destroy(*ctx_.scene);
        RestoreLook(*ctx_.scene, ctx_.app ? &ctx_.app->renderer() : nullptr, lookSnapshot_);
    }
    lookSnapshot_ = LookSnapshot{};
    model_.reset();
}

void UntitledFighterMode::reconcile_() {
    // Not behind the lobby screen either: the mesh would stand in the scene
    // under "waiting for the peer", a fight nobody has joined.
    if (!matchReady_ || InLobby() || !scene3d_.Active() || !ctx_.scene) return;
    // A scene swap clears the registry under the mode (SceneSerializer::Load
    // -> ResetToDefaults) and takes the look with it: build both again.
    if (!scene3d_.Valid(*ctx_.scene)) {
        std::string error;
        lookSnapshot_ = LookSnapshot{};
        if (!model_ || !scene3d_.Create(*ctx_.scene, model_, look_, error)) {
            presentationNote_ = "presentation entities lost to a scene swap and not rebuilt: " + error;
            scene3d_.Destroy(*ctx_.scene);
            return;
        }
        applyLook_();
    }
    const cse::presentation::FrameComposition frame = cse::presentation::ComposeFrame(
        session_.Data(), session_.State(), clips_, look_, stageHalfWidthSub_, cameraCentrePx_,
        viewportW_, viewportH_);
    // The overlay's camera reads the same centre next frame (Draw), so the
    // scene camera and the box overlay scroll together.
    cameraCentrePx_ = frame.camera.framing.centreX;
    scene3d_.Apply(*ctx_.scene, frame);
    scene3d_.SetOpacity(cse::presentation::OverlayLookFor(overlay_, true).meshOpacity);
}

// The authoring loop (ROADMAP M1.5). ADR-016 in four sentences: a change to
// the loaded character file RESTARTS the match with the freshly built data,
// through the same adopt path C uses; it never swaps MatchData under the live
// session, because rollback across the edit is undefined (FightSession.h),
// `resimulated` would never flag the changed ticks, and a replay's single
// matchDataHash would describe a match nobody simulated; a broken edit -- the
// normal state while typing -- keeps the last good match and says so; and the
// author's time posture (pause, slow motion) survives, because the person
// saving the file is usually frame-stepping the very move they are editing.
//
// The property is pinned headlessly in tests/test_character_hotreload.cpp,
// against real files and a real FightSession; this function is its mirror
// with the hardware attached, the same division test_press_delivery.cpp draws
// for the tap accumulator.
//
// NOTE THE FILE IT WATCHES: the STAGED copy under the content root, beside the
// executable -- the only copy the sandbox lets this mode read. An edit to the
// authoring source under Games/UntitledFighter/Assets/ lands when the build
// restages it (or when it is copied by hand); docs/manual/fighting-core.md
// says so where the authoring loop is described.
void UntitledFighterMode::pollHotReload_(float dt) {
    // Every watch is polled every frame -- each refreshes its stamp when it
    // reports -- and any one of them is a reason to rebuild: the character
    // file, the presentation model, or its clip sidecar (M3.4b). The rebuild
    // is the same load either way, so a re-export that disagrees with the
    // frame data fails A21/A22 below and keeps the last good match.
    const bool characterEdited = reloadWatch_.Update(dt);
    const bool modelEdited     = modelWatch_.Update(dt);
    const bool sidecarEdited   = sidecarWatch_.Update(dt);
    if (!characterEdited && !modelEdited && !sidecarEdited) return;
    const std::string edited = characterEdited ? kCharacters[characterIndex_].file
                             : modelEdited     ? character_.anim3dModel
                                               : "the clip sidecar of " + character_.anim3dModel;

    const std::string file = kCharacters[characterIndex_].file;
    cse::data::CharacterData character{};
    cse::data::MatchBuild    build{};
    std::string              error;
    if (!prepareCharacter_(file, character, build, error)) {
        // KEEP-LAST-GOOD: nothing was adopted, so the running match (or the
        // honest-error screen) stands exactly as it was. The watch refreshed
        // its stamps when it reported, so this says its piece ONCE and the
        // save that fixes the file lands like any other edit.
        reloadNote_   = "edit refused, keeping the last good match -- " + error;
        reloadFailed_ = true;
        return;
    }

    // The author's decisions about time survive the reload; everything that
    // names an ABSOLUTE TICK of the old match dies with it in teardownMatch_
    // (demonstration, pending taps, pending steps, latched advantage), for
    // resetMatch_'s own reasons.
    const bool keepPaused = paused_;
    const int  keepSlow   = slowDivisor_;
    if (!adoptPrepared_(std::move(character), std::move(build))) return;
    paused_      = keepPaused;
    slowDivisor_ = keepSlow;

    reloadNote_   = "edit landed -- " + edited +
                    " rebuilt, match restarted at tick 0"
                    + (paused_ ? std::string(", still paused") : std::string());
    reloadFailed_ = false;
}

// Where the two fighters start, for whichever position the mode is in. The GAP
// between them is the same in both, so the only thing that changes is how much
// room the dummy has behind it -- which is exactly the variable being toggled.
void UntitledFighterMode::applyStagePosition_() {
    const std::int32_t gap = 2 * bodyHalfWidthSub_ +
                             kTrainingGapPx * cse::kernel::kSubUnitsPerPixel;

    if (stageMidscreen_) {
        // Centred, so there is a full half-stage of room on both sides and a
        // knockback has somewhere to carry the dummy.
        setup_.start.startPosX[kDummySlot]  = gap / 2;
        setup_.start.startPosX[kPlayerSlot] = -(gap - gap / 2);
    } else {
        setup_.start.startPosX[kDummySlot]  = stageHalfWidthSub_;
        setup_.start.startPosX[kPlayerSlot] = stageHalfWidthSub_ - gap;
    }
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
    // A pending tap was aimed at the match that just ended; delivered now it
    // would start a move on tick 0 of a match nobody pressed anything in.
    taps_.Clear();
    if (intent_ == ModeIntent::Replay) {
        // THE TWO ReplayInputSources STAY BOUND -- Begin keeps sources
        // (FightSession.h) and a rebind here would be a second place for the
        // slot-to-source pairing to get out of step with the adopt. What
        // restarts is what was MEASURED: the verifier's cursor and verdict,
        // the end-of-file flag and its note, all about ticks the Begin above
        // just put back to zero.
        if (verifier_) verifier_->Reset();
        replayOver_ = false;
        replayNote_.clear();
    } else {
        bindPlayerSource_();
    }
    if (watcher_) watcher_->Reset();
    // And the history, for the reason the ring is fresh in adoptPrepared_:
    // Begin put the tick index back to zero, and every state in the ring is
    // about a match that no longer has those ticks.
    if (history_) history_->history = cse::game::StateHistory{};
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

// --- The replay (ROADMAP M2.5; ADR-022 D3, D4) -------------------------------------

bool UntitledFighterMode::loadReplay_() {
    replayIndex_ = ((replayIndex_ % kReplayCount) + kReplayCount) % kReplayCount;
    replayRel_   = kReplays[replayIndex_].file;
    replay_      = cse::game::ReplayData{};

    // AGAINST THE HASH OF THE MATCH JUST BUILT, never zero: zero skips the
    // character-changed check with a warning (Replay.h), and a play path that
    // skipped it would show a fight whose checkpoints disagree from tick 0
    // and call the kernel the culprit.
    cse::game::ReplayReadOptions options{};
    options.expectedMatchDataHash = cse::game::HashMatchData(build_.data);
    cse::game::ReplayReport report{};
    if (!cse::game::ReadReplayFile(ctx_.contentRoot, replayRel_, options, replay_, report)) {
        replay_     = cse::game::ReplayData{};
        setupError_ = "replay " + replayRel_ + ": " + report.error + kReplayRegenerate;
        return false;
    }
    // The cheap re-assertion at the point of use Replay.h asks for, in the
    // reader's own sentence naming both ids and both hashes.
    std::string mismatch;
    if (!cse::game::ReplayMatchesData(replay_, build_.data, mismatch)) {
        replay_     = cse::game::ReplayData{};
        setupError_ = "replay " + replayRel_ + ": " + mismatch + kReplayRegenerate;
        return false;
    }
    return true;
}

void UntitledFighterMode::replayTick_() {
    // Nothing to latch: the file authors both slots, and Tick() asks the two
    // bound sources At(CurrentTick()) itself. Past the file's end it would ask
    // them forever and be fed NEUTRAL (FightSession.cpp), so the end is read
    // right after the tick and the mode stops calling.
    session_.Tick();
    latchHitAdvantage_();

    // THE VERIFIER'S VERDICT, EVERY TICK, AND IT STOPS THE MATCH. Inputs
    // first: a mismatch there is this host's wiring, not the kernel's history,
    // and Replay.h insists the two are not confused. A diverged checkpoint is
    // the finding the file exists to make -- the simulation changed since it
    // was recorded -- shown on the banner in those words, never corrected and
    // never played past (ADR-022 D3; the network case is ADR-002 CHOICE C and
    // the replay case is a regression against the past, but neither is a
    // fight to keep showing).
    const cse::game::ReplayDivergence& verdict = verifier_->Result();
    if (verdict.inputMismatch) {
        fatal_ = "replay " + replayRel_ + ": the bits fed to tick " +
                 std::to_string(verdict.inputMismatchTick) +
                 " were not the recording's -- a host wiring bug (the wrong source bound, "
                 "or a tick index off by one), not a kernel change. The replay stopped.";
        return;
    }
    if (verdict.diverged) {
        fatal_ = "replay " + replayRel_ + " DIVERGED at checkpoint tick " +
                 std::to_string(verdict.tick) + " (recorded checksum " +
                 std::to_string(verdict.recordedChecksum) + ", live " +
                 std::to_string(verdict.liveChecksum) + "; " +
                 (verdict.hadPreviousAgreement
                      ? "last agreeing checkpoint " + std::to_string(verdict.previousAgreeingTick)
                      : std::string("no checkpoint agreed")) +
                 "): the simulation changed since this file was recorded. The replay stopped "
                 "and is not corrected; if the change was meant, re-cook the file" +
                 kReplayRegenerate;
        return;
    }

    replayOver_ = session_.CurrentTick() >= replaySrc_[0]->AuthoredEndTick();
    if (replayOver_) {
        // Paused, so the training clock's own gate is what holds the tick,
        // and the step key cannot run one past the end (FixedTick returns on
        // the flag before the gate is even asked). R restarts; C is the next
        // file when there is one.
        paused_       = true;
        pendingSteps_ = 0;
        replayNote_   = "replay over at tick " + std::to_string(session_.CurrentTick()) +
                        " of " + std::to_string(replay_.TickCount()) + "; R restarts it" +
                        (kReplayCount > 1 ? std::string(", C loads the next") : std::string());
    }
}

// --- The live session (ROADMAP M2.4) ---------------------------------------------

bool UntitledFighterMode::AttachSession(cse::net::ISession* session, int padSlot,
                                        std::uint8_t localSlots, std::string& error) {
    if (session == nullptr) {
        error = "no session to attach";
        return false;
    }
    // REFUSED BEFORE ANY MEMBER IS WRITTEN, so a refused attach leaves the
    // match exactly as it was -- the Begin below is a restart, and a restart
    // for a session that never bound would be a tick index put back to zero
    // for nothing. A replay's two slots are the file's (ADR-022 D3): a
    // session bound over them would feed the kernel bits the recording did
    // not author and the verifier would call the kernel the culprit. The pad
    // slot must be one of the two and one this host supplies: the driver
    // offers `pad` only to the slot that is both (SessionDriver::Frame), so
    // either mistake is a keyboard offered to nobody and a match that runs on
    // with a dead pad and nothing on screen saying so; `1u << padSlot` on a
    // negative slot is undefined besides.
    if (intent_ == ModeIntent::Replay) {
        error = "a replay drives both slots from the file; no session attaches to it (ADR-022 D3)";
        return false;
    }
    if (padSlot < 0 || padSlot >= kMatchPlayers) {
        error = "padSlot " + std::to_string(padSlot) + " is not a slot of a two-player match";
        return false;
    }
    if ((localSlots & static_cast<std::uint8_t>(1u << padSlot)) == 0) {
        error = "localSlots mask " + std::to_string(static_cast<int>(localSlots)) +
                " does not include padSlot " + std::to_string(padSlot) +
                ", so the pad would be offered to no slot this host supplies";
        return false;
    }
    if (!matchReady_) {
        error = "no running match to attach a session to" +
                (setupError_.empty() ? std::string() : ": " + setupError_);
        return false;
    }
    if (!fatal_.empty()) {
        error = "the match has stopped: " + fatal_;
        return false;
    }

    // A SESSION ATTACHES AT TICK 0, STRUCTURALLY. Begin again before the bind,
    // so the session's frame F is the kernel's tick F on BOTH peers -- the
    // Versus lobby needs the two matches to start equal (the offer carries no
    // tick), and the desync post-mortem needs the state GekkoNet checksummed
    // at frame F to be the one the history holds at tick F + 1. A convention
    // ("the lobby resets first") would hold until the first caller that did
    // not; putting it here makes it a property of attaching.
    resetMatch_();
    if (!matchReady_) {
        error = "no running match to attach a session to: " + setupError_;
        return false;
    }
    liveSession_ = session;
    localSlot_   = padSlot;
    driver_.Bind(session, &session_, localSlots, padSlot);
    // A fresh session has no report: whatever the last one said is about a
    // match the Begin above just replaced, and a grace left counting would
    // begin a tail for it on the first pump.
    abortGrace_ = -1;
    mineHeld_   = false;
    report_     = cse::net::DesyncReport{};
    // And no peer has been counted yet: the drop this looks for is THIS
    // session's, and a local one never counts any.
    peakPeers_  = 0;

    // Whatever the training controls had decided is void from here: the
    // session decides. A press made before the peer existed must not fire on
    // the first frame it carries, so the accumulator starts empty too.
    paused_       = false;
    slowDivisor_  = 1;
    slowCounter_  = 0;
    pendingSteps_ = 0;
    taps_.Clear();

    // The Application's half of T3 and N5 (core/FrameGate.h): pause, time
    // scale and the pad suppression stand down for as long as this is set.
    if (ctx_.app) ctx_.app->setSessionLive(true);
    return true;
}

void UntitledFighterMode::DetachSession() {
    if (liveSession_ == nullptr) return;
    // A session this mode created is the Versus lobby's, and a Versus match
    // is a match with exactly one other copy: detached from it, there is no
    // match to resume, only a fight the peer has left. So the visit ends here
    // -- sticky, like a refusal -- rather than the local kernel ticking on
    // under the pad with the training controls back, which is what a host's
    // DetachSession on a LIVE Versus mode did before this branch existed.
    if (ownedSession_ != nullptr) {
        endSession_("the session was detached; the match stopped");
        return;
    }
    // Borrowed: the owner decides what the session does next, and this mode
    // is training again.
    unbindSession_();
}

void UntitledFighterMode::unbindSession_() {
    if (liveSession_ == nullptr) return;
    driver_.Unbind();
    liveSession_ = nullptr;
    if (ctx_.app) ctx_.app->setSessionLive(false);

    // The latched log stopped at the tick it last recorded and the session ran
    // on past it through Tick(inputs) -- the inputs of those ticks were the
    // session's to keep, not this log's. Latching resumes at the tick the
    // kernel is on, or the first training tick after this would be refused
    // (LatchedInputSource::Latch is monotonic) and stop the match.
    local_.Reset(session_.CurrentTick());
    taps_.Clear();
}

void UntitledFighterMode::endSession_(const std::string& why) {
    unbindSession_();
    // Destroyed only if this mode created it -- a borrowed session is its
    // owner's to free -- and the pointer nulled so no later path frees it
    // twice. The handshake goes with it: the bridge held it only for the
    // session (GekkoSession.cpp), and a Handshake pumped after this would
    // drain a transport the desync exchange may be reading.
    if (ownedSession_ != nullptr) {
        cse::net::DestroySession(ownedSession_);
        ownedSession_ = nullptr;
    }
    handshake_.reset();
    versusNote_ = why;
    lobby_      = Lobby::Ended;
}

// --- The Versus lobby (ROADMAP M2.5; ADR-022 D2, D5) -------------------------------
//
// tests/online_peer.cpp is the reference flow -- bind, offer, pump until Agreed
// or Refused, create the session ON the handshake, keep pumping for the grace
// -- spread over fixed steps instead of a loop with a sleep in it, because a
// mode is called and never calls. Every text a player reads off the lobby is
// the failing layer's own sentence (D5): the transport's bind error, the
// handshake's refusal with both values, the loader's refusal of versus.json.
// This file adds "waiting for", "refused:", and "LIVE against", and nothing
// it would have to have guessed.

void UntitledFighterMode::enterLobby_() {
    lobby_      = Lobby::Ended;   // until the offer is on the wire
    lobbySteps_ = 0;
    versusNote_.clear();
    // 30 s at the HOST's fixed rate, so a 30 Hz host waits as long as a 144 Hz
    // one. Headless there is no host and the rate is the kernel's nominal 60.
    const float hz     = ctx_.app ? ctx_.app->fixedTimestepHz() : 60.0f;
    lobbyTimeoutSteps_ = static_cast<std::uint32_t>(kLobbyTimeoutSeconds * hz);

    // --- who we are and who we call -------------------------------------------
    versus_ = cse::data::VersusConfig{};
    std::string error;
    if (!cse::data::LoadVersusConfig(ctx_.contentRoot, kVersusFile, versus_, error) ||
        !cse::data::ApplyVersusOverrides(lobbyArgs_(), versus_, error)) {
        versusNote_ = error;
        return;
    }
    peerAddress_ = versus_.peer;
    localSlot_   = versus_.slot;

    // --- the match the offer describes --------------------------------------
    //
    // No match, no offer: an offer built from an empty MatchData would carry
    // a real-looking hash, and the peer would be refused for "content hash"
    // when the truth is that OUR character did not load. The loader's own
    // sentence is the lobby's verdict, and no socket is opened for it.
    if (!matchReady_) {
        versusNote_ = setupError_;
        return;
    }

    // --- the wire ------------------------------------------------------------
    if (transport_ == nullptr) {
        udp_ = cse::net::UdpTransport::Bind(versus_.port, &error);
        if (!udp_) {
            versusNote_ = error;   // "bind(47011) failed: 10048" -- the socket's words
            return;
        }
    }

    // --- the offer -----------------------------------------------------------
    //
    // The sizes come from the SAME WireConfig the session will be created with
    // (goLive_), never from constants: an offer that agreed on sizes the driver
    // did not use would pass the lobby and hit SessionDriver::Fatal on the
    // first Save.
    const cse::net::SessionConfig cfg = SessionDriver::WireConfig(kMatchPlayers);
    cse::net::HandshakeOffer offer{};
    offer.contentHash = cse::game::HashMatchData(build_.data);
    offer.stateBytes  = cfg.stateBytes;
    offer.inputBytes  = cfg.inputBytesPerPlayer;
    offer.seed        = setup_.start.seed;
    offer.playerCount = cfg.playerCount;
    offer.slot        = static_cast<std::uint8_t>(versus_.slot);
    handshake_ = std::make_unique<cse::net::Handshake>(*wire_(), peerAddress_, offer);

    lobby_      = Lobby::Handshaking;
    versusNote_ = "waiting for " + peerAddress_ + " as slot " + std::to_string(versus_.slot) +
                  " on port " + std::to_string(versus_.port) + " (IPv4 literals only)";
}

void UntitledFighterMode::lobbyStep_() {
    // Ended waits for Escape; Exchanging is the desync tail's step.
    if (lobby_ == Lobby::Exchanging) {
        exchangeStep_();
        return;
    }
    if (lobby_ != Lobby::Handshaking || !handshake_) return;
    ++lobbySteps_;
    const cse::net::HandshakeResult& result = handshake_->Pump();
    switch (result.state) {
        case cse::net::HandshakeState::Waiting:
            if (lobbySteps_ >= lobbyTimeoutSteps_) {
                // The Handshake has no timeout of its own; this is the one
                // online_peer keeps on a wall clock. The peer's silence has
                // several causes the transport cannot tell apart -- not
                // running, a wrong address, a non-literal one UdpTransport
                // silently dropped -- so the sentence names the address.
                versusNote_ = "the peer never offered: nothing from " + peerAddress_ +
                              " in " + std::to_string(static_cast<int>(kLobbyTimeoutSeconds)) +
                              " s (IPv4 literals only; is the other copy running with --slot " +
                              std::to_string(1 - versus_.slot) + "?)";
                lobby_ = Lobby::Ended;
            }
            return;
        case cse::net::HandshakeState::Refused:
            // The first disagreeing field with both values, in the
            // handshake's words (A5). A refused handshake sends nothing more,
            // so the peer learns of it from its own comparison, not from us.
            versusNote_ = "refused: " + result.reason;
            lobby_      = Lobby::Ended;
            return;
        case cse::net::HandshakeState::Agreed:
            goLive_();
            return;
    }
}

void UntitledFighterMode::goLive_() {
    // The session's peers by slot, the local one empty (ISession.h), on the
    // SAME config the offer quoted its sizes from.
    cse::net::SessionConfig cfg = SessionDriver::WireConfig(kMatchPlayers);
    cfg.localDelay    = 2;
    // The silence the session tolerates before it drops the peer and
    // pollDisconnect_ ends the match: the library's default unless the host
    // shortened it (SetDisconnectTimeoutMs -- a test's ~100 ms).
    cfg.disconnectTimeoutMs = disconnectTimeoutMs_;
    cfg.peerAddresses = { versus_.slot == 0 ? std::string() : peerAddress_,
                          versus_.slot == 1 ? std::string() : peerAddress_ };
    // ON the handshake, which keeps peeling the peer's grace offers off and
    // hands the session everything else (Handshake.h).
    ownedSession_ = cse::net::CreateGekkoOnlineSession(cfg, handshake_.get());
    if (ownedSession_ == nullptr) {
        // Null for a handful of reasons the factory does not distinguish; the
        // one a running title can hit is the bridge: four online sessions per
        // process, each held until DestroySession.
        versusNote_ = "the online session could not be created: no bridge slot free "
                      "(four per process) or a malformed session config";
        lobby_ = Lobby::Ended;
        return;
    }
    std::string error;
    if (!AttachSession(ownedSession_, versus_.slot, static_cast<std::uint8_t>(1u << versus_.slot),
                       error)) {
        // A session nothing attached is a bridge slot leaked; destroyed
        // here, and the pointer nulled so no later path frees it twice.
        cse::net::DestroySession(ownedSession_);
        ownedSession_ = nullptr;
        versusNote_   = error;
        lobby_        = Lobby::Ended;
        return;
    }
    lobby_      = Lobby::Live;
    versusNote_ = "LIVE against " + peerAddress_ + " as slot " + std::to_string(versus_.slot);
}

void UntitledFighterMode::endVersus_() {
    // The bare unbind: dropNet_ destroys the owned session itself, and the
    // Ended that DetachSession would write here is a state Exit clears one
    // line later.
    unbindSession_();
    dropNet_();
}

void UntitledFighterMode::dropNet_() {
    // The exchange first -- it holds a reference to the transport that goes
    // last -- then session, then handshake, then socket: the bridge holds
    // the Handshake until DestroySession, and the Handshake holds the
    // transport. The report goes with the match it was about.
    exchange_.reset();
    abortGrace_ = -1;
    mineHeld_   = false;
    report_     = cse::net::DesyncReport{};
    if (ownedSession_ != nullptr) {
        cse::net::DestroySession(ownedSession_);
        ownedSession_ = nullptr;
    }
    handshake_.reset();
    udp_.reset();
    lobby_      = Lobby::None;
    lobbySteps_ = 0;
}

// --- The desync tail (ROADMAP M2.5; DETERMINISM.md T4, T6; ADR-002 CHOICE C) -----
//
// tests/online_peer.cpp's abort, which is a loop with a sleep in it, spread
// over fixed steps: the grace, the exchange and its cap are all COUNTED, never
// waited for, because a mode is called and never calls. From the first report
// the match is LOST -- the only open question is which field -- but for the
// grace it still runs under the session exactly as before: the pad is offered,
// the kernel advances on the session's word, the HUD counts frames. That is
// not indecision; it is what the peer needs. Its copy detects the desync from
// OUR checksums (Desync.h), and a session destroyed on the report would leave
// it finishing the match alone until GekkoNet's disconnect timeout, never told
// why. When the grace runs out the tail detaches, swaps states, names the
// field and stops.

void UntitledFighterMode::pollDesync_() {
    // Polled AFTER the pump, every live step. fatal_ is NOT written at the
    // report: FixedTick returns before the pump while it is set, and a grace
    // nobody pumps is no grace at all -- the peer never receives the
    // checksum that lets it detect, and finishes the match alone after the
    // disconnect timeout (Desync.h, seen over UDP).
    cse::net::DesyncReport fresh{};
    if (abortGrace_ < 0 && liveSession_->PollDesync(&fresh)) {
        report_     = fresh;
        abortGrace_ = kDesyncGraceSteps;
        versusNote_ = "desync reported at frame " + std::to_string(report_.frame) +
                      "; the match is lost and stops in " + std::to_string(kDesyncGraceSteps) +
                      " steps, once the peer has the checksums to see it too";
    }
    if (abortGrace_ > 0) --abortGrace_;
    if (abortGrace_ == 0) beginDesyncTail_();
}

void UntitledFighterMode::pollDisconnect_() {
    const int peers = liveSession_->ConnectedPeers();
    if (peers > peakPeers_) peakPeers_ = peers;
    // Not during a desync's grace: that match is already ending, on the tail's
    // clock, and the peer that detected first may have gone quiet exactly
    // because its own tail destroyed its session.
    if (peakPeers_ < 1 || peers > 0 || abortGrace_ >= 0) return;
    // The frame is read before the unbind resets the driver. The sentence
    // names what the player can act on -- WHO stopped answering and the frame
    // the session gave up at -- and no duration: the silence the session
    // tolerates is SessionConfig::disconnectTimeoutMs (a test's ~100 ms, a
    // player's five seconds), and a number here would be one no test asserts
    // (STYLE.md). The peer is named as the wire spells it; a borrowed session
    // whose owner never said (SetPeerAddress) gets "the peer", not an empty
    // address.
    const std::string who = peerAddress_.empty() ? std::string("the peer")
                                                 : "the peer at " + peerAddress_;
    const std::string why = who + " stopped answering and the session dropped it at frame " +
                            std::to_string(driver_.CurrentFrame()) + "; the match stopped";
    endSession_(why);
    // The same sentence on the banner, as finishDesync_ does: THE MATCH
    // STOPPED is the one place this mode says a match will not tick again.
    fatal_ = why;
}

void UntitledFighterMode::beginDesyncTail_() {
    abortGrace_ = -1;
    const std::uint32_t frame = static_cast<std::uint32_t>(report_.frame);
    // THE FRAME IS NOT THE TICK (Desync.h): the session reports the frame
    // whose ADVANCE produced the differing state, so the state to compare is
    // the one after it. Copied out NOW, while the ring is exactly as the
    // last pump left it: the exchange spans many steps and the slot is
    // overwritten 128 ticks on.
    const std::uint32_t tick = frame + 1;
    const cse::kernel::GameState* mine =
        history_ ? history_->history.Find(tick) : nullptr;
    mineHeld_ = mine != nullptr;
    if (mineHeld_) mineAtDesync_ = *mine;

    // The session goes: detached from the driver, destroyed if this mode
    // created it, left to its owner if a host lent it (a borrowed session is
    // the owner's to free). With it goes the handshake pump -- the live
    // branch is never reached again -- so from here the raw transport's
    // packets are the exchange's and nothing else drains them.
    unbindSession_();
    if (ownedSession_ != nullptr) {
        cse::net::DestroySession(ownedSession_);
        ownedSession_ = nullptr;
    }
    handshake_.reset();

    cse::net::ITransport* wire = wire_();
    if (mineHeld_ && wire != nullptr && !peerAddress_.empty()) {
        exchange_ = std::make_unique<cse::net::BlobExchange>(
            *wire, peerAddress_, tick,
            reinterpret_cast<const std::uint8_t*>(&mineAtDesync_),
            static_cast<std::uint32_t>(sizeof(cse::kernel::GameState)));
        exchangeSteps_ = 0;
        exchangeGrace_ = kExchangeGraceSteps;
        lobby_         = Lobby::Exchanging;
        versusNote_    = "desync reported at frame " + std::to_string(frame) +
                         "; exchanging the two states at tick " + std::to_string(tick) +
                         " with " + peerAddress_;
        return;
    }
    // Nothing to exchange, or nothing to exchange on: the verdict is written
    // from what is here, and says which of the two it was.
    finishDesync_();
}

void UntitledFighterMode::exchangeStep_() {
    ++exchangeSteps_;
    // Pump returns true once the peer's blob is whole; the sends go on for
    // kExchangeGraceSteps more for the peer's sake -- it may have begun its
    // own tail up to a whole grace after ours, and our chunks before that
    // were drained by its still-live session.
    if (exchange_->Pump()) --exchangeGrace_;
    if (exchangeGrace_ <= 0 || exchangeSteps_ >= kExchangeCapSteps) finishDesync_();
}

void UntitledFighterMode::finishDesync_() {
    const std::uint32_t frame = static_cast<std::uint32_t>(report_.frame);
    const std::uint32_t tick  = frame + 1;

    // The verdict, in the order the evidence can run out: our own state, a
    // wire to the peer, the peer's state, a difference at all (identical
    // bytes under disagreeing checksums is a finding about the checksum, not
    // the kernel), and finally the field.
    cse::game::Divergence        divergence{};
    const cse::game::Divergence* named = nullptr;
    std::string                  what;
    if (!mineHeld_) {
        what = "this side no longer held tick " + std::to_string(tick) + " (the history keeps " +
               std::to_string(cse::game::StateHistory::kCapacity) + " ticks)";
    } else if (!exchange_) {
        what = peerAddress_.empty() ? std::string("no peer address to exchange states with")
                                    : std::string("no transport to exchange states on");
    } else if (!exchange_->Complete() ||
               exchange_->Theirs().size() != sizeof(cse::kernel::GameState)) {
        what = "the peer's state did not arrive in " + std::to_string(exchangeSteps_) + " steps";
    } else {
        cse::kernel::GameState theirs{};
        std::memcpy(&theirs, exchange_->Theirs().data(), sizeof(cse::kernel::GameState));
        cse::game::FirstDivergence(mineAtDesync_, theirs, &divergence);
        named = &divergence;
        what  = divergence.found
                    ? "field " + divergence.field + " local " + std::to_string(divergence.local) +
                          " remote " + std::to_string(divergence.remote)
                    : "the two states at tick " + std::to_string(tick) + " are byte-identical";
    }

    // The artifact is the library's (one flat object a script reads), named
    // after the slot THIS keyboard played so two peers on one machine never
    // write the same file, in the directory the host chose -- "." is beside
    // the executable, because both hosts run from its directory.
    const std::string json = cse::game::DesyncArtifactJson(
        frame, report_.localChecksum, report_.remoteChecksum, report_.remotePlayer, named);
    const std::string file = "desync_slot" + std::to_string(localSlot_) + ".json";
    const std::string path = artifactDir_.empty() || artifactDir_ == "."
                                 ? file
                                 : (std::filesystem::path(artifactDir_) / file).generic_string();
    std::string writeError;
    const bool  written = cse::game::WriteDesyncArtifact(path, json, &writeError);

    versusNote_ = "desync at frame " + std::to_string(frame) + ": " + what +
                  (written ? " (artifact " + path + ")"
                           : " (artifact not written: " + writeError + ")");
    // The same sentence on the banner: THE MATCH STOPPED is the one place
    // this mode says a match will not tick again, and this is such a match.
    // Ended keeps it so -- FixedTick returns and the training keys are inert
    // until Escape (readControls_).
    fatal_ = versusNote_;
    exchange_.reset();
    lobby_ = Lobby::Ended;
}

// --- Input ---------------------------------------------------------------------

MyCoreEngine::InputMap* UntitledFighterMode::input_() const {
    if (inputOverride_) return inputOverride_;
    return ctx_.app ? &ctx_.app->input() : nullptr;
}

std::vector<std::string> UntitledFighterMode::lobbyArgs_() const {
    if (commandLineSet_) return commandLine_;
    // The Application's argv, whole: the Player has already taken its scene off
    // it and left the `--key value` pairs in place (PlayerMain.cpp), and a
    // null app is a headless test, which reads no command line at all.
    return ctx_.app ? ctx_.app->commandLine() : std::vector<std::string>{};
}

void UntitledFighterMode::bindActions_() {
    MyCoreEngine::InputMap* mapPtr = input_();
    if (!mapPtr) return;
    MyCoreEngine::InputMap& map = *mapPtr;

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
    MyCoreEngine::InputMap* mapPtr = input_();
    if (!mapPtr) return;
    MyCoreEngine::InputMap& map = *mapPtr;
    for (const MoveKey& key : kMoveKeys)      map.clearAction(key.action);
    for (const MoveKey& key : kDirectionKeys) map.clearAction(key.action);
    for (const ControlKey& key : kControlKeys) map.clearAction(key.action);
}

cse::kernel::Input UntitledFighterMode::readPad_() const {
    cse::kernel::Input input{};
    MyCoreEngine::InputMap* mapPtr = input_();
    if (!mapPtr) return input;
    MyCoreEngine::InputMap& map = *mapPtr;

    // isDown, NOT consumePressed -- and since ROADMAP M1.1d that is the RIGHT
    // read rather than a concession. The kernel derives the edge itself (in
    // ReadIntent, against Fighter::prevButtons), because rollback re-simulates
    // a tick from a snapshot and hands Simulate only that tick's bits: an edge
    // computed out here would survive one replay and not the next. What this function owes the kernel is
    // the honest LEVEL every tick, INCLUDING the ticks a button is not held --
    // a reader that dropped those would replay a press as a hold and the kernel
    // would never see a second press.
    //
    // A level read is one half of the pad (ROADMAP M1.3h): it carries HOLDS,
    // and it cannot carry a tap that went down and up between the ticks that
    // run -- slow motion, pause and zero-tick render frames all open such
    // gaps. notePadPresses_ is the other half: it consumes the same keys'
    // press edges on EVERY fixed step into taps_, and FixedTick ORs the
    // pending presses into this read's bits before latching, so the tap
    // arrives as a one-tick pulse the kernel reads its own edge from.
    //
    // Holding a button therefore starts a move ONCE. It used to repeat the move
    // the tick the last one recovered, which is what review point R0 found by
    // playing; holding is now reserved for mechanics that do not exist yet.
    for (const MoveKey& key : kMoveKeys)
        if (map.isDown(key.action)) input.bits |= key.button;
    for (const MoveKey& key : kDirectionKeys)
        if (map.isDown(key.action)) input.bits |= key.button;
    return input;
}

void UntitledFighterMode::notePadPresses_() {
    MyCoreEngine::InputMap* mapPtr = input_();
    if (!mapPtr) return;
    MyCoreEngine::InputMap& map = *mapPtr;

    // consumePressed on the SAME keys readPad_ level-reads, every fixed step
    // including the ones no tick runs on -- that is the point: the steps slow
    // motion and pause skip are exactly where a tap dies (ROADMAP M1.3h).
    // Consuming here does not starve readPad_: isDown is a level and has no
    // latch to eat. Directions are noted too -- a tapped Down or Up between
    // run ticks is a one-tick stance wish on the next, which is the press the
    // player made.
    std::uint16_t pressed = 0;
    for (const MoveKey& key : kMoveKeys)
        if (map.consumePressed(key.action)) pressed |= key.button;
    for (const MoveKey& key : kDirectionKeys)
        if (map.consumePressed(key.action)) pressed |= key.button;
    taps_.Note(pressed);
}

void UntitledFighterMode::readControls_() {
    MyCoreEngine::InputMap* mapPtr = input_();
    if (!mapPtr) return;
    MyCoreEngine::InputMap& map = *mapPtr;

    // WHILE A SESSION IS LIVE, ONE CONTROL (ROADMAP M2.4; DETERMINISM.md T3).
    // The overlay changes what is drawn and nothing else. Every other key on
    // this list pauses, steps, slows, restarts or re-sources the match, and a
    // peer is running the same match -- each of them is a desync, so each is
    // inert. The presses are dropped rather than queued (clearPressLatches, as
    // for a stopped match below), so nothing fires when the session detaches.
    //
    // AND WHILE THE LOBBY IS UP (ROADMAP M2.5), for the same reason one step
    // earlier: the match behind the lobby screen is the one the offer on the
    // wire describes, and a reset, a swap or a stage change would make it a
    // match the peer never agreed to. An ENDED lobby stays inert too -- a
    // refusal is not a training screen with a red line on it; Escape leaves.
    if (SessionLive() || InLobby()) {
        if (map.consumePressed(kActOverlay))
            overlay_ = cse::presentation::NextOverlayMode(overlay_);
        return;
    }

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
    //
    // IN A REPLAY, C IS THE NEXT FILE, not the next character: a replay names
    // its character by hash, so the character swap could only make the file
    // refuse. The same adopt path runs (startCharacter_ re-reads the replay
    // for the character it stands on), so one file is a restart from disk.
    if (map.consumePressed(kActSwap)) {
        if (intent_ == ModeIntent::Replay) {
            ++replayIndex_;
            (void)startCharacter_(characterIndex_);
        } else {
            (void)startCharacter_(characterIndex_ + 1);
        }
        return;
    }

    if (map.consumePressed(kActReset)) resetMatch_();

    // A position change is a RESTART, not a teleport. Moving two fighters in a
    // live match would be presentation writing state the simulation did not
    // produce, which is the one thing ADR-011 forbids outright; going through
    // the same path R does keeps every tick something the session produced.
    //
    // INERT IN A REPLAY, and not consumed (the latch is dropped at the end of
    // the frame like any other press nobody took): the opening position is
    // the file's, and a restart from anywhere else disagrees with the first
    // checkpoint before a key is pressed.
    if (intent_ != ModeIntent::Replay && map.consumePressed(kActStage)) {
        stageMidscreen_ = !stageMidscreen_;
        applyStagePosition_();
        resetMatch_();
    }

    // The overlay cycle (M3.4e): boxes over mesh -> boxes over translucent
    // mesh -> mesh only. It changes what is DRAWN and never a tick, but it is
    // read with the other controls so one press is one step at any frame
    // rate, and it is meaningful with no match loaded.
    if (map.consumePressed(kActOverlay))
        overlay_ = cse::presentation::NextOverlayMode(overlay_);

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
    //
    // A replay played to its end is such a match: FixedTick runs nothing past
    // the last authored tick, so SPACE here would only turn the chip from
    // PAUSED to "running" beside the word OVER. R (above) restarts it.
    if (!matchReady_ || !fatal_.empty() || replayOver_) return;

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
    // Not in a replay: a demonstration is a ScriptedInputSource put in front
    // of the player's slot, and in a replay that slot is the file's. The note
    // on screen says so (refreshDemoNote_) rather than the key going dead.
    if (intent_ != ModeIntent::Replay && map.consumePressed(kActDemo)) startDemonstration_();
}

// --- The tool-assisted player ---------------------------------------------------

bool UntitledFighterMode::demoArmed_() const {
    return analysisReady_ &&
           analysis_.status == cse::data::ProverStatus::Infinite &&
           !analysis_.loop.empty();
}

void UntitledFighterMode::refreshDemoNote_() {
    if (!matchReady_) { demoNote_.clear(); return; }

    // THE REPLAY'S HONEST BRANCH, before the analysis is consulted: TAB is
    // inert here whatever the verdict says, because both slots are the
    // recording's, and a dead key with no sentence is the failure every other
    // branch of this function exists to prevent.
    if (intent_ == ModeIntent::Replay) {
        demoNote_ = "Demonstrate is inert in a replay: both slots are the recording's, and a "
                    "rehearsal put in front of one would make the file's own checkpoints "
                    "disagree with the fight on screen. R restarts the file from tick 0.";
        return;
    }

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
    // dt feeds ONLY the hot-reload poll interval below. The session itself
    // takes no dt at all: it is a tick index, not a clock.
    ++modeTicks_;

    // BEFORE the match check, so the character key still works on a machine where
    // the content did not stage. A screen whose only escape is Escape teaches the
    // playtester nothing about the file that is missing. readControls_ carries
    // the same check INSIDE it, at the line below which every control needs a
    // running match -- being called early buys C and R their reach and buys the
    // rest of them nothing.
    readControls_();

    // AFTER the controls, so an explicit C or R this step acts first and a
    // just-swapped character starts from fresh stamps; ALSO before the match
    // check, so a fixed or newly staged file revives the honest-error screen
    // without a keypress (ADR-016) -- and clears `fatal_`, which is about an
    // input log this restart has just replaced.
    //
    // Not while a session is live: a landed edit is a restart, and a restart
    // under a peer is a desync (T3). The edit lands when the session detaches,
    // because the stamps still differ then.
    //
    // And NEVER for Versus: the offer on the wire carries the hash of the
    // data this match was built from (A4), and an edit landing behind the
    // lobby screen would rebuild the data under an offer the peer may already
    // have agreed to -- a lobby that said "same content" about content we no
    // longer hold. The authoring loop is training's.
    //
    // Nor behind an ended lobby of ANY intent: a desync's verdict ends the
    // match for the visit, and an edit landing under it would rebuild a
    // match behind a screen that says the match is over.
    //
    // Nor for Replay: the file names the data it was recorded against by
    // hash, so a landed edit would rebuild data the file must refuse and
    // turn the fight on screen into a CharacterChanged sentence mid-play.
    // The authoring loop is training's; a stale replay is the cook's to
    // re-run.
    if (!SessionLive() && !InLobby() && intent_ == ModeIntent::Training) pollHotReload_(dt);

    // --- the lobby (ROADMAP M2.5) ----------------------------------------------
    //
    // The local match does not tick behind the lobby screen -- not while the
    // handshake is in flight and not after it ended. A tick here would be a
    // tick the peer never ran, and an ended lobby is not a training screen.
    // The pump is one step of the handshake; Agreed attaches the session and
    // the NEXT step takes the live branch below.
    if (InLobby()) {
        lobbyStep_();
        return;
    }

    if (!matchReady_ || !fatal_.empty()) return;

    // EVERY fixed step, before the run gate: the press edges this step made,
    // held for the next tick that runs. After the match check on purpose --
    // a press aimed at no match should not fire on the first tick of the next
    // one (resetMatch_ clears taps_ for the same reason).
    //
    // Not in a replay: no tick reads the pad there, so a tap noted here would
    // sit in the accumulator for the visit and be spent into the first
    // training-intent tick of a later one -- a press nobody made.
    if (intent_ != ModeIntent::Replay) notePadPresses_();

    // --- a live session decides (ROADMAP M2.4; DETERMINISM.md T1, T2, T3) ------
    //
    // The pump runs once per fixed step at the real rate (core/FrameGate.h keeps
    // the Application's pause and time scale out of it) and the SESSION says
    // how many ticks that is: zero while the peer has not answered, several
    // when it rolls back, never one dropped -- the fixed step's backlog rule
    // caps pumps, not ticks. The pad is offered only on a frame the session
    // will take (SessionDriver.h: one input per session frame, and a re-offer
    // during a stall is ignored), and the taps are spent into THAT offer and no
    // other, so a tap made during a stall waits for the frame that carries it
    // instead of vanishing into an offer nobody recorded. N4's order holds here
    // as on the training path: spent before the record is written; the record
    // is the session's input ring rather than local_.
    if (SessionLive()) {
        // The handshake's grace resends (Handshake.h): the last offer of a
        // two-way exchange can be lost, so a peer that agreed keeps saying so
        // for a while, beside the session that was created on it. Only while
        // the lobby is LIVE -- the desync tail stops this pump, because a
        // pumped Handshake drains the raw transport the state exchange reads.
        if (handshake_ && lobby_ == Lobby::Live) handshake_->Pump();
        cse::kernel::Input padIn = readPad_();
        if (driver_.AcceptsInput()) padIn.bits = taps_.Spend(padIn.bits);
        const int ran = driver_.Frame(padIn);
        if (!driver_.Fatal().empty()) {
            // The session asked for something no FightSession can answer. A
            // match that ran on from here would be one the peer is not
            // simulating; it stops, and says why, like the latch refusal below.
            fatal_ = "session: " + driver_.Fatal() +
                     " This mode stopped the match rather than run a tick the session did not ask for.";
            return;
        }
        if (ran > 0) latchHitAdvantage_();
        // The report, the grace, and -- when the grace runs out -- the tail
        // that ends the match (ROADMAP M2.5). After the pump, so it sees this
        // step's report; it can detach the session, and the next step takes
        // the lobby gate above.
        pollDesync_();
        // And the peer's silence, which GekkoNet reports as a count that
        // went 1 -> 0 and then keeps advancing past (peakPeers_). Guarded:
        // the tail may just have detached.
        if (SessionLive()) pollDisconnect_();
        return;
    }

    // --- whether a tick runs at all (training and replay) ---------------------
    //
    // This is the whole of pause, slow motion and frame step. FightSession owns
    // no clock, so all three are decided here and none of them is visible to the
    // simulation: the same ticks run, in the same order, with the same inputs.
    //
    // A replay past its last authored tick runs NOTHING, whatever the keys
    // say: SPACE or `.` would otherwise run a tick both slots answer with
    // NEUTRAL, a fight nobody recorded (replayTick_ paused the mode when it
    // set the flag; this is the half that survives an unpause). R clears it.
    if (replayOver_) {
        pendingSteps_ = 0;
        return;
    }
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

    // --- a replay drives both slots (ROADMAP M2.5; ADR-022 D3, D4) ------------
    //
    // The training clock above, and none of the latch below: the file is the
    // input log, already written, for both slots. The session asks the two
    // bound ReplayInputSources itself.
    if (intent_ == ModeIntent::Replay) {
        replayTick_();
        return;
    }

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
    cse::kernel::Input padIn = readPad_();
    // The taps the skipped steps collected, delivered as part of THIS tick's
    // recorded input -- pre-latch, so replay and rollback see the same bytes.
    padIn.bits = taps_.Spend(padIn.bits);
    if (!local_.Latch(tick, padIn)) {
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
    // IT IS NOT ACCEPTABLE with a peer, and with a peer this line does not run:
    // the live branch above takes the tick count from the session's Advance
    // events (ROADMAP M2.4; DETERMINISM.md T1, T2). A recorded or verified
    // replay will want the same branch with a different session behind it.
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
    MyCoreEngine::InputMap* map = input_();

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
    if (map && map->wasPressed("Quit")) requestExit();

    // The 3D presentation is written HERE, in the variable phase, because the
    // host runs Scene::UpdateTransforms and the camera director after every
    // update subscriber and before the render (Application::RunLoop): a
    // Transform written in Draw would be a frame late. Composition is a pure
    // function of the session's state; nothing is remembered between frames
    // but the camera's deadzone centre (ADR-019 D3).
    reconcile_();
}

// --- Drawing --------------------------------------------------------------------

FightHudModel UntitledFighterMode::HudModel() const {
    FightHudModel model{};

    model.matchReady = matchReady_;
    model.modeWord   = ModeWord(intent_);
    model.character  = &character_;
    model.analysis   = analysisReady_ ? &analysis_ : nullptr;
    model.watcher    = watcher_.get();
    model.bindings   = &bindings_;

    model.setupError    = &setupError_;
    model.analysisError = &analysisError_;
    model.demoNote      = &demoNote_;
    model.reloadNote    = &reloadNote_;
    model.presentationNote = &presentationNote_;
    model.overlayMode   = cse::presentation::OverlayModeName(overlay_);
    model.reloadFailed  = reloadFailed_;
    model.fatal         = &fatal_;

    model.modeTicks   = modeTicks_;
    model.hostHz      = ctx_.app ? ctx_.app->fixedTimestepHz() : 0.0f;
    model.paused      = paused_;
    model.slowDivisor = slowDivisor_;
    // THE SLOT THIS KEYBOARD PLAYS, which is kPlayerSlot in training and
    // versus.json's slot in Versus (AttachSession's padSlot, either way). The
    // analysis, the judge and the latched advantage still speak of kPlayerSlot
    // -- one attacker, the training dummy's opponent -- so on a slot-1 host
    // "your fighter" is the fighter you drive while the verdicts are about
    // p[0]; the Versus HUD hides the judge for that reason (ROADMAP M2.5).
    model.playerSlot  = localSlot_;
    // The second slot is a TRAINING DUMMY only in training. In versus it is a
    // peer's fighter and in a replay both slots are recorded players; a HUD
    // that typed the training words would be reporting a mode it is not in.
    // Keyed on playerSlot rather than on kPlayerSlot so that the day the local
    // side plays slot 1 the labels move with it.
    switch (intent_) {
        case ModeIntent::Replay:
            model.slotLabel[0] = "P1";
            model.slotLabel[1] = "P2";
            break;
        case ModeIntent::Versus:
            model.slotLabel[model.playerSlot]     = "YOU";
            model.slotLabel[1 - model.playerSlot] = "PEER";
            break;
        case ModeIntent::Training:
            model.slotLabel[model.playerSlot]     = "YOU";
            model.slotLabel[1 - model.playerSlot] = "TRAINING DUMMY";
            break;
    }
    // Never armed in a replay: the key is inert there (readControls_) and the
    // panel must not promise what the key will not do.
    model.demoArmed   = intent_ != ModeIntent::Replay && demoArmed_();
    model.stageMidscreen = stageMidscreen_;
    // BY VALUE, and it is the only field here that is a measurement rather than a
    // reading. See FightHudModel::hitAdvantage and latchHitAdvantage_.
    model.hitAdvantage = hitAdvantage_;
    model.stageHalfWidthSub = stageHalfWidthSub_;

    // The lobby (ROADMAP M2.5): the screen instead of the match while the
    // handshake is in flight or ended; the one sentence about the wire in
    // every state, including LIVE, so the test that reads it and the screen
    // that draws it read one string.
    model.lobby          = InLobby();
    model.lobbyEnded     = lobby_ == Lobby::Ended;
    model.sessionNote    = &versusNote_;
    model.peer           = &peerAddress_;
    model.port           = versus_.port;
    model.connectedPeers = liveSession_ != nullptr ? liveSession_->ConnectedPeers() : 0;
    // The session's own numbers, read off the driver and the session (ADR-022
    // D4): the frame it is on, how far ahead of the peer GekkoNet says we
    // run, and the ticks its rollbacks re-ran. Zero for a training match.
    model.sessionLive   = liveSession_ != nullptr;
    model.sessionFrame  = driver_.CurrentFrame();
    model.framesAhead   = liveSession_ != nullptr ? liveSession_->FramesAhead() : 0;
    model.rollbackTicks = driver_.Counts().rollbackTicks;
    // TRAINING'S VERDICTS ARE HIDDEN IN VERSUS. The judge watches p[0] as the
    // one attacker against a silent dummy; the peer's fighter is neither,
    // R and TAB are inert (T3), and the first rollback flips the watcher
    // Stale for the rest of the match (ComboWatcher.h). A judge about a
    // fight nobody is judging is a second answer, so the mode says "none".
    model.verdictPanels = intent_ != ModeIntent::Versus;
    model.controls      = ModeControls(intent_);

    // The replay (ADR-022 D4): where the file stands, whether it is over,
    // what the verifier has compared and agreed, and whether the last tick
    // re-ran -- each a member read or a count the observer kept, never a
    // second tally here.
    model.replay                    = intent_ == ModeIntent::Replay;
    model.replayTicks               = replay_.TickCount();
    model.replayOver                = replayOver_;
    model.replayNote                = &replayNote_;
    model.replayCheckpointsCompared = verifier_ ? verifier_->CheckpointsCompared() : 0u;
    model.replayCheckpointsAgreed   = verifier_ ? verifier_->CheckpointsAgreed() : 0u;
    model.lastTickResimulated       = history_ ? history_->lastResimulated : false;

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
        //
        // UNLESS A SESSION IS LIVE. Then the driver hands the kernel both
        // slots' bits straight from the session (SessionDriver.h) and the
        // latched log is not written at all, so playerSource_->Active would
        // answer YOU or DEMO about ticks it never authored. One word, and it
        // is the truthful one.
        //
        // AND WITH NO FALLBACK BOUND -- a replay -- the word is whatever source
        // the session holds for the slot, in that source's own Name()
        // ("REPLAY", ReplayInputSource): read off the binding, so the chip
        // cannot say REPLAY about a slot something else is feeding.
        if (liveSession_ != nullptr) {
            model.speaking = "NET";
        } else if (playerSource_) {
            const cse::game::IInputSource* const active =
                playerSource_->Active(model.tick);
            model.speaking = active != nullptr ? active->Name() : nullptr;
        } else if (const cse::game::IInputSource* const bound =
                       session_.InputSourceFor(kPlayerSlot)) {
            model.speaking = bound->Name();
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

    viewportW_ = widthPx;
    viewportH_ = heightPx;
    // The fight is drawn only when it is the screen: not behind the lobby,
    // whose match nobody has joined yet (or ever will, if it ended).
    const bool fight   = matchReady_ && !InLobby();
    const bool scene3d = fight && scene3d_.Active() && ctx_.scene && scene3d_.Valid(*ctx_.scene);

    // Opaque, full screen. This mode owns the screen (OwnsScreen), and the 3D
    // pass still ran over whatever scene the host had loaded, so without this the
    // fight would be painted over a menu backdrop it has nothing to do with.
    // UNLESS the 3D pass just drew the fighters (M3.4c): then it is the
    // picture, and the backdrop would paint over it.
    if (!scene3d)
        r2d.DrawQuad({ 0.0f, 0.0f },
                     { static_cast<float>(widthPx), static_cast<float>(heightPx) },
                     kBackdrop, 0);

    if (fight) {
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
        const MyCoreEngine::Camera2D cam =
            FightCamera(session_.State(), widthPx, heightPx,
                        stageHalfWidthSub_, cameraCentrePx_);
        cameraCentrePx_ = cam.position.x;   // the deadzone's memory
        r2d.BeginWorld(cam, widthPx,
                       heightPx);
        const cse::presentation::OverlayLook overlay = cse::presentation::OverlayLookFor(overlay_, scene3d);
        DrawFightWorld(r2d, session_.State(), session_.Data(), stageHalfWidthSub_,
                       /*boxesOnly*/ scene3d, overlay.drawBoxes);
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

    DrawFightHud(r2d, *ctx_.font, widthPx, heightPx, HudModel());
}

} // namespace untitledfighter
