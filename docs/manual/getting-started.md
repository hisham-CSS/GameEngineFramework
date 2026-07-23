# Getting Started

This page takes you from a fresh clone to a running editor and a running game. It covers the
prerequisites, the three build configurations and when to use each, where the binaries and
assets end up, and the asset-staging rule that decides whether a build can overwrite a scene
you saved.

Read this before your first build. The choice of configuration in particular has consequences
that are easy to misdiagnose as engine bugs.

## What gets built

The top-level `CMakeLists.txt` adds four subprojects:

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
   **Linux** (gcc ≥ 11 / clang ≥ 14) — see **[Building on Linux](../BUILDING_LINUX.md)**.
2. **CMake 3.20 or newer** (`cmake_minimum_required(VERSION 3.20)`).
3. **Ninja** — the generator this project is normally built with. Visual Studio 2022 ships it
   with the C++ workload; from a plain terminal, run inside a `vcvars64` environment.
4. **vcpkg**, used in **manifest mode**. There is a `vcpkg.json` at the repo root, so you do
   not install packages by hand — pointing CMake at the vcpkg toolchain file is enough and
   vcpkg restores the manifest during configure.

`vcpkg-configuration.json` pins the default registry to a specific vcpkg baseline commit, so
everyone resolves the same package versions.

### Dependencies (from `vcpkg.json`)

`glfw3`, `glad`, `stb`, `glm`, `assimp`, `entt`, `nlohmann-json`, `meshoptimizer`, `imguizmo`,
`miniaudio` (audio), `joltphysics`, `physx` (Windows-only), `sol2` + `lua[cpp]` (scripting),
and `imgui` with the `docking-experimental`, `glfw-binding`, and `opengl3-binding` features.

**The two physics SDKs are optional.** `Engine/CMakeLists.txt` looks for them with
`find_package(... CONFIG QUIET)` behind the `CSE_ENABLE_JOLT` and `CSE_ENABLE_PHYSX` options
(both `ON` by default), and prints its decision at configure time:

```
-- Physics: Jolt backend ENABLED
-- Physics: PhysX not found - backend disabled
```

A build with neither still works — the dependency-free "Simple" backend is always registered.

> **Important — do not link a physics SDK straight into `Engine`.** Each SDK is linked into
> its own `STATIC` object library by the `cse_add_physics_backend` function in
> `Engine/CMakeLists.txt`, never directly into `Engine`. The reason is recorded in the
> comment there: these imported targets propagate `INTERFACE` compile definitions to every
> consumer source file, and Jolt's include defines `_HAS_EXCEPTIONS=0`. Linking Jolt directly
> into `Engine` rebuilt the *entire* engine without exception support, which turned the
> `std::filesystem` throw that `AssetIndex` relies on (non-codepage filenames) into a
> `0xC0000409` fast-fail and would have silently broken every other `try`/`catch` in the
> engine. `STATIC` rather than `OBJECT` is load-bearing: for a static library CMake records
> `PRIVATE` deps as `$<LINK_ONLY:...>`, so `Engine` inherits the SDK's `.lib` for linking but
> not its compile definitions or include directories.

## Configuring and building

The repo ships a committed **`CMakePresets.json`**, so the same named configurations show
up everywhere the project is opened — Visual Studio 2022, VS Code, CLion, and the command
line all read it. It needs one thing from the environment: **`VCPKG_ROOT`**, pointing at
your vcpkg checkout, so the toolchain resolves without a machine-specific path baked into
the file.

> Visual Studio and the *Developer* command prompts set `VCPKG_ROOT` to VS's bundled vcpkg
> automatically, so inside VS you usually need to do nothing. For a plain shell, set it once:
> `setx VCPKG_ROOT C:\path\to\vcpkg` (then restart the shell). On Linux, export it from your
> profile.

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

