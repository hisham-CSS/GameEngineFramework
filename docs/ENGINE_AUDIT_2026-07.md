# Engine Audit & Shipping Roadmap — July 2026

Audit of the GameEngineFramework codebase, graded against the development/target hardware
(ASUS laptop: i5-11400H, RTX 3050 Laptop 4GB, 8GB RAM, NVMe SSD, Windows 11 Home).

---

## 1. Executive summary

The engine is a **solid rendering-tech foundation with no "game" layer yet**. The renderer is
genuinely mature — modular render passes, frustum culling, material-sorted batching, GPU
instancing, and a sophisticated stabilized CSM implementation that is also the only well-tested
subsystem. But the project cannot yet author, save, or ship a game: there is no scene
serialization, no standalone player, no packaging, no audio/physics/animation/scripting, and the
editor is a renderer-tuning control panel rather than an authoring tool.

**Measured on this laptop (Release, default 401-entity / 7,584-instance demo scene, 1280×720):**

| Metric | Result |
|---|---|
| FPS on Intel UHD iGPU (current default!) | **12.6 FPS** (79.4 ms) |
| FPS forced onto RTX 3050 | **58.3 FPS** (17.1 ms) |
| RTX 3050 utilization at 58 FPS | ~55% (GPU has headroom; CPU submission is the limiter) |
| Editor CPU usage | ~69% of one core (single-threaded) |
| Editor RAM | 214–570 MB working set |
| Editor VRAM (dGPU) | ~300 MB (incl. 67 MB CSM @ 4×2048, ~25 MB HDR target) |
| Clean engine+editor build (Ninja, Release) | **26 s** |
| Incremental build (1 .cpp touched) | **5 s** |
| One-time dependency rebuild (vcpkg, 21 pkgs) | ~6 min (re-triggered by MSVC updates) |

**The single most important finding:** the engine does not export `NvOptimusEnablement`, so by
default Windows runs it on the Intel iGPU — a 4.6× slowdown. During this audit a per-app
override was set in `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` for the Release
`Editor.exe` so it uses the RTX 3050; the permanent fix is a 3-line export in the executable
(see §4, P0-1).

### Grades against this hardware

| Area | Grade | Why |
|---|---|---|
| Build/iteration speed | **A** | 5 s incremental, 26 s clean. Great dev loop on this machine. |
| Rendering architecture | **B+** | Clean pass system, culling, instancing, excellent CSM. Single directional light only. |
| GPU fit (RTX 3050 4GB) | **B** | 58 FPS @ 720p with headroom; no texture compression/AA yet; 4GB VRAM is comfortable at indie scope. |
| CPU efficiency | **C** | Per-frame `glGetUniformLocation` string lookups everywhere, per-frame sort of all items, fully single-threaded. |
| Default GPU selection | **F** | Runs on the iGPU out of the box (12.6 FPS). Trivial fix, mandatory before anything else. |
| Robustness | **C** | GL resource leaks (no destructors), silent shader-compile failures, resize aspect bug, broken PCF uniforms. |
| Editor authoring | **D+** | Great renderer tuning panels; no viewport, gizmos, entity CRUD, asset browser, undo, or docking. |
| Shippability | **D** | No scene save/load, no player target, no packaging, no audio/physics/animation/scripting/game UI. |
| Test coverage | **C+** | ~14 GTest cases, all CSM/render-pass focused; nothing else covered; Release test build broken (Debug DLL names). |
| Dev-machine fit (8GB RAM) | **C+** | Engine itself is light; VS + editor + browser saturate 8GB (0.9GB free measured). Workflow/RAM mitigations in §5. |

**Verdict: yes, you can reliably edit and ship a game with this engine on this laptop** — the
iteration loop is already excellent and the GPU has headroom — but only after the P0 fixes, and
a game cannot ship until Phase 1 (persistence + player + packaging) exists.

---

## 2. Benchmark methodology

- Release build (`out/build/x64-Release`, Ninja + MSVC 14.44, default CMake Release flags).
- Default hardcoded demo scene: 20×20 backpack grid + 1 hero = 401 entities → 7,584 submitted
  mesh instances after culling (305 culled), drawn in **79 instanced draw calls**.
- FPS read from the editor's own Information → Rendering Stats panel; GPU utilization via
  `nvidia-smi` and Windows GPU-engine performance counters; RAM via process working set.
