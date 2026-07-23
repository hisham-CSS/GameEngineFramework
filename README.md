# Cat Splat Engine

A working C++17 / OpenGL 3.3 **game engine, editor, and runtime** — not just a renderer.
It ships a separate **Engine** (DLL), a dockable ImGui **Editor** with its own borderless
title bar and theme, a standalone **Player**, and a headless **AssetCooker**, on a
CMake + vcpkg build.

Rendering is AAA-lite: a chainable multi-pass pipeline with cascaded shadow maps, PBR +
image-based lighting, a procedural sky/skybox, sorted transparency, bloom, ACES tonemapping,
and a stack of post effects (ink outline, colour grade, vignette, FXAA), plus per-material
cel/toon shading and HDRP-lite quality tiers. On top sits a real game layer: an EnTT ECS,
physics and scripting behind swappable backends, versioned scene serialization, a project
system, and full editor authoring (play-in-editor, undo/redo, asset browser, entity
create/delete).

## Documentation

| | |
|---|---|
| **[Manual](docs/manual/index.md)** | How the engine works and how to build things with it — architecture, editor, components, physics, scripting, rendering, shipping a build |
| **[Getting Started](docs/manual/getting-started.md)** | Prerequisites, build configurations, running the editor and player |
| **[Building on Linux](docs/BUILDING_LINUX.md)** | The Linux build path (the engine targets Windows **and** Linux) |
| **[API Reference](docs/api-index.md)** | Generated per-class reference. Build it with the `docs` CMake target (requires Doxygen) |
| **[Engine Audit & Roadmap](docs/ENGINE_AUDIT_2026-07.md)** | The phased roadmap ledger and its status |

New here? Read [Getting Started](docs/manual/getting-started.md), then
[Engine Architecture](docs/manual/architecture.md).

## Feature Matrix

Legend: ✅ working · 🟡 partial · 🔲 planned

| Area | Status | Notes |
|---|:---:|---|
| **Render pipeline** | ✅ | Multi-pass `IRenderPass`/`RenderPipeline`: CSM → forward PBR → skybox → sorted transparent → bloom → tonemap → ink outline → colour grade → vignette → FXAA |
| **Shadows** | ✅ | Cascaded shadow maps (≤4), texel-snap stabilization, split blending, PCF, per-cascade update budgeting |
| **Lighting** | ✅ | Directional sun (shadowed) + up to 16 point/spot lights (unshadowed); Cook-Torrance GGX PBR |
| **IBL / Sky** | ✅ | Split-sum IBL (irradiance / prefiltered / BRDF LUT) baked from an `.hdr` **or** a procedural sky; drawn skybox |
| **Transparency** | ✅ | glTF-style `Opaque` / `Mask` (cutout) / `Blend`, sorted back-to-front with a depth pre-pass |
| **Post-processing** | ✅ | Chainable LDR ping-pong stack: bloom (HDR), depth-edge ink outline, procedural colour grade, vignette, FXAA |
| **Materials** | ✅ | Per-material PBR + textures, alpha mode, double-sided, per-entity overrides, and **cel/toon shading** with per-material controls |
| **Quality tiers** | ✅ | HDRP-lite `Low` / `Medium` / `High` / `Custom` presets fanned out across LOD, culling, shadows, bloom, AA |
| **ECS / components** | ✅ | EnTT registry: Transform (hierarchy), Model, Material overrides, Camera, Light, RigidBody/Collider, Script, Audio source/listener, Name, Parent, NoShadow |
| **Editor** | ✅ | Dockable ImGui workspace, custom title bar + theme + File/Edit/Window menus + panel visibility, gizmos, click-picking, hierarchy, inspector, asset browser, deep render settings |
| **Play-in-editor** | ✅ | Play/Stop with a scene snapshot + restore; gameplay input focus-gated to the Game view |
| **Undo / redo** | ✅ | Command history with clickable entries |
| **Physics** | ✅ | `IPhysicsBackend` seam — **Jolt**, **PhysX**, or a **Simple** built-in, one backend per world; fixed-tick; collision/trigger events |
| **Scripting** | ✅ | `IScriptBackend` seam — sandboxed **Lua** (sol2), per-entity isolated environments; a Null backend |
| **Audio** | ✅ | `IAudioBackend` seam — **miniaudio** (cross-platform, no link deps) or a Null backend; 2D/3D positional sources, a listener, master volume; authorable + serialized |
| **Assets** | 🟡 | Assimp import + texture caching + by-path dedup; async worker-pool loading; **AssetCooker validates** (no binary cooked format yet) |
| **Serialization** | ✅ | Versioned JSON: entities, components, material overrides, lighting, environment, post-FX, quality tier |
| **Project system** | ✅ | `project.json` with a startup scene the Player boots |
| **Player** | ✅ | `Player.exe [scene.json]` runs a saved scene with no editor deps |
| **Packaging** | ✅ | `cpack -G ZIP` → self-contained Windows game bundle |
| **Job system** | ✅ | Worker-pool `JobSystem` backing async asset loads |
| **Platform** | 🟡 | Windows (primary) + **Linux** (port phases 0–1: compiles under gcc/clang; PhysX is Windows-only there) |
| **Tests** | ✅ | GoogleTest: CSM math, shadow stability, render passes, serialization, physics, scripting, audio, IBL/FXAA, input |
| **Skeletal animation** | 🔲 | Static meshes only today |
| **In-game / runtime UI** | 🔲 | ImGui is editor-only; no HUD/menu/text path yet |
| **Networking** | 🔲 | Not started |

