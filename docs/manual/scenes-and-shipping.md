# Scenes and Shipping a Build

A scene is a JSON file describing every entity in your level plus the scene-level lighting and shading settings. The editor writes it, the player reads it, and the packaging step copies it into the shipped bundle. This page covers the file format, saving and loading, choosing the startup scene, what the standalone player does at boot, and how to produce a distributable ZIP with CPack.

Three traps in this pipeline have each cost real debugging time. They are called out as **Gotcha** notes below — read them before you ship anything.

## The scene file

Scenes are plain JSON written with `nlohmann::json` at two-space indent. The top level is an object with three keys:

| Key | Contents |
| --- | --- |
| `version` | Integer format version. Written from `SceneSerializer::kVersion` (currently `1`). |
| `settings` | Scene-level lighting/shading/render state — the light (`lightDir`/`lightColor`/`lightIntensity`), PBR + map toggles, `instancingEnabled`, `iblEnabled`/`iblIntensity`, `lodEnabled`/`lodDistanceScale`, the projected-size cull (`smallCullEnabled`/`smallCullPixels`), `depthPrepass`, `aaEnabled`, the `qualityLevel` tier, an `environment` object (sky/IBL source, HDRi path, skybox + procedural sky colours), and a `postFX` object (vignette, ink outline, colour grade, bloom). |
| `entities` | Array of entity objects, in creation order. |

Each entity object carries only the components that entity actually has: `name`, `parent`, `transform`, `model`, `noShadow`, `camera`, `light`, `rigidBody`, one of `boxCollider` / `sphereCollider` / `capsuleCollider` / `planeCollider`, `script`, `audioSource` (clip path, volume, pitch, loop, spatial, playOnStart, min/max distance), `audioListener` (a bare `true` tag), `uiDocument` (markup and stylesheet paths, `sortOrder`, `enabled`, `interactive`, and a `region` array of four surface fractions `[x, y, w, h]` — an omitted or malformed `region` means the whole surface), and `materialOverrides` (per-slot base colour, PBR scalars, transparency, and `shadingModel` + toon params). Model, audio-clip and UI markup/stylesheet paths are run through `PathIsContained` on load; a rejected path is dropped but the component survives, so the entity keeps its slot. (The script path is not checked here — it is sandboxed later, when the script runtime resolves it against the configured script directory.) See `Engine/src/core/SceneSerializer.cpp` for the exact per-component field lists.

Two structural rules are worth knowing if you ever hand-edit a file:

- **Parent links are array indices, not ids.** An entity's `parent` is the position of its parent within the `entities` array. Every entity therefore occupies a slot even when it has no components at all, so the indices stay aligned. Loading resolves parent links in a second pass, so a child may appear before its parent.
- **Entity order is stable across save/load.** `Save` iterates the registry and reverses the result to write in creation order, because EnTT views iterate newest-first. This matters because camera-priority ties break on lowest entity index — without the reversal, the selected camera could flip on every save.

Derived data is never trusted from the file. AABBs are regenerated from the model on load, and models that fail to load (empty mesh list) get no AABB at all rather than garbage bounds.

### Versioning

`Load` reads `version` and refuses anything outside `0 < version <= kVersion`:

```
ERROR::SCENE::LOAD_FAILED unsupported scene version N in '<path>'
```

A file that is not an object, or has no `entities` array, is rejected the same way.

**Important:** `Load` parses and validates the entire file *before* touching the registry. A bad file returns `false` and leaves your current scene completely intact — you never lose work to a corrupt or newer-format scene. That guarantee covers wrong-*typed* fields too, not just syntax errors: `Load` first runs the whole load against a throwaway `Scene` (the `dryRun_` probe, which performs every JSON read but skips model I/O), so by the time the real pass starts, nothing left in the file can throw. Re-using `Load` itself as the validator rather than a parallel schema check means the two can never disagree.

Once the file is known good, the load calls `Scene::ResetToDefaults()` before applying the `settings` block. Every setting is read with the current scene value as its fallback, so **an absent key means "the default"**, not "keep whatever the previously loaded scene had" — which used to let one scene's bloom, HDRi or quality tier follow you into the next one and then get written into *its* file on the next save. `ResetToDefaults` restores the whole `postFX` and `environment` structs and the `qualityLevel` along with the individual knobs, so adding a field to either struct cannot quietly escape the reset.

Hand-edited values are also range-checked on load, not on trust:

| Field | Clamp applied on load |
| --- | --- |
| `camera.fovDeg` | clamped to `[1, 179]` |
| `camera.nearClip` | at least `1e-3` |
| `camera.farClip` | at least `MinFarClipFor(nearClip)` |
| `rigidBody.type` | range-checked against `BodyType`, else `Dynamic` |
| `rigidBody.friction` / damping | at least `0` |
| `rigidBody.restitution` | clamped to `[0, 1]` |
| collider extents / radii | at least `1e-3` (`capsuleCollider.halfHeight` at least `1e-4`) |

`MinFarClipFor` (in `Engine/src/core/Components.h`) keeps the near/far separation *relative*:

```c++
inline float MinFarClipFor(float nearClip) {
    return std::max(nearClip + 1e-3f, nearClip * 1.0001f);
}
```

A plain absolute epsilon is absorbed by float rounding above roughly 32k, which would let `near == far` reach `glm::perspective` as a division by zero — a NaN projection and a silent black render. Any code you write that enforces `near < far` must use this helper.

Parent links that would close a cycle are skipped with a warning rather than applied; cycle members would be unreachable from any root and would vanish from the hierarchy panel.

### Physics is backend-agnostic

Physics components serialize as engine enums and plain floats, never backend handles. A scene authored against Jolt loads unchanged under PhysX.

## Saving and loading in the editor

Scene persistence lives in the **File** menu on the editor's title bar (`EditorApplication::DrawMainMenuBar` in `Editor/src/EditorApplication.cpp`). The current scene path defaults to `Exported/scene.json`; the title bar shows its file name, centred, with the last save/load result taking that slot instead for 30 seconds before decaying back to the name, and a `[PLAYING]` prefix during play.

| Menu item | Effect |
| --- | --- |
| **New Scene** | Confirmation popup, then replaces the scene with a Main Camera plus a ground plane. |
| **Open Scene…** | Popup for the path, then a `SceneLoader` swap plus the editor-side invalidation described below. |
| **Save Scene** (`Ctrl+S`) | `SceneSerializer::Save` to the current scene path. |
| **Save Scene As…** | Popup for a new path, then saves — the new path becomes the current one. |
| **Save All** (`Ctrl+Shift+S`) | The scene *and* the editor layout (ImGui otherwise only persists the layout on a clean shutdown). |
| **Set Current Scene as Player Startup** | Writes the current path into `Exported/project.json`. |

Every scene-changing item — and the `Ctrl+S` shortcuts — is disabled while in Play mode. Saving mid-play would persist transient play state, and loading would be overwritten by Stop's snapshot restore anyway. `Ctrl+S` is also gated on not typing, so it stays a plain keystroke inside a text field.

The **Settings** window has no scene section. Its three tabs are **Rendering**, **Editor** and **Audio**.

You can also act on a scene from the Asset Browser: double-click a `.json` scene to load it, or right-click for **Load Scene**, **Set as Startup Scene**, and **Copy Path**. Loading is blocked during play there too; setting the startup scene is safe at any time.

**New Scene is deliberately never empty.** It seeds a `Main Camera` and a ground plane with a `PlaneCollider`, because a camera-less scene leaves the Game view with nothing to render and drops the shipped player onto a free-fly diagnostic camera. Both report the problem now, but the fix is always to give the scene a camera.

### What a load invalidates

