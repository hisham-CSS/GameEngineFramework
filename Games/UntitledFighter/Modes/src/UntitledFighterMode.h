// "Untitled Fighting Game", as the three modes a general-purpose host can enter:
// TRAINING, REPLAY and VERSUS -- one class, constructed with a ModeIntent, one
// presentation (ADR-022 D1). The intent decides where the second slot's bits
// come from and nothing else -- a silent dummy, both slots of a committed
// replay file, a peer over CseNet -- so WHAT THIS IS below is written for
// training, and the two paragraphs after the live-session ones say what the
// other two intents add. Which intent an instance is, and why it never
// changes, is at ModeIntent.
//
// ---------------------------------------------------------------------------
// WHAT THIS IS
// ---------------------------------------------------------------------------
// A fight a playtester can stand in front of, drive with a keyboard, freeze one
// tick at a time, and be judged by. It loads a shipped character, runs the
// analysis over it, starts a FightSession with the dummy IN THE CORNER (which is
// the only position the decision procedure answers for), draws the boxes the
// kernel actually built, and reports what the combo judge makes of whatever the
// player just did.
//
// The pieces below it were all already there and none of them changed for this:
// the sim is CseKernel, the content is CseData, and the session, the input
// sources, the demonstration rehearsal and the combo judge are CseGame. This
// file is the HOST -- it decides when a tick runs and where the bits come from,
// and it draws. It is the only thing in the title that knows what a window is.
//
// ---------------------------------------------------------------------------
// FRAME STEP, SLOW MOTION AND PAUSE ARE NOT FEATURES HERE
// ---------------------------------------------------------------------------
// FightSession owns no clock and no frame rate -- FightSession.h is emphatic
// about it -- so all three are this file deciding whether to call Tick() on a
// given fixed step. There is no timeScale, no accumulator and no special path:
// paused is "do not call it", slow motion is "call it every Nth step", and frame
// step is "call it exactly once". That is why they cost about six lines between
// them, and it is the payoff for the session not owning a timestep.
//
// WITH A LIVE SESSION (AttachSession, ROADMAP M2.4) THEY ARE NOT EVEN
// DECISIONS. A peer is running the same match, so the session's Advance events
// are the tick count -- zero on a frame is legal, dropping one is not -- and
// pause, step, slow motion, reset, the character swap, the stage position,
// Demonstrate and hot reload are inert until DetachSession
// (docs/DETERMINISM.md T1, T3). The Application's own pause and time scale stand
// down too, through core/FrameGate.h.
//
// AND A DESYNC IS THE MATCH'S LAST ACT (ROADMAP M2.5; DETERMINISM.md T4, T6;
// ADR-002 CHOICE C). The session reports a frame and two checksums and nothing
// more; this mode holds the last 128 states (a StateHistory observer), keeps
// the session pumping for a grace so the peer detects too, then detaches,
// swaps states with the peer on the raw transport, names the FIRST FIELD the
// two disagree about on the HUD and in desync_slot<N>.json, and stops --
// never a correction. All of it is counted in fixed steps, because a mode is
// called and never calls; nothing here blocks inside a step.
//
// REPLAY (ADR-022 D3, D4) IS THIS FIGHT WITH BOTH SLOTS READ OFF ONE FILE.
// UntitledFighter/Replays/base.csrp is opened against the hash of the match
// this mode builds, so a stale recording is the honest-error screen with the
// regenerate command in it, never a match played against different data
// (docs/DETERMINISM.md S9). A ReplayVerifier rides along; a checkpoint that
// disagrees is the red banner, never a correction. Pause, step and slow motion
// still work because nothing else is simulating -- this file still decides
// whether Tick() runs -- while V, TAB and hot reload are inert; the file's last
// tick pauses the mode and R re-Begins it from tick 0 on the same two sources.
//
// VERSUS (ADR-022 D2, D5) IS THIS FIGHT BEHIND A LOBBY THIS FILE DRAWS. Slot,
// port and peer come from UntitledFighter/versus.json, overridden by
// --slot/--port/--peer on the command line; the mode forces the corner opening
// (both peers must Begin identical), binds a UdpTransport, runs the Handshake
// over the built data's hash and, on Agreed, attaches an online session at
// tick 0 -- AttachSession Begins the match structurally, so the session's
// frame F is the kernel's tick F and the history above is indexable by the
// report's frame. Every lobby sentence is this file's, the loader's or the
// transport's, verbatim. Ended is sticky within the visit: refused, timed out,
// desynced, the peer gone silent past the session's disconnect timeout, or the
// owned session detached by the host, the match does not tick and the match
// controls are inert until Escape. SetTransport, SetCommandLine, SetPeerAddress,
// SetArtifactDirectory and SetDisconnectTimeoutMs are the headless seams beside
// SetInputMap; tests/test_fight_mode.cpp enters every intent through them.
//
// The mode's pause is deliberately NOT Application::setPaused. That would stop
// the host's variable-rate updates too, which is where the registry drains a
// mode's exit request -- a mode that paused the application and then pressed
// Escape would find its own Back key dead (GameMode.h says so at the drain).
//
// ---------------------------------------------------------------------------
// LATCH, THEN TICK. IN THAT ORDER, AND IT IS THE ORDER THAT MATTERS
// ---------------------------------------------------------------------------
// The keyboard is read and WRITTEN DOWN (LatchedInputSource::Latch) for tick T
// before the session is asked to run tick T. From that moment the answer to
// "what did the player press on tick T" is a pure function of T, forever, which
// is what makes this live match recordable, re-simulable and rollback-correct
// without any of those features existing yet. Reading the pad from inside the
// tick instead would make the input a function of when the tick happened to run.
//
// InputSource.h calls latching "the local pad made pure by writing it down"; this
// file is the half of that sentence with the hardware in it.
//
// ---------------------------------------------------------------------------
// EVERY NUMBER ON SCREEN IS READ, NOT RECOMPUTED -- WITH ONE MEASUREMENT
// ---------------------------------------------------------------------------
// The HUD is handed pointers to the GameState, the MatchData, the MoveIndexMap,
// the ProverResult and the ComboWatcher, and it reads them. This mode keeps no
// parallel tally of hits, no shadow copy of a position and no idea of its own
// about how long a move is. See FightHud.h for the two places something has to be
// derived and for the kernel fields each derivation names.
//
// THE EXCEPTION IS ONE VALUE AND IT IS AN OBSERVATION RATHER THAN A COPY.
// Frame advantage is a property of a HIT, not of an instant, and asking for it
// every frame produced a number that meant something different on every tick --
// it inverted whenever a held button restarted the move, several times a second,
// contradicting the combo judge underneath it. So it is measured ONCE, on the
// tick a hit connects, and carried with the tick number it was measured on.
// latchHitAdvantage_ is the whole of it; the argument is at LatchedAdvantage in
// FightHud.h. The distinction that keeps it honest is that it answers a question
// about a tick that has gone, and says which one, so there is nothing live for it
// to drift from -- which is exactly what ComboWatcher has always done with hits,
// damage and gapTicks one layer down.
#pragma once

