// The Build Settings panel: WHAT SHIPS, and the button that produces it.
//
// ---------------------------------------------------------------------------
// WHY THERE IS A PANEL HERE AT ALL
// ---------------------------------------------------------------------------
// Because a game developer runs ONE application. `Editor`, `PlayerDebug` and
// `PlayerShipping` being three peer CMake executables you pick between in a
// Visual Studio dropdown is an ENGINE developer's model; it asks the person
// making a game to know that their game has three build targets. This panel is
// the other model: the editor IS the engine, and the player is something the
// editor PRODUCES from an ordered scene list and a profile.
//
// The data model's own header (Engine/src/core/BuildSettings.h) carries the full
// argument, including the measured fact that nothing needs STRIPPING -- the
// shipping player links no editor code and no ImGui, so a Build only has to
// build the right target and assemble a folder. BuildPipeline.h carries the
// contract for what lands in that folder. This file is the surface those two
// meet a human through, and everything below is why it is shaped the way it is.
//
// ---------------------------------------------------------------------------
// 1. IT EDITS THE LIST DIRECTLY. IT EMITS ACTIONS FOR EVERYTHING ELSE.
// ---------------------------------------------------------------------------
// ComboProverPanel.h sets the house standard -- a panel EMITS intents and the
// editor executes them, so that a panel never has to know what the editor is --
// and AssetBrowserPanel does the same. This panel obeys it with one deliberate
// exception, and the exception has a line under it:
//
//   THE LIST is a value this panel is a view of, and BuildSettings ALREADY owns
//   the rules for editing it. `AddScene`, `RemoveSceneAt`, `MoveScene` and
//   `SetStartupScene` exist, in BuildSettings.h's own words, "so the panel does
//   not invent its own rules for normalization, duplicates and bounds". Routing
//   a reorder through an action struct would mean the editor re-deriving which
//   row moved where -- a second copy of the same logic, in the place least able
//   to test it. So the panel is handed `BuildSettings&` and calls those four.
//
//   THE FILESYSTEM, THE PROCESS TABLE AND THE SCENE are not. Saving build.json,
//   running preflight, starting and cancelling a build, and opening a scene into
//   the editor are all actions. Every one of them either outlives the frame or
//   touches state the editor owns exclusively (the undo history, play gating,
//   the child process it has to kill at shutdown).
//
// That line also answers the obvious "why not just let the panel call
// BuildJob::Start": because the job outlives the panel's visibility and the
// panel's Draw is not called while it is hidden -- see 2.
//
// ---------------------------------------------------------------------------
// 2. THE JOB IS NOT OWNED HERE AND IT IS NOT POLLED HERE
// ---------------------------------------------------------------------------
// A build takes seconds to minutes. Draw() is called only on frames the panel is
// visible -- that is true of the built-in panels (`if (panels_.x) ...` in the UI
// loop) and stated outright for registered ones (EditorPanel.h). A `Poll()` from
// inside Draw would therefore mean: close the window and the build stops
// draining its pipe, the child fills the pipe buffer, and the compile WEDGES.
// Not slows -- wedges, on Windows, at 64KB of compiler output, with no error
// anywhere and a Cancel button that is no longer being drawn.
//
// So EditorApplication owns the `BuildJob`, polls it once per frame regardless
// of this panel's visibility, and hands the resulting snapshot in. This panel
// renders a `BuildProgress` and a `BuildReport`; it never holds a job.
//
// The same split is why `report` arrives as a pointer to something the editor
// keeps: BuildPipeline.h calls the report "a value the panel keeps after the job
// is gone", and the panel that keeps it must be one that is still there when the
// author reopens the window a minute later.
//
// ---------------------------------------------------------------------------
// 3. THE TWO ORDINARY BAD STATES ARE DRAWN AT THE THING THAT IS WRONG
// ---------------------------------------------------------------------------
// Both of these are ordinary. Neither is a corruption to assert on: a scene gets
// renamed, a build list gets copied between machines, somebody types an output
// path. And both are refusals that `PreflightBuild` already reports -- so the
// temptation is to draw preflight's `errors` vector at the bottom and stop.
//
// THAT IS NOT ENOUGH, and the reason is specific rather than aesthetic. A flat
// list of sentences at the bottom of a panel names the PROBLEM but not the ROW.
// With twelve scenes in the list and "Exported/arena.json is not in the asset
// root" printed under a Build button, the author's next move is to read twelve
// paths and compare them by eye. The refusal has to be ON the eleventh row.
//
//   A LISTED SCENE THAT IS NOT ON DISK -> the row says MISSING, in red, in
//   place, and index 0 says it loudest because index 0 is what the game boots.
//   The panel stats the list itself to do that.
//
//   AN OUTPUT DIRECTORY IT MAY NOT WRITE -> split in two, because the two halves
//   have different owners and different costs:
//
//     * THE SPELLING is refused lexically -- absolute, a drive/UNC root, or any
//       ".." component -- and those three refusals are the ones an author
//       CREATES BY TYPING. `PathIsContained` answers them without touching the
//       filesystem and without knowing the real root, so the panel answers them
//       itself, per keystroke, under the field. (The header of the field says
//       what the path is relative to, because "Builds/Game" is meaningless
//       without it.)
//     * WHERE IT ACTUALLY RESOLVES TO, and whether something is already there,
//       is preflight's -- `BuildPreflight::resolvedOutputDir` is absolute and
//       resolved against the real `outputRoot`, which this panel does not know
//       and must not guess. Shown verbatim, so the author reads the absolute
//       destination before pressing Build rather than in a report afterwards.
//
//   Preflight's own `errors` and `warnings` are drawn IN FULL as well, directly
//   above the Build button. They are the authority; the markers above are the
//   localization of them. When the two could disagree the panel defers: it says
//   "this directory already exists", never "this build will be refused".
//
// AND THE STATS ARE NOT PER FRAME. This is ComboProverPanel.h note 5's rule
// applied to a cheaper input: a docked panel draws 60-300 times a second whether
// or not anything was typed, and 256 `exists()` calls a frame on a network share
// is a stutter nobody will connect to a build list. The scene list is folded
// into a 64-bit fingerprint every draw -- one pass over the strings, no
// allocation, orders of magnitude cheaper than the stats -- and the stats re-run
// only when it moves. The output directory has its own fingerprint, so typing in
// that field does not re-stat the scene list. A Recheck button bumps a nonce
// that is folded into both, which is how a file deleted outside the editor gets
// noticed without a timer, and it goes down the ordinary path rather than a
// second one that could rot.
//
// ---------------------------------------------------------------------------
// 4. A MALFORMED build.json TURNS THE PAGE READ-ONLY. IT IS NOT AN ERROR BOX.
// ---------------------------------------------------------------------------
// `BuildSettingsLoadResult` is three-valued precisely so a panel with a Save
// button can tell MISSING from MALFORMED, and BuildSettings.h spells out the
// stake: saving after a malformed load overwrites somebody's twelve-scene list
// with the two-line default because one comma was wrong.
//
// So Malformed is not a red line above a working panel. Save is disabled, Build
// is disabled (a build would assemble from settings that are NOT what the file
// says), the message -- which names the offending key or index -- is shown, and
// the only two ways out are explicit: fix the file and Reload, or Discard, which
// is a confirmation-guarded "yes, overwrite it with what is on screen".
//
// ---------------------------------------------------------------------------
// 5. INDETERMINATE PROGRESS IS DRAWN AS INDETERMINATE
// ---------------------------------------------------------------------------
// `BuildProgress::fraction` is negative during Compiling and during the install
// half of Assembling, and BuildPipeline.h explains that this is the honest
// answer: neither cmake invocation reports progress and the number of
// translation units it will rebuild is not knowable in advance. The panel draws
// ImGui's animated indeterminate bar for a negative fraction rather than
// inventing a percentage, because a fake bar stuck at 40% for two minutes is the
// thing that makes people kill a build that was working.
//
// ---------------------------------------------------------------------------
// 6. "FAILED" IS SIX DIFFERENT SENTENCES, AND ONE OF THEM SAYS THE OPPOSITE
// ---------------------------------------------------------------------------
// `BuildResult` distinguishes six outcomes and they do NOT differ only in
// severity. `FailedPreflight`, `FailedCompile`, `FailedAssemble` and `Cancelled`
// all mean nothing was left on disk. `FailedValidation` means the bundle EXISTS
// and was deliberately left in place so the author can look at what was produced
// -- BuildPipeline.h says in as many words that "the panel must not describe it
// as 'nothing was written'". Each result therefore gets its own sentence written
// out here rather than a shared "build failed" with a code after it.
//
// ---------------------------------------------------------------------------
// 7. WHAT THE PANEL SAYS RATHER THAN LETTING SOMEBODY DISCOVER IT
// ---------------------------------------------------------------------------
//   * DROPPING A SCENE REMOVES THE SCENE, NOT ITS ASSETS. Scoped out
//     deliberately and at length (BuildPipeline.h section 4): there is no asset
//     dependency graph, and the partial one that could be written today would be
//     silently wrong in the direction of a shipped game with a missing texture.
//     A bundle is as large as the whole asset root no matter how few scenes are
//     in it, and the first person to notice will file it as a bug unless the
//     page says so first.
//   * WHETHER scenes[0] REALLY BOOTS depends on the player honouring the build
//     manifest (`ProjectSettings::startupSceneFromBuild`). `ApplyTo` sets that
//     flag, so the engine half is live; the player's boot order is a separate
//     change (PlayerMain.cpp resolves command line, then a linked title's front
//     end, then project.json). Until it lands, a build whose index 0 is a level
//     rather than the title's front end is overruled in the bundle. The panel
//     states it beside the boot row instead of the pipeline guessing at it --
//     which is exactly what BuildEnvironment::titleFrontEndScene's comment asks
//     a panel to do, and the editor genuinely cannot know the answer: it is not
//     linked against the title's front end and deliberately so.
//
// ---------------------------------------------------------------------------
// 8. BUILD IS DISABLED WHILE PLAYING
// ---------------------------------------------------------------------------
// Not for tidiness. Both player targets carry `add_dependencies(... runtime_assets)`
// (Player/CMakeLists.txt), so a compile RE-STAGES the runtime `Exported/`
// directory beside the editor -- and cmake/stage_runtime_assets.cmake MIRRORS,
// overwriting changed files and deleting ones whose source is gone. A play
// session reading out of that directory while it is rewritten under it -- a
// scene swap, a streamed asset -- can fail a load with no visible cause. Stop,
// then build.
//
// THE SYMMETRIC GATE IS NOT HERE and is worth knowing: pressing Play while a
// build runs has the same hazard from the other end. This panel cannot refuse
// that (the Play control is not its), so it is a gap rather than a covered case.
#pragma once