- iGPU run = OS default adapter selection. dGPU run = `GpuPreference=2` per-app registry
  override (left in place; remove via *Settings → System → Display → Graphics* or delete the
  `Editor.exe` value under `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`).
- 58.3 FPS sits suspiciously near 60: either driver vsync or CPU-bound at ~17 ms — GPU at 55%
  either way. The engine never calls `glfwSwapInterval`, so present behavior is at the driver's
  mercy (P0-3).

---

## 3. Codebase audit findings

Engine ≈ 3,980 LOC (39 files) + Editor ≈ 544 LOC + tests ≈ 753 LOC. Single-threaded throughout
(the only sync primitive is one uncontended mutex in `AssetManager`).

### 3.1 What is genuinely good
- **Render pass architecture** (`Engine/src/render/`): `IRenderPass` + `RenderPipeline`,
  ShadowCSM → ForwardOpaque → Tonemap. Clean and extensible.
- **CSM** (`ShadowCSMPass.cpp`, 354 lines): stabilized ortho with center texel snapping,
  movement-change detection, round-robin cascade update budget (1/frame), per-cascade Z-slice
  culling. This is the engine's crown jewel and it is well-tested (~14 GTest cases).
- **Draw submission** (`Scene::RenderScene`): frustum cull → sort by {texKey, mesh, depth} →
  GPU instancing for runs ≥ 2. 7,584 instances in 79 draws proves it works.
- **PBR + IBL correctness**: sRGB-correct texture uploads, trilinear mips, ACES tonemap + gamma.
- **Build hygiene**: CMake + vcpkg manifest builds first try; 5 s incremental loop.

### 3.2 Concrete bugs (file:line, fix now)
1. **No `NvOptimusEnablement`/`AmdPowerXpressRequestHighPerformance` exports** → renders on the
   iGPU by default. 12.6 vs 58.3 FPS measured. Add to the Editor (and future Player) executable.
2. **Stale aspect ratio on resize** — `Window.h:39` `getAspectRatio()` returns constructor-time
   dims; `Renderer.cpp:183` builds the projection from it while culling uses live framebuffer
   size (`ForwardOpaquePass.cpp:59`). Image stretches after any resize and disagrees with culling.
3. **AABB generation uses `FLT_MIN`** (`Components.h:308,328`) — smallest *positive* float, not
   lowest. Meshes fully negative on an axis get wrong bounds → misculling. Use
   `std::numeric_limits<float>::lowest()`.
4. **PCF shadows silently disabled** — `frag.glsl:112` reads `uCascadeKernel[ci]` and shadow
   bias uniforms that `ForwardOpaquePass` never uploads → radius 0, 1-tap hard shadows, zero
   shader bias. (Textures are even configured for hardware compare that the shader never uses.)
5. **Shader failures are silent** — `Shader.cpp:134-156` logs and continues; `Shader` has no
   destructor (program leak). HDR/shadow FBO completeness failures are empty branches
   (`Renderer.cpp:81-83`, `ShadowCSMPass.cpp:309`).
6. **GL resource leaks** — no destructors freeing GPU objects in `Mesh` (`Model.h:44`), `Shader`,
   `ShadowCSMPass`, `Renderer` (HDR FBO/tex/RBO, fullscreen quad), `Scene::instanceVBO_`; global
   `Model::sTextureCache_` (`Model.cpp:512`) never cleared, not thread-safe, not context-aware.
   `Mesh` also lacks move semantics while stored by value in `std::vector`.
7. **Unclamped delta time** (`Renderer.cpp:44-48`) — a debugger pause teleports the camera; no
   fixed timestep for future gameplay/physics.
8. **Release test build breaks** — `tests/CMakeLists.txt:50-97` post-build copies hardcode Debug
   DLL names (`assimp-vc143-mtd.dll`, `zlibd1.dll`). Use `$<TARGET_RUNTIME_DLLS:...>` or
   generator expressions instead.
9. **Broken Name editing** — `Name` is `const char*` (`Components.h:14`), Inspector `InputText`
   on it is non-functional (`InspectorPanel.cpp:19`). Must become `std::string` (also required
   for serialization).
10. **Model-load failure is silent** (`Model.cpp:273-276`) → empty model + inverted AABB
    downstream.

