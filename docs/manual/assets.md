# Assets

Assets are the files your game loads at runtime: models, their textures, shaders and saved scenes. This page covers where they live, how the engine loads them (synchronously and asynchronously), how the editor sees them, and how to validate them before you ship.

Everything here is CPU/GPU plumbing you can drive from code, plus two user-facing surfaces: the editor's **Assets** panel and the headless `AssetCooker` executable.

## The asset root

The engine loads from a single runtime asset root directory, `Exported`, resolved relative to the working directory (which is the directory containing the executables). The default is baked into the `AssetIndex` constructor in `Engine/src/assets/AssetIndex.h`:

```c++
explicit AssetIndex(std::string root = "Exported");
```

Paths used throughout the engine are *engine-style*: forward slashes, rooted at the asset root, e.g. `Exported/Model/backpack.obj`.

Source content lives in `Editor/src/Exported/` and is staged into the runtime `Exported/` next to the built executables by the shared `runtime_assets` CMake target (see `Player/CMakeLists.txt`).

## Supported formats

| Domain | Loader | Notes |
| --- | --- | --- |
| Models | Assimp (`Assimp::Importer`) | The importer is linked in full (`Engine/CMakeLists.txt` links `assimp::assimp`), so anything your Assimp build supports will *import*. |
| Textures | `stb_image` | The full implementation is compiled in `Engine/src/thirdparty/stb_impl.cpp`, so stb's standard format set applies. Every image is requested as 4-channel RGBA8 (`stbi_load(..., 4)` in `Engine/src/core/Model.cpp`). |

**Important:** what Assimp *can* import and what the editor *shows you* are different things. The editor's asset index classifies files by extension (`classify()` in `Engine/src/assets/AssetIndex.cpp`), and only these are recognised:

| Extension | `AssetIndex::Kind` | Browser tag |
| --- | --- | --- |
| `.obj` | `Model` | `[MDL]` |
| `.json` (except `project.json`) | `SceneJson` | `[SCN]` |
| `.png` `.jpg` `.jpeg` `.hdr` `.tga` `.bmp` | `Texture` | `[TEX]` |
| `.glsl` | `Shader` | `[SHD]` |
| anything else | `Other` | `[ - ]` |

A `.fbx` or `.gltf` file dropped into `Exported/` shows up as `Other`: you cannot spawn it from the browser and `AssetCooker validate` will not check it, even though `Model::Decode` would probably load it if you passed the path by hand. Extend `classify()` if you need more model formats surfaced.

## Materials and colour space

`Model::Decode` reads PBR scalars and probes six texture slots per Assimp material, each with a primary and a fallback slot:

| Material slot | Primary `aiTextureType` | Fallback | Colour space |
| --- | --- | --- | --- |
| albedo | `BASE_COLOR` | `DIFFUSE` | sRGB |
| normal | `NORMALS` | `HEIGHT` | linear |
| metallic | `METALNESS` | — | linear |
| roughness | `DIFFUSE_ROUGHNESS` | — | linear |
| ambient occlusion | `AMBIENT_OCCLUSION` | `AMBIENT` | linear |
| emissive | `EMISSIVE` | `EMISSIVE` | sRGB |

Colour space decides the GL internal format at upload: `GL_SRGB8_ALPHA8` for sRGB slots, `GL_RGBA8` otherwise (`Model::uploadTextureRGBA_`). Scalars read from the material are `AI_MATKEY_COLOR_DIFFUSE`, `AI_MATKEY_COLOR_EMISSIVE`, `AI_MATKEY_METALLIC_FACTOR` and `AI_MATKEY_ROUGHNESS_FACTOR` (the last two clamped to `[0,1]`).

Uploaded textures are shared through a global cache keyed by normalized path plus `|srgb` / `|lin`, so the same file used at two colour spaces is two GPU textures, and the same file used by twenty materials is one.

## The decode / finalize split

Model loading is one pipeline in two halves (`Engine/src/core/Model.h`):

```c++
// WORKER-SAFE (no GL, no shared state)
static ModelCPUData Decode(const std::string& path, bool gamma = false,
                           const std::unordered_set<std::string>* skipDecodeKeys = nullptr);

// MAIN THREAD (GL context current): the upload half
explicit Model(ModelCPUData&& cpu);

// Synchronous: both stages back-to-back on the calling thread
explicit Model(const std::string& path, bool gamma = false);
```