// The two frozen headers this panel is a view of. Reached through the engine's
// aggregate header because that is the only include directory the Engine target
// exports (`Engine/include`) -- EditorApplication.h and AssetBrowserPanel.cpp
// take the same route for the same reason.
#include "Engine.h"

#include <cstdint>
#include <string>
#include <vector>

// What the panel wants done and cannot do itself, in the shape of
// AssetBrowserActions and ComboProverActions.
//
// EVERY FIELD HERE EITHER OUTLIVES THE FRAME OR TOUCHES EDITOR-OWNED STATE. That
// is the test applied to each one; anything that failed it stayed in the panel
// (list edits, the clipboard, which sections are collapsed).
struct BuildSettingsActions {
    // The settings changed this frame. The editor uses it to mark the file dirty
    // and to invalidate its cached preflight -- NOT to save. Auto-saving on a
    // keystroke would write build.json thirty times while somebody types an
    // output path, and one of those writes is the one that races a Malformed
    // reload.
    bool settingsChanged = false;

    // Write build.json. Refused by the editor when the last load was Malformed;
    // the panel does not offer it in that state either, so the two agree.
    bool saveRequested = false;

    // Re-read build.json from disk, discarding what is on screen. The way out of
    // a Malformed file once it has been fixed in a text editor.
    bool reloadRequested = false;

