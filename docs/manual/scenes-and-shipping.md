# Scenes and Shipping a Build

A scene is a JSON file describing every entity in your level plus the scene-level lighting and shading settings. The editor writes it, the player reads it, and the packaging step copies it into the shipped bundle. This page covers the file format, saving and loading, choosing the startup scene, what the standalone player does at boot, and how to produce a distributable ZIP with CPack.

Three traps in this pipeline have each cost real debugging time. They are called out as **Gotcha** notes below — read them before you ship anything.

## The scene file

Scenes are plain JSON written with `nlohmann::json` at two-space indent. The top level is an object with three keys:

| Key | Contents |
| --- | --- |
| `version` | Integer format version. Written from `SceneSerializer::kVersion` (currently `1`). |
| `settings` | Scene-level lighting and shading state — `lightDir`, `lightColor`, `lightIntensity`, `pbrEnabled`, `normalMapEnabled`, `instancingEnabled`, `metallic`, `roughness`, `ao`, `metallicMapEnabled`, `roughnessMapEnabled`, `aoMapEnabled`, `iblEnabled`, `iblIntensity`, `lodEnabled`, `lodDistanceScale`, `depthPrepass`. |
| `entities` | Array of entity objects, in creation order. |

Each entity object carries only the components that entity actually has: `name`, `parent`, `transform`, `model`, `noShadow`, `camera`, `rigidBody`, one of `boxCollider` / `sphereCollider` / `capsuleCollider` / `planeCollider`, and `materialOverrides`. See `Engine/src/core/SceneSerializer.cpp` for the exact per-component field lists.

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

**Important:** `Load` parses and validates the entire file *before* touching the registry. A bad file returns `false` and leaves your current scene completely intact — you never lose work to a corrupt or newer-format scene.

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

Scene persistence lives in the **Settings** window, under the **Scene** collapsing header (`EditorApplication::DrawScenePersistence` in `Editor/src/EditorApplication.cpp`).

| Control | Effect |
| --- | --- |
| **Scene file** | Path used by Save and Load. Defaults to `Exported/scene.json`. |
| **New Scene** | Confirmation popup, then replaces the scene with a Main Camera plus a ground plane. |
| **Save Scene** | `SceneSerializer::Save` to the Scene file path. |
| **Load Scene** | `SceneSerializer::Load`, plus the editor-side invalidation described below. |
| **Set Current File as Startup Scene** | Writes the path into `Exported/project.json`. |

New / Save / Load are disabled while in Play mode. Saving mid-play would persist transient play state, and loading would be overwritten by Stop's snapshot restore anyway.

You can also act on a scene from the Asset Browser: double-click a `.json` scene to load it, or right-click for **Load Scene**, **Set as Startup Scene**, and **Copy Path**. Loading is blocked during play there too; setting the startup scene is safe at any time.

**New Scene is deliberately never empty.** It seeds a `Main Camera` and a ground plane with a `PlaneCollider`, because a camera-less scene leaves the Game view with nothing to render and drops the shipped player onto a free-fly diagnostic camera. Both report the problem now, but the fix is always to give the scene a camera.

### What a load invalidates

`EditorApplication::loadSceneFromFile_` does more than call the serializer, and if you drive loading yourself you need the same steps: every entity handle from the old scene is dead. It clears the selection, clears the undo history (undoing a pre-load entry would resurrect ghosts into the loaded scene), resets the Game view's `CameraDirector`, drops in-flight async model ops, force-updates all CSM cascades (otherwise the old scene's shadows stay baked into cascades the new content never touches), and clears the `PhysicsWorld`.

### The editor boots from the scene file

The editor loads the same startup scene the player ships with — boot content comes from the scene file, never from code. This used to build a hardcoded demo grid at launch, which made the editor lie about what a scene contained: authored components (physics especially) looked like they "didn't save" when in fact the hardcoded scene had replaced them before you ever saw them.

If the startup scene fails to load, the editor falls back to `createDefaultScene_`. Either way the outcome is shown as `bootStatus_` at the top of the Scene section, so "why am I looking at this scene?" is answerable without reading the console.

## The startup scene and project.json

Which scene the player boots is stored in `Exported/project.json`, read and written through `ProjectSettings` (`Engine/src/core/ProjectSettings.h`):