**Decode** (worker-safe) does the Assimp import, vertex/index extraction, LOD index generation via meshoptimizer, material scalars, and `stb_image` decoding into `ModelCPUData`. **Finalize** (main thread) turns that into GL objects: VAO/VBO/EBO uploads, LOD element buffers, and texture uploads through the shared cache.

The split is not optional. The `JobSystem` contract in `Engine/src/core/JobSystem.h` states it plainly: work running on a worker **must never touch GL, the entt registry, or ImGui** — GL function-pointer tables are per-module and the context is current on the main thread only. That is why `Decode` is a free-standing static that produces plain data, and every `gl*` call lives in the `ModelCPUData` constructor.

Two details worth knowing:

- The texture cache is **main-thread-only and unsynchronized**. Workers ship pixels; finalize does every lookup and insert.
- Decoded pixels are released per-texture the moment the GL id exists, rather than being held through mesh building. A backpack-class model carries over 100 MB of decoded pixels, and holding all of them at once was a real memory problem.

A failed import is not an exception. `Decode` returns `ModelCPUData` with `valid == false`, and finalize yields a `Model` with no meshes — identical to a failed synchronous load. Callers test `model->Meshes().empty()`.

## The AssetManager cache

`AssetManager` (`Engine/src/core/AssetManager.h`) dedupes loaded models by normalized path (slashes to `/`, lowercased — Windows-friendly). It holds `weak_ptr`s, so a model stays alive only while something references it.

```c++
std::shared_ptr<Model> GetModel(const std::string& path, bool gamma = false);
std::shared_ptr<Model> ReloadModel(const std::string& path, bool gamma = false);
void GarbageCollect();
void Clear();
```

`GetModel` is synchronous: it decodes and uploads inline on the calling thread, which **must be the main thread**. It blocks for the whole load. Use it at boot time and for paths where you expect a cache hit.

`ReloadModel` forces a fresh load from disk and replaces the cache entry. Existing holders keep their old `shared_ptr` — retarget them yourself if you want them to see the new model.

`GarbageCollect` drops expired entries. `Clear` empties the map; callers holding `shared_ptr`s keep their instances.

**Important:** `Model` construction creates GL resources, so the GL context must already be initialized before any load.

## Async loading with RequestModel

For anything user-triggered (dragging a model into the viewport, assigning one to an entity), use the async path so the frame doesn't stall:

```c++
enum class LoadState {
    Queued,   // waiting for a decode slot
    Decoding, // on a worker (or awaiting the main-thread finalize)
    Live,     // model is GPU-ready (model != nullptr, meshes present)
    Failed,   // import failed: model exists but has no meshes
};

struct ModelRequest {
    LoadState state = LoadState::Queued;
    std::shared_ptr<Model> model; // set once Live/Failed
    std::string path;             // as requested (engine style)
};
using ModelRequestHandle = std::shared_ptr<ModelRequest>;

ModelRequestHandle RequestModel(JobSystem& jobs, const std::string& path,
                                bool gamma = false);
```

`RequestModel` returns immediately. The flow is:

1. **Cache hit** — the handle comes back already `Live` (or `Failed` if the cached model has no meshes). No job is submitted at all.
2. **Already in flight** — you get *the same status block* as the earlier requester. Requests for one path never duplicate work.
3. **Otherwise** — the request is queued and a decode launches if a slot is free. The state goes `Queued` → `Decoding` → `Live`/`Failed`.

The decode is submitted to the `JobSystem` as a worker job (`Model::Decode`) plus a main-thread completion (the `ModelCPUData` constructor plus the cache/bookkeeping update). Completions only run inside `JobSystem::pumpCompletions`, which `Application::RunLoop` calls once per frame with a 2 ms budget (`Engine/src/core/Application.cpp`), plus a drain loop on exit that repeats until no more completions appear.

Concurrent decodes are capped:

```c++
static constexpr std::size_t kMaxConcurrentDecodes = 2;
```