**Without presets** (or on a machine where you'd rather not set `VCPKG_ROOT`), pass the
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

### `x64-Debug`

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

### `x64-Release`

Optimized, no debug symbols to speak of. This is the reference configuration: it is what the
audit benchmarks were taken in and what the perf tests are budgeted against. Use it for
performance work and for a final sanity pass before packaging.

### `x64-RelWithDebInfo`

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
console and shown under **Settings → Scene**, e.g. `Loaded startup scene: Exported/scene.json`.

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

    static const char* DefaultPath() { return "Exported/project.json"; }

    // Missing file is not an error: defaults stand. Malformed JSON logs
    // and returns false, keeping defaults.
    bool Load(const std::string& path = DefaultPath());
    bool Save(const std::string& path = DefaultPath()) const;
};
```

You set `startupScene` from the editor: **Settings → Scene → Build Settings → "Set Current
File as Startup Scene"**.

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

Authored assets live in the source tree at `Editor/src/Exported/` (`Model/`, `Shaders/`, and a
seed `scene.json`). They are copied to the runtime `Exported/` directory beside the
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
| `Model/`, `Shaders/` | owned by the source tree | **overwritten every build**, so shader and model edits show up |
| `*.json` at the `Exported/` root | authored by the editor at runtime | **seeded only if missing** |

```cmake
file(COPY "${SRC}/Model" DESTINATION "${DST}")
file(COPY "${SRC}/Shaders" DESTINATION "${DST}")

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
editor writes it (Settings → Scene → Build Settings), and the same seed-only-if-missing rule
protects it from then on.

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
> coming back. If you need new static assets staged, add them to `Editor/src/Exported/` and
> extend the script — do not add a second copy command.

The corollary: if you actually *want* the checked-in seed scene back, delete
`Exported/scene.json` from the runtime directory and build again.

## Tests

```bash
ctest --test-dir out/build/x64-Release
```

Test executables live in the `tests/` subdirectory of the binary tree, with their runtime
dependencies staged by the single `test_runtime_deps` target (`tests/CMakeLists.txt`) —
`Engine.dll`, its third-party DLLs, and a copy of `Exported/`. Again, one target rather than
per-test copies, because parallel copies of the same file raced under Ninja.

`test_perf_render` is the render performance harness. It carries the label `perf`, is marked
`RUN_SERIAL TRUE` (a timing test must not share the machine), and has a 300-second timeout.
Run only it with `ctest -L perf`, or exclude it with `ctest -LE perf`.

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

## Packaging a build

```bash
cd out/build/x64-Release
cpack -G ZIP
```

This produces `CatSplatGame-<version>-win64.zip` containing `Player.exe` (the shipping
player), `Engine.dll`, the third-party DLL closure, and the `Exported/` assets. Setting
`X_VCPKG_APPLOCAL_DEPS_INSTALL ON` before `project()` in the root `CMakeLists.txt` is what
makes vcpkg deploy that DLL closure at install time as well as at build time.
`cmake --install <binary-dir> --prefix <dir>` stages the same layout to a directory.

The package layers the **runtime** `Exported/` (your editor-authored scenes and
`project.json`) on top of the source-tree defaults, and `.import` sidecars are excluded —
they are editor-only metadata, like Unity's `.meta` files, and the player never reads them.

> **Gotcha:** the install step resolves the authored directory using the configuration being
> installed. This path was once hardcoded to `.../bin/Release/Exported`, so installing any
> other configuration — notably `RelWithDebInfo` — found nothing and silently shipped the
> source-tree defaults instead of your saved scene: a packaged game that ignored your
> authoring, with no error to explain it. It now warns loudly instead:
>
> ```
> No editor-authored Exported/ for configuration 'RelWithDebInfo' ...
> The package ships the source-tree defaults, NOT your saved scene.
> Run the editor in this configuration and save first.
> ```
>
> If you see that warning, run the editor in the configuration you are packaging and save your
> scene before running `cpack`.

## A first-run checklist

1. Configure and build `x64-Release` (or `x64-RelWithDebInfo` if you plan to debug).
2. `cd` into `<binary-dir>/build/bin/<Config>/`.
3. Run `Editor.exe`. Confirm the boot status line under **Settings → Scene** says it loaded
   `Exported/scene.json`.
4. Author something and save the scene (the editor writes to `Exported/scene.json` by
   default).
5. Set it as the startup scene: **Settings → Scene → Build Settings**.
6. Run `PlayerDebug.exe` from the same directory. It should load your scene and render through
   its camera; the console will print `PLAYER: rendering from scene camera.`
7. Rebuild. Re-run the player. Your scene is still there — that is the seed-only-if-missing
   rule doing its job.