// The whole engine surface through one header, the way both hosts include it --
// only Engine/include is a PUBLIC include directory, so there is no narrower
// include to write, and inventing one for a title would mean widening the
// engine's exported paths for a consumer's convenience.
#include "Engine.h"

#include "FightHud.h"
#include "FightView.h"

#include "cse/data/CharacterData.h"
#include "cse/data/CharacterFileWatch.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/data/VersusConfig.h"

#include "cse/game/ComboWatcher.h"
#include "cse/game/Desync.h"
#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"
#include "cse/game/Replay.h"

#include "cse/presentation/FighterClips.h"
#include "cse/presentation/FightPresentation.h"

#include "FightScene.h"
#include "SessionDriver.h"

#include "cse/kernel/GameState.h"
#include "cse/net/BlobExchange.h"
#include "cse/net/Handshake.h"
#include "cse/net/ISession.h"
#include "cse/net/UdpTransport.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace untitledfighter {

// WHICH OF THE THREE MODES an instance is (ADR-022 D1). One class, three
// registry entries, one presentation: the intent decides where the second
// slot's bits come from -- a silent dummy, a replay file, or a peer over
// CseNet -- and which words the HUD uses for the title and the two slots.
// It is a constructor argument and never changes for the life of the
// instance, because the registry constructs a mode once and enters it as
// many times as the player likes (GameMode.h): an intent that could change
// between visits would make "which menu verb is this" a question with two
// answers.
//
// Not `Intent`: cse::kernel::Intent already names the kernel's own thing, and
// this header includes the kernel.
enum class ModeIntent : std::uint8_t { Training, Replay, Versus };

class UntitledFighterMode final : public MyCoreEngine::IGameMode {
public:
    // Training by default, so a bare `UntitledFighterMode` is the mode this
    // file has always been -- the registry names the intent, a test may not.
    explicit UntitledFighterMode(ModeIntent intent = ModeIntent::Training)
        : intent_(intent) {}

    // A Versus visit may own an online session, and the session holds a
    // pointer to this mode's Handshake in a process-global static until
    // DestroySession (GekkoSession.cpp). A host that skipped Exit would
    // otherwise leave that static pointing at a dead Handshake and one of the
    // process's four bridge slots held for good. The Application's own
    // session flag is NOT touched here -- ctx_.app may already be gone.
    ~UntitledFighterMode() override;

    ModeIntent Mode() const { return intent_; }

    // EXACTLY the strings the ENGINE's demo menu shows, one per intent. The
    // host draws them verbatim -- no uppercasing, no truncation -- so this is
    // the one place the three names are written for that menu. The title's
    // own front end TYPES its verbs and wires them to the registration slots
    // (Assets/UntitledFighter/UI/menu.cxml), so it never reads these; the two
    // agree by registration order, which is what the registry test pins.
    const char* DisplayName() const override {
        switch (intent_) {
            case ModeIntent::Replay:   return "Replay";
            case ModeIntent::Versus:   return "Versus (online)";
            case ModeIntent::Training: break;
        }
        return "Untitled Fighting Game";
    }

    bool Enter(const MyCoreEngine::GameModeContext& ctx, std::string& error) override;
    void Exit() override;
    void FixedTick(float dt) override;
    void Update(float dt) override;
    void Draw(MyCoreEngine::Renderer2D& r2d, int widthPx, int heightPx,
              float dt) override;