### 3.3 Performance issues on the hot path (ranked by measured impact)
1. **Uniform string lookups**: every `Shader::set*` calls `glGetUniformLocation(name)` per call,
   per frame (`Shader.cpp:75-130`); ~19 globals + per-draw model + material scalars + per-cascade
   `snprintf`-built names (`ForwardOpaquePass.cpp:24-40`). Cache locations at link time; move
   per-frame/per-view data into a UBO. This is the top CPU cost and the engine is CPU-bound.
2. **Per-frame rebuild + sort** of the full draw list (`Scene.cpp:64-105`) including re-hashing
   the FNV texture key per mesh per frame (`Scene.cpp:11-19`) — cache the key on `Material`.
3. **Instance buffer orphaning** per batch (`glBufferData(NULL)` + `glMapBuffer`,
   `Scene.cpp:156-163`) and instanced attrib pointers re-specified per batch — bake into VAOs,
   use a persistent/ring buffer.
4. **Redundant rebind** for single-draw items (`Scene.cpp:176-186`) — material bound twice,
   `model` uniform set twice.
5. **Virtual dispatch** on bounding volumes in the cull loop (`Components.h:107-286`); loose
   bounding spheres (radius = full diagonal, ~2× too big → over-culling misses).
6. **No vsync/frame-pacing control** anywhere; dead clear of the default FBO each frame
   (`Renderer.cpp:179` — tonemap overwrites 100% of it).
7. **No texture compression** (RGBA8 everything, forced 4-channel), no anisotropy, mutable
   `glTexImage2D` — the main 4GB-VRAM risk as content grows.
8. **Single-threaded everything** — asset loads hitch the main thread; no job system.

### 3.4 Architecture debt
- `Renderer` is a God object: owns window, input, camera, timing, pipeline, and the main loop
  (`Renderer.h`, 219 lines). `Application` is an empty stub (`Application.cpp:14-16`); there is
  **no game update tick** anywhere for gameplay code to live in.
- `Scene` mixes ECS container with renderer state (`metallic_`, `iblIntensity_`, …
  `Scene.h:120-135`) and is ~90% render-queue builder.
- `EventBus` is dead code: `KeyEvent` published but never subscribed; resize/mouse events wired
  around it via direct GLFW callbacks (`Renderer.cpp:266-288`). Not extensible (hardcoded types,
  singleton).
- Input: 5 hardcoded keys polled, camera baked in (`InputSystem.h:29-52`); no rebinding, mouse
  buttons handled ad-hoc, no gamepad.
- Camera is a Renderer member, not an entity/component; projection rebuilt per-frame with
  hardcoded near/far.
- No parent/child transform hierarchy despite "scene graph" branding; `Transform.dirty` manual
  flag is a footgun.
- Asset management: models only; textures live in a leaking global cache; no GUIDs, no async, no
  hot-reload notify (documented gap in `AssetManager.h:20-22`).
- No logging system (raw `printf`/`cout`), no assertions.
- Editor: no viewport (renders to backbuffer, panels float over it), no docking (flag never set
  despite the vcpkg feature), no gizmos, no entity create/delete UI, no undo, no asset browser,
  no play mode. `imgui.ini` layout persists only on clean exit.
- No install/packaging: no `install()`, no CPack; distribution = hand-zip `build/bin`.

### 3.5 Test coverage
GoogleTest v1.14 via FetchContent, 6 executables, ~14 cases — all CSM math/render-pass wiring
(splits, snapping, texture-unit binding, tonemap target). **Zero coverage**: editor logic,
AssetManager, model import, camera math, material overrides, transform updates, frustum culling,
`Renderer::run`. Tests require `Exported/` assets + DLLs copied beside binaries; that copy step
is Debug-only (bug §3.2-8).

---

## 4. Roadmap

Phased so each phase ends in something usable. Effort: S ≤ 1 day, M = days, L = 1–2+ weeks.

