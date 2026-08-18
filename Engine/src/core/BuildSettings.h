// Engine/src/core/BuildSettings.h
#pragma once
// WHAT SHIPS, AS DATA THE EDITOR AUTHORS.
//
// The model this file exists to make true: A GAME DEVELOPER RUNS ONE
// APPLICATION -- the editor -- and the player is something the editor
// PRODUCES. Today `Editor`, `PlayerDebug` and `PlayerShipping` are three peer
// CMake executables and you pick one in a Visual Studio dropdown, which is an
// ENGINE developer's model: it asks the person making a game to know that their
// game has three build targets. Unity asks them to know a scene list and a
// button.
//
// THE STRIPPING IS ALREADY STRUCTURAL AND NEEDS NO NEW MECHANISM. Verified
// against the tree rather than repeated from the brief:
//   * `PlayerShipping` links `Engine` (Player/CMakeLists.txt:10) plus, from the
//     composition block in the root CMakeLists.txt:196-220, the title's
//     `UntitledFighterModes` and `UntitledFighterFrontEnd`. It links no editor
//     target and no ImGui: `Engine/CMakeLists.txt:313-321` is the engine's
//     complete link line and ImGui is not on it, so there is no transitive route
//     either. The editor's panels, gizmos and asset browser are compiled into
//     `Editor` and reachable from nowhere else.
//   * The configure-time assertion at root `CMakeLists.txt:337-363` polices the
//     related rule -- no general-purpose target may NAME `CseKernel`, `CseData`
//     or `CseGame`.
// So a Build action does not have to strip anything. It has to BUILD THE RIGHT
// TARGET and ASSEMBLE A FOLDER, which is what BuildPipeline.h describes.
//
// WHAT WAS MISSING is only this file: an ORDERED scene list. `ProjectSettings`
// carries `startupScene` -- a build setting with a population of one -- and the
// editor writes it from File > Set Current Scene as Player Startup. One scene is
// not a game; a game is a menu, then a fight, then a results screen.
//
// ---------------------------------------------------------------------------
// THIS FILE IS THE EDITOR'S SURFACE. ProjectSettings IS THE RUNTIME CONTRACT.
// ---------------------------------------------------------------------------
// They are deliberately two files and the direction between them is one-way:
//
//     build.json  --(the Build action)-->  the bundle's Exported/project.json
//
// `BuildSettings::ApplyTo` is that arrow, and it is the whole relationship:
// `scenes[0]` becomes `ProjectSettings::startupScene`. Nothing in the PLAYER
// changes -- `PlayerMain.cpp:200` goes on reading `settings.startupScene`, and
// the boot order it documents (command line, then the title's front end, then
// project.json) is untouched.
//
// AND THEY MUST NOT BE ONE FILE, for a reason that is already in the tree:
// `MenuUIContent.cpp:200-207` does a `ProjectSettings` LOAD-MODIFY-SAVE from
// inside the running game when a player moves the volume slider, and
// `ProjectSettings::Save` rewrites the file from the two fields the struct
// models. A scene list living in project.json would therefore be destroyed by a
// player adjusting the volume in the shipped game -- silently, in the bundle,
// where nobody would look. Beside, not inside.
//
// A SHIPPED GAME NEVER READS THIS FILE. It is editor metadata, the same class as
// the `.import` sidecars, and it is kept out of a bundle the same way they are:
// by NAME, in the install rules themselves (Player/CMakeLists.txt:116-117, :151),
// so that `cpack` and the editor's Build action produce the same bundle. The
// Build action's prune removes it too, announced, as a backstop.
//
// UNTRUSTED ON READ, like every other authored file. It is JSON somebody can
// hand-edit, and it names paths that a build will read and a directory a build
// will WRITE INTO. Every count is bounded and every violation REFUSES THE WHOLE
// FILE rather than clamping it: a truncated scene list is a shipped game missing
// its last level, with nothing anywhere to say so.
#include "Core.h"
#include "PathSandbox.h"     // the containment rule scene paths are checked against
// INCLUDED, NOT FORWARD-DECLARED. ApplyTo and SeedFromProjectSettings take it by
// reference, so a forward declaration compiles this header -- and then every
// caller that actually invokes one gets "uses undefined struct ProjectSettings"
// and has to work out which second header it wanted. The file is Core.h plus
// <string>; the saved include is not worth the papercut.
#include "ProjectSettings.h"

#include <cstddef>
#include <string>
#include <vector>