    // --- the headless seam and the live session (ROADMAP M2.4) ---------------
    //
    // The InputMap this mode binds its keys on and reads them from. Null (the
    // default) means the Application's, through ctx_.app; a test hands in its
    // own so the mode runs with no window at all (tests/test_fight_mode.cpp),
    // which is what let the tick-loop rules below be held by a test rather
    // than by the eye. Set before Enter, which binds the actions.
    void SetInputMap(MyCoreEngine::InputMap* map) { inputOverride_ = map; }

    // --- the lobby's seams (ROADMAP M2.5, ADR-022) ---------------------------
    //
    // A caller-owned transport, used for EVERY intent in place of the UDP
    // socket the Versus lobby would otherwise bind on versus.json's port: a
    // test hands in a LoopbackNetwork endpoint so two modes reach LIVE, or
    // exchange a desync's state, in one process with no socket at all -- a
    // real bind of 47011 under `ctest -j 4` collides with tests/two_peers.py.
    // Borrowed; it must outlive the mode's Exit. Null (the default) means the
    // lobby binds its own. Set before Enter.
    void SetTransport(cse::net::ITransport* transport) { transport_ = transport; }

    // How long the session lets the peer stay silent before it drops it and
    // the mode ends the match (pollDisconnect_). WALL CLOCK -- GekkoNet's
    // steady_clock, not a frame count (ISession.h
    // SessionConfig::disconnectTimeoutMs) -- so nothing a test can call
    // advances it: a test holds the drop in ~100 ms through this seam, where
    // the library's default would make the disconnect test sleep five real
    // seconds on every run, and a five-second test is the one that stops
    // being run. 5000 by default so the shipped hosts are unchanged: a player
    // on a lossy link keeps the five seconds GekkoNet gives. Written into the
    // SessionConfig the lobby builds in goLive_; a session a caller creates
    // and attaches by hand carries its own config. Set before Enter.
    void SetDisconnectTimeoutMs(std::uint32_t ms) { disconnectTimeoutMs_ = ms; }

    // The command line the lobby reads --slot/--port/--peer from
    // (cse::data::ApplyVersusOverrides), in place of the Application's
    // (Application::commandLine(), whole argv). A test sets it because it has
    // no Application; setting it EMPTY is a real choice ("read nothing"),
    // which is why unset and empty are told apart. Set before Enter.
    void SetCommandLine(std::vector<std::string> args) {
        commandLine_    = std::move(args);
        commandLineSet_ = true;
    }

    // The address the wire's peer is spoken to at, for every intent. The
    // Versus lobby sets the same member from versus.json; a test that
    // bypasses the lobby (a session it created itself, attached to a
    // Training-intent mode) sets it here so the mode still knows who to send
    // a desync's state to. Set before Enter.
    void SetPeerAddress(const std::string& address) { peerAddress_ = address; }

    // Where desync_slot<N>.json is written. "." by default, which is "beside
    // the executable" because both shipped hosts run from its directory (the
    // same convention tests/online_peer.cpp keeps). A test points two modes at
    // a directory of its own, because tests/two_peers.py writes the default
    // names into the tests directory under `ctest -j 4`. Set before Enter.
    void SetArtifactDirectory(const std::string& directory) { artifactDir_ = directory; }

    // THE LOBBY SCREEN IS UP instead of the match: the handshake is in flight,
    // a desync's state is being exchanged, or the lobby ended (refused, timed
    // out, could not bind, the peer disconnected, the host detached the owned
    // session, or the desync verdict is in) and waits for Escape. While this is true the
    // local match does not tick, the training controls are inert (the overlay
    // key excepted), and Draw shows the lobby, never the fight behind it.
    // False in training, and false while a Versus session is LIVE -- that is
    // the match, and SessionLive() says so.
    bool InLobby() const {
        return lobby_ == Lobby::Handshaking || lobby_ == Lobby::Exchanging ||
               lobby_ == Lobby::Ended;
    }

    // A live session. From here until DetachSession the SESSION decides how
    // many ticks run -- zero on a frame is legal, dropping one is not
    // (DETERMINISM.md T1) -- and every training control that would pause, step,
    // slow, restart or re-source the match is inert (T3), because a peer is
    // running the same match and each of those is a desync. Borrowed; the
    // session must have been created with SessionDriver::WireConfig. `padSlot`
    // is the slot this keyboard plays; `localSlots` has a bit for every slot
    // this host supplies (a local session: both; online: one). False with
    // `error` when there is no running match to attach to; when `padSlot` is
    // not a slot of the two-player match or `localSlots` lacks its bit (the
    // driver offers the pad only to the slot that is both, so either mistake
    // is a keyboard offered to nobody and a match that runs on with a dead pad
    // and nothing on screen saying so); or when this mode is a Replay (ADR-022
    // D3: the file drives both slots, nothing attaches to it). A refused
    // attach writes no member -- the match is not re-Begun.
    bool AttachSession(cse::net::ISession* session, int padSlot, std::uint8_t localSlots,
                       std::string& error);
    // Unbinds a BORROWED session and hands the training controls back (the
    // M2.4 tests rely on that). A session THIS MODE OWNS -- the Versus
    // lobby's -- is a match with exactly one other copy, so detaching it
    // ENDS THE VISIT instead: the session is destroyed, the handshake dropped,
    // and the lobby is Ended with "the session was detached; the match
    // stopped", sticky like every other Ended. Before that a host's
    // DetachSession on a LIVE Versus mode left lobby_ at Live with the owned
    // session and handshake alive, and the local match resumed ticking under
    // the pad with pause, reset and the swap back -- a fight the peer had
    // left, dressed as training.
    void DetachSession();
    bool SessionLive() const { return liveSession_ != nullptr; }
    const SessionDriver& Driver() const { return driver_; }