### Phase 0 — Correctness & performance floor (all S, do immediately)
| # | Item | Notes |
|---|---|---|
| P0-1 | Export `NvOptimusEnablement` + `AmdPowerXpressRequestHighPerformance` in Editor exe | Fixes 4.6× iGPU penalty for every user |
| P0-2 | Cache uniform locations in `Shader`; pre-resolve per-cascade names | Top CPU win; unblocks 60+ FPS headroom |
| P0-3 | Add `glfwSwapInterval` control + dt clamp (e.g. 0.1s max) | Deterministic pacing; benchmark mode |
| P0-4 | Fix resize aspect (track framebuffer size in `Window`) | Bug §3.2-2 |
| P0-5 | Fix `FLT_MIN` AABB bug; tighten bounding spheres | Bug §3.2-3 |
| P0-6 | Upload `uCascadeKernel` + shadow bias uniforms | Restores PCF soft shadows |
| P0-7 | Add destructors/move semantics for GL resources (`Mesh`, `Shader`, passes, `Renderer`) | Leak hygiene before systems multiply |
| P0-8 | `Name` → `std::string` | Prereq for serialization + fixes Inspector |
| P0-9 | Add `x64-Release` to `CMakeSettings.json`; fix test DLL copy with `$<TARGET_RUNTIME_DLLS>` | Release becomes a first-class config |
| P0-10 | Loud failure paths: shader compile, FBO completeness, model load | Turn silent wrongness into errors |

### Phase 1 — Make it a game engine (persistence, player, loop) — the shipping gate
| # | Item | Effort | Notes |
|---|---|---|---|
| P1-1 | **Scene serialization** (JSON via nlohmann or cereal): Transform, ModelComponent (by asset path), MaterialOverrides, Name, lights, render settings | L | **Done 2026-07-10** — `SceneSerializer` (JSON v1), editor Save/Load panel, 4 round-trip tests |
| P1-2 | **Game update tick**: fixed-timestep accumulator + variable render, separate from editor | M | **Done 2026-07-10** — `FixedTimestep` + `SetUpdate`/`SetFixedUpdate` hooks, time scale/pause, editor Time panel, 5 unit tests |
| P1-3 | **Standalone Player target**: loads a serialized scene, no ImGui/editor deps | M | **Done 2026-07-10** — `Player.exe [scene.json]`, shares bin dir + Exported/ with Editor |
| P1-4 | **Packaging**: CMake `install()` + CPack (or script) bundling exe + DLLs + assets | S–M | **Done 2026-07-10** — `cpack -G ZIP` → `CatSplatGame-0.1.0-win64.zip` (vcpkg applocal-on-install handles the DLL closure) |
| P1-5 | Decompose the `Renderer` God object + reimplement CSM shadows | M–L | **Done 2026-07-10.** Decomposition: `Application` owns window/input/camera/timing/loop; `Renderer` is render-only (`Setup`/`RenderFrame`/`OnFramebufferResize`). CSM rebuild fixed the user-reported pop/shimmer: (a) cascade extents now fit the slice's bounding *sphere* (orientation-independent radius → constant texel size → snapping actually stabilizes; was a box fit that resized the texel grid on every camera turn); (b) all cascades update atomically by default (round-robin budget left stale cascade matrices that no longer covered the current slice → shadows popped out at boundaries; still available via `setCSMCascadeBudget`); (c) casters cull against the light frustum only + `GL_DEPTH_CLAMP` pancaking (camera-Z-slice caster culling dropped objects that still cast into the slice). New regression tests: extents stable under camera rotation, light-frustum-only caster culling. Follow-up (same day): atomic full updates made camera movement expensive, so cascades now use a **movement-proportional cadence** — each rendered cascade bakes a movement margin (15% of radius, min 1 m) into its extent and only re-renders when the slice center drifts past it. Near cascades update often (cheap), the far cascade ~2×/s at full fly speed (was 60×/s), with coverage guaranteed inside the margin so nothing pops. Default max shadow distance also corrected 1000 → 200 m (the old default made the far cascade swallow every caster and spread its texels absurdly thin). Verified 58 FPS vsync-locked during sustained movement. |
| P1-6 | Logging + assertions (spdlog or minimal homegrown) | S | Prereq for debugging everything above |
| P1-7 | Input action/axis mapping + gamepad (GLFW gamepad API); retire or wire the EventBus | M | **Done 2026-07-10** — `InputMap` (actions/axes, key+mouse+gamepad, deadzone, edge queries) replaces the hardcoded `InputSystem`; gamepad implemented per GLFW gamepad API + unit-tested via fakes, not yet verified on physical hardware. EventBus still dead — decide with P1-6. |

