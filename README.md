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

Games get their own UI, not the editor's: a retained-mode toolkit with `.cxml` markup,
`.cstyle` stylesheets, flexbox layout, two-way data binding and hot reload, drawn by a
batched 2D renderer with `stb_truetype` text. At 12.5k lines it is the engine's largest
single subsystem.

## Where this is going

The engine's showcase is a **deterministic, rollback-netcode fighting game** — SF6-like,
data-driven, cross-platform — which doubles as the case study for a combo-termination proof.
That target is what now drives the architecture, and it has already changed the shape of
the engine:

- A **gameplay kernel** (`Kernel/`) that is a fixed-size POD of integers, simulated by a pure
  function, snapshotted by `memcpy`. It links **nothing** — not Jolt, not EnTT, not Lua — and
  that is enforced by a configure-time assertion rather than by a convention, because
  bit-identical cross-platform arithmetic is a property of what the kernel *cannot reach*.
- A **rollback session seam** (`Net/`) over a vendored GekkoNet. Under a stress session it
  rolls the kernel back 231 times and re-simulates 1617 ticks, byte-identical to a
  straight run.
- **Character behaviour as data**, in a schema the combo prover reads *unmodified* — so the
  thing analysed is the thing shipped, with no export step.

The plan and the decisions behind it are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
and the ADRs. They are unusually candid: ADR-001 leads with a result that is half negative,
and records two instructions the plan originally gave that would have fabricated an infinite
combo.

**Scale:** 261 first-party C++ files / 74.7k lines · 26 GLSL shaders ·
1,118 tests in 58 executables (61 CTest entries) · 300 commits since October 2024.

## Documentation

| | |
|---|---|
| **[Manual](docs/manual/index.md)** | How the engine works and how to build things with it — architecture, editor, components, physics, scripting, rendering, shipping a build |
| **[Getting Started](docs/manual/getting-started.md)** | Prerequisites, build configurations, running the editor and player |
| **[Building on Linux](docs/BUILDING_LINUX.md)** | The Linux build path (the engine targets Windows **and** Linux) |
| **[API Reference](docs/api-index.md)** | Generated per-class reference. Build it with the `docs` CMake target (requires Doxygen) |
| **[Architecture Decisions](docs/ARCHITECTURE.md)** | The fighting-game direction: D1–D9, the build order, the determinism contract, and a table of rejected ideas with the condition each comes back under |
| **[North Star](docs/NORTHSTAR.md)** | What the engine is today, made testable, and what blocks the target |
| **ADRs** | [001 — does the declarative model fit?](docs/ADR-001-fighting-core.md) (measured, on three real characters) · [002 — the eleven open decisions](docs/ADR-002-open-decisions.md) · [003 — the GekkoNet spike](docs/ADR-003-gekkonet-spike.md) · [004 — Choronos considered](docs/ADR-004-choronos-considered.md) |
| **[Engine Audit & Roadmap](docs/ENGINE_AUDIT_2026-07.md)** | The phased roadmap ledger and its status |
| **[Maintenance Guide](docs/MAINTENANCE.md)** | Working on the engine itself: the change loop, the documentation audit, and the invariants that keep biting |
| **[Style Guide](docs/STYLE.md)** | How code is written here — comments, tests, diagnostics and API shape |

New here? Read [Getting Started](docs/manual/getting-started.md), then
[Engine Architecture](docs/manual/architecture.md). Changing the engine rather
than using it? Start with the [Maintenance Guide](docs/MAINTENANCE.md).

## Feature Matrix

Legend: ✅ working · 🟡 partial · 🔲 planned