    // Facts a host or a test reads off the mode. Each is a member read, never
    // a second answer; the HUD reads the same members.
    bool               MatchReady()   const { return matchReady_; }
    const std::string& SetupError()   const { return setupError_; }
    const std::string& Fatal()        const { return fatal_; }
    std::uint32_t      CurrentTick()  const { return session_.CurrentTick(); }
    const cse::kernel::GameState& State() const { return session_.State(); }
    bool               Paused()       const { return paused_; }
    std::uint32_t      PendingSteps() const { return pendingSteps_; }
    int                SlowDivisor()  const { return slowDivisor_; }

    // The desync the session reported, and the state this mode compared for
    // it -- the one after the reported frame's ADVANCE, tick frame + 1
    // (cse/game/Desync.h: the frame is not the tick). Null until a tail has
    // begun, or when that tick had left the history. Both exist so a test can
    // hold the off-by-one at THIS level: Checksum(*StateAtDesync()) must equal
    // LastDesync().localChecksum, or the field named on the HUD is about a
    // tick the session never compared.
    const cse::net::DesyncReport& LastDesync() const { return report_; }
    const cse::kernel::GameState* StateAtDesync() const {
        return mineHeld_ ? &mineAtDesync_ : nullptr;
    }

    // --- the replay (ROADMAP M2.5; ADR-022 D3, D4) ---------------------------
    //
    // The file has been played to its last authored tick: the tick index
    // stands at its tick count, the mode paused itself, and no tick runs
    // until R restarts it -- FightSession would feed both slots NEUTRAL
    // forever past the end, and a fight nobody recorded is not the replay.
    bool ReplayOver() const { return replayOver_; }
    // The verifier's verdict so far (cse/game/Replay.h ReplayDivergence):
    // whether a checkpoint disagreed and where, whether the bits fed in were
    // the recording's. Null when no replay is loaded. A divergence stops the
    // match (Fatal) and is never corrected -- it is a finding about the
    // kernel having changed since the file was recorded.
    const cse::game::ReplayDivergence* ReplayVerdict() const {
        return verifier_ ? &verifier_->Result() : nullptr;
    }

    // The options every match this mode starts is built with -- the body
    // numbers and the (button x stance) binding table. Public and static
    // because the SHIPPED REPLAY'S HASH DEPENDS ON IT: base.csrp was cooked
    // by UntitledFighterCatalogue against Catalogue.cpp's own copy of the
    // same table, and MoveDef::button is part of the hashed MatchData, so
    // the two tables must build identical bytes or the Replay intent refuses
    // the file as "character changed".
    // FightMode.TheCatalogueAndTheModeBuildTheSameMatchData pins that.
    static cse::data::BuildOptions MatchBuildOptions();

    // Everything the HUD draws, as the HUD sees it: pointers into this mode's
    // members plus the host's own scalars (FightHud.h). Public so a test can
    // read the words the screen would show without a GL context -- the
    // presentation is one struct and one Draw for all three intents, and
    // this is where that is checkable. THE MODEL BORROWS: a caller must not
    // hold one across a FixedTick or a Frame, because a character swap, a
    // reset or a reload replaces the objects it points at.
    FightHudModel HudModel() const;

private:
    // Load, build, analyse and start. Returns false with setupError_ filled in;
    // Enter still succeeds, because a mode that can show something honest should
    // (IGameMode::Enter). Split into prepare/teardown/adopt since ROADMAP M1.5,
    // because hot reload needs the same pieces in a different failure order:
    // C is a SWAP (the old match goes even when the new file is broken), a
    // reload is KEEP-LAST-GOOD (a broken edit leaves the match alone).
    bool startCharacter_(int index);

    // Load + build into the CALLER'S temporaries, touching no member. The
    // reload path points this at scratch objects so a half-typed save -- the
    // normal state while editing -- cannot take the running match down with
    // it; LoadCharacterFile zeroes its output even on a failed parse, which is
    // why pointing it at character_ directly is not an option (ADR-016).
    bool prepareCharacter_(const std::string& file,
                           cse::data::CharacterData& outCharacter,
                           cse::data::MatchBuild& outBuild, std::string& error);

    // Everything the previous character owned goes, in dependency order, and
    // the members the session borrowed are reset. Must run before build_ is
    // reassigned -- a build_ replaced while the session still held a pointer
    // into it would be a dangling MatchData on the very next tick.
    void teardownMatch_();

    // teardownMatch_, then move the prepared pair into the members and run
    // the analyse / watcher / Begin tail. The one adopt path both C and hot
    // reload restart through.
    bool adoptPrepared_(cse::data::CharacterData&& character,
                        cse::data::MatchBuild&& build);

    // The authoring loop (ROADMAP M1.5, ADR-016): poll the loaded file's
    // (mtime, size) stamp, and land an edit by prepare + adopt -- a restart,
    // never a data swap under the live session. Called every fixed step; dt
    // feeds only the watch's poll interval.
    void pollHotReload_(float dt);

    // The host's named actions, bound on entry and cleared on the way out so the
    // mode does not leave its own vocabulary in the shared InputMap.
    void bindActions_();
    void clearActions_();

