# Getting Started

Verified: 2026-08-17 @ e2f08bd

This page takes you from a fresh clone to a running editor and a running game. It covers the
prerequisites, the three build configurations and when to use each, where the binaries and
assets end up, and the asset-staging rule that decides whether a build can overwrite a scene
you saved.

Read this before your first build. The choice of configuration in particular has consequences
that are easy to misdiagnose as engine bugs.

## What gets built

The top-level `CMakeLists.txt` adds seven subdirectories — `ThirdParty`, `Net`,
`Games/UntitledFighter`, `Engine`, `Editor`, `Player`, `Cooker`, plus `docs` and (with
`ENABLE_TESTS`) `tests`. Five of them produce the targets you run:

| Target | Kind | Purpose |
|---|---|---|
| `Engine` | SHARED library (`Engine.dll`) | The engine itself, namespace `MyCoreEngine` |
| `Editor` | executable (`Editor.exe`) | ImGui editor: viewport, hierarchy, inspector, renderer tuning |
| `PlayerDebug` | executable (`PlayerDebug.exe`) | Standalone player, console subsystem — keeps the terminal for logs |
| `PlayerShipping` | executable (`Player.exe`) | Standalone player, WIN32 subsystem — no console window |
| `AssetCooker` | executable (`AssetCooker.exe`) | Headless asset work; the editor spawns it as a child process |

`PlayerDebug` and `PlayerShipping` are built from the same source file
(`Player/src/PlayerMain.cpp`). `PlayerShipping` sets `OUTPUT_NAME "Player"`, is built with
`WIN32_EXECUTABLE TRUE`, and defines `MYCE_SHIPPING=1` — with no console, a startup failure
would otherwise be a silent exit, so it reports fatal errors through a `MessageBox` instead.

`Editor` has an explicit `add_dependencies(Editor AssetCooker)` in the root `CMakeLists.txt`.
The editor invokes `AssetCooker.exe` at runtime (the Validate button), so building the editor
must refresh the cooker too — otherwise a dev-loop build ships a stale or missing cooker next
to a new editor.

Unit tests are built by default; `option(ENABLE_TESTS "Build unit tests" ON)` turns them off.

## Prerequisites

