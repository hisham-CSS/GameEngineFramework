# Cat Splat Engine

Verified: 2026-08-17 @ 9f518c2

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

The engine's showcase is a **deterministic, rollback-netcode fighting game** —
SF6-like, data-driven, cross-platform — which doubles as the case study for a
combo-termination proof, and that target now drives the architecture. It has
already changed the engine's shape: a gameplay kernel
(`Games/UntitledFighter/Kernel/`) that is a fixed-size POD of integers advanced by
a pure function and snapshotted by `memcpy`, which **links nothing** — not Jolt,
not EnTT, not Lua — enforced at configure time rather than by convention, because
bit-identical cross-platform arithmetic is a property of what the kernel *cannot
reach*; a rollback session seam (`Net/`) over a vendored GekkoNet; and character
behaviour as data, in a schema the published combo prover reads **unmodified**, so
the thing analysed is the thing shipped with no export step.

**What is built, what is in flight and what is next is
[`docs/ROADMAP.md`](docs/ROADMAP.md), and only there.** This file deliberately
keeps no second copy — two lists of status disagree within a week, and the one
nobody edits is the one people read.

## Documentation

Six living documents, a frozen decision record, a manual, and an archive.

| | |
|---|---|
| **[Roadmap](docs/ROADMAP.md)** | What is done, in flight, next, and deliberately not scheduled. The only place status lives |
| **[North Star](docs/NORTHSTAR.md)** | What the engine is for, and the test that decides whether each of its four properties is done |
| **[Architecture](docs/ARCHITECTURE.md)** | Why it is shaped this way: decisions D1–D9, and a table of rejected ideas with the condition each comes back under |
| **[Determinism](docs/DETERMINISM.md)** | Every rule the simulation, the build and the authored data must obey — and, for each, what actually stops you breaking it |
| **[Maintenance](docs/MAINTENANCE.md)** | Working on the engine itself: the change loop, the documentation rule, and the invariants that keep biting |
| **[Style](docs/STYLE.md)** | How code is written here — comments, tests, diagnostics and API shape |
| **[Manual](docs/manual/index.md)** | How to *use* each subsystem: editor, components, physics, scripting, rendering, UI, assets, shipping a build |
| **[Decision records](docs/adr/README.md)** | Eleven ADRs, frozen the day each was accepted. ADR-001 leads with a half-negative result and records two instructions the original plan gave that would have fabricated an infinite combo |
| **[Archive](docs/archive/README.md)** | Superseded documents, kept verbatim. Nothing there is current |

New here? Read [Getting Started](docs/manual/getting-started.md), then
[Engine Architecture](docs/manual/architecture.md). Changing the engine rather
than using it? Start with [CLAUDE.md](CLAUDE.md) and the
[Maintenance Guide](docs/MAINTENANCE.md).

## Project Structure

```
GameEngineFramework/
├── Engine/          # Core engine (DLL): core systems, render passes, 2D renderer,
│                   #   in-game UI toolkit, physics + script + audio backends
├── Editor/          # Editor application (ImGui) + Exported/ shaders & sample assets
├── Player/          # Standalone player (loads a scene.json, no editor UI)
├── Cooker/          # Headless AssetCooker (asset validation)
├── Net/             # ISession: the rollback session seam. GekkoNet is PRIVATE to it,
│                   #   so exactly one .cpp includes gekkonet.h
├── Games/
│   └── UntitledFighter/   # THE TITLE. A title may depend on the engine, never the
│       ├── Kernel/        #   reverse -- a configure-time boundary check enforces it.
│       ├── Data/          # Kernel: integer POD state, pure Simulate(), links NOTHING
│       ├── Game/          # Data: character files -> memory. Game: session, input
│       ├── Modes/         #   sources, replay, combo judge. Modes: training mode.
│       ├── Editor/        # Editor: the Combo Prover panel, pushed in through a seam
│       └── Assets/        # Assets: the shipped characters
├── ThirdParty/      # Vendored: GekkoNet as a pinned submodule, and comboprover.hpp
├── docs/            # Six living documents + adr/ + manual/ + archive/
├── tests/           # GoogleTest unit tests
├── cmake/           # Build helpers (runtime-asset staging, etc.)
├── resources/       # App icon (.ico + shared .rc; regenerate via scripts/make_icon.py)
├── scripts/         # Build + gate scripts (linux-build.sh, check_determinism_flags.py,
│                   #   check_docs.py, make_icon.py)
├── tools/           # Small dev tools
├── CMakeLists.txt
├── CLAUDE.md        # How to work in this repository
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

See **[docs/manual/getting-started.md](docs/manual/getting-started.md#building-on-linux)** — in short, install the GLFW X11 dev
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

**The editor produces the player**: *File > Build Settings > Build*, in the same
configuration you authored in. It is the only entry point, and it refuses a build
whose scene list is empty, whose startup scene fails validation, or whose
configuration is not the one this tree was generated for
([ADR-008](docs/adr/ADR-008-editor-produces-the-player.md)). There is no `cpack`
and no `package` target; both existed, both skipped those checks, and `cpack` on a
tree the editor had never saved in shipped the wrong content and exited 0.

A headless release job is one line, because it is what the Build action runs:

```bash
cmake --install out/build/x64-release --config Release --prefix dist/
```

## Contributing

Contributions are welcome — please open an issue or pull request to discuss any changes.

## License

MIT — see [LICENSE.txt](LICENSE.txt).