    // The InputMap every read and binding goes through: the override when a
    // host or test set one, else the Application's, else null (no input at all,
    // and every reader treats that as a released pad).
    MyCoreEngine::InputMap* input_() const;

    // The keyboard and pad as kernel bits. A LEVEL read (isDown), never an edge
    // -- and that is right, not a shortcut. The kernel derives the edge itself
    // from Fighter::prevButtons, because rollback re-simulates a tick from a
    // snapshot and hands Simulate only that tick's bits: an edge computed out
    // here would survive one replay and not the next. What this function owes
    // the kernel is the honest LEVEL, every tick, including the release ticks --
    // a reader that dropped them would replay a press as a hold.
    cse::kernel::Input readPad_() const;

    // The press edges the same keys made THIS fixed step, noted into taps_
    // whether or not a tick runs (ROADMAP M1.3h). This is the half a level
    // read cannot see: a tap made and released between run ticks -- under
    // slow motion, while paused, inside zero-tick render frames -- vanished
    // before the kernel ever saw it. taps_.Spend ORs the pending presses into
    // the next latched input, BEFORE the latch, so replay and rollback read
    // the same bytes the simulation did. The rule itself is pinned against
    // the real InputMap and session in tests/test_press_delivery.cpp; these
    // two call sites are its mirror.
    void notePadPresses_();

    // The training controls, read as EDGES in the fixed phase with
    // consumePressed. Not wasPressed: that is scoped to a rendered frame, and
    // above the fixed rate most frames run no tick at all while a stalled frame
    // runs several -- so a fixed-tick reader of wasPressed misses most presses
    // and multiplies the rest (InputMap.h).
    void readControls_();

    // Corner or midscreen, and the numbers that decide it. See the long note at
    // the opening position in the .cpp: the corner is where the in-engine
    // verdicts mean anything and it is also the one place knockback cannot show,
    // so the mode does both and says which.
    // Where the camera was last frame, world pixels. Presentation state and
    // nothing else reads it -- see FightCamera for why the camera has memory.
    float        cameraCentrePx_   = 0.0f;

    void applyStagePosition_();
    bool         stageMidscreen_   = false;
    // Which overlay the author is looking through (M3.4e): boxes over the
    // mesh, boxes over a translucent mesh, or the mesh alone. Presentation
    // state of the author's choice -- it changes what is drawn, never a tick.
    cse::presentation::OverlayMode overlay_ = cse::presentation::kDefaultOverlayMode;
    std::int32_t bodyHalfWidthSub_ = 0;

    void resetMatch_();
    void startDemonstration_();
    // Puts the current demonstration (or nothing) in front of the local pad and
    // binds the result to the player's slot.
    void bindPlayerSource_();

    // adoptPrepared_'s Replay step, between the build moving in and the match
    // beginning: read kReplays[replayIndex_] against the hash of the data
    // just built. False with setupError_ filled in -- the honest-error screen,
    // with the regenerate command in the sentence, because a refused replay
    // is nearly always a stale one.
    bool loadReplay_();
    // One tick of the replay, after the run gate: no latch (the file authors
    // both slots), the verifier's verdict, and the end of the file.
    void replayTick_();

    // THE ONE MEASUREMENT THIS MODE TAKES, and it is taken here because this is
    // the layer that can see a tick go by.
    //
    // Called immediately after FightSession::Tick(). When a hit connected on the
    // tick that just ran, the frame-advantage answer for it is computed ONCE and
    // held; see LatchedAdvantage in FightHud.h for why a value recomputed every
    // frame from live state cannot be the number a playtester learns from.
    //
    // The contact tick is not detected here. It is READ off
    // ComboReport::lastHitTick -- ComboWatcher signal 3, with the multi-hit guard
    // and the move-started disjunct already applied -- so "a hit landed" has one
    // implementation in the running game and this is a reader of it, not a second
    // one. The watcher is an observer and has therefore already been notified for
    // this tick by the time this runs (FightSession notifies inside Tick()).
    void latchHitAdvantage_();

    // Whether a demonstration is still speaking for the player's slot. The same
    // question the HUD's `scripted tick(s) left` chip asks, asked once here so
    // that the chip and the Demonstrate key cannot disagree about it.
    bool demoInFlight_() const;

    // Whether the analysis handed this character a loop worth performing. Asked
    // rather than remembered, so it cannot disagree with the verdict on screen.
    bool demoArmed_() const;
    // What the Demonstrate key will do, or why it will do nothing, written once
    // and refreshed whenever the answer can have changed.
    void refreshDemoNote_();

    // Set once, by the constructor; see ModeIntent.
    const ModeIntent intent_;

    MyCoreEngine::GameModeContext ctx_{};

    // --- the content, held for the session's whole life ----------------------
    //
    // FightSetup::data is BORROWED, not copied, and FightSession keeps that
    // pointer for every tick including the ones a rollback re-simulates. So the
    // MatchBuild has to outlive the session, which is why it is a member here
    // rather than a local in Enter -- a MatchData built on the stack and handed
    // over would be a dangling pointer by the first tick. The same is true of
    // analysis_, which the ComboWatcher borrows for the same duration.
    int                      characterIndex_ = 0;
    cse::data::CharacterData character_{};
    cse::data::MatchBuild    build_{};
    cse::data::ProverResult  analysis_{};
    bool                     analysisReady_ = false;

