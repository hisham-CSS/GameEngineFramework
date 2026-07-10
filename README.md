# GameEngineFramework

## Project Overview

This project is a modular game engine framework built with modern C++17. It features a clean
architecture with a separate Engine (DLL) and Editor, a render-pass-based OpenGL pipeline with
PBR, IBL, and cascaded shadow maps, and a CMake + vcpkg build system.

The renderer is the most mature part of the engine. Scene authoring, persistence, and gameplay
systems are still in development — see [Development Status](#development-status) below and the
full audit + roadmap in [docs/ENGINE_AUDIT_2026-07.md](docs/ENGINE_AUDIT_2026-07.md).

## What Works Today

- **Modern C++17 architecture**: separate Engine DLL and Editor executable.
- **Render-pass pipeline**: `IRenderPass` interface with ShadowCSM → ForwardOpaque → Tonemap
  passes composed by a `RenderPipeline`.
- **Cascaded Shadow Maps**: up to 4 cascades with texel-snap stabilization, per-cascade
  update budgeting, split blending, PCF filtering, and extensive editor tuning controls.
- **PBR shading**: Cook-Torrance GGX with optional metallic/roughness/AO/normal maps, plus
  image-based lighting (irradiance/prefiltered/BRDF LUT inputs).
- **HDR pipeline**: RGBA16F render target with ACES tonemapping and gamma correction.
- **Performance paths**: frustum culling, material/mesh-sorted draw batching, and GPU
  instancing (the demo scene draws ~7,600 mesh instances in ~80 draw calls).
- **ECS scene**: EnTT-based registry with Transform/Model/Material components.
- **Asset loading**: assimp model import with texture caching and by-path deduplication.
- **Editor**: ImGui panels for scene hierarchy, transform/material inspection, scene
  save/load, and deep renderer tuning (lighting, shadows, IBL/HDR, toggles, live stats).
- **Scene serialization**: versioned JSON save/load of entities (name, transform, model
  by asset path, material overrides, flags) plus scene lighting/shading settings.
- **Standalone Player**: `Player.exe [scene.json]` boots the engine and runs a serialized
  scene with no editor dependencies (defaults to `Exported/scene.json`).
- **Unit tests**: GoogleTest suite covering CSM split math, shadow stability, render-pass
  wiring, scene serialization round-trips, and input/event basics.

## Current Limitations (Honest Edition)

- **No packaging/export** — no install/CPack step yet; distribution means zipping the bin dir.
- **Editor is renderer-focused** — no viewport panel, gizmos, entity create/delete UI, undo,
  asset browser, or play mode yet. Docking is not enabled.
- **Single directional light** — no point/spot lights or additional shadow casters.
- **Opaque-only rendering** — no transparency pass, anti-aliasing, skybox, or post-processing
  beyond tonemapping.
- **No physics, animation, audio, scripting, or in-game UI** — planned (see roadmap).
- **Single-threaded** — no job system or async asset loading yet.

## Project Structure

```
GameEngineFramework/
├── Editor/         # Editor application (ImGui) + Exported/ shaders & sample assets
├── Engine/         # Core engine (DLL): core systems + render passes
├── Player/         # Standalone player (loads a scene.json, no editor UI)
├── docs/           # Audit and roadmap documentation
├── tests/          # GoogleTest unit tests
├── CMakeLists.txt
├── vcpkg.json
└── vcpkg-configuration.json
```

## Dependencies

- **GLFW** — window and input
- **GLAD** — OpenGL loading (GL 3.3 core)
- **GLM** — math
- **STB** — image loading
- **ASSIMP** — model import
- **EnTT** — entity-component-system
- **ImGui** — editor UI

## Building the Project

### Prerequisites

1. **CMake** (3.20+)
2. **vcpkg**
3. **C++17 compiler** (MSVC 2022 tested)

### Build Steps

```bash
git clone https://github.com/hisham-CSS/GameEngineFramework
cd GameEngineFramework
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the Editor or Player from their output directory (assets are loaded by relative path):
`build/build/bin/<Config>/Editor.exe` or `build/build/bin/<Config>/Player.exe [scene.json]`.
Scenes saved in the editor (default `Exported/scene.json`) are immediately runnable in the
player — both executables share the same output directory.

Tests: `ctest --test-dir build` (or run the `test_*` executables in `build/tests`).

## Development Status

- ✅ **Rendering pipeline (opaque + CSM + HDR + PBR/IBL)**: Working, actively tuned
- ✅ **Build system (CMake + vcpkg)**: Working
- 🔨 **Editor**: Renderer tuning panels working; scene authoring tools not started
- 🔨 **Testing**: CSM/render passes covered; most other systems untested
- ✅ **Scene serialization (save/load)**: Working (versioned JSON, editor Save/Load panel)
- ✅ **Standalone player**: Working (`Player.exe [scene.json]`)
- 🔲 **Packaging/export (install/CPack)**: Not started
- 🔲 **Physics integration**: Planned (Jolt)
- 🔲 **Animation system**: Planned
- 🔲 **Audio system**: Planned
- 🔲 **Scripting system**: Planned

The phased roadmap (P0 fixes → persistence/player → editor authoring → gameplay features →
performance/scale) lives in [docs/ENGINE_AUDIT_2026-07.md](docs/ENGINE_AUDIT_2026-07.md).

## Contributing

Contributions are welcome! Please open an issue or pull request to discuss any changes.

## License

This project is licensed under the MIT License. See the [LICENSE.txt](LICENSE.txt) file for details.