```c++
struct ENGINE_API ProjectSettings {
    std::string startupScene = "Exported/scene.json";

    static const char* DefaultPath() { return "Exported/project.json"; }

    bool Load(const std::string& path = DefaultPath());
    bool Save(const std::string& path = DefaultPath()) const;
};
```

`Load` treats a missing file as success — the defaults simply stand. Malformed JSON logs to `stderr` and returns `false`, again keeping the defaults.

To set it: put the path in the **Scene file** box and press **Set Current File as Startup Scene**, or right-click the scene in the Asset Browser and choose **Set as Startup Scene**. The status line confirms `Saved to Exported/project.json (ships with the game)`. The editor loads existing settings before rewriting the file, so the fields `ProjectSettings` models are preserved. `Save` rewrites project.json from scratch, so any keys the struct does not model are dropped.

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
2. **Pick the scene path.** Command line wins, then project settings: `Player.exe path/to/scene.json` overrides `ProjectSettings::startupScene`.
3. **Load the scene** with `SceneSerializer`. A failure is fatal and the message tells you to save one from the editor or pass a path.
4. **`scene.UpdateTransforms()` before building physics.** A freshly loaded scene has dirty Transforms whose cached world matrices are still identity, and bodies are built from world poses. Building first put the ground (authored at `y = -3`, scaled 300×) at the origin as a 1×1 box, so everything fell straight past it. `PhysicsWorld::Build` is now robust to this on its own, but the ordering is still the honest way to express it.
5. **`InstallPhysics(*this, scene, physics_)` then `physics_.Build(scene.registry)`.** The player is always "playing" — ticks run from frame one, since only the editor gates gameplay — so there is no Play transition to build bodies on.
6. **`setRenderFromSceneCamera(true)`** so the view comes from the scene's camera entity through the same `CameraDirector` selection and blending as the editor's Game view.
7. **`scene.UpdateTransforms()` again**, then `FindActiveCamera(scene.registry)`. If a camera is found, `setInternalCameraInput(false)` turns the engine's built-in WASD/mouse-look OFF, so gameplay is the only thing that can move the view. A shipped game must not hand the player a debug fly-camera.
8. `RunLoop(scene, shader)` — ESC or closing the window exits.

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

- **Static assets** (`Model/`, `Shaders/`) are owned by the source tree and re-copied every build, so shader and model edits show up.
- **Authored files** (`*.json` — anything the editor writes at runtime into the same directory) are seeded **only when missing**.

There is exactly one staging target, `runtime_assets`, defined in `Editor/CMakeLists.txt` and depended on by `Editor`, `PlayerDebug`, and `PlayerShipping`. Concurrent copies into the same directory race under Ninja, so one writer is the rule.

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
| Third-party DLLs (assimp, glfw3, zlib, …) | Deployed by vcpkg applocal-on-install |
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
2. **Save Scene**, then **Set Current File as Startup Scene** if it is not already.
3. Run `PlayerDebug.exe` from the same output directory and confirm the console prints `PLAYER: rendering from scene camera.`
4. `cpack -G ZIP -C <the configuration you just authored in>`, and check the output for `Bundling editor-authored content from ...` rather than the `No editor-authored Exported/` warning.
5. Unzip elsewhere and run `Player.exe`.

## Source reference

| File | Role |
| --- | --- |
| `Engine/src/core/SceneSerializer.h`, `.cpp` | Scene JSON read/write, versioning, load-time validation |
| `Engine/src/core/ProjectSettings.h`, `.cpp` | `Exported/project.json`, startup scene |
| `Engine/src/core/CameraDirector.h` | Camera selection and blending |
| `Engine/src/core/Components.h` | `CameraComponent`, `MinFarClipFor`, `Parent` |
| `Engine/src/physics/PhysicsInstall.h` | Shared physics install for editor and player |
| `Editor/src/EditorApplication.cpp` | Scene panel, Build Settings, boot-from-scene-file, load invalidation |
| `Editor/src/panels/AssetBrowserPanel.cpp` | Scene context menu |
| `Player/src/PlayerMain.cpp` | Player boot sequence |
| `Player/CMakeLists.txt` | Player targets and the bundle's install rules |
| `Editor/CMakeLists.txt` | `runtime_assets` staging target |
| `cmake/stage_runtime_assets.cmake` | Static-vs-authored staging policy |
| `CMakeLists.txt` | CPack configuration |
