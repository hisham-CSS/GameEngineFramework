# Building on Linux

The engine targets **Windows and Linux**. Windows remains the primary path
(Visual Studio + `CMakeSettings.json`); this document covers the Linux build,
which is kept deliberately separate so it never disturbs the VS workflow.

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

## What differs from the Windows build

- **PhysX is Windows-only.** `vcpkg.json` platform-qualifies `physx` to
  `windows`, because the vcpkg omniverse-physx-sdk port has fragile x64-linux
  support and CMake already treats the backend as optional. Linux physics runs
  on the **Jolt** and **Simple** backends.
- **No custom title bar.** The editor's borderless Win32 title bar is
  `#if _WIN32`-only; Linux keeps the native window-manager title bar.
- **The editor's AssetCooker validation** runs through a portable subprocess
  seam (`Editor/src/Subprocess.cpp` — `posix_spawn` + `pipe` on Linux).
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
- **Phase 3 — tests:** the test harness stages Windows `.dll` names; swap to
  rpath so the suite builds and runs.
- **Phase 4 — packaging:** replace the Windows applocal-DLL deploy with a
  Linux install layout.
- **Phase 5 — polish:** hide the editor's redundant custom title-bar strip when
  the borderless install is a no-op (Linux), and add a dGPU launch helper.