## Not Yet Built

Honest gaps, roughly in impact order:

- **Skeletal / skinned animation** — the renderer draws static meshes only.
- **In-game / runtime UI + text** — ImGui is editor-only; shipped games have no menus/HUD/text.
- **Networking** — none.
- **Shadowed punctual lights** — the 16 point/spot lights are unshadowed and use a bounded uniform array (not a UBO).
- **Binary cooked-asset pipeline** — the AssetCooker only *validates*; models are still Assimp-imported at load time.
- **Scripting breadth** — Lua only, a thin API (transform / input / raycast / time), no hot-reload.

## Project Structure

```
GameEngineFramework/
├── Engine/          # Core engine (DLL): core systems, render passes, physics + script backends
├── Editor/          # Editor application (ImGui) + Exported/ shaders & sample assets
├── Player/          # Standalone player (loads a scene.json, no editor UI)
├── Cooker/          # Headless AssetCooker (asset validation)
├── docs/            # Manual (docs/manual/), API index, Linux build guide, audit/roadmap
├── tests/           # GoogleTest unit tests
├── cmake/           # Build helpers (runtime-asset staging, etc.)
├── resources/       # App icon (.ico + shared .rc; regenerate via scripts/make_icon.py)
├── scripts/         # Build scripts (linux-build.sh, make_icon.py)
├── tools/           # Small dev tools
├── CMakeLists.txt
├── vcpkg.json
└── vcpkg-configuration.json
```

## Dependencies

Resolved via the vcpkg manifest (`vcpkg.json`):

- **GLFW** — window and input · **GLAD** — GL 3.3 core loader · **GLM** — math · **STB** — image loading
- **Assimp** — model import · **meshoptimizer** — mesh optimization/LODs · **EnTT** — ECS
- **nlohmann-json** — scene serialization · **ImGui** (docking) + **ImGuizmo** — editor UI / gizmos
- **Jolt** and **PhysX** — physics backends (PhysX is Windows-only) · **Lua** + **sol2** — scripting
- **GoogleTest** — tests

The physics and scripting backends are built as optional libraries; each disables gracefully
if its package is absent, so a minimal build still runs (with the Simple/Null backends).

## Building the Project

### Prerequisites

- **CMake** 3.21+, **Ninja**
- **vcpkg** (manifest mode)
- A **C++17 compiler** — MSVC 2022 (Windows) or gcc ≥ 11 / clang ≥ 14 (Linux)
- **`CSE_VCPKG_ROOT`** set to your vcpkg checkout. The committed CMake presets read it — this
  is **per machine** (it is not, and can't be, stored in the repo).

### Windows

Set `CSE_VCPKG_ROOT` once (then restart the shell / Visual Studio so it's picked up):

```bash
setx CSE_VCPKG_ROOT C:\path\to\vcpkg
```

Then build with the committed presets:

```bash
git clone https://github.com/hisham-CSS/GameEngineFramework
cd GameEngineFramework
cmake --preset x64-relwithdebinfo
cmake --build --preset x64-relwithdebinfo
```

Visual Studio users can open the folder directly and pick a preset from the configuration
dropdown — `CMakePresets.json` provides `x64-debug` / `x64-release` / `x64-relwithdebinfo`
(tests off) plus `x64-relwithdebinfo-tests`. Without setting `CSE_VCPKG_ROOT` first, configure
fails with `Could not find toolchain file`. (To skip the variable, pass the toolchain by hand
instead: `cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`.)

### Linux

See **[docs/BUILDING_LINUX.md](docs/BUILDING_LINUX.md)** — in short, install the GLFW X11 dev
packages, export `VCPKG_ROOT`, and run:

```bash
scripts/linux-build.sh
```

### Running

Executables share one output directory, so a scene saved in the Editor is immediately
runnable in the Player. `<build>` is your configure output dir — `out/build/<preset>` for a
preset build (e.g. `out/build/x64-relwithdebinfo`), or `build` for the manual/Linux commands
above:

- `<build>/build/bin/<Config>/Editor.exe`
- `<build>/build/bin/<Config>/Player.exe [scene.json]` (defaults to `Exported/scene.json`)
- `<build>/build/bin/<Config>/AssetCooker.exe validate Exported`

Tests: `ctest --preset x64-relwithdebinfo-tests` (or `ctest --test-dir <build>`, or run the
`test_*` executables directly).

### Shipping a build

```bash
cd build && cpack -G ZIP
```

Produces `CatSplatGame-<version>-win64.zip` containing `Player.exe`, `Engine.dll`, the
third-party DLLs, and the `Exported/` assets + startup scene — a self-contained game bundle.
(`cmake --install build --prefix <dir>` stages the same layout.)

## Contributing

Contributions are welcome — please open an issue or pull request to discuss any changes.

## License

MIT — see [LICENSE.txt](LICENSE.txt).