| Area | Status | Notes |
|---|:---:|---|
| **Render pipeline** | ✅ | 11 passes over `IRenderPass`/`RenderPipeline`: CSM → forward PBR → skybox → sorted transparent → bloom → tonemap → ink outline → colour grade → vignette → FXAA → UI overlay |
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
| **Platform** | ✅ | Windows (primary) + **Linux** — gcc 13 **builds AND runs the full test suite** on every push, including the cross-toolchain determinism check. PhysX is Windows-only there |
| **Tests** | ✅ | 1,118 GoogleTest cases in 58 executables (61 CTest entries): CSM math, shadow stability, render passes, post-process chain, serialization, physics conformance across all three backends, scripting, audio, IBL/FXAA, input, 25 executables covering the UI toolkit, and the kernel + rollback-session suites. `ctest -LE "perf\|gl"` → 50/50 in ~4 s |
| **CI** | ✅ | GitHub Actions: **four required jobs** — a determinism flag gate that fails the build on any fast-math flag (10 s, runs first), all four Windows configurations + the GPU-free tests, the 11 GL tests under Mesa llvmpipe, and the Linux build. Nothing is advisory |
| **Gameplay kernel** | 🟡 | `Kernel/` — integer-only POD state, pure `Simulate`, `memcpy` snapshot, FNV-1a checksum. Links nothing, enforced at configure time. Hitboxes, hit resolution and cancels work; no blocking, throws or meter spending yet |
| **Rollback netcode** | 🟡 | `Net/` — `ISession` over a vendored GekkoNet (pinned commit, built with our flags). Save/load/re-simulate proven byte-identical under a stress session. No socket has been opened yet |
| **Skeletal animation** | 🔲 | Static meshes only today |
| **In-game / runtime UI** | ✅ | Retained-mode toolkit, separate from ImGui: `.cxml` markup + `.cstyle` stylesheets (selectors, cascade, pseudo-classes), yoga flexbox layout, two-way data binding, hot reload, focus/keyboard/gamepad navigation, scrolling and clipping, and widgets (Button, Label, Image, TextField, Slider, TabView, `repeat=` collections). Authored as a scene component |
| **2D renderer & text** | ✅ | Batched `Renderer2D` (quads/sprites, screen + world camera modes, rounded rects, borders, gradients, 9-slice) with `stb_truetype` glyph-atlas text, word wrap, hyphenation and paragraph fitting |
| **Networking (transport)** | 🔲 | The rollback *session* exists (above); the transport under it does not. GekkoNet is built with `GEKKONET_NO_ASIO`, so nothing has sent a packet |

## Not Yet Built

Honest gaps, roughly in impact order:

- **Skeletal / skinned animation** — the renderer draws static meshes only. The vertex format
  carries no bone IDs or weights, and there is no animation system of any kind.
- **Networked play** — the rollback session layer is in and proven against the kernel, but no
  transport is built and no two machines have ever exchanged a frame. Everything verified so
  far is one process with two local players.
- **Combat systems** — the kernel simulates walking, jumping and stun. Hitboxes, hit
  detection, cancels and character data driving any of it are not built. The character
  *schema* and three transcribed characters exist; nothing reads them into the kernel yet.
- **Shadowed punctual lights** — the 16 point/spot lights are unshadowed and use a bounded uniform array (not a UBO).
- **Binary cooked-asset pipeline** — the AssetCooker only *validates*; models are still Assimp-imported at load time.
- **Scripting breadth** — Lua only, a thin API (transform / input / raycast / time), and no hot
  reload (`IScriptBackend::supportsHotReload()` returns `false` for every backend). The Lua
  backend is a working proof of the `IScriptBackend` seam rather than a finished scripting
  story, and is expected to be revisited.

## Project Structure

```
GameEngineFramework/
├── Kernel/          # The authoritative fighting-game simulation. Integer POD state,
│                   #   pure Simulate(), memcpy snapshot. Links NOTHING, on purpose.
├── Net/             # ISession: the rollback session seam. GekkoNet is PRIVATE to it,
│                   #   so exactly one .cpp includes gekkonet.h
├── Data/            # Character data loading (schema v2 -> memory)
├── ThirdParty/      # Vendored: GekkoNet as a pinned submodule, built with our flags
├── Engine/          # Core engine (DLL): core systems, render passes, 2D renderer,
│                   #   in-game UI toolkit, physics + script + audio backends
├── Editor/          # Editor application (ImGui) + Exported/ shaders & sample assets
├── Player/          # Standalone player (loads a scene.json, no editor UI)
├── Cooker/          # Headless AssetCooker (asset validation)
├── docs/            # Manual (docs/manual/), architecture + ADRs, maintenance + style guides
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
- **miniaudio** — audio backend · **yoga** — flexbox layout for the in-game UI · **pugixml** — `.cxml` parsing

**GoogleTest** is the exception: it is *not* in the vcpkg manifest. `tests/CMakeLists.txt`
fetches googletest v1.14.0 from GitHub via `FetchContent`, so the first configure of a
tests build needs network access.

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
git clone --recursive https://github.com/hisham-CSS/GameEngineFramework
cd GameEngineFramework
cmake --preset x64-relwithdebinfo
cmake --build --preset x64-relwithdebinfo
```

**`--recursive` matters.** `ThirdParty/GekkoNet` is a submodule pinned to a specific commit.
Without it, configure stops with a message telling you to run
`git submodule update --init --recursive` — which is the fix if you already cloned.

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