Every entity handle from the old scene is dead, and a pile of derived state has to go with them. None of that is written out at each load site any more — it is subscribed once, next to the thing being invalidated, and the `SceneLoader` runs it. See [Changing scene at runtime](#changing-scene-at-runtime) for the mechanism; what the editor subscribes is:

| Subscribed by | On unload | On load |
| --- | --- | --- |
| `EditorApplication` | selection, undo history, the Game view's `CameraDirector`, in-flight async model ops | re-apply the scene's quality tier unless `Custom`, force-update all CSM cascades |
| `InstallPhysics` | `PhysicsWorld::Clear` | — |
| `InstallScripting` | `ScriptWorld::Clear` (fires every `OnDestroy`) | — |
| `InstallAudio` | `AudioWorld::Clear` (stops voices from the old scene) | — |

Two of those are less obvious than they look. The quality-tier re-apply is needed because the per-scene perf toggles serialize but the CSM cascade-count/resolution half of a tier lives on the `Renderer` and is not; without it a reloaded High scene gets default shadows. The CSM force-update is needed because wholesale replacement bypasses the departure-sphere dirty-caster flow, so the old scene's shadows stay baked into cascades the new content never touches.

The teardown hooks fire **before** the load, while the outgoing entities are still alive — a script's `OnDestroy` against an already-cleared registry silently no-ops, and a voice has to be stopped before the entity that owns it is gone.

New Scene is not a file load, so it does not go through the loader; it performs the same invalidations directly (`EditorApplication::newScene_`).

### The editor boots from the scene file

The editor loads the same startup scene the player ships with — boot content comes from the scene file, never from code. This used to build a hardcoded demo grid at launch, which made the editor lie about what a scene contained: authored components (physics especially) looked like they "didn't save" when in fact the hardcoded scene had replaced them before you ever saw them.

If the startup scene fails to load, the editor falls back to `createDefaultScene_`. Either way the outcome is shown as `bootStatus_` under **Settings > Editor**, at the top of the tab, so "why am I looking at this scene?" is answerable without reading the console.

## Changing scene at runtime

A game changes scene by asking the `Application`:

```c++
app.LoadScene("Exported/level2.json");
```

`MyCoreEngine::SceneLoader` (`Engine/src/core/SceneLoader.h`, `.cpp`) is what that call reaches. It exists because replacing a scene is not the same problem as reading a file, and `SceneSerializer` should not have to know about either half:

**The swap is deferred.** `LoadScene` records the request and returns; the registry is replaced at the next frame boundary. The reason is the caller: a main-menu button's handler runs inside `UIWorld::Update`, inside the UI render pass, between `BeginScreen` and `End`. Clearing the registry there would leave the UI iterating a tree whose entities had just vanished. `Application::RunLoop` drains the request immediately after `jobs_.pumpCompletions` — input has been polled, GL finalises are done, no subsystem is mid-tick, and `UpdateTransforms`, the camera director and `RenderFrame` have not run yet, so nothing downstream is holding a handle into the scene about to go.

**The file is validated at request time, not at swap time.** `LoadScene` returns `false` for a path that will not load, and in that case nothing at all has been touched. Validating late would mean tearing the old scene's subsystems down and only then discovering the destination was a typo.

**A swap frame renders but does not simulate.** After a swap the fixed-timestep accumulator is reset, `paused_` is cleared and `timeScale_` returns to 1, the camera director is reset and the CSM cascades are forced to rebuild, and gameplay is skipped for that one frame. Otherwise the first step integrates a `dt` that accumulated while the old scene was still up — for a slow load, an arbitrarily large one — and physics resolving that with brand-new bodies is how things end up inside walls. The pause/time-scale reset is why "Quit to menu" from a pause menu does not deliver a frozen main menu.

The other two are state that outlives the registry the swap replaced:

* **The camera director.** `Scene::ResetToDefaults` calls `registry.clear()`, so every handle the director holds is dead — but it still has the last pose it put on screen. Without the reset the new scene sees a changed target with a valid previous output and blends the opening shot in from the *previous* level's final camera pose.
* **The shadow cascades.** A swap replaces the whole caster set, which the dirty-caster flow cannot express: the departed casters have no `Transform` left to mark dirty, and the arrivals' dirty flags were already consumed by the loader's own `UpdateTransforms`. Without `forceCSMUpdate`, the new scene renders against the old scene's depth maps until the camera drifts far enough to invalidate a cascade on its own.

Both live in `Application::Run`, the engine's single swap point, so every host gets them. Anything else a host caches per scene is its own to reset — the Player, for example, re-applies the quality tier through a `SceneLoader` observer, because the CSM half of a tier lives on the `Renderer` and is not serialized.

### Subscribing to a swap

Anything that derives state from the registry subscribes to the loader, and does it where it is created rather than where scenes are loaded:

```c++
loader->AddObserver([&world](Scene&) { world.Clear(); });          // teardown only
loader->AddObserver([](Scene&) { ... }, [](Scene& s) { ... });     // teardown + rebuild
```

The loader owns the adapter, so there is nothing to keep alive. Observers fire in registration order in both directions. `InstallPhysics`, `InstallScripting` and `InstallAudio` each do exactly this, which is the point: a subsystem that exists but forgot to clean up after a scene swap is no longer a thing that can happen.

For a veto — refusing a swap outright — implement `ISceneSwapObserver` and return `false` from `AllowSceneSwap`. The refusal happens before anything is torn down. Each request carries a `SceneSwapOrigin` (`Game` or `Host`) so a host can refuse one and allow the other; the editor uses this to reject a *game*-initiated swap while stopped, because the Game panel dispatches the running document's clicks even in edit mode and a menu button in a document being authored must not replace the scene under the author.

### The result

`SceneLoader::SetOnSwapComplete` reports every swap, successful or not, and `lastResult()` holds the same thing:

| `SceneSwapStatus` | Meaning |
| --- | --- |
| `Ok` | Loaded. `result.report` carries the entity count and any models that did not import. |
| `Invalid` | The file failed validation at request time. Nothing was touched. |
| `Refused` | An observer vetoed it. Nothing was torn down. |
| `Superseded` | A later request replaced this one before it ran. |
| `LoadFailed` | Validated, then failed during the real load — the file changed on disk in between, or an asset-stage throw the dry run cannot rehearse. Teardown had already happened, so the subsystems are rebuilt against whatever survived. |

`Ok` is not the same as "fine". A scene can load perfectly and still be unusable, so check `result.report.complete()`: an entity whose model never imported is invisible while its collider is entirely real. The loader logs that case regardless.

The editor drains its own File-menu loads **immediately** rather than at the frame boundary, because they come from a menu handler rather than from game code holding a view, and the caller wants the yes/no now for the status line. Game-originated swaps still take the deferred path.

### Loading without a stall

```c++
app.LoadSceneAsync("Exported/level2.json");
```

Same swap, same guarantees — deferred, validated at request time, same observers in the same order. What changes is *when* the expensive part happens.

**What can and cannot move off the main thread.** The destructive half never will: a load creates entities, and the registry is single-threaded by design. But that half is cheap. The seconds go into importing the meshes and textures the scene names, and those can move — `AssetManager` has decoded on worker threads and finalised GL on the main thread since the asset pipeline landed.

So an async request collects the model paths at request time, hands them to `AssetManager::RequestModel`, and **holds the swap until every one has settled**. Nothing is torn down while they load, so the outgoing scene keeps running and rendering at full rate. That is the whole feature: it is what makes a loading screen possible, because a synchronous load has no frame in which to draw one.

By the time the swap runs, every `GetModel` inside it is a cache hit — so the window in which the world is actually gone shrinks from "however long the models take" to "however long entity creation takes".

The paths come from the same probe `Validate` runs, so a path the sandbox refuses is never warmed, for the same reason it is never opened.

**Progress**, for the screen you now have somewhere to draw:

```c++
if (loader.swapPrewarming())
    DrawBar(loader.prewarmDone(), loader.prewarmTotal());
```

The shipped menu binds exactly this: `menuLoading`, `menuLoadDone`, `menuLoadTotal` and `menuLoadPct` are published every frame, and the footer swaps its status line for a `LOADING n/m` while a swap warms.

**Failure is settling, not hanging.** A model that will never arrive settles as `Failed` and the swap proceeds; the miss is reported through `SceneLoadReport::failedModels`, where a missing asset has always been reported. A swap that waited for a file that does not exist would be a worse bug than the one it was avoiding.

**It degrades honestly.** With no `JobSystem` attached — `SetJobSystem`, which both hosts call — or with a scene that names no models, `LoadSceneAsync` *is* `LoadScene`. The swap still happens; it simply is not warmed first. Superseding or cancelling a warming swap releases its handles, so a scene you changed your mind about does not stay pinned in the cache.

Which to use: async for a scene a player waits on (a level, New Game), synchronous for anything small enough that a hitch is cheaper than a frame of loading UI. Still not implemented: parsing the JSON itself on a worker, and streaming a scene in pieces rather than as one atomic swap.

## The startup scene and project.json

Which scene the player boots is stored in `Exported/project.json`, read and written through `ProjectSettings` (`Engine/src/core/ProjectSettings.h`):

```c++
struct ENGINE_API ProjectSettings {
    std::string startupScene = "Exported/scene.json";
    float       masterVolume = 1.0f; // 0..1, scales the whole audio mix

    static const char* DefaultPath() { return "Exported/project.json"; }

    bool Load(const std::string& path = DefaultPath());
    bool Save(const std::string& path = DefaultPath()) const;
};
```

`Load` treats a missing file as success — the defaults simply stand. Malformed JSON logs to `stderr` and returns `false`, again keeping the defaults.

`masterVolume` is the global audio mix level, edited under **Settings > Audio** and clamped to `0..1`. Because it lives in `project.json` (not the scene), both the editor and the shipped player boot at the same volume, and it is unaffected by loading a different scene.

To set it: open or save the scene you want, then choose **File > Set Current Scene as Player Startup**, or right-click the scene in the Asset Browser and choose **Set as Startup Scene**. The title bar confirms `Saved to Exported/project.json (ships with the game)`. The editor loads existing settings before rewriting the file, so the fields `ProjectSettings` models are preserved. `Save` rewrites project.json from scratch, so any keys the struct does not model are dropped.

The file lives next to the assets specifically so it ships inside the packaged bundle.

## What the player does at boot

`Player/src/PlayerMain.cpp` builds twice from one source:

| Target | Output | Subsystem | Notes |
| --- | --- | --- | --- |
| `PlayerDebug` | `PlayerDebug.exe` | console | Keeps the terminal for logs. |
| `PlayerShipping` | `Player.exe` | `WIN32` | No console. Defines `MYCE_SHIPPING=1`; MSVC links with `/ENTRY:mainCRTStartup` to keep the plain `main()` from `Main.h`. |

Both write into the same output directory as the Editor, so a scene saved in the editor is immediately runnable in the player.

Boot sequence:

1. `InitGL()`, then build the shader from `Exported/Shaders/vertex.glsl` and `Exported/Shaders/frag.glsl`. An invalid shader is fatal.
2. **Pick the scene path.** Command line wins, then project settings: `Player.exe path/to/scene.json` overrides `ProjectSettings::startupScene`. `project.json` is loaded either way, because the master volume comes from it too.
3. **Load the scene** with `SceneSerializer`. A failure is fatal and the message tells you to save one from the editor or pass a path.
4. **Re-apply the saved quality tier** unless it is `Custom`. The CSM half of a tier (cascade count and base resolution) lives on the Renderer and is not serialized, so without this the shipped game would boot with default shadows.
5. **`scene.UpdateTransforms()` before building physics.** A freshly loaded scene has dirty Transforms whose cached world matrices are still identity, and bodies are built from world poses. Building first put the ground (authored at `y = -3`, scaled 300×) at the origin as a 1×1 box, so everything fell straight past it. `PhysicsWorld::Build` is now robust to this on its own, but the ordering is still the honest way to express it.
6. **`InstallPhysics(*this, scene, physics_)` then `physics_.Build(scene.registry)`.** The player is always "playing" — ticks run from frame one, since only the editor gates gameplay — so there is no Play transition to build bodies on.
7. **Scripting and audio, started immediately** for the same reason: `InstallScripting` (script directory `Exported/Scripts`) then `scripts_.Build` / `scripts_.Start`, and `InstallAudio` at the saved `masterVolume` then `audio_.Start`.
8. **In-game UI from the scene.** Load `Exported/Fonts/Roboto.ttf` (a missing font draws the UI without text), wire the real system clipboard and the GLFW key/char callbacks, call `InstallDemoUIContent` for the two things a markup file cannot carry — a named action and a converter — and install the render-pass callback that drives every `UIDocumentComponent` in the scene. Nothing here names a UI file: swapping the HUD is a scene edit.
9. **`setRenderFromSceneCamera(true)`** so the view comes from the scene's camera entity through the same `CameraDirector` selection and blending as the editor's Game view.
10. **`scene.UpdateTransforms()` again**, then `FindActiveCamera(scene.registry)`. If a camera is found, `setInternalCameraInput(false)` turns the engine's built-in WASD/mouse-look OFF, so gameplay is the only thing that can move the view. A shipped game must not hand the player a debug fly-camera.
11. `RunLoop(scene, shader)` — ESC or closing the window exits.

Because the shipping build has no console, startup failures go through a `MessageBoxA` as well as `stderr`; otherwise a failed boot would be an instant silent exit.

`InstallPhysics` (`Engine/src/physics/PhysicsInstall.h`) is shared by both hosts, so "it works in Play" and "it works in the shipped game" cannot drift apart:

```c++
inline Application::TickHandle InstallPhysics(Application& app, Scene& scene,
                                             PhysicsWorld& world,
                                             const std::string& backendName = {},
                                             const PhysicsSettings& settings = {});
```

It subscribes via `AddFixedUpdate`, never `SetFixedUpdate` — the single primary slot is reserved for your game's own gameplay hook, and taking it would silently replace that logic. Backend selection falls back to the default and then to `"Simple"`, so a bad backend name can never leave the app without physics.

### Which camera renders

`CameraDirector::Update` picks the manual override if one is set and still valid, otherwise it falls back to `CameraDirector::SelectCamera`, which returns the **highest-priority enabled** `CameraComponent` that also has a `Transform`. Ties break on **lowest entity index** — not raw handle, because handle versions reset on scene load — which is what makes selection deterministic and save/load-stable.

```c++
struct CameraComponent {
    float fovDeg = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    int  priority = 0;
    bool enabled = true;
};
```

Gameplay drives cameras by editing these fields; the director notices on its own and nothing needs to call into it. When the winner changes, the view blends over `defaultBlendSeconds` (0 by default — hard cuts).

> ### Gotcha 1 — a scene with no enabled camera falls back to a free-fly debug camera
>
> If `FindActiveCamera` returns `entt::null`, the director cannot drive anything and the engine falls back to the fly cam. On screen this looks exactly like "the shipped game ignored my camera", and for a long time it happened silently.
>
> The player now reports it instead — via the console and, in the shipping build, a message box:
>
> ```
> scene '<path>' contains no enabled CameraComponent, so there is nothing to render from.
>
> Add a Camera component to an entity in the editor and SAVE the scene
> (File > Save Scene). Falling back to a free-fly debug camera for now.
> ```
>
> Free-fly stays enabled purely as a diagnostic so the level is still inspectable. **The fix is to add a Camera and save the scene.** Adding one in the editor and *not* saving changes nothing about the build — the player only ever sees the file. Note also that a camera with `enabled: false` does not count: disabled cameras are never selected.
>
> The editor's Game view surfaces the same condition rather than guessing, showing "No camera in the scene." and pointing at Inspector > Add Component > Camera.

## Runtime asset staging

`cmake/stage_runtime_assets.cmake` copies assets next to the executables. It splits content into two classes and treats them differently:

- **Static assets** (every *subdirectory* of `Editor/src/Exported/` — today `Env/`, `Fonts/`, `Icon/`, `Layouts/`, `Model/`, `Scripts/`, `Shaders/`, `UI/`) are owned by the source tree and re-copied every build, so shader, model, script and UI edits show up. The script globs the subdirectories rather than naming them: the list used to be hardcoded, and adding `Scripts/` and then `Env/` each silently shipped a feature whose assets never reached the runtime directory.
- **Authored files** (`*.json` — anything the editor writes at runtime into the same directory) are seeded **only when missing**.

There is exactly one staging target, `runtime_assets`, defined in `Editor/CMakeLists.txt` and depended on by `Editor`, `PlayerDebug`, `PlayerShipping`, and `AssetCooker` (`Cooker/CMakeLists.txt`). Every executable that shares the output directory takes a dependency on that one target rather than copying for itself: concurrent copies into the same directory race under Ninja, so one writer is the rule. A fifth consumer added later belongs on the same `add_dependencies(... runtime_assets)` line, never on a copy step of its own.

> ### Gotcha 2 — authored `.json` files are staged only-if-missing
>
> This is deliberate. A blind `copy_directory` here silently reverted editor-saved scenes to the checked-in copy on *every build*, which also meant the packaged game shipped a stale scene.
>
> The consequence to remember: **an existing build directory keeps its current `Exported/scene.json` and `Exported/project.json` forever.** Rebuilding will not pull in changes you made to the checked-in `Editor/src/Exported/*.json`. If you want the source-tree copy back, delete the staged file (for example `build/bin/Release/Exported/scene.json`) and rebuild — or just re-save from the editor, which is what you usually want.

## Packaging with CPack

Packaging is configured in the root `CMakeLists.txt`:

```cmake
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "CatSplatGame")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone game build (player + assets)")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
```

`X_VCPKG_APPLOCAL_DEPS_INSTALL` is set to `ON` before `project()`, which makes the vcpkg toolchain deploy each installed target's DLL dependency closure during `cmake --install`, exactly as it already does at build time.

Build the package with the `package` target, or:

```
cpack -G ZIP -C Release
```

### What lands in the bundle

| Item | Install rule |
| --- | --- |
| `Player.exe` (the shipping player) | `install(TARGETS PlayerShipping RUNTIME DESTINATION .)` in `Player/CMakeLists.txt` |
| `Engine.dll` | `install(TARGETS Engine RUNTIME DESTINATION .)` in `Engine/CMakeLists.txt` |
| Third-party DLLs (assimp, glfw3, zlib, …) | Deployed by vcpkg applocal-on-install — with one exception, below |
| `lua-c++.dll` (only when the Lua backend is built: `CSE_ENABLE_LUA=ON` and sol2/lua-cpp were found) | `install(FILES "$<TARGET_FILE:lua-cpp>" DESTINATION .)` in `Engine/CMakeLists.txt` — vcpkg's applocal matching does not pick this one up, because the `+` characters in the filename defeat it, so it is installed explicitly. A matching `POST_BUILD` `copy_if_different` in the same block stages it beside `Engine.dll` in the build tree for the same reason. |
| `Exported/` source-tree defaults (Model, Shaders, seed JSON) | `install(DIRECTORY Editor/src/Exported/ DESTINATION Exported)` |
| Editor-authored `Exported/` (your saved scenes and `project.json`) | `install(CODE ...)`, layered on top |

`.import` sidecars are excluded from both copies. They are editor-only metadata, like Unity's `.meta` files, and the player never reads them.

The two `Exported/` layers matter: the source-tree copy provides the baseline, and the authored copy from the runtime output directory is applied on top, so the shipped bundle actually contains what you configured in the editor rather than the checked-in defaults.

`PlayerDebug.exe`, the Editor, and the Cooker have no install rules — the package is the game, not the toolchain.

> ### Gotcha 3 — the bundle takes the authored content from the configuration you install
>
> The authored-content layer resolves its source at **install** time, from `build/bin/${CMAKE_INSTALL_CONFIG_NAME}/Exported`. `install(DIRECTORY)` resolves its source at *configure* time and cannot use `$<CONFIG>`, which is why this step is an `install(CODE)` block instead.
>
> This path used to be hardcoded to `.../bin/Release/Exported`. Installing any other configuration — RelWithDebInfo especially — found nothing there and silently shipped the source-tree defaults instead of the saved scene: a packaged game that ignored the user's authoring, with no error to explain it.
>
> **Install the same configuration you authored in.** When content is found you get:
>
> ```
> -- Bundling editor-authored content from <path>
> ```
>
> When it is not, the step no longer fails silently — it warns:
>
> ```
> No editor-authored Exported/ for configuration '<config>' (looked in <path>).
> The package ships the source-tree defaults, NOT your saved scene.
> Run the editor in this configuration and save first.
> ```
>
> If you see that warning, the ZIP is not the game you authored. Run the editor in that configuration, save the scene, and package again.

## Checklist before shipping

1. Open the scene in the editor and confirm the **Game** view renders (not "No camera in the scene.").
2. **File > Save Scene**, then **File > Set Current Scene as Player Startup** if it is not already.
3. Run `PlayerDebug.exe` from the same output directory and confirm the console prints `PLAYER: rendering from scene camera.`
4. `cpack -G ZIP -C <the configuration you just authored in>`, and check the output for `Bundling editor-authored content from ...` rather than the `No editor-authored Exported/` warning.
5. Unzip elsewhere and run `Player.exe`.

## Source reference

| File | Role |
| --- | --- |
| `Engine/src/core/SceneSerializer.h`, `.cpp` | Scene JSON read/write, versioning, load-time validation |
| `Engine/src/core/SceneLoader.h`, `.cpp` | Deferred scene swapping, the observer contract, swap results |
| `Engine/src/core/ProjectSettings.h`, `.cpp` | `Exported/project.json`, startup scene |
| `Engine/src/core/CameraDirector.h` | Camera selection and blending |
| `Engine/src/core/Components.h` | `CameraComponent`, `MinFarClipFor`, `Parent` |
| `Engine/src/physics/PhysicsInstall.h` | Shared physics install for editor and player |
| `Editor/src/EditorApplication.cpp` | File menu (new/open/save/startup scene), boot-from-scene-file, load invalidation |
| `Editor/src/panels/AssetBrowserPanel.cpp` | Scene context menu |
| `Player/src/PlayerMain.cpp` | Player boot sequence |
| `Player/CMakeLists.txt` | Player targets and the bundle's install rules |
| `Editor/CMakeLists.txt` | `runtime_assets` staging target |
| `cmake/stage_runtime_assets.cmake` | Static-vs-authored staging policy |
| `CMakeLists.txt` | CPack configuration |
