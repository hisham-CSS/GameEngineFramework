# Performance

Cat Splat Engine gives you two ways to answer "why is this frame slow?": a live
counter panel in the editor, and an automated benchmark suite that fails `ctest`
when a frame-time budget regresses. This page explains both, then lists what
profiling this engine has actually shown — several of the "obvious" optimisations
measure as no-ops here, and knowing that saves you days.

---

## Measuring in the editor: the Rendering Stats panel

Open the **Information** window and expand the **Rendering Stats** header
(`EditorApplication::DrawInformationPanel`, `Editor/src/EditorApplication.cpp`).

Every counter comes from `Scene::GetRenderStats()`, which returns the
`RenderStats` struct declared in `Engine/src/core/Scene.h`:

```c++
struct RenderStats {
    unsigned draws = 0;           // non-instanced draw calls
    unsigned instancedDraws = 0;  // instanced draw calls
    unsigned instances = 0;       // total instances drawn via instancing
    unsigned vaoBinds = 0;
    unsigned textureBinds = 0;    // when a new texture bucket is bound
    unsigned culled = 0;          // entities rejected by frustum
    unsigned culledSmall = 0;     // entities rejected by projected-size cull
    unsigned submitted = 0;       // items submitted to GPU (draws + instances)
    unsigned itemsBuilt = 0;      // items_ after culling (meshes that passed)
    unsigned entitiesTotal = 0;   // 'total' (as you already increment)
    unsigned lodInstances[3] = { 0, 0, 0 }; // submitted instances per LOD level
};
```

### What each line means