### Phase 2 — Editor becomes an authoring tool
| # | Item | Effort |
|---|---|---|
| P2-1 | Render scene to texture + viewport panel; enable ImGui docking (flag + DockSpace) | **Done 2026-07-11** — `RenderTarget` + `RenderFrame(targetFBO)` with auto HDR resize; dockable Viewport panel; camera controls gated to viewport hover |
| P2-2 | Entity create/delete/rename; component add/remove in Inspector | **Done 2026-07-11** — hierarchy Create button + right-click Delete; Inspector: rename, Casts Shadows toggle, model assign/replace/remove (AABB auto-regen), add Name/Transform. Also fixed gizmo-drag moving docked windows (`ConfigWindowsMoveFromTitleBarOnly` + InvisibleButton viewport input, Unity-style tab behavior) |
| P2-3 | ImGuizmo translate/rotate/scale + mouse picking (ID buffer or ray-AABB) | **Done 2026-07-11** — ImGuizmo (vcpkg) with toolbar + LOCAL-mode manipulation writing back to Transform; ray-vs-world-AABB click picking (miss = deselect). Caveat: decompose/recompose rotation order can drift on compound rotations — revisit with undo (P2-7) |
| P2-4 | Save/Load/New scene UI on top of P1-1; startup scene in project settings file | S |
| P2-5 | Asset browser (filesystem view of project assets, drag into scene) | M |
| P2-6 | Play-in-editor: snapshot registry → tick game loop → restore on stop | M–L |
| P2-7 | Undo/redo command stack for transform/component edits | **Done 2026-07-11** — snapshot-based `UndoHistory` (before/after per entity; create/delete = missing side; entt `create(hint)` resurrects the same handle). Gizmo drags + Inspector drag/text widgets coalesce into one entry (`IsItemActivated`/`IsItemDeactivatedAfterEdit`); covers rename, transform, shadows toggle, model assign/remove, material overrides, entity create/delete (full component restore, model by asset path, deep-copied materials). Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (off while typing); "Edit" panel with click-to-jump history; 100-entry cap; 12 headless gtests. Shared-material rows are now read-only (were silently editable, mutating every user of the model). Caveats: hotkeys are ignored mid-drag (rewinding history during a manipulation corrupts it); undoing an edit on a FixedUpdate-animated entity (the spinning Hero) also rewinds the animated fields captured in the whole-entity snapshot — proper fix is P2-6's sim/authoring separation; Load Scene clears history (stale entt handles) |
| P2-8 | Parent/child transform hierarchy + hierarchy panel tree view | M–L |

### Phase 3 — Runtime feature completeness (drive by the actual game's needs)
| # | Item | Effort | Notes |
|---|---|---|---|
| P3-1 | Transparency/blend pass (sorted back-to-front) | M | Pass system makes this natural |
| P3-2 | Anti-aliasing: MSAA on HDR target or FXAA post pass | S–M | Biggest visual win per effort |
| P3-3 | Skybox/environment pass + IBL generation from HDRi | M | IBL plumbing already exists |
| P3-4 | Multiple lights (point/spot; UBO array; forward+ only if needed) | M–L | Shader currently hardcodes one directional |
| P3-5 | Audio system (miniaudio or OpenAL-soft via vcpkg) | M | Entirely absent today |
| P3-6 | Physics (Jolt via vcpkg) + collider components, on the fixed tick | L | |
| P3-7 | Skeletal animation (assimp import, bone palette, skinning shader) | L | Vertex format has no bone data yet |
| P3-8 | In-game UI/text (start: ImGui runtime mode; later SDF text) | M | |
| P3-9 | Scripting (Lua/sol2) or C++ gameplay-module approach | L | Decide once P1-2 exists |
| P3-10 | Particles (CPU-instanced first) | M | Instancing path already exists |