namespace MyCoreEngine {

    // ---------------------------------------------------------------------
    // A PROFILE PICKS TWO AXES THAT ALREADY EXIST. IT INVENTS NO THIRD.
    // ---------------------------------------------------------------------
    // The two axes, both already in the build:
    //
    //   1. THE CMAKE CONFIGURATION -- Debug / RelWithDebInfo / Release. All
    //      three are real here: CMakePresets.json defines a configure preset per
    //      configuration, and Engine/CMakeLists.txt:325-338 gives each one its
    //      own output directory (the RelWithDebInfo one carries a comment
    //      explaining that without it Engine.dll scatters away from the exes).
    //
    //   2. WHICH PLAYER TARGET -- `PlayerDebug` or `PlayerShipping`. Same source
    //      file, two link/subsystem decisions (Player/CMakeLists.txt:6-38):
    //      PlayerDebug keeps a console for logs; PlayerShipping is
    //      /SUBSYSTEM:WINDOWS with `MYCE_SHIPPING=1` and OUTPUT_NAME "Player",
    //      which is what turns a startup failure from a silent exit into a
    //      message box (PlayerMain.cpp:39-44).
    //
    // A profile is a NAMED PAIR of those, and that is all it is. There is no
    // third thing a profile turns on, and adding one here would be inventing an
    // axis the build system does not have.
    //
    //   Debug        (Debug,          PlayerDebug)     unoptimized + console
    //   Development  (RelWithDebInfo, PlayerDebug)     optimized + symbols + console
    //   Shipping     (Release,        PlayerShipping)  what you give somebody
    //
    // DEVELOPMENT IS THE DEFAULT because it is the one that is both playable and
    // diagnosable; a Debug-configuration build of this engine is too slow to
    // judge a game by, and Debug exists here only for attaching a debugger to
    // the PLAYER (as opposed to the editor) with full locals.
    //
    // THE CONFIGURATION AXIS IS NOT FREELY CHOOSABLE ON EVERY TREE, and this is
    // the trap the pipeline has to refuse rather than paper over. CMakePresets.json
    // uses the NINJA generator -- single-config, `CMAKE_BUILD_TYPE` fixed at
    // configure time -- so one build directory can produce exactly one
    // configuration and `--config` is meaningless there. On a multi-config
    // generator (Visual Studio) every profile is reachable from one tree. See
    // BuildPipeline.h's preflight: choosing a profile this tree cannot produce is
    // an error that names the preset to configure, never a build that silently
    // ships the wrong configuration.
    enum class BuildProfile {
        Debug,
        Development,
        Shipping,
    };

    // The JSON token. STABLE -- it is written into build.json, so renaming one
    // of these orphans a user's file. Lowercase, matching the enum order.
    ENGINE_API const char* BuildProfileToken(BuildProfile p);
    // The panel label. Free to change; nothing parses it.
    ENGINE_API const char* BuildProfileDisplayName(BuildProfile p);
    // Strict: an unknown token is a REFUSAL, not a fallback to Development. A
    // typo'd profile that silently became Development would ship a console
    // window and a debug-named exe to somebody's players.
    ENGINE_API bool ParseBuildProfile(const std::string& token, BuildProfile& out);

    // Axis 1: the CMake configuration. "Debug" / "RelWithDebInfo" / "Release".
    ENGINE_API const char* BuildProfileCMakeConfig(BuildProfile p);
    // Axis 2: the CMake target to build. "PlayerDebug" / "PlayerShipping".
    ENGINE_API const char* BuildProfileCMakeTarget(BuildProfile p);
    // What that target actually produces on disk, platform extension included:
    // "PlayerDebug.exe" / "Player.exe" on Windows, "PlayerDebug" / "Player"
    // elsewhere. NOT derivable from the target name -- PlayerShipping sets
    // OUTPUT_NAME "Player" (Player/CMakeLists.txt:31-32) so that the bundle ships
    // a clean Player.exe, and a copy step that assumed target==filename would
    // look for a file that is never written.
    ENGINE_API std::string BuildPlayerExecutableName(BuildProfile p);

