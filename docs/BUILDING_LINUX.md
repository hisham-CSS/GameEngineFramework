# Building on Linux

The engine targets **Windows and Linux**. Windows remains the primary path
(Visual Studio driving the committed `CMakePresets.json`); this document covers
the Linux build, which is kept deliberately separate so it never disturbs the VS
workflow.

> **Status:** Phases 0–1 of the Linux port are done — the whole tree *compiles*
> under gcc/clang, with the PhysX backend dropped on Linux (Jolt + Simple
> remain). **Runtime linking/deployment (rpath) is Phase 2 and not finished**,
> so freshly built binaries may need `LD_LIBRARY_PATH` set until then. This
> build has not yet been validated end-to-end on a real Linux machine.

## Prerequisites

- **CMake ≥ 3.21** and **Ninja**
- A **C++17 compiler** — gcc ≥ 11 or clang ≥ 14
- **vcpkg**, with `VCPKG_ROOT` exported to its checkout:
  ```bash
  export VCPKG_ROOT=$HOME/vcpkg
  ```
  (On Windows the presets read **`CSE_VCPKG_ROOT`** instead and set `VCPKG_ROOT`
  from it — see [Configurations](#configurations) below. The Linux script takes
  plain `VCPKG_ROOT`.)
- **GLFW's X11 development packages** (vcpkg builds glfw3 against X11 and cannot
  install these system packages itself):
  ```bash
  # Debian / Ubuntu
  sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
  # Fedora
  sudo dnf install libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
  ```
  (X11 is the default; XWayland runs it fine under a Wayland session. A native
  Wayland backend would additionally need `libxkbcommon`/`wayland` and the
  glfw3 `wayland` feature.)

## Build

```bash
scripts/linux-build.sh              # RelWithDebInfo
scripts/linux-build.sh Debug        # or Release
```

The script configures with Ninja, the vcpkg toolchain, and the
`x64-linux-dynamic` triplet (the engine is a shared library; the default static
triplet's non-PIC archives won't link into `libEngine.so`). Output lands in
`out/build/linux-<BuildType>/build/bin/<BuildType>/`.

## Configurations

The Windows configurations live in the **committed `CMakePresets.json`** at the
repo root, so Visual Studio, VS Code and a bare `cmake --preset` all see the same
list:

| Preset | Build type | Tests |
| --- | --- | --- |
| `x64-debug` | `Debug` | `ENABLE_TESTS=OFF` |
| `x64-release` | `Release` | `ENABLE_TESTS=OFF` |
| `x64-relwithdebinfo` | `RelWithDebInfo` | `ENABLE_TESTS=OFF` |
| `x64-relwithdebinfo-tests` | `RelWithDebInfo` | `ENABLE_TESTS=ON` |

The three app presets keep tests off so the Visual Studio target dropdown stays
readable; build `x64-relwithdebinfo-tests` when you want `ctest`. Each has a
matching build preset, and the tests preset also has a test preset
(`ctest --preset x64-relwithdebinfo-tests`).

They read the vcpkg checkout from **`CSE_VCPKG_ROOT`**, not `VCPKG_ROOT`:

```json
"environment": { "VCPKG_ROOT": "$penv{CSE_VCPKG_ROOT}" }
```

Visual Studio overwrites `VCPKG_ROOT` in its own environment to point at its
bundled copy, which then resolves ports the repo never asked for and fails on
long paths. Reading a project-specific variable and assigning `VCPKG_ROOT` from
it inside the preset makes the toolchain path immune to that. Set
`CSE_VCPKG_ROOT` once, in your user environment.

The presets are `hostSystemName == Windows` only. On Linux use
`scripts/linux-build.sh` above, which takes `VCPKG_ROOT` directly.

> `CMakeSettings.json` is **gone** — it was Visual-Studio-only, gitignored, and
> silently diverged from what everyone else built. Anything that still mentions
> it is out of date.

## What differs from the Windows build

- **PhysX is Windows-only.** `vcpkg.json` platform-qualifies `physx` to
  `windows`, because the vcpkg omniverse-physx-sdk port has fragile x64-linux
  support and CMake already treats the backend as optional. Linux physics runs
  on the **Jolt** and **Simple** backends.
- **No custom title bar.** The editor's borderless Win32 title bar is
  `#if _WIN32`-only; Linux keeps the native window-manager title bar.
- **The editor's AssetCooker validation** runs through a portable subprocess
  seam (`Editor/src/Subprocess.cpp` — `posix_spawn` + `pipe` on Linux).
- **The test harness stages no DLLs on Linux.** `tests/CMakeLists.txt` guards
  its third-party DLL copy behind `if(WIN32)`: those names
  (`assimp-vc143-mt.dll`, `zlib1.dll`, …) do not exist next to `libEngine.so`,
  and `cmake -E copy_if_different` fails on a missing source — which aborted the
  whole build, Editor and Player included, since every test target depends on
  that one staging target. Linux needs no staging: the loader resolves
  `libEngine.so` through the build RPATH. Only the `Exported/` assets are copied
  on both platforms.
- **Discrete-GPU selection is not an export symbol on Linux.** On hybrid-GPU
  laptops, request the dGPU at launch instead:
  ```bash
  # NVIDIA PRIME
  __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./Editor
  # Mesa / AMD
  DRI_PRIME=1 ./Editor
  ```
  This matters for the same reason it does on Windows — the integrated GPU is
  substantially slower.

## Remaining work (Phases 2–5)

Tracked separately; not required to *compile* but needed to *ship* on Linux:

- **Phase 2 — runtime linking:** set `$ORIGIN` rpath project-wide and co-locate
  `libEngine.so` with the executables so binaries launch without
  `LD_LIBRARY_PATH`.
- **Phase 3 — tests:** the Windows-DLL staging no longer breaks the Linux
  configure/build (it is `if(WIN32)`-guarded), so the suite *builds*; what
  remains is running it end-to-end on a real Linux machine, including the
  GL-headless suites.
- **Phase 4 — packaging:** replace the Windows applocal-DLL deploy with a
  Linux install layout.
- **Phase 5 — polish:** hide the editor's redundant custom title-bar strip when
  the borderless install is a no-op (Linux), and add a dGPU launch helper.