    cse::game::FightSession                  session_{};
    std::unique_ptr<cse::game::ComboWatcher> watcher_;

    // The opening position, kept so that Reset and Enter start the SAME match
    // through one description of it rather than two. It borrows build_.data,
    // which is a member and outlives it.
    cse::game::FightSetup setup_{};

    // --- where the bits come from --------------------------------------------
    //
    // The player's slot is always fed through a FallbackInputSource, even before
    // any demonstration exists (a null primary means the secondary answers
    // everything). That is not an optimisation: `Active(tick)->Name()` is the
    // pure question the HUD asks to say whether DEMO or YOU is speaking, and a
    // source that is only wrapped once a demonstration starts has no answer to
    // it the rest of the time.
    //
    // The dummy's slot is bound to NOTHING, which FightSession reads as neutral
    // on every tick -- the silent training dummy the ground-truth recipe
    // prescribes ("input: zero on every tick").
    cse::game::LatchedInputSource                    local_{ 0u, "YOU" };
    std::unique_ptr<cse::game::ScriptedInputSource>  demo_;
    std::unique_ptr<cse::game::FallbackInputSource>  playerSource_;
    // The taps the run ticks never saw (see notePadPresses_ above).
    cse::game::PressAccumulator                      taps_{};

    // --- the replay (ROADMAP M2.5; ADR-022 D3) ---------------------------------
    //
    // THE REPLAY INTENT RE-SOURCES BOTH SLOTS AND CHANGES NOTHING ELSE: one
    // ReplayInputSource per slot, both borrowing replay_ (Replay.h: the
    // pairing is what was recorded, and one flat ReplayData is what keeps a
    // host from playing slot 0's tick T against slot 1's tick T+1), the
    // match begun from the file's own MatchStart, and a ReplayVerifier
    // beside the watcher. replay_ must outlive the sources and the verifier,
    // so teardown resets those first and the data last.
    //
    // kReplays[replayIndex_] is the file (content-root-relative, through the
    // same sandbox as the characters); C advances the index in this intent
    // and leaves the character alone -- a replay names its character by
    // hash, so the swap that made sense for training would only make the
    // file refuse.
    int                                            replayIndex_ = 0;
    std::string                                    replayRel_;
    cse::game::ReplayData                          replay_{};
    std::unique_ptr<cse::game::ReplayInputSource>  replaySrc_[2];
    std::unique_ptr<cse::game::ReplayVerifier>     verifier_;
    bool                                           replayOver_ = false;
    // "replay over at tick N; R restarts it" -- the one sentence the replay
    // screen adds. A divergence goes to fatal_ instead: the banner.
    std::string                                    replayNote_;

    // --- the picture's own facts ---------------------------------------------
    std::vector<BindingRow> bindings_;
    std::int32_t            stageHalfWidthSub_ = 0;

    // The frame advantage of the last hit that connected, and the tick it was
    // measured on. THE ONE THING THIS MODE REMEMBERS ABOUT A TICK THAT HAS GONE.
    //
    // It is not the parallel tally the header's "every number on screen is read"
    // rule forbids: that rule is about a second answer to a question about NOW,
    // which can drift from the simulation's. This is an answer about THEN, it
    // carries the tick it is about, and the HUD prints that tick beside it -- the
    // same shape as ComboReport, which has accumulated hits and damage off past
    // ticks since it was written. Cleared wherever the tick index it names stops
    // meaning anything: a new character, and a reset.
    LatchedAdvantage hitAdvantage_{};

    // --- the seam and the session (ROADMAP M2.4) -------------------------------
    MyCoreEngine::InputMap* inputOverride_ = nullptr;
    cse::net::ISession*     liveSession_   = nullptr;   // borrowed; null = training
    SessionDriver           driver_;
    // The most peers the live session has counted since AttachSession
    // (ISession::ConnectedPeers). THE DROP, NOT THE ZERO, IS THE SIGNAL: an
    // online session counts 0 for its first frames, before the peers have
    // found each other, and a local one counts 0 for its whole life. Once the
    // count has been >= 1 and is 0 again, GekkoNet has marked the peer
    // disconnected after its silence timeout (backend.cpp
    // HandleTooFarBehindActors; SessionConfig::disconnectTimeoutMs, which the
    // lobby fills from SetDisconnectTimeoutMs) -- and it then
    // KEEPS emitting Advance events with neutral inputs for the gone slot, so
    // a kernel that followed them would tick a match nobody else simulates
    // under a HUD that said LIVE and counted 0 peers. pollDisconnect_ ends
    // the match on that transition instead.
    int                     peakPeers_     = 0;
    void pollDisconnect_();