    // Overwrite a Malformed build.json with what is on screen. Separate from
    // saveRequested because it is a different decision: this one is "I accept
    // losing whatever that file said", and the panel gates it behind a
    // confirmation before ever setting it.
    bool discardRequested = false;

    // Re-run PreflightBuild. It is NOT free (it stats the scene list and walks
    // the staged asset root), so it is a request rather than something the panel
    // does per frame -- BuildPipeline.h says so explicitly.
    bool preflightRequested = false;

    bool buildRequested = false;
    bool cancelRequested = false;

    // Open this scene in the editor. Same shape as ComboProverActions::
    // jumpToMoveId: the panel reports the click and the editor decides what
    // opening a scene means, because loading one clears the undo history, resets
    // camera directors and is refused during play.
    std::string openScene;
};

// Everything the panel needs to know that it cannot work out for itself. One
// struct rather than eleven parameters, because it is going to grow by exactly
// one field the day the pipeline learns something new to report.
struct BuildPanelInputs {
    // How build.json's last load went. Drives the read-only state in note 4.
    MyCoreEngine::BuildSettingsLoadResult load;

    // Edits not yet written to build.json.
    bool dirty = false;

    // The cached preflight, or null when it has never run. `preflightStale`
    // means the settings moved since it did -- shown, rather than silently
    // hidden, because a stale preflight is the one that would otherwise make the
    // panel state something confidently wrong. A stale one cannot produce a
    // wrong BUILD: BuildJob::Start re-runs preflight itself and never trusts a
    // cached one.
    const MyCoreEngine::BuildPreflight* preflight = nullptr;
    bool preflightStale = true;

