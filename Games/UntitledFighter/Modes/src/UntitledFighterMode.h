// "Untitled Fighting Game", as a TRAINING MODE a general-purpose host can enter.
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

#include "cse/game/ComboWatcher.h"
#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"

#include "cse/presentation/FighterClips.h"
#include "cse/presentation/FightPresentation.h"

#include "FightScene.h"

#include "cse/kernel/GameState.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace untitledfighter {

class UntitledFighterMode final : public MyCoreEngine::IGameMode {
public:
    // EXACTLY the string the main menu shows. The host draws it verbatim -- no
    // uppercasing, no truncation -- so this is the one place the game's name is
    // written, and changing it changes the menu.
    const char* DisplayName() const override { return "Untitled Fighting Game"; }

    bool Enter(const MyCoreEngine::GameModeContext& ctx, std::string& error) override;
    void Exit() override;
    void FixedTick(float dt) override;
    void Update(float dt) override;
    void Draw(MyCoreEngine::Renderer2D& r2d, int widthPx, int heightPx,
              float dt) override;

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

    FightHudModel hudModel_() const;

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