    // --- the Versus lobby (ROADMAP M2.5; ADR-022 D2, D5) ----------------------
    //
    // THE MODE OWNS THE WIRE FOR VERSUS: the transport (unless a caller lent
    // one), the handshake, and the session the handshake agrees to. Every
    // path that created the session destroys it -- Exit, the destructor, a
    // failed attach, the desync tail -- because the process has four bridge
    // slots and a session is one of them until DestroySession.
    //
    // DECLARATION ORDER IS DESTRUCTION ORDER, and it is load-bearing: the
    // handshake wraps the transport (Handshake.h) and the session is created
    // ON the handshake, so udp_ must outlive handshake_ and both must outlive
    // ownedSession_ -- which is why the session is a raw pointer released by
    // hand in dropNet_ before either unique_ptr goes.
    //
    // The arguments the lobby's overrides are read from: the override when one
    // was set, else the Application's whole argv (argv[0] and the scene
    // included -- ApplyVersusOverrides skips positional tokens), else nothing,
    // because a headless test hands Enter a GameModeContext with no app.
    std::vector<std::string> lobbyArgs_() const;
    // The transport the lobby speaks on: the caller's if one was lent (a
    // test's loopback endpoint), else the socket it bound itself. Null before
    // the lobby binds and after it drops.
    cse::net::ITransport*    wire_() const { return transport_ ? transport_ : udp_.get(); }

    // Read versus.json and the overrides, bind, and put the offer on the wire
    // -- or end the lobby with the failing layer's own sentence. Enter's
    // Versus tail, after the character is up.
    void enterLobby_();
    // One fixed step of the lobby: pump the handshake, count toward the
    // timeout, and on Agreed create the session and attach it.
    void lobbyStep_();
    // Agreed: the session, on the handshake, attached at tick 0.
    void goLive_();
    // Detach, destroy the owned session, drop the handshake and the socket.
    // The one teardown Exit, the destructor and the desync tail share.
    void endVersus_();
    void dropNet_();
    // The bare unbind DetachSession, the desync tail and Exit share: driver
    // off, the Application told, the latched log re-based at the kernel's
    // tick. No ownership decision in it -- that is the callers'.
    void unbindSession_();
    // The match is over because the session is: unbind, destroy the session
    // if this mode created it, drop the handshake, and end the lobby with
    // `why` as its sentence. DetachSession (owned), and the peer's
    // disconnect (owned or borrowed -- a borrowed session's owner still
    // decides its life, but a match whose only other copy is gone is over
    // whoever holds the pointer).
    void endSession_(const std::string& why);

    enum class Lobby : std::uint8_t {
        None,          // not Versus, or Versus before Enter / after Exit
        Handshaking,   // the offer is on the wire; waiting for the peer's
        Live,          // the session is attached; the match is the screen
        Exchanging,    // a desync: the session is gone and the two states cross on the raw transport
        Ended          // refused, timed out, could not bind, the peer disconnected, the host detached, or the desync verdict is in
    };
    Lobby                    lobby_          = Lobby::None;
    // The timeout, in FIXED STEPS rather than wall time: the lobby is pumped
    // from FixedTick, and a wall clock in the fixed phase is the smell every
    // other clock decision in this file avoids. 30 s at the host's rate.
    std::uint32_t            lobbySteps_        = 0;
    std::uint32_t            lobbyTimeoutSteps_ = 1800;

    cse::net::ITransport*                   transport_ = nullptr;   // borrowed; null = the lobby binds its own
    std::uint32_t                           disconnectTimeoutMs_ = 5000; // SetDisconnectTimeoutMs -> the lobby's SessionConfig
    std::unique_ptr<cse::net::UdpTransport> udp_;                   // the lobby's own socket
    std::unique_ptr<cse::net::Handshake>    handshake_;
    cse::net::ISession*                     ownedSession_ = nullptr; // ours to DestroySession; liveSession_ borrows it
    cse::data::VersusConfig                 versus_{};
    // The peer as the wire spells it (versus.json's, or SetPeerAddress's):
    // the Handshake matches offers against it and the session names its
    // remote slot with it.
    std::string                             peerAddress_;
    // The slot THIS keyboard plays: AttachSession's padSlot, and versus.json's
    // slot from the moment the lobby opens. The HUD's YOU follows it, and the
    // desync artifact is named after it so two peers on one machine never
    // write the same file.
    int                                     localSlot_ = 0;
    // The lobby's one sentence, in the words of whichever layer wrote it:
    // "waiting for ...", a transport's or handshake's refusal verbatim (D5),
    // "LIVE against ...", and the desync tail's three sentences after it.
    // FightHudModel::sessionNote borrows it.
    std::string                             versusNote_;
    std::vector<std::string> commandLine_;
    bool                     commandLineSet_ = false;

    // --- the desync tail (ROADMAP M2.5; DETERMINISM.md T4, T6) ---------------
    //
    // tests/online_peer.cpp's abort, spread over fixed steps. The session
    // reports the frame whose advance produced the differing state; the
    // history holds the state after it (Find(frame + 1), Desync.h); the two
    // peers swap those on the RAW transport once the session is gone and the
    // handshake is no longer pumped -- a pumped Handshake drains the same
    // transport and would swallow the chunks; FirstDivergence names the field.
    //
    // THE HISTORY PUSHES EVERY TICK, RE-SIMULATED ONES INCLUDED -- the exact
    // opposite of the ComboWatcher's Stale rule, and for the same underlying
    // fact: a rollback's corrected state IS the state GekkoNet checksummed
    // and the peer compared, so the ring must hold the correction, not the
    // prediction. Heap-held: 128 GameStates is more stack than a test
    // fixture holding three modes can afford.
    //
    // It also keeps the LAST view's `resimulated` flag (ROADMAP M2.5, ADR-022
    // D4's one new chip): FightSession computes it inside Tick and hands it
    // to observers only, so the HUD can read "this tick re-ran" off the same
    // observer rather than the mode re-deriving it from two tick counters.
    struct HistoryObserver final : cse::game::ITickObserver {
        cse::game::StateHistory history;
        bool                    lastResimulated = false;
        void OnTick(const cse::game::TickView& view) override {
            history.Push(*view.state);
            lastResimulated = view.resimulated;
        }
    };
    std::unique_ptr<HistoryObserver> history_;