    // A job is running. `progress` is this frame's snapshot; `report` is the last
    // FINISHED build's, kept by the editor and valid across the job's death (and
    // across this window being closed and reopened).
    bool building = false;
    MyCoreEngine::BuildProgress progress;
    const MyCoreEngine::BuildReport* report = nullptr;

    // The child's accumulated stdout, owned by the editor. Never null in
    // practice; checked anyway, because a null here would be a crash inside a
    // draw call rather than an empty box.
    const std::string* log = nullptr;

    // The editor's current scene file, for "Add Current Scene". Empty for an
    // untitled scene, which is what New Scene leaves behind -- the button is
    // disabled with that reason rather than adding "".
    std::string currentScene;

    // Play-in-editor is running. See note 8.
    bool playing = false;

    // The editor knows where its own source and binary trees are; without them
    // there is nothing to build. Empty message means fine. This is a separate
    // channel from preflight's errors because it is a property of how the EDITOR
    // was built, not of what the author typed, and the fix is "reconfigure with
    // CMake" rather than anything on this page.
    std::string environmentProblem;

    // One-shot: bring the window to the front. Set by File > Build Settings...,
    // so choosing the menu item while the window is already open behind three
    // others does what the author meant.
    bool focus = false;
};

class BuildSettingsPanel {
public:
    // `settings` is edited IN PLACE -- see note 1. `pOpen` drives the window's
    // close button and is the same bool the Window menu checkbox toggles, so the
    // tab X and the menu item are one piece of state rather than two that drift.
    BuildSettingsActions Draw(MyCoreEngine::BuildSettings& settings,
                              const BuildPanelInputs& in, bool* pOpen = nullptr);

private:
    // The reorder drag payload. Its own name rather than the asset browser's:
    // dragging a model into this list means nothing, and sharing a payload id
    // would make it silently accepted.
    static constexpr const char* kRowPayload = "CSE_BUILD_SCENE_ROW"; // int index

    // ---- the disk checks of note 3 -------------------------------------
    //
    // THREE STATES, NOT TWO, and the third is why this is an enum rather than a
    // bool. "Present" and "not there" are the expected pair; a path the sandbox
    // REFUSES is a different failure with a different fix and cannot even be
    // looked for. It should not be reachable -- `Load` refuses the whole file
    // over one such path and `AddScene` refuses the entry -- but `scenes` is a
    // public vector on a public struct, and a row rendered as "missing" when the
    // real answer is "that path may not be opened at all" sends the reader
    // looking for a file they were never allowed to name.
    enum class SceneRowState {
        Present,
        Missing,
        Refused,
    };

    struct SceneRow {
        SceneRowState state = SceneRowState::Missing;
        // Only for Refused, where the sandbox's own sentence is worth repeating
        // verbatim rather than paraphrased into "bad path".
        std::string detail;
    };

    // Recomputed when `scenesFingerprint_` moves, never per frame.
    std::vector<SceneRow> rows_;
    std::uint64_t scenesFingerprint_ = 0;
    // Separate from a zero fingerprint because 0 is a reachable hash value and
    // "never checked" must not be confused with "checked and hashed to zero" --
    // the same distinction ComboProverPanel keeps with haveResult_.
    bool haveRows_ = false;