Each in-flight decode holds a whole model's pixels in RAM until its finalize runs, so a burst of requests would otherwise spike memory. Excess requests queue and launch as slots free up.

Poll for status yourself, from the main loop:

```c++
std::size_t pendingRequests() const; // queued + decoding ("loading…" indicators)
std::size_t inFlightDecodes() const; // decodes currently on workers
```

Typical usage — take the cache-hit fast path, otherwise remember the handle and poll it (mirroring `EditorApplication::spawnModelEntity_` / `pollPendingModelOps_`):

```c++
using LoadState = MyCoreEngine::AssetManager::LoadState;

auto req = assets.RequestModel(jobs, "Exported/Model/backpack.obj");
if (req->state == LoadState::Live) {
    useModel(req->model);          // cache hit: same frame
} else if (req->state != LoadState::Failed) {
    pending.push_back(std::move(req));  // poll it from the main loop
}

// ...later, once per frame:
for (auto& r : pending) {
    if (r->state == LoadState::Queued || r->state == LoadState::Decoding) continue;
    if (r->state == LoadState::Failed) { /* report */ }
    else                               { useModel(r->model); }
    // remove from `pending`
}
```

> **Gotcha — main thread only.** `RequestModel` and all async bookkeeping are main-thread-only. States change inside the main-thread completion pump: poll handles from the main loop, never from a worker.

> **Gotcha — don't mix styles on one path.** Calling `GetModel` and `RequestModel` on the *same* path can duplicate a load, because the synchronous path cannot wait on an in-flight decode. It is never wrong (the texture cache still dedupes GPU ids), just wasted CPU. Pick one style per path.

> **Gotcha — the object may outlive its request.** `ReloadModel` and `Clear` supersede in-flight loads by dropping their `pending_` entry, which strips the completion's ownership token. The old handle still resolves normally; its model just doesn't end up in the cache.

## Mesh LOD, and the OBJ import flag

Every mesh gets up to `Mesh::kLodCount == 3` levels: simplified index buffers over the *same* vertex buffer, generated at load with meshoptimizer. Level 0 is the full mesh; a level that fails to simplify usefully falls back to (aliases) the previous level's element buffer.

Simplification is skipped entirely for meshes under 192 indices (`3 * 64`). Above that, target ratios are `{1.0, 0.25, 0.08}` at a target error of `0.05`, and a level is accepted only if it reduced the previous level's index count by at least 10%.

The Assimp post-process flags used by `Model::Decode` are:

```c++
aiProcess_Triangulate |
aiProcess_JoinIdenticalVertices |
aiProcess_GenNormals |
aiProcess_CalcTangentSpace |
aiProcess_FlipUVs
```

> **Important — `aiProcess_JoinIdenticalVertices` is what makes LOD work on OBJ.**
> Without it the OBJ importer emits per-face vertices: a disconnected triangle soup where no two triangles share a vertex index. `meshopt_simplify` cannot collapse a single edge in that topology, so **every LOD level is rejected and the LOD system is silently inert for every OBJ asset** — the mesh pays its full vertex cost at every distance, with no error anywhere. This was a real, shipped regression: a first validation run confirmed `backpack.obj` accepting 0 of 55 LOD levels.
> The flag only deduplicates positions, it never moves them, so AABBs are unchanged. If you ever touch this flag list, keep it.

If you suspect this has regressed, `AssetCooker validate` reports it directly (see below).

## The AssetIndex

`AssetIndex` (`Engine/src/assets/AssetIndex.h`) is the engine's cached view of what *exists* on disk, as opposed to `AssetManager`, which caches what is *loaded*. All directory walking lives here; the editor's Assets panel renders the cached tree instead of touching `std::filesystem` itself.

```c++
struct Node {
    std::string name;     // display filename
    std::string relPath;  // engine-style path ("Exported/Model/foo.obj")
    Kind kind = Kind::Other;
    std::vector<Node> children; // directories first, then files, each A→Z
};

void tick(float dt, JobSystem* jobs = nullptr);
void forceRescan();
void setRescanInterval(float seconds);   // clamped to a 0.1s minimum
float rescanInterval() const;            // default 2.0
const Node& root() const;
std::uint64_t version() const;
const Node* find(const std::string& relPath) const;
```