    // The live branch's last act each step: poll the report, count the grace,
    // begin the tail when it runs out.
    void pollDesync_();
    // One shot: detach, destroy the owned session, copy the state at
    // frame + 1 out of the ring, put it on the wire. lobby_ -> Exchanging,
    // or straight to the verdict when there is nothing to exchange on.
    void beginDesyncTail_();
    // One fixed step of the exchange, from lobbyStep_.
    void exchangeStep_();
    // Name the field, write the artifact, stop the match. lobby_ -> Ended.
    void finishDesync_();

    // -1 = no report; N = fixed steps of session pumping left before the
    // tail, so the peer receives the checksums that let it detect too
    // (Desync.h; a session destroyed at once leaves the peer finishing alone).
    int                                     abortGrace_ = -1;
    cse::net::DesyncReport                  report_{};
    // The ring slot for frame + 1 is overwritten 128 ticks later and the
    // exchange takes longer than one step, so the state is COPIED out when
    // the tail begins. `mineHeld_` is whether the ring still had it.
    cse::kernel::GameState                  mineAtDesync_{};
    bool                                    mineHeld_ = false;
    // On wire_(): declared AFTER udp_ so it is destroyed before the socket it
    // holds a reference to; dropNet_ resets it first for the same reason.
    std::unique_ptr<cse::net::BlobExchange> exchange_;
    std::uint32_t                           exchangeSteps_ = 0;
    int                                     exchangeGrace_ = 0;
    std::string                             artifactDir_   = ".";

    // --- the host's decisions about time --------------------------------------
    bool          paused_       = false;
    int           slowDivisor_  = 1;   // 1 = full speed; 2, 4, 8 = one tick in N
    int           slowCounter_  = 0;
    std::uint32_t pendingSteps_ = 0;   // frame-step requests not yet spent

    // Frames this mode has been ticked, as distinct from the session's tick
    // index. TWO COUNTERS ON PURPOSE: without a match the session never ticks,
    // and a single counter frozen at zero cannot tell "the host is not calling
    // me" apart from "the content did not load". They separate those.
    std::uint64_t modeTicks_ = 0;

    // --- what went wrong, in the words of whichever layer wrote it ------------
    bool        matchReady_ = false;
    std::string setupError_;
    std::string analysisError_;
    std::string demoNote_;
    // What the file watch last did -- "edit landed" or "edit refused" with the
    // loader's own words -- and which, so the HUD can pick a colour. Cleared
    // by teardownMatch_ (a new character or an explicit swap changes the
    // subject) and rewritten by pollHotReload_ after the adopt.
    std::string reloadNote_;
    bool        reloadFailed_ = false;
    // The (mtime, size) watch on the loaded character file. Bound in
    // startCharacter_ BEFORE the load and regardless of its outcome, so the
    // save that fixes a broken file is noticed and revives the honest-error
    // screen -- previously only C could, and C advances to the NEXT character.
    cse::data::CharacterFileWatch reloadWatch_;
    // The presentation model's own two watches (ROADMAP M3.4b): the glTF and
    // its `<stem>.clips.json`, bound in adoptPrepared_ when the character
    // authors engine.anim3d.model and polled beside reloadWatch_, so a
    // re-export lands like a frame-data edit -- through the same load, the
    // same A21/A22, and the same keep-last-good + HUD line when it disagrees.
    cse::data::CharacterFileWatch modelWatch_;
    cse::data::CharacterFileWatch sidecarWatch_;
    // Which clip each (kind, move slot) wears -- bound BY MOVE ID in
    // adoptPrepared_, beside the binding table and for the same reason: a
    // reload that renumbers slots must not hand one move another's clip.
    cse::presentation::FighterClips clips_;

    // --- the 3D presentation (ROADMAP M3.4c) --------------------------------
    //
    // On when the character authors engine.anim3d.model and the model loaded;
    // off, and every frame draws the 2D placeholders as before, otherwise.
    // The scene owns the entities; scene3d_ owns their handles and re-creates
    // them if a scene swap took them away. look_ is the committed
    // fight_look.json (or its defaults, with a note); lookSnapshot_ is what it
    // overwrote on the host, put back on teardown and Exit.
    void reconcile_();
    void loadLook_();
    void applyLook_();
    void createScene3d_();
    void destroyScene3d_();
    FightScene                       scene3d_;
    std::shared_ptr<MyCoreEngine::Model> model_;
    cse::presentation::FightLook     look_{};
    LookSnapshot                     lookSnapshot_{};
    std::string                      presentationNote_;   // why the 3D pass is off, for the HUD
    // The last viewport Draw was handed: the camera's aspect for the NEXT
    // frame's composition, which runs in Update, before this frame's Draw.
    int viewportW_ = 1280;
    int viewportH_ = 720;
    // A latching sequencing failure. Fatal to the match rather than to the
    // process: LatchedInputSource::Latch returning false means the input log
    // would have a hole in it, and its header says a caller that gets false back
    // must stop the match rather than carry on.
    std::string fatal_;
};

} // namespace untitledfighter