1. **A C++17 compiler.** On Windows, **Visual Studio 2022** with the C++ desktop workload is
   the primary path (the build has MSVC-specific pieces like `/Zi` and
   `/ENTRY:mainCRTStartup`, all guarded behind `if(MSVC)`). The engine also builds on
   **Linux** (gcc ≥ 11 / clang ≥ 14) — see [Building on Linux](#building-on-linux) below.
2. **CMake 3.21 or newer.** The root `CMakeLists.txt` only requires 3.20
   (`cmake_minimum_required(VERSION 3.20)`), but `CMakePresets.json` is a version-3 preset
   file declaring `"cmakeMinimumRequired": { "major": 3, "minor": 21 }` — so the preset
   workflow below needs 3.21+. (The "Without presets" raw `cmake -B ...` invocation further
   down still configures fine on 3.20.) Visual Studio 2022's bundled CMake is already past
   3.21, so this normally only bites on Linux or a hand-installed CMake.
3. **Ninja** — the generator this project is normally built with. Visual Studio 2022 ships it
   with the C++ workload; from a plain terminal, run inside a `vcvars64` environment.
4. **vcpkg**, used in **manifest mode**. There is a `vcpkg.json` at the repo root, so you do
   not install packages by hand — pointing CMake at the vcpkg toolchain file is enough and
   vcpkg restores the manifest during configure.
5. **`CSE_VCPKG_ROOT` — set this once on *every* machine you build on.** The committed CMake
   presets read your vcpkg location from this variable (it is deliberately not the standard
   `VCPKG_ROOT` — see [Configuring and building](#configuring-and-building) for why). Set it,
   then restart Visual Studio / your shell so it is inherited:
   ```bat
   setx CSE_VCPKG_ROOT C:\path\to\vcpkg
   ```
   It is not stored in the repo (every machine's vcpkg lives somewhere different), so a fresh
   clone will not have it. Skip this step and every preset fails to configure with
   `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake`.

`vcpkg-configuration.json` pins the default registry to a specific vcpkg baseline commit, so
everyone resolves the same package versions.

### Dependencies (from `vcpkg.json`)

`glfw3`, `glad`, `stb`, `glm`, `assimp`, `entt`, `nlohmann-json`, `meshoptimizer`, `imguizmo`,
`miniaudio` (audio), `yoga` (flexbox layout for the in-game UI) and `pugixml` (CXML markup),
`joltphysics`, `physx` (Windows-only), `sol2` + `lua[cpp]` (scripting), and `imgui` with the
`docking-experimental`, `glfw-binding`, and `opengl3-binding` features.

**The two physics SDKs are optional.** `Engine/CMakeLists.txt` looks for them with
`find_package(... CONFIG QUIET)` behind the `CSE_ENABLE_JOLT` and `CSE_ENABLE_PHYSX` options
(both `ON` by default), and prints its decision at configure time:

```
-- Physics: Jolt backend ENABLED
-- Physics: PhysX not found - backend disabled
```

A build with neither still works — the dependency-free "Simple" backend is always registered.

> **Important — do not link an SDK straight into `Engine`.** Each SDK goes into its own
> `STATIC` library via `cse_add_isolated_backend`, never directly into `Engine`, because
> an imported target's INTERFACE compile definitions reach every consumer source file —
> and Jolt's include defines `_HAS_EXCEPTIONS=0`. If you are about to change that, read
> [the SDK isolation rule](architecture.md#the-sdk-isolation-rule) first: it records what
> broke last time and why `STATIC` rather than `OBJECT` is the load-bearing half.

## Configuring and building

The repo ships a committed **`CMakePresets.json`**, so the same named configurations show
up everywhere the project is opened — Visual Studio 2022, VS Code, CLion, and the command
line all read it. It needs one thing from the environment: **`CSE_VCPKG_ROOT`**, pointing at
your vcpkg checkout, so the toolchain resolves without a machine-specific path baked into
the file. Set it once per machine:

```bat
setx CSE_VCPKG_ROOT C:\path\to\vcpkg
```

then restart Visual Studio / the shell so it is picked up. On Linux, export it from your
shell profile.

> **Why a custom variable and not `VCPKG_ROOT`?** Visual Studio's developer environment
> forces `VCPKG_ROOT` to the vcpkg it bundles, overriding whatever you set. That bundled
> vcpkg uses a different package baseline, so it ignores your populated binary cache and tries
> to rebuild every dependency from source — and under a long project path (e.g. inside
> OneDrive) that source build can outright fail, because some headers (draco's) run past
> Windows' 260-character path limit. Routing the toolchain through `CSE_VCPKG_ROOT`, which VS
> leaves alone, keeps the build on *your* vcpkg and its cache.

From the repo root:

```bat
cmake --list-presets
cmake --preset x64-relwithdebinfo
cmake --build --preset x64-relwithdebinfo
```

| Preset | Build type | Unit tests |
| --- | --- | --- |
| `x64-debug` / `x64-release` / `x64-relwithdebinfo` | Debug / Release / RelWithDebInfo | **off** |
| `x64-relwithdebinfo-tests` | RelWithDebInfo | **on** — run with `ctest --preset x64-relwithdebinfo-tests` |

The app presets leave tests **off** so the editor and player are the only launch targets in
the IDE's Startup Item list; build and run the suite from the `-tests` preset. In Visual
Studio, pick a preset from the configuration dropdown.

**Without presets** (or on a machine where you'd rather not set `CSE_VCPKG_ROOT`), pass the
toolchain explicitly — this is the raw build the presets wrap:

```bat
cmake -B out/build/x64-Release -S . -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release
```

(`^` is the `cmd.exe` line continuation. In PowerShell use a backtick, in bash a backslash.)
Substitute `Debug` or `RelWithDebInfo` for `Release` to get the other configurations. A
manual configure builds the tests by default (`ENABLE_TESTS` defaults `ON` in
`CMakeLists.txt`); pass `-DENABLE_TESTS=OFF` to skip them. `out/` is in `.gitignore`, so your
build trees are yours; `CMakePresets.json` is committed so the configuration *list* is shared.

The first configure is slow: vcpkg has to build the manifest's dependencies. That cost is
one-time per toolchain, but an MSVC update invalidates the vcpkg binary cache and re-triggers
it, which is expected rather than a fault.

## The three configurations, and when to use each

### Debug (`x64-debug`)

Unoptimized, full debug CRT. Use it only when you need to step through logic in a small,
isolated repro — a serializer round-trip, a container bug, a unit test.

> **Important: never judge performance — or even usability — in Debug.** The draw-submission
> path runs on the order of ten times slower than an optimized build. Two things stack up:
> `glm` is header-only, small-function math that depends entirely on inlining, and MSVC's
> debug CRT enables iterator debugging (`_ITERATOR_DEBUG_LEVEL=2`), which adds checking to
> every container access in the cull/sort/submit loop. A scene that runs comfortably in
> Release can feel broken in Debug. Every perf number in this repo — the baselines and budgets
> in `tests/test_perf_render.cpp`, the measurements in `docs/ENGINE_AUDIT_2026-07.md` — is a
> Release number, and none of them mean anything in Debug.

### Release (`x64-release`)

Optimized, no debug symbols to speak of. This is the reference configuration: it is what the
audit benchmarks were taken in and what the perf tests are budgeted against. Use it for
performance work and for a final sanity pass before packaging.

### RelWithDebInfo (`x64-relwithdebinfo`)

Optimized **and** symbol-bearing. `Engine/CMakeLists.txt` describes it exactly as intended:

> `RelWithDebInfo = optimized + symbols (debug real scenes at Release speed).`

**This is the configuration to debug real scenes in.** You get a usable frame rate and a
usable call stack at the same time. Reach for it whenever a bug only reproduces at scale, in
a full scene, or under real camera movement — which is most rendering, culling, shadow, and
physics bugs.

> **Gotcha:** the `RELWITHDEBINFO` output directories in `Engine/CMakeLists.txt` are set
> explicitly, and that is deliberate. Without them this configuration falls back to each
> target's own binary directory, which scatters `Engine.dll` away from the executables that
> need to load it.

## Building on Linux

The engine targets **Windows and Linux**. Windows is the primary path; Linux is
kept deliberately separate so it never disturbs the Visual Studio workflow, and
it is the only compiler that ever sees this code that is not MSVC — the Linux CI
job builds *and runs the suite*, which is what makes the cross-toolchain
determinism gate mean anything.

**Prerequisites** beyond the ones above: gcc ≥ 11 or clang ≥ 14, Ninja, and
GLFW's X11 development packages, which vcpkg builds glfw3 against and cannot
install itself:

```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev   # Debian/Ubuntu
sudo dnf install libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel   # Fedora
```

X11 is the default and XWayland runs it fine under a Wayland session; a native
Wayland backend would additionally need `libxkbcommon`/`wayland` and glfw3's
`wayland` feature.

**Export `VCPKG_ROOT`, not `CSE_VCPKG_ROOT`.** The presets are
`hostSystemName == Windows` only, and `scripts/linux-build.sh` takes plain
`VCPKG_ROOT`:

```bash
export VCPKG_ROOT=$HOME/vcpkg
scripts/linux-build.sh              # RelWithDebInfo; also accepts Debug or Release
```

The script configures with Ninja, the vcpkg toolchain and the
`x64-linux-dynamic` triplet — the engine is a shared library, and the default
static triplet's non-PIC archives will not link into `libEngine.so`. Output lands
in `out/build/linux-<BuildType>/build/bin/<BuildType>/`.

### What differs from the Windows build

- **PhysX is Windows-only.** `vcpkg.json` platform-qualifies `physx` to
  `windows`, because the vcpkg omniverse-physx-sdk port has fragile x64-linux
  support and CMake already treats the backend as optional. Linux physics runs on
  the **Jolt** and **Simple** backends.
- **No borderless window.** `EditorTitleBar::Install` is a no-op outside `_WIN32`,
  so Linux keeps the native window-manager title bar *and* the editor's own
  title-bar strip, which is drawn unconditionally. You get both.
- **The editor's AssetCooker validation** runs through a portable subprocess seam
  (`Editor/src/Subprocess.cpp` — `posix_spawn` + `pipe` on Linux).
- **No DLL staging.** `tests/CMakeLists.txt` guards its third-party copy behind
  `if(WIN32)`: those names do not exist next to `libEngine.so`, and
  `cmake -E copy_if_different` fails on a missing source — which once aborted the
  whole build, Editor and Player included, because every test target depends on
  that one staging target. The loader resolves `libEngine.so` through the build
  RPATH instead. Only the `Exported/` assets are copied on both platforms.
- **Discrete-GPU selection is not an export symbol.** On a hybrid-GPU laptop,
  ask for the dGPU at launch — it matters for the same reason it does on Windows:

  ```bash
  __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./Editor   # NVIDIA PRIME
  DRI_PRIME=1 ./Editor                                                    # Mesa / AMD
  ```

Freshly built binaries may still need `LD_LIBRARY_PATH`: an `$ORIGIN` rpath and a
Linux install layout are not built yet, and neither is scheduled — see
[ROADMAP.md](../ROADMAP.md)'s "Not scheduled, on purpose".

## Where the binaries land

All four executables and `Engine.dll` share one output directory per configuration:

```
<cmake-binary-dir>/build/bin/<Config>/
```

So a Release build configured into `out/build/x64-Release` puts everything in
`out/build/x64-Release/build/bin/Release/`. Import libraries go to `build/lib/<Config>/`.

`Editor`, `PlayerDebug`, `PlayerShipping`, and `AssetCooker` all set the same
`RUNTIME_OUTPUT_DIRECTORY_<CONFIG>` values. That sharing is intentional and load-bearing: it
means all of them see the same `Engine.dll`, the same third-party DLLs that vcpkg deploys
beside them, and — most importantly — the same `Exported/` asset directory. A scene you save
in the editor is immediately runnable in the player because they are literally reading the
same file.

## Running the editor

```bash
cd out/build/x64-Release/build/bin/Release
Editor.exe
```

**Run from that directory.** The engine loads assets by relative path — for example
`Editor/src/EditorApplication.cpp` builds its shader from `Exported/Shaders/vertex.glsl` and
`Exported/Shaders/frag.glsl`. Launching with a different working directory means those paths
do not resolve.

At boot the editor reads `Exported/project.json` (a `MyCoreEngine::ProjectSettings`) and opens
`startupScene`, falling back to `Exported/scene.json` when the setting is empty. If that file
cannot be loaded it creates a default scene instead. Either way the outcome is reported in the
console and shown under **Settings → Editor**, e.g. `Loaded startup scene: Exported/scene.json`.

> **Important:** the editor's boot content comes from the scene file, never from code. The
> comment at `Editor/src/EditorApplication.cpp` records why: it used to build a hardcoded
> demo scene at startup, which made the editor lie about what a scene contained. Your saved
> file was never what you saw at launch, so authored components (physics especially) looked
> like they "didn't save" — they had saved fine, the hardcoded scene just replaced them
> before you ever saw them.

## Running the player

```bash
cd out/build/x64-Release/build/bin/Release
PlayerDebug.exe                       # console build — logs visible
PlayerDebug.exe Exported/scene.json   # explicit scene
Player.exe                            # shipping build — no console
```

Scene selection in `Player/src/PlayerMain.cpp` is: **command line beats project settings beats
default.** With no argument the player loads `Exported/project.json` and uses `startupScene`,
whose default value is `Exported/scene.json` (`Engine/src/core/ProjectSettings.h`):

```c++
struct ENGINE_API ProjectSettings {
    std::string startupScene = "Exported/scene.json";
    float masterVolume = 1.0f; // 0..1; the editor writes it, the player boots at it

    static const char* DefaultPath() { return "Exported/project.json"; }

    // Missing file is not an error: defaults stand. Malformed JSON logs
    // and returns false, keeping defaults.
    bool Load(const std::string& path = DefaultPath());
    bool Save(const std::string& path = DefaultPath()) const;
};
```

You set `startupScene` from the editor: **File → "Set Current Scene as Player Startup"** (the
File menu on the editor's title bar).

Unlike the editor, the player is always "playing" — ticks run from frame one. It calls
`setRenderFromSceneCamera(true)` so it renders through the scene's camera entity, exactly like
the editor's Game view.

> **Gotcha — a scene with no camera.** If the loaded scene contains no enabled
> `CameraComponent`, the camera director has nothing to drive and the engine falls back to a
> free-fly debug camera. That looks exactly like "the game ignored my camera", so the player
> now reports it explicitly rather than leaving you to guess; free-fly stays enabled purely so
> the level is still inspectable. Fix it by adding a Camera component to an entity in the
> editor and saving the scene. When a camera *is* found, the player calls
> `setInternalCameraInput(false)` — a shipped game must not hand the player a debug fly
> camera, so gameplay becomes the only thing that can move the view.

`ESC` or closing the window exits.

## How assets get next to the executables

Authored assets live in the source tree at `Editor/src/Exported/`: a seed `scene.json` at the
root, plus static asset subdirectories — today `Env/` (HDRIs), `Fonts/`, `Icon/`, `Layouts/`,
`Model/`, `Scripts/` (Lua), `Shaders/` and `UI/` (`.cxml` / `.cstyle` documents). They are
copied to the runtime `Exported/` directory beside the
executables by a single custom target, `runtime_assets`, defined in `Editor/CMakeLists.txt`:

```cmake
add_custom_target(runtime_assets
  COMMAND ${CMAKE_COMMAND}
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/src/Exported
    -DDST=$<TARGET_FILE_DIR:Editor>/Exported
    -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_assets.cmake
  COMMENT "Staging runtime assets")
```

`Editor`, `PlayerDebug`, `PlayerShipping`, and `AssetCooker` all `add_dependencies(... 
runtime_assets)`, so any of them being built stages the assets — but there is only ever **one
writer**.

The staging script (`cmake/stage_runtime_assets.cmake`) treats two classes of content
differently:

| Content | Source | Behaviour |
|---|---|---|
| Every subdirectory (today `Env/`, `Fonts/`, `Icon/`, `Layouts/`, `Model/`, `Scripts/`, `Shaders/`, `UI/`) | owned by the source tree | **overwritten every build**, so shader, model, script, font and UI edits show up |
| `*.json` at the `Exported/` root | authored by the editor at runtime | **seeded only if missing** |

```cmake
file(GLOB children RELATIVE "${SRC}" "${SRC}/*")
foreach(child ${children})
    if(IS_DIRECTORY "${SRC}/${child}")
        file(COPY "${SRC}/${child}" DESTINATION "${DST}")
    endif()
endforeach()

file(GLOB seedFiles "${SRC}/*.json")
foreach(f ${seedFiles})
    get_filename_component(name "${f}" NAME)
    if(NOT EXISTS "${DST}/${name}")
        file(COPY "${f}" DESTINATION "${DST}")
    endif()
endforeach()
```

### The rule, and why it exists

**A build never clobbers a saved scene.** `Exported/scene.json` is seeded on a clean output
directory and then left alone forever. Once you save a scene from the editor, rebuilding will
not revert it.

`Exported/project.json` is not seeded at all — the staging script only copies `*.json` files
that exist in the source asset tree, and that one does not. It appears the first time the
editor writes it (File → Set Current Scene as Player Startup, or the Audio tab's master-volume
slider), and the same seed-only-if-missing rule protects it from then on.

> **Important — never add another copy step into `bin/Exported`.** Both failure modes here
> were reproduced in practice and are recorded in `Editor/CMakeLists.txt` and
> `cmake/stage_runtime_assets.cmake`:
>
> - A blind `copy_directory` silently reverted editor-saved scenes to the checked-in copy on
>   **every build**, which also meant the packaged game shipped a stale scene.
> - Concurrent copies from several targets into the same directory **race under Ninja**,
>   producing intermittent sharing violations (the same lesson as the tests'
>   `test_runtime_deps` staging target).
>
> One target, one writer, and authored files seeded only-if-missing is what keeps both from
> coming back. If you need new static assets staged, put them in a subdirectory of
> `Editor/src/Exported/` — the script picks up *any* subdirectory, so no script edit is
> needed — and do not add a second copy command. (The list used to be hardcoded; `Scripts/`
> and then `Env/` each shipped a feature whose assets never reached the runtime directory.)

The corollary: if you actually *want* the checked-in seed scene back, delete
`Exported/scene.json` from the runtime directory and build again.

## Tests

Only the `-tests` preset builds them — the three app presets set `ENABLE_TESTS=OFF`, so
`ctest` in one of those directories finds nothing:

```bash
ctest --preset x64-relwithdebinfo-tests --output-on-failure
```

```bash
ctest --preset x64-relwithdebinfo-tests -LE "perf|gl"
```

The second line is **what CI gates on**, and the exclusions are not arbitrary. `gl` marks the
tests that create an OpenGL context, which a bare CI runner does not have (a separate job runs
them under Mesa's llvmpipe); `perf` marks a timing budget, which means nothing on a shared
vCPU. Run both locally — the `gl` suites cover the render passes, the post chain, IBL and the
UI pass, which is to say the places where failures are silent.

Test executables live in the `tests/` subdirectory of the binary tree, with their runtime
dependencies staged by the single `test_runtime_deps` target (`tests/CMakeLists.txt`) —
`Engine.dll`, its third-party DLLs, and a copy of `Exported/`. One target rather than per-test
copies, because parallel copies of the same file raced under Ninja.

`test_perf_render` is the render performance harness. It carries **both** labels (`perf;gl`),
is `RUN_SERIAL TRUE` (a timing test must not share the machine), and has a 300-second
timeout.

> **Gotcha — hybrid-GPU laptops.** New GL contexts are routed to the power-saving integrated
> GPU by default. Any executable doing real GL work must export `NvOptimusEnablement` and
> `AmdPowerXpressRequestHighPerformance` **from the executable itself, not from a DLL**.
> `Editor/src/EditorMain.cpp`, `Player/src/PlayerMain.cpp`, and `tests/test_perf_render.cpp`
> all do. The comment in the perf test states the stakes: without them the whole benchmark
> silently measures the Intel iGPU, roughly 5–10x slower. If you add a new GL-heavy
> executable or test, copy those exports into it.

If a perf budget fails on a machine slower than the reference (i5-11400H + RTX 3050 Laptop at
1920x1080), set the `CSE_PERF_BUDGET_SCALE` environment variable to loosen the budgets — e.g.
`2.0`. If a failure is an intentional cost from a new feature, re-measure and update both the
budget constants and the baseline comment block in `tests/test_perf_render.cpp`.

## Producing a shippable build

**The editor produces the player**: *File > Build Settings > Build*, in the same
configuration you authored in. That is the only entry point, and it is the one
that refuses a build whose scene list is empty, whose startup scene fails
validation or sits outside the asset root, whose output directory is inside the
build tree, or whose configuration is not the one this tree was generated for
([ADR-008](../adr/ADR-008-editor-produces-the-player.md)).

There is no `cpack` and no `package` target. Both existed once and both asked
none of those questions: run `cpack` on a tree the editor had never saved in and
it warned, shipped the source-tree defaults instead of your scenes, and exited 0.
The install rules stayed; the door that skipped the questions closed. The whole
story is on the [scenes and shipping page](scenes-and-shipping.md#producing-a-bundle-the-editor-and-nothing-else).

A headless release job is one line, because it is what the Build action runs:

```bash
cmake --install out/build/x64-release --config Release --prefix dist/
```

The bundle is `Player.exe`, `Engine.dll`, the third-party DLLs, and `Exported/`
with your editor-authored scenes layered over the source-tree defaults.
`.import` sidecars are excluded — they are editor-only metadata, like Unity's
`.meta` files, and the player never reads them.

> **Gotcha — install the configuration you authored in.** The authored-content
> step resolves its source from the configuration being installed. That path was
> once hardcoded to `.../bin/Release/Exported`, so installing any other
> configuration found nothing and silently shipped the source-tree defaults
> instead of your saved scene. It warns loudly now:
>
> ```
> No editor-authored Exported/ for configuration 'RelWithDebInfo' ...
> The package ships the source-tree defaults, NOT your saved scene.
> Run the editor in this configuration and save first.
> ```
>
> If you see that, the bundle is not the game you authored.

## A first-run checklist

1. Configure and build the `x64-release` preset — `cmake --preset x64-release`, then
   `cmake --build --preset x64-release` (use `x64-relwithdebinfo` if you plan to debug).
   Preset names are lower-case, and CMake matches them case-sensitively.
2. `cd` into `<binary-dir>/build/bin/<Config>/`.
3. Run `Editor.exe`. Confirm the boot status line under **Settings → Editor** says it loaded
   `Exported/scene.json`.
4. Author something and save the scene with **File → Save Scene** (`Ctrl+S`; the editor writes
   to `Exported/scene.json` by default).
5. Set it as the startup scene: **File → Set Current Scene as Player Startup**.
6. Run `PlayerDebug.exe` from the same directory. It should load your scene and render through
   its camera; the console will print `PLAYER: rendering from scene camera.`
7. Rebuild. Re-run the player. Your scene is still there — that is the seed-only-if-missing
   rule doing its job.