### Phase 4 — Performance & scale (as content grows)
| # | Item | Notes |
|---|---|---|
| P4-1 | Per-frame/per-view UBOs (camera, lights, CSM) | Follow-on to P0-2 |
| P4-2 | Texture compression (BC7/BC5 offline cook or GPU compress on import) + anisotropy | Protects 4GB VRAM |
| P4-3 | Job system (thread pool) + async asset loading | 6c/12t currently idle |
| P4-4 | Persistent-mapped instance ring buffer; VAO-baked instance attribs | **Largely done 2026-07-11**: instanced runs are discovered once per frame, all instance matrices upload in a single `glBufferData`, and draws use per-run attrib offsets (color + shadow paths). The old per-run orphan+map/unmap cycle was a driver sync per run — measured as the top frame cost at high instance counts (wide view: 71→36 ms combined with view-range shadow invalidation). Depth prepass was built (toggle, default OFF): early-Z already handles this content, so it cost more than it saved (85 vs 71 ms). Dynamic casters now invalidate only cascades whose **view-depth slice** their shadow footprint overlaps — spinning objects get live shadows at rest (60 FPS) without re-rendering far cascades. |
| P4-5 | Draw-list caching (rebuild only on change), material key caching | |
| P4-6 | LOD system; consider occlusion culling only if scenes demand it | **LOD done 2026-07-11** (meshoptimizer: per-mesh LOD1 ~25% / LOD2 ~8% index buffers, distance-based selection, LOD-aware batching, cascade-based shadow LOD, editor toggle/scale/histogram). Finding: ground-level wide shots are **fragment-bound** (LOD saves ~1–3 ms there); LOD's big wins are bird's-eye zoom-outs and shadow-caster geometry. Next fill-rate levers: depth prepass, resolution scale, PCF cost tuning. |
| P4-7 | Asset database with GUIDs + cooked binary format | Enables real content pipeline |
| P4-8 | **Automated performance tests** (user-requested 2026-07-11): headless benchmark scenes rendered N frames with frame-time budget assertions, runnable via ctest (e.g. `perf` label) so regressions surface without manual editor A/B sessions. Should cover: at-rest, sustained camera movement, wide-view instance counts, dynamic-caster shadows. | Manual A/B measurement was needed for every perf change in July 2026 — automate it |
| P4-9 | Parallel cull/submission (job system application): wide views with heavy geometry still drop to 20–30 FPS while moving (user-observed after the 2026-07-11 rework) — the ~23k-item build/sort/submit is single-threaded | Deferred by user until job system lands |

---

## 5. Working on this laptop — practical guidance

- **RAM is the pinch (8GB, 0.9GB free measured while developing).** VS 2022 + editor + browser
  saturate it. Mitigations: prefer VS Code or fewer VS extensions for day-to-day, close browser
  tabs while profiling, and if the ASUS has a free SODIMM slot, a +8GB stick is the single best
  hardware upgrade for this workflow.
- **Move the repo out of OneDrive** (or exclude `out/` from sync). Build outputs churn thousands
  of files; OneDrive sync adds I/O overhead and can lock files mid-build. `git` is the backup.
- **Performance budget to design for**: 60 FPS @ 1080p on the RTX 3050 (16.6 ms). Today's demo
  runs 17.1 ms @ 720p CPU-bound, with the GPU at ~55% — after P0-2/P4-1 the CPU limit lifts and
  1080p60 is realistic for indie-scope scenes. Keep 4×2048 CSM as the "High" preset; default to
  3×1536 (~28 MB) on 4GB-class GPUs.
- **Dependency rebuilds**: MSVC updates invalidate the vcpkg binary cache (~6 min rebuild,
  one-time). Expected; don't chase it.
- ~~A per-app registry override currently forces `Editor.exe` onto the RTX 3050~~ **Done
  2026-07-09:** P0-1 (`NvOptimusEnablement` export), P0-6 (PCF kernel/bias uniforms), the
  compare-mode UB fix, and the Release test-DLL copy fix have landed; the registry override was
  removed. The demo scene also gained a ground plane so shadows have a receiver. Note: the
  driver vsync-locks the editor near 60 FPS; uncapped numbers need `glfwSwapInterval` control
  (P0-3, still open).
- **Zoom-out perf reality:** the demo model is ~68k triangles; with all 401 instances in frustum
  that is ~27M triangles/frame with no LOD. Culling can't help (nothing is outside the view) and
  draw calls stay at ~80, so wide shots are vertex-bound — on the iGPU this measured 129 ms.
  Mitigations today: lower the Max Shadow Distance slider (fewer shadow casters per cascade
  update), smaller demo grid; the real fix is the LOD system (P4-6).

---

*Benchmarked and audited 2026-07-09 (Release build, MSVC 14.44, driver 572.16).*
