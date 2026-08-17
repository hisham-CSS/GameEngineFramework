// Engine/src/core/BuildPipeline.h
#pragma once
// WHAT A BUILD DOES. Declaration only -- the .cpp is somebody else's file.
//
// The verb this header names is the one the author asked for: "a step to build
// the player executable", from inside the editor, against a scene list the
// editor authored (BuildSettings.h). Not a Visual Studio dropdown.
//
// ---------------------------------------------------------------------------
// THE ONE DECISION ALREADY TAKEN: BUILD COMPILES, AND THEN ASSEMBLES
// ---------------------------------------------------------------------------
// The game's own code is C++ -- the title libraries ARE the game, the way a
// Unity project's C# scripts are (root CMakeLists.txt:81-99 is the whole of
// what the engine knows about a title) -- so "build the player" cannot mean
// "copy a prebuilt exe". The toolchain is present, because the author is
// already building from Visual Studio, and a compile with nothing dirty is
// seconds. Assembling from a stale exe would be the wrong default: it would
// ship a Player.exe compiled before the fighter's last balance pass, with
// nothing to indicate it.
//
// FOUR PHASES, and the first has no side effects at all:
//
//   PREFLIGHT   synchronous, cheap, repeatable. Answers "would this build
//               work" without starting anything, so the panel can grey the
//               button out and say WHY. Creates and deletes nothing.
//   COMPILE     `cmake --build` on the profile's target, as a child process
//               whose stdout is the build log.
//   ASSEMBLE    `cmake --install` into the output directory, then PRUNE the
//               scenes that are not in the build and WRITE the manifest.
//   VALIDATE    check the thing that was just produced, before calling it a
//               success. ADR-008 decision 9: a build is the only moment the
//               tool knows what will ship.
//
// and then a REPORT, which is a value the panel keeps after the job is gone.
//
// ---------------------------------------------------------------------------
// THE ENGINE DOES NOT SPAWN PROCESSES. A HOST DOES.
// ---------------------------------------------------------------------------
// `IBuildProcess` below is declared here and implemented by the caller, the
// same seam shape as `RegisterTitleGameModes` (GameMode.h) and
// `ScriptWorld::SetSourceResolver` (ScriptWorld.h:64-65): the engine names what
// it needs and the host supplies it.
//
// Two reasons, and the second is the one that matters.
//
//   * The process code already exists and it lives in the EDITOR --
//     Editor/src/Subprocess.cpp, CreateProcess + anonymous pipe on Windows and
//     posix_spawn + pipe() on POSIX, with a held handle so a hung child can be
//     killed. Reinventing it inside the engine would be a second copy of a file
//     whose comments record two real bugs (a closed fd 1, a reaped pid being
//     signalled). Reuse it.
//   * Engine.dll is linked by the PLAYER as well as the editor
//     (Player/CMakeLists.txt:10). Putting process spawning in the engine puts
//     "launch an arbitrary program" inside every shipped game. Behind a seam
//     the host fills, the shipped player passes nothing and the capability is
//     simply absent.
//
// It also makes the pipeline testable without a toolchain: a fake launcher that
// replays a canned compiler log and returns an exit code exercises every path
// through this file, which is how a build system gets a regression test at all.
//
// ---------------------------------------------------------------------------
// TWO THINGS IN Editor/src/Subprocess.cpp MUST CHANGE BEFORE THE ADAPTER WORKS
// ---------------------------------------------------------------------------
// Read rather than assumed, and neither is hypothetical -- both are load-bearing
// for `cmake` specifically and neither was ever exercised by the AssetCooker
// run, which is the only current caller.
//
//   1. `Spawn` CANNOT LAUNCH A PROGRAM THAT IS NOT BESIDE THE EXECUTABLE.
//      Subprocess.cpp:42 builds `".\\" + argv[0] + ".exe"` and :139 builds
//      `"./" + argv[0]`, deliberately, so that AssetCooker resolves out of the
//      editor's own directory immune to PATH-search rules. `cmake` is on PATH,
//      not in build/bin/<CONFIG>/, so today `Spawn({"cmake", ...})` fails with
//      "CreateProcess failed (is cmake.exe built?)" -- a message that sends the
//      reader looking for a target that does not exist. The adapter (or Spawn
//      itself) must pass argv[0] through unchanged when it contains a path
//      separator or is to be found on PATH.
//
//   2. WINDOWS ARGUMENT QUOTING. Subprocess.cpp:43 joins the argument vector
//      with plain spaces and hands the result to CreateProcessA, which then
//      re-splits it on whitespace. AssetCooker's arguments are "validate" and
//      "Exported", so this has never bitten. Every argument this pipeline
//      passes is an ABSOLUTE DIRECTORY PATH, and the first user whose checkout
//      lives under a directory with a space in it gets a build that fails with
//      cmake complaining about a directory whose name is the first word of
//      theirs. The POSIX branch takes a real argv and is already correct, which
//      is exactly why this would be found on one platform only.
//
// ---------------------------------------------------------------------------
// IT MUST NOT BLOCK THE EDITOR'S FRAME LOOP, AND IT DOES NOT USE THE JOBSYSTEM
// ---------------------------------------------------------------------------
// Both existing mechanisms were read before choosing. The JobSystem is the
// wrong one and the AssetCooker's ValidateRun is the right one:
//
//   * JobSystem's pool is `hardware_concurrency/3` clamped to [1,4]
//     (JobSystem.h:57-66) and exists for the decode-on-worker / finalize-on-main
//     ASSET pipeline. A build occupies a thread for minutes. On a machine that
//     gets one worker, submitting a build starves every model decode in the
//     editor for the whole compile -- the asset browser stops resolving
//     thumbnails and nothing says why.
//   * JobSystem HAS NO CANCELLATION. Its own header lists "cancellation tokens"
//     among the things that are "Planned (NOT implemented)" (:55-56). Cancel is
//     not optional here: a compile is the longest-running thing the editor can
//     start.
//   * A build ALSO needs a thread parked in a blocking pipe read, and
//     EditorApplication.h:202-213 already says why that may not be a pool
//     worker: "a blocked pipe read must never stall asset decodes or the RunLoop
//     exit drain".
//
// So: ONE DEDICATED std::thread per job, exactly like `ValidateRun`
// (EditorApplication.cpp:2748-2760), and it is the same thread that drains the
// pipe rather than a second one -- ValidateRun needs two because its main thread
// stays free; here the job thread has nothing else to do.
//
// PROGRESS REACHES A 60Hz PANEL BY POLLING, NEVER BY CALLBACK. `Poll()` is a
// main-thread call that copies a small snapshot out from under a mutex. No
// function supplied by the caller is ever invoked from the job thread -- a panel
// callback firing off-main would touch ImGui from a worker, which is the one
// thing this codebase's threading contract forbids outright (JobSystem.h:22-31).
//
// ---------------------------------------------------------------------------
// THE KNOWN FAILURE THAT WILL SURPRISE PEOPLE: Engine.dll IS LOCKED
// ---------------------------------------------------------------------------
// The editor has Engine.dll loaded. On Windows a loaded DLL cannot be
// overwritten, so if the engine's own sources are dirty when Build is pressed,
// `cmake --build --target PlayerShipping` will try to relink Engine.dll (the
// player depends on it) and die with LNK1104/LNK1168 naming a file the author
// did not ask to build.
//
// It is not the common case -- editing a scene does not make Engine.dll dirty,
// and a clean tree relinks nothing -- but it is exactly the case a programmer
// hits, and the raw linker error explains none of it. Two ways out, and the API
// forecloses neither:
//
//   DETECT AND EXPLAIN (recommended first): match the linker error in the child's
//   output and turn it into "the editor is running the engine this build would
//   have to rebuild -- close the editor and build from Visual Studio, or build
//   the engine first". One string match, no architecture.
//
//   A SEPARATE BUILD TREE: point `BuildEnvironment::binaryDir` at a second
//   binary directory used only for player builds; nothing there is loaded, so
//   nothing is locked. Correct and costs a full engine compile the first time.
//   That is a one-FIELD change from the caller's side, which is why binaryDir is
//   a plain string here rather than something derived internally.
#include "Core.h"
#include "BuildSettings.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    // -------------------------------------------------------------------
    // Can the thing we just assembled actually start?
    // -------------------------------------------------------------------
    // Names every *.dll present beside the engine in the BUILD TREE that did not
    // reach the BUNDLE. Empty means the bundle has everything the build tree
    // thinks the game needs.
    //
    // WHY THIS IS A FREE FUNCTION AND NOT A PRIVATE STEP OF THE VALIDATE PHASE.
    // It exists because a bundle holding two of seventeen DLLs was reported as a
    // successful build, and the reason nobody noticed is that no layer above the
    // failure asked the question. A check written to catch a silent failure has
    // to be PROVABLE, and a private method inside a class that owns a thread and
    // spawns child processes is not reachable from a test. This is: pure, two
    // paths in, a list of names out.
    //
    // NEITHER DIRECTORY HAS TO EXIST. A missing bundle directory reports
    // everything as missing, which is the truth; a missing staged directory
    // reports nothing, because with no source of truth there is nothing this
    // function can honestly claim.
    //
    // THE STAGED DIRECTORY IS THE SOURCE OF TRUTH RATHER THAN A LIST OF NAMES,
    // and that is the whole design. app_runtime_deps populates it with everything
    // the engine needs to RUN, including DLLs that appear in no import table --
    // PhysXCommon_64 is loaded by PhysX_64 at runtime, which is why a dependency
    // walk cannot find it and why a hand-written list was already measured wrong
    // by four entries (cmake/stage_runtime_dlls.cmake). A comparison against a
    // directory cannot drift; a list can.
    ENGINE_API std::vector<std::string> MissingRuntimeLibraries(
        const std::string& bundleDir, const std::string& stagedDir);

    // -------------------------------------------------------------------
    // The process seam: declared here, implemented by the host
    // -------------------------------------------------------------------
    // Deliberately the same four operations `editor::Subprocess` already
    // exposes, so the adapter is a forwarding shim rather than a design.
    //
    // THE CALLING DISCIPLINE IS PART OF THE CONTRACT, because getting it wrong
    // is a use-after-reap rather than a wrong answer, and Subprocess.cpp:188-198
    // records that this repository has already met it:
    //
    //   ReadChunk  runs ONLY on the job thread, blocking, until it returns false.
    //   Wait       runs ONLY on the job thread, ONLY after ReadChunk returned
    //              false. It reaps, after which the pid may be recycled by the
    //              kernel, so nothing may signal the child again.
    //   Kill       may run on ANY thread while ReadChunk is blocked -- that is
    //              how cancellation works: killing the child EOFs the pipe and
    //              the blocked ReadChunk returns. It must be a no-op after Wait.
    class ENGINE_API IBuildProcess {
    public:
        virtual ~IBuildProcess() = default;
        // Append the next chunk of the child's stdout to `out`. Blocking.
        // Returns false at end of stream.
        virtual bool ReadChunk(std::string& out) = 0;
        // Reap and return the exit code. Idempotent.
        virtual int  Wait() = 0;
        // Force-kill. Safe to call once, from another thread, while a ReadChunk
        // is in flight.
        virtual void Kill() = 0;
    };

    // Launch a child process with its stdout piped.
    //
    // argv[0] IS A PROGRAM, NOT A TARGET NAME: it may be a bare name to be found
    // on PATH ("cmake") or an absolute path. See the Subprocess note in this
    // file's header -- today's Spawn assumes neither, and this is the assumption
    // the adapter has to break.
    //
    // Returns null on failure with a human-readable reason in `error`.
    using BuildProcessLauncher = std::function<std::unique_ptr<IBuildProcess>(
        const std::vector<std::string>& argv, std::string& error)>;

    // -------------------------------------------------------------------
    // Everything the pipeline needs to know about the tree it is running in
    // -------------------------------------------------------------------
    // A PLAIN STRUCT THE HOST FILLS, with no discovery hidden inside it. The
    // engine cannot work these out for itself and should not pretend to: a
    // running executable does not know its CMake binary directory, and deriving
    // it from the exe path (`exeDir/../../..`, which happens to be right today
    // because Engine/CMakeLists.txt:325-338 and Player/CMakeLists.txt:18-22 both
    // pin the layout to build/bin/<CONFIG>/) is a layout assumption that would
    // fail silently the day somebody changes an output directory.
    //
    // WHERE THE VALUES COME FROM is the host's problem, and the intended answer
    // is a small JSON written next to the editor at CONFIGURE time
    // (`configure_file` in Editor/CMakeLists.txt, one line, that agent's file)
    // holding sourceDir, binaryDir, the generator's multi-config flag and
    // CMAKE_BUILD_TYPE. A file rather than compile definitions on purpose:
    // baking absolute build-machine paths into Engine.dll puts them in every
    // shipped game, which is the same class of leak ADR-007 §2.1 complains about.
    struct ENGINE_API BuildEnvironment {
        // CMAKE_SOURCE_DIR. Used as the default root for the output directory,
        // and named in preflight messages.
        std::string sourceDir;

        // CMAKE_BINARY_DIR -- the tree `cmake --build` is pointed at. See the
        // Engine.dll-lock note above for why this is a field rather than a
        // derivation.
        std::string binaryDir;

        // Where the built applications and the staged content live:
        // <binaryDir>/build/bin/<CONFIG>/. THE ASSEMBLE PHASE'S SOURCE.
        //
        // This is the config the EDITOR is running as, which is not necessarily
        // the config the profile selects -- so the assemble phase uses the exeDir
        // belonging to the PROFILE's config, which the pipeline derives by
        // replacing the trailing config directory. Preflight reports the derived
        // directory so a wrong answer is visible before anything is copied.
        std::string exeDir;

        // What BuildSettings::outputDirectory is relative to. Empty means
        // sourceDir. Containment is enforced against this, so a build can never
        // write outside it -- see the assemble contract below.
        std::string outputRoot;

        // How to invoke CMake. A bare "cmake" is found on PATH; an absolute path
        // pins a specific one.
        std::string cmakeCommand = "cmake";

        // TRUE for Visual Studio/Xcode, FALSE for Ninja and Makefiles.
        //
        // THIS FIELD IS WHY THE PROFILE'S CONFIGURATION AXIS IS NOT ALWAYS FREE.
        // A multi-config tree takes `--config <Config>` and can produce any
        // profile. A single-config tree had CMAKE_BUILD_TYPE fixed when it was
        // configured, `--config` means nothing there, and asking for a profile
        // whose configuration differs from `configuredBuildType` is a request
        // this tree cannot satisfy. CMakePresets.json uses Ninja and so does CI,
        // so the single-config case is the DEFAULT case on this project, not the
        // exotic one. Preflight must refuse it and name the preset to configure;
        // building anyway would silently ship the wrong configuration under the
        // right filename.
        bool multiConfigGenerator = false;

        // CMAKE_BUILD_TYPE. Meaningful only when multiConfigGenerator is false.
        std::string configuredBuildType;

        // OPTIONAL, AND THE EDITOR WILL LEAVE IT EMPTY.
        //
        // When a build links a title, `TitleFrontEndScene()` BEATS project.json's
        // startupScene at boot (PlayerMain.cpp:173-223 lays out the order and
        // says why: project.json's default is the engine demo, so letting it win
        // would ship a game that boots the backpack room). A build list whose
        // index 0 is not the title's front end is therefore overruled in the
        // bundle -- the player prints a line saying so, but the panel should have
        // said it first.
        //
        // The editor CANNOT fill this in. It is not linked against
        // UntitledFighterFrontEnd and deliberately so (root CMakeLists.txt:202-216:
        // an editor never asks "what does this host start at"), so the hook is
        // not even declared in that translation unit. Left here for a host that
        // does know -- a headless build tool that links the front end -- and
        // documented so that the panel can state the consequence as help text
        // instead of the pipeline guessing at it.
        std::string titleFrontEndScene;

        // REQUIRED. Without it the compile phase cannot run and preflight fails.
        BuildProcessLauncher launchProcess;
    };

    // -------------------------------------------------------------------
    // Phases, outcomes, progress
    // -------------------------------------------------------------------
    enum class BuildPhase {
        Idle,        // constructed, thread not yet running
        Preflight,   // re-validating; nothing created yet
        Compiling,   // cmake --build, output streaming into the log
        Assembling,  // cmake --install, then prune + manifest
        Validating,  // checking the bundle that now exists
        Finished,    // report() is valid and final
    };

    enum class BuildResult {
        NotFinished,
        Succeeded,
        FailedPreflight,  // nothing was created
        FailedCompile,    // nothing was created; exitCode says what cmake said
        FailedAssemble,   // partial output was REMOVED -- see the contract below
        // THE BUNDLE EXISTS AND IS LEFT ON DISK. Unlike every other failure: the
        // author needs to look at what was produced to understand why it was
        // rejected, and a validation failure is a statement about content
        // (a boot scene with no camera) rather than a broken artifact. The report
        // says so, and the panel must not describe it as "nothing was written".
        FailedValidation,
        Cancelled,        // partial output removed, as FailedAssemble
    };

    ENGINE_API const char* BuildPhaseName(BuildPhase p);
    ENGINE_API const char* BuildResultName(BuildResult r);

    // What the panel draws this frame. Returned by value from Poll; small on
    // purpose, because it is copied 60 times a second.
    struct ENGINE_API BuildProgress {
        BuildPhase phase = BuildPhase::Idle;

        // 0..1 within the current phase. NEGATIVE MEANS INDETERMINATE, which is
        // the honest answer during Compiling and during the install half of
        // Assembling: neither cmake invocation reports progress, the number of
        // translation units it will rebuild is not knowable in advance, and a
        // fake percentage that sticks at 40% for two minutes is worse than a
        // spinner. The PRUNE half has a real file count and reports a real
        // fraction.
        float fraction = -1.0f;

        // One line, already phrased for a human: "Compiling PlayerShipping
        // (Release)...", "Installing to Builds/Game...", "Checking scenes
        // (3/4)". Never a path on its own.
        std::string status;

        // Once true, report() is valid and further Polls are stable.
        bool finished = false;
    };

    // The value the panel keeps after the job object is destroyed.
    struct ENGINE_API BuildReport {
        BuildResult result = BuildResult::NotFinished;

        // Human-facing, already logged. On failure this is the ONE SENTENCE the
        // panel shows -- the raw compiler output goes to the log, not here.
        std::string message;

        // The itemised reasons behind `message`, when there are several: the
        // preflight refusals for FailedPreflight, the failed checks for
        // FailedValidation. Empty for FailedCompile, whose detail is the log.
        //
        // A VECTOR FOR THE SAME REASON BuildPreflight::errors IS ONE: reporting
        // one problem per attempt makes the author play twenty questions with
        // their own settings.
        std::vector<std::string> errors;

        // cmake's exit code when result == FailedCompile; 0 otherwise.
        int exitCode = 0;

        // Absolute, after containment resolution. Set even on failure, so the
        // panel can say what it was going to write.
        std::string outputDirectory;

        BuildPhase failedIn = BuildPhase::Idle;

        std::size_t filesCopied = 0;
        std::uintmax_t bytesCopied = 0;

        // Exactly the build list, in order, as they were written into the bundle.
        std::vector<std::string> scenesIncluded;
        // Scenes found in the asset root and deliberately left out. NAMED, not
        // counted: a scene silently missing from a bundle is the failure this
        // whole file exists to make visible, and it is the same shape as the
        // `[stage] REMOVING` announcements that cmake/stage_runtime_assets.cmake
        // prints for exactly this reason (:176-191).
        std::vector<std::string> scenesExcluded;

        // Non-fatal things the author should read: a scene in the list that the
        // asset root does not contain would be an ERROR, but a bundle with no
        // camera, a Shipping build with no title linked, or an output directory
        // that already existed are warnings.
        std::vector<std::string> warnings;

        double seconds = 0.0;

        bool ok() const { return result == BuildResult::Succeeded; }
    };

    // -------------------------------------------------------------------
    // Preflight: the answer before anything happens
    // -------------------------------------------------------------------
    struct ENGINE_API BuildPreflight {
        bool ok = false;

        // EVERY reason, not the first. A panel that reports one error per attempt
        // makes the author play twenty questions with their own settings.
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        // What the build resolved to, shown in the panel BEFORE the button is
        // pressed so the author can see they are about to produce
        // Release/Player.exe rather than reading it in the log afterwards.
        std::string cmakeConfig;         // BuildProfileCMakeConfig(profile)
        std::string cmakeTarget;         // BuildProfileCMakeTarget(profile)
        std::string playerExecutable;    // BuildPlayerExecutableName(profile)
        std::string resolvedExeDir;      // the profile's config output directory
        std::string resolvedOutputDir;   // absolute, contained under outputRoot

        // Scenes the asset root has that this build will leave out. Same data as
        // BuildReport::scenesExcluded, available before the build so the panel
        // can show it beside the list. Requires walking the staged tree, which is
        // why this is not free -- see the note on PreflightBuild.
        std::vector<std::string> scenesExcluded;
    };

    // Validates the settings against the environment. CREATES NOTHING, DELETES
    // NOTHING, spawns nothing.
    //
    // What it must refuse (each an entry in `errors`):
    //   - an empty scene list: there is nothing to boot
    //   - a scene in the list that the staged asset root does not contain, or
    //     that SceneSerializer::Validate rejects -- a build must not ship a
    //     bundle whose startup scene fails to load, and Validate is cheap (a JSON
    //     walk, no asset I/O: SceneSerializer.h:49-57)
    //   - an outputDirectory that PathIsContained refuses against outputRoot
    //   - an output path that resolves inside the SOURCE TREE, inside binaryDir,
    //     or inside exeDir. All three are the "owned by the build system"
    //     refusals of the assemble contract below, and they are checked here
    //     because the cheapest place to stop is before the compile.
    //   - an existing output directory carrying no marker from a previous build
    //   - a profile whose configuration this tree cannot produce (see
    //     BuildEnvironment::multiConfigGenerator)
    //   - a profile whose configuration this tree's install rules put no player
    //     in the bundle for. Player/CMakeLists.txt:106-109 installs PlayerShipping
    //     for Release and PlayerDebug for Debug and RelWithDebInfo; a tree
    //     generated before those rules existed installs neither for two of the
    //     three profiles, and a bundle carrying the whole game except the program
    //     that runs it is broken output. See the assemble contract.
    //   - scenes[0] != titleFrontEndScene, WHEN that field is set and the player
    //     does not yet honour a build manifest. This is the second road to
    //     ADR-008 decision 5's invariant; see the manifest section below for
    //     which road is live.
    //   - a missing launchProcess, sourceDir, binaryDir or exeDir
    //
    // AND WHAT IT MUST NOT REFUSE, which is the same list read the other way and
    // is the harder half to keep honest. THE TEST IS WHETHER THE OUTPUT WOULD BE
    // BROKEN, not whether the build is unusual, because a preflight that refuses
    // unusual builds teaches the author to stop reading it.
    //
    // A BUILD LIST OF ONE SCENE IS A COMPLETE GAME and gets a note. Plenty of
    // shipped titles are exactly one scene, and this project's own front end plus
    // training mode is very nearly that; the useful thing to say is not "add
    // more" but "the bundle carries what is listed and prunes the rest". So do:
    // an output directory that already holds a previous build; a Shipping profile
    // in a tree with no title linked, which produces a perfectly good player that
    // boots the engine demo; a build assembling content from a configuration other
    // than the editor's; and an install rule this preflight could not read an
    // answer out of, where validate over the assembled bundle answers instead.
    //
    // NOT FREE, so do not call it per frame: it stats the scene list and walks
    // the staged asset root looking for scenes to exclude. Call it when the
    // settings change and when the panel opens, and cache the result.
    ENGINE_API BuildPreflight PreflightBuild(const BuildSettings& settings,
                                             const BuildEnvironment& env);

    // -------------------------------------------------------------------
    // The job
    // -------------------------------------------------------------------
    // MAIN THREAD ONLY for every member below. The job owns one thread; these
    // functions talk to it and never run on it.
    //
    // PIMPL because the implementation's members are a std::thread, a mutex and
    // some atomics, and none of those belong in a header crossing a DLL boundary
    // -- nor should this header decide the implementer's internals. The engine
    // already keeps its SDK-facing types this way (the physics and scripting
    // backends).
    class ENGINE_API BuildJob {
    public:
        // Re-runs preflight (never trusting a cached one -- the settings or the
        // disk may have moved since the panel last asked) and, if it passes,
        // starts the thread. Always returns a job: a preflight failure comes back
        // as an already-finished job whose report carries the reasons, so the
        // panel has ONE code path for "how did the build go" instead of two.
        //
        // Takes both by value: the job outlives the panel's copy of the settings,
        // and a build must not change under its own feet because somebody
        // reordered the list while it ran.
        static std::unique_ptr<BuildJob> Start(BuildSettings settings,
                                               BuildEnvironment env);

        ~BuildJob();
        BuildJob(const BuildJob&) = delete;
        BuildJob& operator=(const BuildJob&) = delete;

        // Once per frame. Cheap: takes a mutex, copies a snapshot, releases.
        //
        // `appendLog`, when given, receives everything the job has produced since
        // the last Poll -- APPENDED, so the panel owns the accumulated buffer and
        // this never hands back a growing string. Same shape as
        // Subprocess::readChunk(std::string&), and for the same reason: the log
        // of a long compile is large and must be moved once, not copied per
        // frame.
        BuildProgress Poll(std::string* appendLog = nullptr);

        // Asks the job to stop. Returns immediately -- the thread notices either
        // as soon as killing the running child (cmake, or the cooker) EOFs the
        // pipe, or at the next cancellation point between files during the prune.
        //
        // CANCELLING NEVER LEAVES HALF A GAME ON DISK. The assemble phase records
        // every path it created and removes them, which is why Cancelled and
        // FailedAssemble share a sentence in the enum above. A partial bundle is
        // worse than none: it has a Player.exe in it and will start.
        //
        // A CANCEL DURING `cmake --install` IS THE AWKWARD ONE and is worth
        // naming: the child is part-way through writing a tree. Killing it and
        // then removing what was created is correct precisely BECAUSE the output
        // directory is owned by the build (see the contract below) -- there is
        // nothing in there that is not ours to delete. That ownership rule is
        // what makes cancellation safe, rather than the cancellation being
        // careful.
        void RequestCancel();

        bool finished() const;

        // Valid once finished(). Before that, result == NotFinished.
        const BuildReport& report() const;

    private:
        BuildJob();
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // =====================================================================
    // WHAT GETS INTO THE BUNDLE
    // =====================================================================
    // The assemble phase's complete contract. Written here rather than in the
    // .cpp because the panel has to describe it to an author and the pipeline has
    // to implement it, and those two must not disagree.
    //
    // INSTALL, THEN PRUNE, THEN WRITE THE MANIFEST. Three steps, in that order,
    // and the ordering is the design: CMake already knows how to produce a
    // bundle, it does not know about a scene list, and the scene list is the only
    // thing this feature adds.
    //
    // THE CONTENT REACHES THE BUNDLE FROM THE STAGED RUNTIME DIRECTORY, which is
    // where `runtime_assets` flattened the engine's asset root and every linked
    // title's (root CMakeLists.txt:249-305, cmake/stage_runtime_assets.cmake).
    // The compile phase refreshes it for free -- both player targets carry
    // `add_dependencies(... runtime_assets)` (Player/CMakeLists.txt:43-44) -- and
    // the `install(CODE ...)` block at Player/CMakeLists.txt:145-163 is what layers
    // it over the source defaults.
    //
    // THAT LAYERING IS ALSO A KNOWN WEAKNESS AND IT IS INHERITED, NOT INTRODUCED.
    // `install(DIRECTORY ${CMAKE_SOURCE_DIR}/Editor/src/Exported/ ...)` (:50)
    // hand-names the ENGINE's asset root and never learned about
    // CSE_ASSET_ROOTS, so a title's content reaches a bundle only via the staged
    // tree layered on top -- ADR-007 §5 calls this "two sources of truth for what
    // ships and only one of them knows about titles", and correct by accident.
    // Using `cmake --install` means this pipeline gets exactly what `cpack` gets,
    // including that. Fixing it is a change to the install rules (ADR-007
    // decision 11), and doing it there fixes both routes at once; reimplementing
    // the copy here would fix one route and leave the other wrong, which is the
    // trade this contract deliberately refuses.
    //
    // ---- 1. THE BUNDLE IS PRODUCED BY `cmake --install`, NOT BY A COPIER ----
    //
    //   cmake --install <binaryDir> [--config <Config>] --prefix <output>
    //
    // AND THE REASON IS NOT TIDINESS. `X_VCPKG_APPLOCAL_DEPS_INSTALL ON` (root
    // CMakeLists.txt:6) makes vcpkg deploy each installed target's third-party
    // DLL closure AT INSTALL TIME. A hand-rolled copier would therefore produce a
    // bundle with a DIFFERENT DLL SET than `cpack` produces from the same tree --
    // two assembly paths, two bug reports, and the difference only visible as a
    // loader error on somebody else's machine. The rules already exist and are
    // already exercised by the `package` target: `install(TARGETS Engine)`
    // (Engine/CMakeLists.txt:377), the two player rules, and the
    // `install(DIRECTORY ...)` + `install(CODE ...)` pair that layers
    // editor-authored content over the source defaults (Player/CMakeLists.txt:106-163).
    //
    // WHICH MEANS `cpack` FOLLOWS THE PROFILE RULE TOO, and that is the point
    // rather than a side effect: the `package` target on a RelWithDebInfo tree now
    // zips PlayerDebug.exe instead of an exe named Player.exe compiled with
    // RelWithDebInfo flags. Two assembly paths agreeing is the whole reason this
    // phase shells out to CMake at all, and they now agree about the executable as
    // well as about the DLLs.
    //
    // THIS IS ADR-008 DECISION 2, and it is a deliberate reversal of the obvious
    // design. The obvious one -- copy the exe, copy *.dll from <exeDir>, copy
    // Exported/ -- is defensible right up to the point where somebody runs
    // `cpack` and gets a different answer.
    //
    // ONE PLAYER PER BUNDLE, AND THE CONFIGURATION CHOOSES IT. Two player targets,
    // three profiles, and a bundle that may contain exactly one executable -- put
    // both in and the player somebody double-clicks is ambiguous, with the wrong
    // guess being the console build of a shipped game. The install rules are
    // therefore restricted to the configurations each profile selects
    // (Player/CMakeLists.txt:106-109):
    //
    //     install(TARGETS PlayerShipping RUNTIME DESTINATION .
    //             CONFIGURATIONS Release)
    //     install(TARGETS PlayerDebug RUNTIME DESTINATION .
    //             CONFIGURATIONS Debug RelWithDebInfo)
    //
    // which is BuildSettings.h's profile table written as install rules.
    // `cmake --install --config <Config>` then selects the player without this
    // pipeline naming a target at all, and the rule and the pipeline cannot drift
    // apart because they agree on the CONFIGURATION -- something both sides
    // already had to get right -- rather than on a second name.
    //
    // NOT COMPONENTS, which is the other obvious answer and the wrong one here:
    // `cmake --install --component <name>` installs ONLY that component, and every
    // other rule in this tree is in the default one (Engine, the lua-cpp DLL, and
    // the two content rules below). A component-selected install would produce a
    // bundle holding a player and no Engine.dll, and making it work means tagging
    // every rule across three files with one name that nobody may ever miss.
    //
    // AND PREFLIGHT SEES IT COMING, which is the requirement that decided this
    // rather than a bonus: CMake wraps each restricted rule in an
    // `if(CMAKE_INSTALL_CONFIG_NAME MATCHES ...)` guard in the generated install
    // script, so "does this profile install a player" has an answer in the build
    // tree with nothing built and no process started. That is what
    // BuildPipeline.cpp's ProfileHasInstallRule reads, and it is why this is a
    // preflight refusal rather than something discovered after a compile.
    //
    // build.json IS NOT INSTALLED. The `install(CODE ...)` block at :145-163 copies
    // every staged file that is not a sidecar, and BuildSettings::DefaultPath()
    // lives in that directory, so it is excluded there by name (:151). Excluded in
    // the RULE rather than only in the prune below, so that `cpack` produces the
    // same bundle this pipeline does; the prune still removes it, announced, as
    // the backstop for any third route in.
    //
    // AND THE EXECUTABLE ALLOW-LIST STILL MATTERS, EVEN THOUGH INSTALL HANDLES
    // IT. Editor.exe, AssetCooker.exe and the OTHER player all live in
    // <exeDir> -- all four applications share build/bin/<CONFIG>/ (root
    // CMakeLists.txt:377-389). Nothing installs the editor or the cooker, and the
    // other player is kept out by the configuration restriction rather than by
    // having no rule, which is exactly why `cmake --install` is safe and why any
    // post-install copy step must name the single file it wants and never glob
    // for executables. A "copy the directory" assemble phase ships the editor
    // inside the game; a glob for `Player*` ships both players.
    //
    // THE LINUX BUNDLE IS NOT SELF-CONTAINED, said here rather than discovered.
    // vcpkg's applocal deployment is a Windows behaviour; on Linux, assimp, glfw
    // and the rest resolve through RPATH or system packages, so an installed
    // Linux bundle runs on a machine that has the dependencies and is not yet a
    // thing to hand to a player. That is RPATH work on the install rules, and it
    // is a separate job from this one.
    //
    // ---- 2. THEN PRUNE: THE SCENES THAT ARE NOT IN THE BUILD ---------------
    //
    // The install rules ship the WHOLE asset root -- they know nothing about a
    // scene list, and teaching CMake about one would put the build list in two
    // places. So the selection happens AFTER, over the assembled output, which is
    // also the only tree that reflects what install actually decided.
    //
    // Removed from <output>/Exported/ :
    //
    //   * EVERY FILE THAT IS A SCENE AND IS NOT IN THE BUILD LIST. This is the
    //     only one the install rules cannot do, and the only reason this step
    //     exists: CMake knows nothing about a scene list.
    //   * project.json is REPLACED rather than removed. See 3.
    //   * build.json, as a BACKSTOP. The install rules already exclude it by name
    //     (Player/CMakeLists.txt:151), so this normally removes nothing.
    //
    // (`*.import` is the same shape: excluded by both install rules
    // -- Player/CMakeLists.txt:117, :151 -- and removed here only as a backstop.)
    //
    // BOTH BACKSTOPS ANNOUNCE WHEN THEY FIRE, which is what stops them from
    // quietly becoming the reason this bundle differs from the one `cpack`
    // produces out of the same rules.
    //
    //   "IS A SCENE" MEANS SceneSerializer::Validate ACCEPTS IT. That is the
    //   engine's own definition, it is the same probe a scene swap runs, and it
    //   is cheap -- a JSON walk with no asset I/O (SceneSerializer.h:49-57).
    //   Nothing else in this pipeline gets to have an opinion about what a scene
    //   is, because a second definition would drift from the loader's.
    //
    //   THE PATH SPELLING HAS TO MATCH OR EVERY SCENE IS PRUNED. Walk the tree
    //   rooted at <output> so an installed file comes out as "Exported/menu.json"
    //   -- exactly the spelling BuildSettings::scenes carries (BuildSettings.h
    //   says why: it is ProjectSettings::startupScene's spelling, so ApplyTo is a
    //   copy rather than a translation, and the shipped player resolves it
    //   against its own working directory the same way). Compare through
    //   BuildSettings::NormalizeScenePath, never with raw string equality: get
    //   this wrong and the build list matches nothing, every scene is excluded,
    //   and the bundle ships a game with no scenes in it and no error.
    //
    //   EVERY EXCLUSION IS ANNOUNCED, into the log and into
    //   BuildReport::scenesExcluded. A scene quietly missing from a bundle is the
    //   same failure the staging script's `[stage] REMOVING` message exists to
    //   prevent, one layer up.
    //
    //   A SCENE IN THE LIST THAT THE ASSET ROOT DOES NOT HAVE IS AN ERROR, caught
    //   in preflight, before the compile. Not a warning: it is a level that will
    //   not be in the game.
    //
    // ---- 3. THE MANIFEST: project.json IS GENERATED, NOT SHIPPED AS FOUND ---
    //
    //   Load <exeDir>/Exported/project.json (the EDITOR's, for masterVolume),
    //   call BuildSettings::ApplyTo on it, save it over
    //   <output>/Exported/project.json.
    //
    //   Load-modify-save rather than write-fresh, so masterVolume -- which the
    //   editor's Settings > Audio wrote and which the shipped game boots at
    //   (PlayerMain.cpp:288) -- survives.
    //
    //   ApplyTo DOES TWO THINGS: startupScene <- scenes[0], and
    //   `startupSceneFromBuild = true`. The second is what makes the bundle boot
    //   scenes[0] rather than the title's compiled-in front end, and it is
    //   ADR-008 decision 5's invariant: THE BUILT GAME BOOTS scenes[0], OR THE
    //   BUILD REFUSES TO PRODUCE IT. ProjectSettings.h carries the full argument.
    //
    //   AND IT NEEDS ONE CONDITION IN THE PLAYER, which is not this file's to
    //   write. PlayerMain.cpp:196-200 currently resolves command line, then
    //   TitleFrontEndScene(), then project.json; the manifest road makes it
    //   command line, then project.json IF startupSceneFromBuild, then
    //   TitleFrontEndScene(), then project.json. An unmarked file keeps exactly
    //   today's order, so PlayerMain.cpp:182-190's protection against a stale
    //   preference booting the engine demo is untouched -- that argument was
    //   always about files the build did not write.
    //
    //   UNTIL THAT CONDITION LANDS, the other road is open and preflight is where
    //   it lives: refuse a build whose scenes[0] is not
    //   BuildEnvironment::titleFrontEndScene when that field is set. Either road
    //   satisfies the invariant. Neither being implemented does not.
    //
    // ---- 4. WHAT HAPPENS TO AN ASSET ONLY THE DROPPED SCENE USED -----------
    //
    //   NOTHING. IT SHIPS. And this is scoped out rather than solved.
    //
    //   Dropping a scene from the build list removes ONE JSON FILE from the
    //   bundle. The models, textures, scripts, UI documents and audio it was the
    //   only referent of are still in Exported/ and still copied. A bundle is
    //   therefore as large as the whole asset root no matter how few scenes are
    //   in it.
    //
    //   IT CANNOT BE DONE WITHOUT AN ASSET DEPENDENCY GRAPH, and there is not one.
    //   What exists is SceneSerializer::CollectModelPaths (SceneSerializer.h:59-70),
    //   which returns the MODEL paths a scene references -- one edge type out of
    //   the several a real answer needs. The rest have no enumerator anywhere:
    //     - a model's textures are inside the model file, resolved by Assimp
    //       relative to the model's own directory (Model.cpp:570-571, :592-594);
    //     - a UIDocumentComponent names a .cxml, which names .cstyle files, which
    //       name background images;
    //     - scripts, audio clips, fonts and the scene's environment map are each
    //       resolved by their own subsystem at its own call site.
    //   ADR-007 §3.1 counts thirteen independent read sites across twelve files,
    //   each turning a project-relative path into an open file on its own. A
    //   dependency graph is a resolver those thirteen sites all report through --
    //   which is precisely ADR-007's mount system, not a feature of this pipeline.
    //
    //   So: NOT IN SCOPE, and the honest statement to put in the panel is
    //   "dropping a scene removes the scene, not its assets". Trimming a bundle
    //   is downstream of the asset search path, and pretending otherwise here
    //   would mean writing a second, partial, silently-wrong answer to which
    //   files a scene needs -- and the failure mode of a wrong answer is a
    //   shipped game with a missing texture.
    //
    // ---- 5. THE OUTPUT DIRECTORY IS OWNED BY THE BUILD ---------------------
    //
    //   Resolved with PathIsContained against BuildEnvironment::outputRoot --
    //   at load (BuildSettings), again against the real root before anything is
    //   created, and again per written path.
    //
    //   FOUR REFUSALS, because a build that writes somewhere unexpected is worse
    //   than one that stops (ADR-008 decision 7):
    //
    //     * REFUSE THE SOURCE TREE. A build writing into Editor/src/Exported/ or
    //       Games/<title>/Assets/ corrupts the inputs of the next one.
    //     * REFUSE THE STAGED RUNTIME Exported/ (and anything under exeDir or
    //       binaryDir). cmake/stage_runtime_assets.cmake MIRRORS: a file with no
    //       source counterpart is deleted and the removal announced (:176-191,
    //       :225-258). A bundle assembled there is deleted by the next build,
    //       loudly, which reads as data loss.
    //     * REFUSE A NON-EMPTY DIRECTORY THE BUILD DID NOT CREATE, or require an
    //       explicit overwrite. The pipeline writes a marker file into a
    //       directory it creates and looks for it on every later build. This
    //       phase deletes, and an assemble step that clears its destination will
    //       eventually be pointed at a Documents folder.
    //     * NEVER RESOLVE THE DESTINATION THROUGH A SEARCH PATH when ADR-007
    //       lands: its decision 5 already says writes name exactly one
    //       destination, and this is its first non-editor caller.
    //
    //   MIRRORED, not merely overwritten. After assembling, a file in the output
    //   directory that this build did not write is REMOVED and the removal
    //   announced. cmake/stage_runtime_assets.cmake:176-191 paid for this lesson
    //   in a way worth repeating: a copy adds and overwrites but never removes,
    //   so files deleted from the source lived on in every tree that had ever
    //   seen them -- and the case that found it was three characters transcribed
    //   from third-party sources that may not be distributed, sitting in a
    //   directory the packaging step walks. Without a mirror, switching a build
    //   from Development to Shipping leaves the previous PlayerDebug.exe and its
    //   PDBs beside the new Player.exe, and a bundle that ships a debug player
    //   nobody meant to send is the same class of accident.
    //
    //   ON FAILURE OR CANCEL the created paths are removed and the directory is
    //   left as it was found. The exception is FailedValidation, which leaves the
    //   bundle in place deliberately -- see the enum.
    //
    // ---- 6. VALIDATE BEFORE DECLARING SUCCESS ------------------------------
    //
    //   ADR-008 decision 9, and the argument for it is one sentence: A BUILD IS
    //   THE ONLY MOMENT THE TOOL KNOWS WHAT WILL SHIP. Three checks, over the
    //   ASSEMBLED OUTPUT rather than over the staged tree, because the assembled
    //   output is the artifact and the two can differ:
    //
    //     * EVERY LISTED SCENE IS PRESENT AND LOADS. SceneSerializer::Validate,
    //       the same probe as everywhere else.
    //     * THE BOOT SCENE HAS AN ENABLED CameraComponent. This is exactly the
    //       failure PlayerMain.cpp:629-635 reports at runtime -- "contains no
    //       enabled CameraComponent, so there is nothing to render from" -- and
    //       whose comment records that falling back to a free-fly camera "looks
    //       exactly like the game ignored my camera". Catching it at build time
    //       is the difference between a message in the editor and a bug report.
    //       MECHANISM DELIBERATELY LEFT OPEN: a real load needs an AssetManager
    //       and would import models, and a JSON-level check would be a second
    //       definition of the scene format. Whoever writes the .cpp picks, and
    //       whichever they pick, this is an ERROR rather than a warning.
    //     * `AssetCooker validate <output>/Exported` IS CLEAN. The cooker is
    //       already spawned this way by the editor (EditorApplication.cpp:2740-2741)
    //       and its exit code is already interpreted there (:376-377: 0 clean,
    //       1 errors found), so this reuses a protocol rather than inventing one
    //       -- through the same BuildProcessLauncher as the compile.
    //
    //   The AssetCooker binary is a SIBLING of the editor, so this is the one
    //   spawn in the pipeline that today's Subprocess could already perform.

} // namespace MyCoreEngine