    // The output directory, as far as the panel is entitled to judge it.
    struct OutputCheck {
        // The LEXICAL half only: absolute, drive/UNC root, or a ".." component.
        // Answered against the working directory, which differs from the real
        // `outputRoot` -- and does not matter for these three, because all three
        // are refused before the base is consulted at all. The base-dependent
        // half (does the canonical result stay under the root) is preflight's,
        // and the panel does not restate it.
        bool spellingOk = true;
        std::string refusal;

        // Whether preflight's absolute destination is already there. Sourced
        // from `BuildPreflight::resolvedOutputDir` rather than resolved here:
        // the panel does not know what the relative path is relative to.
        bool haveResolved = false;
        bool resolvedExists = false;
    };
    OutputCheck output_;
    std::uint64_t outputFingerprint_ = 0;
    bool haveOutput_ = false;

    // Bumped by Recheck and folded into BOTH fingerprints, so a forced recheck
    // travels the same path as a data change instead of needing its own. The
    // ComboProver's forceNonce_ idiom, for the same reason: a second code path
    // that only runs when a button is pressed is a second code path that rots.
    std::uint32_t recheckNonce_ = 0;

    void refreshScenes_(const MyCoreEngine::BuildSettings& settings);
    void refreshOutput_(const MyCoreEngine::BuildSettings& settings,
                        const MyCoreEngine::BuildPreflight* preflight);

    // ---- sections ------------------------------------------------------
    void drawFileState_(const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawScenes_(MyCoreEngine::BuildSettings& settings,
                     const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawAddRow_(MyCoreEngine::BuildSettings& settings,
                     const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawExcluded_(MyCoreEngine::BuildSettings& settings,
                       const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawOutput_(MyCoreEngine::BuildSettings& settings,
                     const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawProfile_(MyCoreEngine::BuildSettings& settings, BuildSettingsActions& a);
    void drawPreflight_(const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawBuildBar_(const MyCoreEngine::BuildSettings& settings,
                       const BuildPanelInputs& in, BuildSettingsActions& a);
    void drawReport_(const BuildPanelInputs& in);
    void drawLog_(const BuildPanelInputs& in);

    // The add field. 260 to match every other path buffer in this editor
    // (kAssetPayload, currentScenePath_, ComboProverPanel::pathBuf_).
    // BuildSettings::kMaxPathLength is 1024, so this field is the tighter of the
    // two limits and a path it cannot hold is one the author should be pasting
    // into build.json rather than typing here.
    char addBuf_[260] = "";

    // The output directory field. Edited into this buffer and copied into the
    // settings on change, rather than bound to the std::string directly, because
    // ImGui::InputText needs a stable char buffer.
    char outputBuf_[260] = "";

    // The value this panel last put into (or took out of) settings.outputDirectory.
    // It is how the buffer above learns about a change it did not make -- a
    // Reload, a Discard, the first load -- WITHOUT copying every frame, which
    // would fight the author's own keystrokes. Bound-to-the-string directly is
    // not an option (InputText needs a char buffer), and "copy when the field is
    // not focused" needs the widget's id before the widget is drawn.
    std::string lastSeenOutput_;

    // The last refusal from AddScene/SetStartupScene, shown until the next
    // attempt. STICKY on purpose: a message that vanished on the frame after the
    // button was released would be unreadable, and this one carries the reason
    // ("already in the build at index 3", "escapes the project with '..'") that
    // is the entire value of the interaction.
    std::string addError_;

    // Discard is a confirmation, and the confirmation is a modal rather than a
    // second click on the same button: the button that overwrites a file the
    // panel just refused to save must not be one you can hit by double-clicking
    // Save.
    bool discardConfirm_ = false;

    // Section state. Session-scoped like every other Game/panel toggle here; the
    // log starts collapsed because it is thousands of lines of compiler output
    // and the summary above it is what answers the question most of the time.
    bool showLog_ = false;
    bool showExcluded_ = true;

    // Reset at the top of every Draw. Rows are identified to ImGui by index, and
    // two widgets with the same id are ONE widget as far as ImGui is concerned.
    int widgetId_ = 0;

    // The last frame this panel drew on. Draw is called ONLY while the panel is
    // visible, so a gap in the counter is the window having been opened -- which
    // is how the panel re-stats the disk and asks for a fresh check on reopen
    // without the menu item having to remember to tell it. Initialized far
    // enough back that the first draw counts as an appearance.
    int lastDrawFrame_ = -1000;
};