    // ---------------------------------------------------------------------
    // THE LOAD RESULT IS THREE-VALUED, AND THE THIRD VALUE IS THE POINT
    // ---------------------------------------------------------------------
    // `ProjectSettings::Load` returns a bool and treats a missing file as
    // success ("defaults stand"), which is right for two scalars with sane
    // defaults. It is wrong here, because MISSING and MALFORMED want opposite
    // handling and the caller is a panel with a Save button:
    //
    //   Missing   -> there is no build list yet. Seed one (see
    //                SeedFromProjectSettings) and saving is correct.
    //   Malformed -> there IS a build list and this process cannot read it.
    //                Saving would OVERWRITE somebody's twelve-scene list with
    //                the two-line default, because one comma was wrong. The
    //                panel must show the message and refuse to save until the
    //                file is fixed or explicitly discarded.
    //
    // A bool cannot carry that distinction, and the version of this that returns
    // false for both is the version that eats the file.
    enum class BuildSettingsStatus {
        Ok,
        Missing,    // no file at that path; *this is untouched (i.e. defaults)
        Malformed,  // present and unreadable; *this is untouched, DO NOT SAVE
    };

    struct ENGINE_API BuildSettingsLoadResult {
        BuildSettingsStatus status = BuildSettingsStatus::Missing;
        // Human-facing and already logged to stderr. On Malformed it names the
        // offending key or index -- "scenes[3] is not a string" -- because "could
        // not read build.json" sends the reader hunting through the whole file.
        std::string message;

        bool ok() const { return status == BuildSettingsStatus::Ok; }
        // "Is it safe to write over this file?" Missing and Ok both yes.
        bool safeToSave() const { return status != BuildSettingsStatus::Malformed; }
    };

    struct ENGINE_API BuildSettings {
        // ORDERED, AND INDEX 0 IS WHAT THE PLAYER BOOTS. The order is the whole
        // reason this is a vector rather than a set: it is Unity's build index,
        // it is what `ApplyTo` reads to write `startupScene`, and it is what a
        // future "load scene by index" API would mean. Reordering the list is
        // therefore a real edit, not a cosmetic one -- see MoveScene.
        //
        // SPELLED THE WAY THE LOADERS RESOLVE, i.e. working-directory-relative
        // with the `Exported/` prefix ("Exported/UntitledFighter/menu.json").
        // That is the same spelling `ProjectSettings::startupScene` already
        // carries, which is what makes ApplyTo a copy rather than a translation,
        // and it is the first convention in the table at
        // docs/adr/ADR-007-asset-search-paths.md §3.2. When the asset search path
        // lands and that prefix comes out of content, it comes out of here the
        // same way and by the same one-release rule.
        std::vector<std::string> scenes;

        // Where the assembled bundle goes, RELATIVE to a root the pipeline
        // supplies (BuildEnvironment::outputRoot, which defaults to the project
        // source directory). Relative and contained, deliberately: the Build
        // action writes into this directory and MIRRORS it, so an absolute path
        // typed into a text field is a directory-deleting bug with a text field
        // in front of it. Load refuses absolute paths, drive/UNC roots and any
        // ".." component through PathIsContained, and the pipeline re-checks
        // against the real root before it creates anything.
        std::string outputDirectory = "Builds";

        BuildProfile profile = BuildProfile::Development;

        // Editor metadata, beside project.json in the same staged directory the
        // editor already writes into. Excluded from every bundle -- see the
        // header comment and BuildPipeline.h's copy contract.
        static const char* DefaultPath() { return "Exported/build.json"; }

        // ---- bounds -------------------------------------------------------
        // Every one of these REFUSES rather than clamps. The file is authored,
        // therefore untrusted, and a silently-truncated build list is a game
        // that ships without its last levels and says nothing.
        //
        // 256 scenes is far past any hand-authored list and small enough that a
        // corrupt file cannot allocate its way through memory. 1024 bytes is
        // past every real path and short of the point where a path becomes a
        // payload. The FILE bound matters too and is easy to forget: nlohmann
        // parses the whole thing into memory before a single bound below is
        // consulted, so the byte count is checked BEFORE the parse.
        static constexpr std::size_t kMaxScenes = 256;
        static constexpr std::size_t kMaxPathLength = 1024;
        static constexpr std::size_t kMaxFileBytes = 1u << 20; // 1 MiB

        // Bumped when the on-disk shape changes incompatibly. A file whose
        // version is HIGHER than this is refused rather than read: it was written
        // by a newer editor and reading it with old rules would silently drop
        // whatever the new field was.
        static constexpr int kVersion = 1;

        // ---- persistence --------------------------------------------------