`tick` is throttled polling: call it once per frame with the frame delta and it rewalks at most every `rescanInterval()` seconds. Between rescans it costs a couple of float operations. Pass a `JobSystem*` and the disk walk runs on a worker with the compare/swap landing back on the main thread in a completion — the UI thread never blocks on the filesystem (a big tree, or a cold OneDrive hydrate, used to stall the frame). At most one scan is in flight. Either way, `tick` is main-thread only.

`version()` bumps only when a rescan actually *changed* the tree, which is the cheap way for dependents (thumbnails, caches) to notice disk changes.

> **Gotcha — a rescan replaces the whole tree.** Hold `relPath` strings across frames, never `Node` pointers.

Three robustness rules are baked in, each covering a recurring crash class:

- `path::string()` **throws** for filenames not representable in the active code page (MSVC's strict conversion, no `?` fallback). The engine's narrow-path IO couldn't open such files anyway, so the entry is skipped, never fatal.
- Directory cycles from junctions and symlinks are cut by a depth cap of 16.
- A root that vanishes mid-session yields an empty tree, not an error state.

### The Assets panel

The editor's Assets panel (`Editor/src/panels/AssetBrowserPanel.cpp`) is a pure view over the index — it owns no disk state.

- **Refresh** calls `index.forceRescan()`.
- **Validate** spawns `AssetCooker validate` (see below); disabled while a run is in progress.
- Breadcrumbs (`Exported > Model > …`) are clickable; every segment navigates.
- A folder tree on the left, a resizable splitter, contents on the right.
- Single-click a file to select it for the Inspector's asset view.
- Double-click: a directory drills in, a model spawns into the scene, a scene `.json` loads (blocked during play).
- Models can be dragged into the viewport to spawn where they land.
- Right-click context menus: models offer *Spawn in Scene*, *Assign to Selected Entity* and *Copy Path*; scenes offer *Load Scene*, *Set as Startup Scene* and *Copy Path*; everything else offers *Copy Path*.
- While async loads are outstanding, the toolbar shows `loading N model(s)...`.

Spawn and assign go through `RequestModel`, so they never block the editor. The drop position and target entity are captured at request time; if the target entity dies while the model decodes, the assign is dropped.

## Import settings (.import sidecars)

Per-asset import settings live in a JSON sidecar next to the asset: `foo.png` → `foo.png.import` (`Engine/src/assets/ImportSettings.h`). This is the seam for the eventual import pipeline — the editor's Inspector edits them, `AssetCooker` enforces them.

```c++
struct ImportSettings {
    int maxDimension = 0; // 0 = unlimited
};

ENGINE_API std::string ImportSettingsPathFor(const std::string& assetPath);
ENGINE_API ImportSettings LoadImportSettings(const std::string& assetPath);
ENGINE_API bool SaveImportSettings(const std::string& assetPath,
                                   const ImportSettings& s);
```

| Setting | Meaning | Enforced by |
| --- | --- | --- |
| `maxDimension` | For textures: the largest width/height this asset should ship at. `0` = unlimited. | `AssetCooker validate` reports a warning when the source exceeds it. |

Loading is deliberately forgiving: a missing *or malformed* sidecar yields defaults, never an error. An asset without settings is the normal case, and these files get hand-edited. Saving writes even default settings, because an explicit reset is user intent; `SaveImportSettings` closes the stream before checking failure, since a buffered write failure (disk full, a OneDrive lock) only surfaces at close.

Edit them in the editor by single-clicking a texture in the Assets panel — the Inspector switches to its asset view and shows an **Import Settings** section with a **Max Dimension** combo offering `Unlimited`, `512`, `1024`, `2048`, `4096` (`Editor/src/panels/InspectorPanel.cpp`). A hand-edited value outside that set is displayed honestly as `N (custom)` rather than masquerading as `Unlimited`. If the save fails, the UI reverts to what is actually on disk and says so. Non-texture assets show *"No import settings for this asset type yet."*

Two things to know about sidecars:

- **They are editor-only and never shipped.** The player never reads them, so `Player/CMakeLists.txt` excludes `*.import` from the installed bundle in both the source-tree copy (`PATTERN "*.import" EXCLUDE`) and the editor-authored overlay. They behave like Unity's `.meta` files in that respect.
- **They are hidden from the Assets panel.** `AssetIndex::scanDir_` skips any filename ending in `.import`, so they are metadata, not browsable content. `index.find("Exported/tex.png.import")` returns `nullptr`.

> **Gotcha — sidecars are keyed by PATH, not by GUID.** Moving or renaming an asset means moving its `.import` file with it, by hand. GUID identity and reference fixup are not implemented.

Also not implemented (planned only): per-asset sRGB override, compression format, and mesh import options.

## AssetCooker validate

`AssetCooker.exe` (`Cooker/src/CookerMain.cpp`) is the headless, out-of-process half of the asset pipeline. It links `Engine.dll` but **never initializes GL** — every operation is CPU-only by design. The editor spawns it rather than validating in-process, so a hostile asset takes down the cooker rather than the editor.

```
AssetCooker validate <assetRoot>
```

`validate` walks the asset tree, decodes every model in parallel on its own `JobSystem`, and checks every texture against its `.import` settings. It reports what would load wrong or wastefully at runtime.

### What it checks

| Check | Level | Meaning |
| --- | --- | --- |
| Texture does not decode | `ERR` | `stbi_info` failed; the reason from `stbi_failure_reason()` is included. |
| Texture exceeds its `maxDimension` | `WARN` | Reported with the actual `WxH`. |
| Model failed to import | `ERR` | Assimp rejected the file. |
| Model imports but contains no meshes | `ERR` | Lenient importers (OBJ especially) accept garbage as an empty scene; an invisible model at runtime is an error, not a warning. |
| Model references a missing/undecodable texture | `WARN` | One line per bad texture. |
| LOD-eligible meshes accepted no level | `WARN` | The OBJ disconnected-vertex symptom. |

The LOD warning is **rolled up to one line per model** — a 70-mesh asset must not flood the report with 70 identical lines. It reads like:

```
WARN Exported/Model/backpack.obj: 55 of 55 LOD-eligible meshes accepted no level (X tris at full cost at every distance; disconnected OBJ vertices?)
```

Texture dimension checks use `stbi_info`, which reads dimensions without a full decode.

### Output protocol

Output is line-oriented on **stdout**, so the editor (or CI) can parse it. Engine diagnostics such as `[Model]` logs go to **stderr** and stay out of the protocol — and are written as one `fprintf` per line, because parallel decode workers previously spliced each other's lines together mid-line.

```
WARN <path>: <reason>
ERR  <path>: <reason>
DONE models=<n> textures=<n> warnings=<n> errors=<n>
```

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Clean — warnings are allowed and do not fail the run. |
| `1` | Errors found. |
| `2` | Bad usage, **or the asset root is not a directory**. |

> **Important — validation fails closed.** If `<assetRoot>` is not an existing directory, the cooker prints `ERR <root>: asset root not found (cwd-relative?)` and exits `2`. Without that check, a typo'd root (or the wrong working directory) would scan zero assets and report a clean exit `0` — a validation gate that passes everything.

### Running it from the editor

The **Validate** button in the Assets panel runs `.\AssetCooker.exe validate Exported` as a child process via `CreateProcess`, with only stdout piped and a dedicated reader thread. The editor holds the process handle so shutdown can `TerminateProcess` a hung cooker: crash isolation *and* hang isolation. The report appears in its own window. The explicit `.\` prefix resolves against the working directory, immune to PATH-search rules like `NoDefaultCurrentDirectoryInExePath`.

Building the Editor also builds `AssetCooker` (`add_dependencies(Editor AssetCooker)` in the root `CMakeLists.txt`), so the button always has something to launch.

> **Important — pass a dedicated JobSystem if you call the validator directly.** `ValidateAssetTree(root, jobs)` pumps completions itself, which must not race an application main loop pumping the same pool. That is why the cooker constructs its own `JobSystem` and the editor spawns a process instead.

### Not implemented

`cook-textures` (BC7/mips applying `maxDimension`), `cook-meshes` and `pack` are planned commands. They do not exist today; `validate` is the only command.