| Panel line | Field | Meaning |
|---|---|---|
| `dt` | — | Last frame's delta time in ms, plus the derived FPS. |
| `GPU` | — | `glGetString(GL_RENDERER)`. See the hybrid-GPU gotcha below. |
| `3D submit` / `editor UI` / `swap/wait` | — | CPU frame breakdown, see next section. |
| `Cascades` / `res` | — | `renderer().getCSMNumCascades()` and `getCSMBaseResolution()`. |
| `Draws` | `draws` | Individual `glDraw*` calls for runs that could not be instanced (a run of 1). |
| `Instanced draws` | `instancedDraws` | Instanced calls. `Scene::RenderScene` instances any run of **2 or more** items sharing a texture key + mesh + LOD. |
| `Instances` | `instances` | Total instances covered by those instanced calls. |
| `Texture binds` | `textureBinds` | Material rebinds — one per new texture bucket. A large number relative to draw calls means poor material sorting. |
| `VAO binds` | `vaoBinds` | Vertex-array rebinds (incremented on a material bind and on each mesh change within a bucket). |
| `Built items` | `itemsBuilt` | Mesh items that survived culling and entered the sort. |
| `Culled (frustum)` | `culled` | Entities whose AABB failed `isOnFrustum`. Off-screen work you never paid for. |
| `Culled (size)` | `culledSmall` | Entities dropped by the projected-size cull (see [Screen-size culling](#screen-size-culling)). Always `0` unless you enable that cull. |
| `Submitted` | `submitted` | The number that matters most: `draws + instances`, i.e. everything the GPU was asked to draw. |
| `LOD 0/1/2` | `lodInstances[3]` | Submitted instances split by chosen mesh LOD. |
| `GPU draw calls` | — | Computed in the panel as `draws + instancedDraws`. |

**How to read them.** `entitiesTotal` → `culled` + `culledSmall` → `itemsBuilt` →
`submitted` is the funnel. If `submitted` is huge while `draws` is tiny and
`instancedDraws` is small, batching is working and you are simply drawing a lot of
geometry — the fix is fewer/cheaper instances, not fewer draw calls. If
`LOD 0` dominates on a wide shot, LOD is not engaging and that is your first
suspect (see [What we measured](#what-we-measured)).

> **Gotcha:** the panel reads the **Scene view's** stats. The editor deliberately
> draws it *before* the Game view renders, because the Game view uses a second
> `Renderer` and would overwrite `lastStats_`.

### The frame breakdown

`Application` (`Engine/src/core/Application.h`) times three slices of the main
loop and exposes them:

```c++
// --- per-frame CPU breakdown (last frame, milliseconds) ---
float frameSceneRenderMs() const { return sceneRenderMs_; }
float frameUiMs()          const { return uiMs_; }
float frameSwapMs()        const { return swapMs_; }
```

They are filled at the bottom of the loop in `Engine/src/core/Application.cpp`
from timestamps around the 3D render, the UI callback, and `swapBuffers()`.

| Line | Covers | Reading it |
|---|---|---|
| `3D submit` | The scene render call | GL commands are asynchronous, so this is **CPU submission cost only** (build → cull → sort → issue). GPU execution is *not* in here. |
| `editor UI` | The whole editor UI callback | Panels plus the ImGui render. A large value means the editor's own UI owns the frame, not the renderer. |
| `swap/wait` | `SwapBuffers` | Absorbs the GPU wait **and** any vsync block. The panel prints `(vsync ON\|off)` next to it so you know which. |

Diagnostic recipe:

- Big `swap/wait`, vsync **ON** → you are probably just waiting for the refresh.
  Turn VSync off in **Settings → Rendering Toggles** (`(off = uncapped, for
  benchmarking)`) and re-read.
- Big `swap/wait`, vsync **off** → GPU-bound.
- Big `3D submit` → CPU-bound in draw-list construction. Check `Built items` and
  `Submitted`.
- Big `editor UI` → the editor, not your game. Play-mode/Player numbers will look
  different.

---

## The automated perf harness

`tests/test_perf_render.cpp` renders headless benchmark scenes through the real
pipeline (`Renderer::RenderFrame`: CSM → forward PBR → skybox → transparent →
tonemap and the rest of the pass chain, into an offscreen 1920x1080
`RenderTarget`) and asserts frame-time budgets, so regressions surface in CI
instead of in a manual A/B session.

### Running it

The test is registered in `tests/CMakeLists.txt` with the label `perf`,
`RUN_SERIAL TRUE`, and a 300 s timeout:

```
ctest -L perf -V
```

`RUN_SERIAL` matters — a timing test must not share the machine with other tests.
`-V` shows the `[PERF]` lines, which include the `GL_RENDERER` string the run
used and a per-scenario summary:

```
[PERF] wide view 25x25   median   16.90 ms (cpu   6.50)  p95   21.40 ms  (submitted 39300, instances 39221, culled 0, lod 4100/9800/25300)
```

### Methodology

`PerfFixture::measure` does the following:

1. Render `warmup` frames (default **40**) so shaders, LOD selection, CSM cadence
   and the instance-buffer high-water mark all settle.
2. `glFinish()`.
3. Render `frames` timed frames (default **120**). Each frame times
   `UpdateTransforms + RenderFrame + glFinish` — full CPU+GPU cost with no
   pipelining overlap. That is a *stable regression metric*, not an FPS estimate.
4. A second timestamp is taken right after `RenderFrame` returns (before
   `glFinish`); that becomes the **`cpu`** column — the main-thread
   build/cull/sort/submit share.
5. Any `perFrame` game-logic callback (camera motion, spinning entities) runs
   **outside** the timed region.
6. Assertions are on the **median** (robust against OS scheduling blips); **p95**
   is printed so you can eyeball stutter.

The frame delta is pinned to `1.f/60.f` so the CSM update cadence is
deterministic run to run.

### Budgets

Budgets are constants at the top of the file, set at roughly **2x** the measured
baseline medians on the shipping target (i5-11400H + RTX 3050 Laptop, 1920x1080)
— loose enough not to flake, tight enough to catch a real regression:

```c++
constexpr double kBudgetAtRestMs = 12.0;
constexpr double kBudgetCameraMoveMs = 15.0;
constexpr double kBudgetWideViewMs = 35.0;
constexpr double kBudgetWideMovingMs = 50.0;
constexpr double kBudgetWideMoveSpinMs = 65.0;
constexpr double kBudgetDynamicCasterMs = 35.0;
```

The comment block above them records the actual baseline medians (2026-07-18), and
also the *pre-LOD-fix* medians — a regression back toward those means the LOD
system went inert and **must** fail.

### Running on a different machine

Set `CSE_PERF_BUDGET_SCALE` to a multiplier:

```
CSE_PERF_BUDGET_SCALE=2.0 ctest -L perf
```

Every budget comparison is multiplied by it, and the run prints
`[PERF] budget scale: 2.00 (CSE_PERF_BUDGET_SCALE)`.

> **Important:** an unparseable value is *ignored with a printed warning*, not
> silently treated as 1.0-with-a-shrug — the harness says so explicitly, because
> a typo'd knob quietly tightening budgets back to 1.0 would fail exactly on the
> machine that needed the loosening.

### Existing scenarios

| Test | Scene | What it stresses |
|---|---|---|
| `AtRest_SpawnView` | 20x20 grid, static camera | Baseline steady state |
| `SustainedCameraMovement` | 20x20 grid, orbiting camera | CSM movement cadence + cascade rebuilds |
| `WideView_HighInstanceCount` | 25x25 grid, wide static shot | Build/sort/submit at ~40k instances |
| `WideView_Moving` | 25x25 grid, flying camera | Wide view + continuous cascade rebuilds |
| `WideView_MovingWithDynamicCaster` | 25x25, flying camera + spinning hero | Worst case for the whole pipeline |
| `DynamicCasterShadows` | 20x20, spinning centre entity | View-depth-scoped shadow invalidation |
| `SmallObjectCull_DropsDistantInstances` | 25x25, low oblique, cull on vs off | Correctness + non-regression of the size cull |

### Adding a scenario

Build a scene, aim a camera, call `measure`, then assert **both** a timing budget
and a minimum workload:

```c++
TEST_F(PerfFixture, MyScenario) {
    Scene scene;
    buildGrid(scene, 25, 25);
    Camera cam;
    aim(cam, { 0.f, 110.f, 150.f }, { 0.f, 0.f, 0.f });

    const auto r = measure("my scenario", scene, cam, [&](int i) {
        // optional per-frame game logic (untimed)
    });
    EXPECT_GT(r.stats.submitted, 15000u) << "view lost its instance load — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetWideViewMs * budgetScale());
}
```

> **Important — always assert the workload.** Every scenario in the file carries an
> `EXPECT_GT(..., submitted, ...)` line. Without it, a culling or scene bug that
> empties the view turns the benchmark into a no-op that always "passes" while
> real performance rots.

Two more fixture details worth copying:

- `aim()` sets `Camera::Right` and `Camera::Up` as well as `Front`. Frustum
  culling (`createFrustumFromCamera`) reads that basis **directly**, not the view
  matrix — setting only `Front` silently culls the world.
- Models are pinned in static `shared_ptr`s for the whole suite. The
  `AssetManager` cache is weak, so without pinning each test reloads them from
  disk once the previous scene releases its handles, and the load lands inside
  your timings.

When a budget failure is an *intentional* cost (you added a feature), re-measure
and update both the budget constant and the baseline comment lines in the file.

---

## What we measured

These are results, not theory. They were produced with the harness above via
per-pass A/B runs (headless, RTX 3050, 1080p), across static bird's-eye, moving
bird's-eye, and low-oblique wide shots.

### Wide views are vertex/instance-count bound

The comment on `SetSmallCullEnabled` in `Engine/src/core/Scene.h` states it
plainly: the size cull is

> "the lever that actually helps a vertex/instance-bound wide view
> (shadows/PCF/fill measured free)."

Concretely, on wide shots:

- **Shadows are free.** Disabling CSM entirely — which also switches off the
  forward-shader PCF — moved the median by roughly nothing. Lower shadow
  resolution, fewer cascades, a shorter shadow distance, and a zero PCF kernel
  were all no-ops. Cascades are cached by the movement-margin cadence and the
  depth-only draws use low LODs, so there is little left to save.
- **Not fill-bound.** Rendering at quarter resolution (480x270 — **16x** fewer
  pixels) produced the same median as 1080p. PCF taps are not the cost.
- **Instance count is the cost.** ~37–44k instances costs ~14–20 ms regardless of
  every other setting.

> **Gotcha:** "turn down shadow quality" is the reflex fix and it does **not**
> work here. Do not build shadow-caster culling either: it buys nothing, and
> camera-dependent caster culling is a known way to reintroduce CSM ghosting
> (stale bakes left in still-valid cascades — see `Scene.h`, which notes the size
> cull deliberately drops objects from the **forward pass only**, leaving the
> caster set untouched).

### LOD is the dominant lever

Mesh LOD is the only toggle that reliably moves a wide-view median (measured
+9 to +11 ms with LOD off — all-LOD0 versus mostly-LOD2). It is on by default:

```c++
void  SetLODEnabled(bool v) { lodEnabled_ = v; }
bool  GetLODEnabled() const { return lodEnabled_; }
// >1 keeps high detail farther out; <1 switches down sooner (cheaper)
void  SetLODDistanceScale(float s) { lodDistanceScale_ = std::clamp(s, 0.1f, 8.f); }
float GetLODDistanceScale() const { return lodDistanceScale_; }
```

Editor: **Settings → Rendering Toggles → Enable mesh LOD** plus the
**LOD distance scale** slider (range 0.25–4.0, "higher = detail farther").

**Verify LOD is actually engaging** with the `LOD 0/1/2` line in Rendering Stats.
A wide shot that is nearly all LOD 0 means LOD is inert — this has happened for
real (OBJ imports lacking `aiProcess_JoinIdenticalVertices` produced un-indexed
geometry that the simplifier refused, roughly tripling wide-view frame times).
The baseline comments in `test_perf_render.cpp` preserve those pre-fix numbers
specifically so a silent relapse fails the budgets.

### Screen-size culling

Once LOD is on, the way to cut instance count further is the projected-size cull:

```c++
void  SetSmallCullEnabled(bool v) { smallCullEnabled_ = v; }
bool  GetSmallCullEnabled() const { return smallCullEnabled_; }
// pixel-height floor; higher culls more (and pops sooner). 0 disables.
void  SetSmallCullPixels(float px) { smallCullPixels_ = std::clamp(px, 0.f, 64.f); }
float GetSmallCullPixels() const { return smallCullPixels_; }
```

It drops any object whose bounding sphere projects smaller than the floor, in
pixels of height, computed in `Scene::RenderScene`
(`Engine/src/core/Scene.cpp`) as:

```
pixelH = viewportHeightPx * radius / (dist * tanHalfFov)
```

It is **off by default** (`smallCullEnabled_ = false`) because it changes what is
visible; the default floor is `3.0f`, described in the source as "a safe
'sub-visible' floor". It needs the viewport pixel height — `RenderScene`'s
`viewportHeightPx` parameter, which defaults to `0` (cull disabled) for callers
with no framebuffer size handy:

```c++
virtual void RenderScene(const Frustum& camFrustum, Shader& shader, Camera& camera,
                         int viewportHeightPx = 0);
```

Editor: **Settings → Rendering Toggles → Cull tiny objects** plus the
**Min on-screen px** slider (0–48). The `(?)` tooltip states the tradeoffs
verbatim:

> "Low values (2-4px) are sub-visible. Higher values cull more but can pop distant
> objects and, with a low sun, leave their (still-cast) shadows briefly visible."

It is adaptive by construction — flying higher or wider shrinks distant objects
below the floor, so the frame cost bounds itself. `SmallObjectCull_DropsDistantInstances`
pins both halves of the contract: the cull must remove a real fraction of
instances (>10%), and it must never make the frame *slower* than drawing
everything.

### Never judge performance in a Debug build

An `x64-Debug` build runs the **draw-submission path about 10x slower** than
Release. Measured in the editor: **44–46 ms** `3D submit` versus **~4.8 ms** for
the same work in Release. Two causes:

- GLM is header-only template math that depends on inlining; `/Od` turns every
  vector/matrix operation into a real function call.
- MSVC's `_ITERATOR_DEBUG_LEVEL=2` makes every `vector` iterator register itself
  on its container and bounds-check each access — which `std::sort` pays roughly
  474k times over 31.6k `DrawItem`s.

**Use `RelWithDebInfo`** (`/O2` + symbols) when you need to debug a real scene.
`Engine`, `Editor`, `Player` and `Cooker` all pin `build/bin/RelWithDebInfo`
output directories so the DLL and the executables land together.

### Hybrid-GPU laptops: check the `GPU` line first

Windows routes new GL contexts on hybrid-graphics laptops to the power-saving
integrated GPU by default. The measured penalty on the reference machine was
**4.6x** (12.6 FPS on the Intel iGPU vs 58.3 FPS on the RTX 3050, same scene).

The fix is an export in each executable, exactly as in `tests/test_perf_render.cpp`:

```c++
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
```

> **Note:** use exactly these two symbol names. `AmdPowerXpressRequestHighPerformance`
> is the spelling AMD documents — a near-miss such as adding a `Gpu` suffix
> compiles and links happily, exports a symbol no driver looks for, and leaves
> the executable on the integrated GPU with nothing to indicate why.

> **Important:** any new GL-heavy executable or test you add needs these exports
> too, or it will silently benchmark the iGPU and your budgets will be nonsense.

This is precisely why the `GPU:` line exists in Rendering Stats and why the
harness prints `[PERF] GL_RENDERER:` on every run — a one-glance check that the
numbers came from the hardware you think they did. If that line says "Intel",
stop measuring and fix the adapter selection before drawing any conclusion.

---

## Quick checklist

1. Read the `GPU` line. Wrong adapter → everything below is meaningless.
2. Confirm you are in Release (or RelWithDebInfo), never Debug.
3. Read the `3D submit / editor UI / swap` split to decide *which third* of the
   frame is slow, and whether vsync is capping you.
4. If the renderer owns the frame: read `Submitted` and `LOD 0/1/2`. Wide view
   with a LOD-0-heavy histogram → fix LOD before anything else.
5. Still too many instances? Enable **Cull tiny objects** and raise **Min
   on-screen px** until `Culled (size)` bites, watching for popping.
6. Do **not** spend time on shadow resolution, cascade count, PCF, or render
   resolution for wide-view cost — all measured as no-ops here.
7. Lock the win in: add or update a scenario in `tests/test_perf_render.cpp` and
   re-baseline the budget comments.