        // ALL-OR-NOTHING. Converts into locals and commits only once every field
        // has converted, for the reason ProjectSettings.cpp:15-23 records at
        // length: nlohmann's `value(key, default)` substitutes the default only
        // when the key is ABSENT, so a present-but-wrong-typed key throws
        // mid-way and a straight assign leaves a half-populated object behind
        // while returning failure. `tests/test_scene_serializer.cpp:1429` is that
        // bug's regression test for ProjectSettings; the same shape is possible
        // here and is avoided the same way.
        BuildSettingsLoadResult Load(const std::string& path = DefaultPath());

        // Writes the file. Returns false on any I/O failure, INCLUDING a stream
        // that went bad after the write started -- a partially written build
        // list that reported success would be found later as a Malformed load
        // with no explanation of when it broke.
        //
        // NEVER call this after a Malformed load. See BuildSettingsStatus.
        bool Save(const std::string& path = DefaultPath()) const;

        // ---- the arrow to the runtime contract ------------------------------

        // Sets `out.startupScene` from `scenes[0]` AND marks the result as a
        // build manifest (`out.startupSceneFromBuild = true`, which is what makes
        // scenes[0] outrank a linked title's front end at boot -- see
        // ProjectSettings.h). The ONE place build settings reach the shipped game.
        // Leaves every other field of `out` alone, so the caller's
        // load-modify-save preserves masterVolume.
        //
        // Returns false when the scene list is empty, in which case `out` is
        // untouched: a bundle whose project.json named an empty startup scene
        // would fall through to the struct's own default (`Exported/scene.json`,
        // the engine demo) and ship a game that boots the backpack room.
        bool ApplyTo(ProjectSettings& out) const;

        // THE MIGRATION PATH. project.json already carries a startupScene, and
        // every existing project has one; this turns it into a one-element build
        // list. Call it when Load returned Missing.
        //
        // Only ever ADDS, and only to an empty list: returns false and changes
        // nothing when the list is already populated or when the legacy scene is
        // empty or would not pass the containment check. A migration that ran
        // twice, or that ran over an authored list, would silently reorder
        // somebody's game.
        bool SeedFromProjectSettings(const ProjectSettings& legacy);

        // ---- the list, as the panel must edit it ----------------------------
        // These exist so the panel does not invent its own rules for
        // normalization, duplicates and bounds -- three places where a hand-
        // rolled version drifts from what Load will accept next launch, and the
        // symptom is a list that saves fine and refuses to load.

        // Lexically normal, forward slashes, no leading "./". Applied to every
        // path that enters the list and to every lookup, so
        // `Exported\scene.json`, `./Exported/scene.json` and
        // `Exported/scene.json` are one entry rather than three.
        //
        // PURELY TEXTUAL -- it touches no filesystem and resolves no symlink. It
        // is a spelling rule, not a security check; PathIsContained is the
        // security check and is applied separately.
        static std::string NormalizeScenePath(const std::string& path);

        // Appends. Fails (with a reason, when `whyNot` is given) on an empty
        // path, a path over kMaxPathLength, a path the sandbox refuses, a
        // duplicate, or a list already at kMaxScenes.
        //
        // `baseDir` is what containment is checked against; "" means the current
        // working directory, which is how every loader in the engine resolves an
        // `Exported/...` path and therefore the right default for the editor.
        bool AddScene(const std::string& path, std::string* whyNot = nullptr,
                      const std::string& baseDir = std::string());

        bool RemoveSceneAt(std::size_t index);

        // Reorder for drag-and-drop. `to` is the index the entry ends up at
        // AFTER removal, which is what a drop target means. Out-of-range fails
        // and changes nothing.
        bool MoveScene(std::size_t from, std::size_t to);

        // "Set Current Scene as Player Startup", re-expressed: move an entry to
        // index 0, adding it first if it is not in the list. This is the verb the
        // editor's existing menu item becomes.
        bool SetStartupScene(const std::string& path, std::string* whyNot = nullptr,
                             const std::string& baseDir = std::string());

        // -1 when absent. Normalizes the argument first, so a caller may pass
        // whatever spelling it is holding (the editor's `currentScenePath_`,
        // an asset-browser click) without knowing this rule.
        int IndexOf(const std::string& path) const;
        bool Contains(const std::string& path) const { return IndexOf(path) >= 0; }

        // "" when the list is empty. What ApplyTo writes and what the built game
        // boots.
        const std::string& StartupScene() const;

        void Clear() { scenes.clear(); }
    };

} // namespace MyCoreEngine
