# Using the Editor

The Cat Splat Engine editor is the authoring tool for scenes: you place and parent entities, add components, tune rendering and physics, and press Play to run the game inside the editor. It is a Dear ImGui application built on top of the same `MyCoreEngine::Application` the shipped player uses, so what you see in the **Game** panel is what the player renders.

Source of truth for everything on this page: `Editor/src/EditorApplication.h`, `Editor/src/EditorApplication.cpp`, `Editor/src/EditorImGuiLayer.cpp`, `Editor/src/ImGuiInputMap.h`, `Editor/src/UndoHistory.h`, and `Editor/src/panels/`.

---

## The window and its panels

The editor opens a 1280x720 window titled "Cat Splat Engine" (`EditorApplication.h`). A single dockspace covers the whole window, so **every panel is dockable** — and, because multi-viewports are enabled, a panel dragged outside the main window becomes a real OS window that can live on another monitor (`EditorImGuiLayer.cpp`).

Two ImGui settings are deliberately non-default:

| Setting | Effect |
| --- | --- |
| `ConfigFlags \|= ImGuiConfigFlags_ViewportsEnable` | Panels detach into real OS windows on any monitor. |
| `ConfigWindowsMoveFromTitleBarOnly = true` | Windows move/undock only when dragged by tab or title bar. Dragging inside a panel body never yanks it around — essential for gizmo dragging in the Scene view. |

The panels are:

| Panel (window name) | Purpose |
| --- | --- |
| **Scene** | The editor "god camera" view. Gizmos, click-picking, asset drops, Play/Stop. |
| **Game** | What the game shows — rendered through the scene's camera entities. |
| **Scene Hierarchy** | Entity tree: create, delete, drag-to-parent. |
| **Inspector** | Components of the selected entity, or import settings of the selected asset. |
| **Assets** | Browser over `Exported/`: folder tree, contents, drag-to-spawn. |
| **Settings** | Three tabs — **Rendering** (quality tier, lighting/shadows, environment/IBL, post-processing, physics), **Editor** (time, input, layouts), and **Audio** (backend + master volume). Scene file I/O is in the title-bar **File** menu. |
| **Information** | Rendering statistics and the per-frame CPU breakdown. |
| **Edit** | Undo/redo buttons and the clickable command history. |
| **Asset Validation** | Output of an `AssetCooker validate` run (opens on demand). |

---

## Startup: the editor opens a scene file

The editor **does not build content in code**. At boot it reads `Exported/project.json` via `MyCoreEngine::ProjectSettings`, takes `startupScene` (default `"Exported/scene.json"`), and loads it with `SceneSerializer` (`EditorApplication.cpp`, in `Run()`).

* If the load succeeds, the status line reads `Loaded startup scene: <path>`.
* If it fails, `createDefaultScene_` builds minimal content — a **Main Camera** entity and a **Ground** plane (visual plane + `RigidBody{Static}` + `PlaneCollider`) — and the status reads `No scene at '<path>' — created a default scene`.

Either way the status string is printed to the console and shown at the top of **Settings > Scene**.

> **Why this matters.** This used to be a hardcoded demo scene built on every launch, which made the editor lie about what a scene contained: your saved file was never what you saw at startup, so authored components (physics especially) looked like they "didn't save". They saved fine — the hardcoded scene replaced them before you ever saw them. The editor and the shipped player now open the same file, so they agree by construction.

> **Gotcha: never leave a scene camera-less.** Both the Game panel and the shipped player render through a `CameraComponent`. With no usable camera the Game panel renders nothing and prints "No camera in the scene.", and the shipped player reports the problem (a console message, plus a message box in shipping builds) before dropping to a free-fly diagnostic camera. Left unreported, that fallback reads as "the build ignored my camera" — which is why it is reported, and why `New Scene` seeds a Main Camera instead of giving you a truly empty registry.

---

## Scene panel — the god camera

The **Scene** window renders the offscreen scene target and resizes it to the panel's content region each frame.

### Camera controls

The editor drives the camera itself through ImGui input (`installInput(std::make_unique<ImGuiInputMap>())` + `setInternalCameraInput(false)`), so camera control keeps working even when the Scene panel is a detached window on another monitor.

| Input | Action |
| --- | --- |
| `W` / `S`, `Up` / `Down` | Move along `MoveForward` |
| `A` / `D`, `Left` / `Right` | Move along `MoveRight` |
| Gamepad left stick | Move; right stick looks |
| Right mouse button + drag | Free look (cursor is hidden and unbounded, so one drag can turn 360°) |
| Mouse wheel | Zoom — adjusts `Camera::Zoom` (the FOV), clamped to 1–45 degrees |
| `Esc` / gamepad Back | Quit ("Quit" binding in `Application::bindDefaultInput_`) |

Default movement speed is `Camera::SPEED_DEFAULT = 20.0f` and mouse sensitivity `0.1f` (`Engine/src/core/Camera.h`). Bindings can be changed via `Application::input()`.

> **Important — the input rule.** Camera keys and `Esc` act **only when the Scene viewport is focused, hovered, or you are mid-right-button look**. Everything else (typing in a field, scrolling the Assets panel, keyboarding in a detached panel on another monitor) is treated as UI input and never moves the camera or quits the app. Text editing blocks camera keys regardless. The rule lives in the `SetUICaptureProvider` lambda in `EditorApplication::Run()`:
>
> ```c++
> const bool inViewport = viewportFocused_ || viewportHovered_ || camLooking_;
> return std::pair<bool, bool>{
>     ui_.WantTextInput() || !inViewport,
>     ui_.WantCaptureMouse() && !viewportHovered_
> };
> ```
>
> The old rule was "fly unless typing", which predates multi-viewports: with input aggregated across detached OS windows it moved the camera while you scrolled a panel on another screen.

Note that look/zoom are applied *after* this frame's scene render, so the image reflects a look one frame later. Gizmo and pick math use the current camera and stay self-consistent.

### Toolbar

`Translate` / `Rotate` / `Scale` radio buttons choose the gizmo operation, followed by **Play** (or a red **Stop** while playing) and the `(Ctrl+P)` hint.

### Transform gizmo

When the selected entity has a `Transform`, an ImGuizmo handle is drawn in `LOCAL` space using the chosen operation. Two behaviours are worth knowing:

* **Parented entities are handled correctly.** The gizmo edits a *world* matrix; a parented entity stores *local* TRS, so the result is converted through the parent's world matrix before being decomposed.
* **Decomposition uses `MyCoreEngine::DecomposeTRS`, not ImGuizmo's.** ImGuizmo's Euler convention differs from `Transform::localMatrix()`'s `Y*X*Z` rebuild, which visibly re-oriented compound-rotated entities on any drag.

One drag produces exactly one undo entry ("Move (gizmo)" / "Rotate (gizmo)" / "Scale (gizmo)"), pushed on release.

### Click-picking

A left click inside the viewport that is not on the gizmo casts a ray from the cursor and tests it against every entity with `Transform` + `AABB`, using a conservative world-space box built from the transformed local AABB corners. The nearest hit is selected; a miss deselects (`pickEntity_`). Entities without an `AABB` cannot be picked this way — select them in the Scene Hierarchy.

### Dropping assets

Dragging a model out of the **Assets** panel and dropping it on the viewport spawns it where the drag lands: a ray is cast through the drop point and intersected with the ground plane `y = 0`; if the ray does not hit it (or hits farther than 500 units), the entity lands 10 units out along the ray.

---

## Game panel — what the game shows

The **Game** window renders the scene through the *camera entities*, using its own `RenderTarget` and its own `MyCoreEngine::Renderer`. Sharing the Scene view's renderer would thrash CSM cascades between two frusta every frame.

If the panel is closed or collapsed, the whole second render is skipped.

### Camera selection and the override picker

Selection is driven by a `MyCoreEngine::CameraDirector` (`Engine/src/core/CameraDirector.h`):

* Automatically, it picks the **highest-priority enabled** `CameraComponent` that also has a `Transform`; ties break on the lowest entity **index** (not the raw handle — versions reset on scene load, so index ordering is what stays save/load-stable).
* When the winner changes, the view **blends** from the previous *output* pose to the new camera (position lerp, orientation slerp, fov/near/far lerp, smoothstep eased).

The toolbar has two controls:

| Control | Meaning |
| --- | --- |
| Camera combo | `Auto (director)` or a specific camera entity. Cameras with `enabled == false` are listed with a ` (disabled)` suffix and can still be previewed. |
| `Blend` | `defaultBlendSeconds`, 0–10 s. `0 = hard cut`. |

If the overridden camera stops being valid (deleted, or its `CameraComponent` removed), the override is dropped rather than left armed — an undo could otherwise resurrect an entity under the same handle and silently hijack the Game view.

With no usable camera the panel shows:

```
No camera in the scene.
Select an entity and use Inspector > Add Component > Camera.
```

### What is shared with the Scene view

Scene-level state (lights, materials, rendering toggles) lives on the `Scene` and is shared automatically. A few values live on the renderer and are mirrored each frame: sun direction, exposure, and the CSM enable flag. The Game renderer is forced out of yaw/pitch sun mode first (`setUseSunYawPitch(false)`) — otherwise its own yaw/pitch default would overwrite the mirrored direction and your sun edits would never reach the Game view.

> **Gotcha:** because the Game view keeps its **own** CSM state, any operation that invalidates shadows must rebuild **both** renderers. The editor does this through `forceAllCSMUpdate_()`, which calls `forceCSMUpdate()` on `renderer()` and `gameRenderer_`.

---

## Scene Hierarchy

`Editor/src/panels/SceneHierarchyPanel.cpp`. The tree is derived from `Parent` links: entities with a valid `Parent` become children, everything else is a root. Rows read `<Name> [<entity id>]`.

| Action | How |
| --- | --- |
| Create an entity | `+ Create Entity` — makes an entity with `Name{"Entity"}` + `Transform`, selects it, records "Create entity". |
| Create a child | Right-click a row > **Create Child**. Adds `Parent{clicked entity}`. |
| Delete | Right-click > **Delete Entity** (leaf) or **Delete Entity (with children)**. One undo entry restores the whole subtree. |
| Unparent | Right-click > **Unparent**, or drag the row onto the empty space below the tree. |
| Re-parent | Drag one row onto another. |

Re-parenting goes through `MyCoreEngine::SetParentKeepWorld`, which refuses cycles and missing transforms and preserves the entity's world pose. Dropping onto the entity's *current* parent is a no-op, not a re-decompose.

All tree mutations are deferred to after the walk (the tree iteration must never mutate the registry mid-walk) and are validity-checked first: a drag payload holds a handle copied at drag *start*, and undo/redo can destroy it before the drop lands.

---

## Inspector

`Editor/src/panels/InspectorPanel.cpp`. The Inspector shows **only the components actually attached** to the selected entity, each in its own collapsible header with a native ✕ to remove it, plus an **Add Component** popup for everything missing. This mirrors the ECS truth underneath: an absent component occupies zero memory, so what the panel shows is exactly what is stored.

At the top, outside any component section: the editable **Name** and a disabled `Entity ID: <n>`.

### Transform

Not removable — everything positional depends on it (the Unity convention). Position / Rotation / Scale are `DragFloat3`s. If the entity has a parent, a hint reads `Local to parent '<name>'`, because a parented entity's TRS is local.

### Camera

| Field | Widget | Notes |
| --- | --- | --- |
| `fovDeg` | Slider 20–120, `AlwaysClamp` | Default `60.0f`. Ctrl+Click typing cannot escape the range — a FOV outside (0,180) degenerates the projection. |
| `nearClip` | Drag, 0.001–100000 | Default `0.1f`. |
| `farClip` | Drag, 0.002–200000 | Default `1000.0f`. |
| `priority` | `DragInt` | Default `0`. Highest priority among enabled cameras wins. |
| `enabled` | Checkbox | Default `true`. Disabled cameras are never auto-selected. |

> **Important — the near/far invariant.** Widget bounds are **fixed constants**, and `near < far` is enforced in code right after each widget via `MinFarClipFor(nearClip)` (`Engine/src/core/Components.h`):
>
> ```c++
> inline float MinFarClipFor(float nearClip) {
>     return std::max(nearClip + 1e-3f, nearClip * 1.0001f);
> }
> ```
>
> Dynamic widget bounds can collapse to `v_min == v_max`, which this ImGui version treats as *unbounded* — clamping silently off — and a plain absolute epsilon is absorbed by float rounding at large depths, letting `near == far` reach `glm::perspective` as a division by zero (NaN projection, black render). Editing near past far pushes far along, Unity-style. Changing any lens value also refits the CSM cascades.

### Model

Shows the model's source path, or `(no model loaded — set a path or use the Assets panel)` for an empty `ModelComponent`. A path field plus a `Load` / `Replace` button assigns a model (which also refreshes the `AABB` and adds a `Transform` if missing).

* **Casts Shadows** — a checkbox over the `NoShadow` tag. Shadow casting is a property of rendered geometry, so it lives with the model.
* **Materials** — one sub-node per material slot. Slots start **(shared)** and are shown read-only: editing the shared material would silently change every entity using that model and bypass undo. Press **Make unique for this entity** to clone it into a `MaterialOverrides` entry (base colour, emissive, metallic, roughness, AO become editable), and **Revert to shared** to drop the override again. A **Textures** summary lists which maps the material has (albedo, normal, metallic, roughness, AO, emissive).

> **Gotcha:** changing the caster set without moving anything — swapping a model, removing a model, toggling **Casts Shadows** — leaves stale shadows baked into far cascades, because the normal dirty-caster flow only reacts to transforms. The Inspector reports this back to the editor (its `Draw` returns `true`), which then calls `forceAllCSMUpdate_()`.

### Rigid Body

| Field | Default | Notes |
| --- | --- | --- |
| `type` | `Dynamic` | `Static` (never moves), `Kinematic` (moved by code, pushes dynamics), `Dynamic` (fully simulated). |
| `mass` | `1.0f` | Dynamic only. `0` = let the backend compute it from the shape. |
| `initialLinearVelocity` | `(0,0,0)` | Dynamic only. |
| `linearDamping` | `0.05f` | Dynamic only. |
| `angularDamping` | `0.05f` | Dynamic only. |
| `friction` | `0.5f` | Slider 0–1. |
| `restitution` | `0.0f` | Slider 0–1. |
| `isTrigger` | `false` | |

Mass/damping/velocity are hidden on non-dynamic bodies — they mean nothing there, so the editor does not offer knobs that silently do nothing. The section warns `No collider: this body will be skipped.` when no collider component is present, and reminds you `Simulated on Play (edit mode is static).`

### Colliders

| Component | Fields | Defaults |
| --- | --- | --- |
| `BoxCollider` | `halfExtents`, `offset` | `0.5` each, `(0,0,0)`. Scaled by the entity's Transform scale. |
| `SphereCollider` | `radius`, `offset` | `0.5f`, `(0,0,0)` |
| `CapsuleCollider` | `radius`, `halfHeight`, `offset` | `0.5f`, `0.5f`, `(0,0,0)`. Y-up; half height excludes the caps. |
| `PlaneCollider` | `offset` | `(0,0,0)`. Infinite ground plane; use on a Static body. |

> **Gotcha:** colliders are effectively **mutually exclusive**. `PhysicsWorld` uses the first shape it finds (Box, Sphere, Capsule, Plane in that order), so the Add Component menu only offers colliders when the entity has none — a second one would silently do nothing.

### Audio Source

| Field | Default | Notes |
| --- | --- | --- |
| `clip` | `""` | sound file path relative to the working dir (WAV/MP3/FLAC/OGG). |
| `volume` | `1.0f` | Slider 0–1. |
| `pitch` | `1.0f` | Slider 0.25–4×; also scales playback speed. |
| `loop` | `false` | |
| `playOnStart` | `true` | begins on Play / when the shipped game boots. |
| `spatial` | `true` | 3D: attenuates with distance from the listener. 2D: constant volume (music/UI). |
| `minDistance` / `maxDistance` | `1` / `100` m | 3D only: full volume within min, silent past max. Shown only when **Spatial** is on; max is kept above min. |

The **Preview** button auditions the clip through the editor's always-on audio backend, so no Play press is needed. It plays **2D and one-shot** (so it is always audible and a stray audition self-stops even if you click away); **Stop** cuts a long one short.

### Audio Listener

A tag component (no fields): it marks the entity whose transform is the audio "ears". The first listener in the scene wins; with none, the render camera is used.

### Add Component

The popup lists only what the entity is missing: `Name`, `Transform`, `Model`, `Camera`, `Light`, `Script`, `Audio Source`, `Audio Listener`, `Rigid Body`, and the four colliders. Every component that needs a transform adds one if it is absent. When nothing is left it shows `(all components added)`.

### Asset view

Clicking a file in the **Assets** panel hands the Inspector over to an asset view (`InspectorPanel::DrawAsset`) showing name, kind (Model / Texture / Scene / Shader), file size, relative path, and a **Copy Path** button. Textures additionally get **Import Settings** with a `Max Dimension` combo (`Unlimited`, 512, 1024, 2048, 4096) written to a sidecar; a hand-edited sidecar with some other value is shown honestly as `<n> (custom)` rather than masquerading as Unlimited. Other asset kinds show `No import settings for this asset type yet.`

Arbitration rule: an asset click gives the Inspector to the asset view, but the **entity selection itself is never cleared** — "Assign to Selected Entity" depends on it surviving. An entity newly selected this frame (hierarchy click, viewport pick, spawn landing) takes the Inspector back. Last selection wins.

---

## Assets

`Editor/src/panels/AssetBrowserPanel.cpp`. A view over the engine-owned `MyCoreEngine::AssetIndex`, rooted at `Exported`. All disk walking and rescan throttling live in the index; the panel never touches the filesystem itself.

Layout: toolbar with `Refresh` and `Validate`, then clickable breadcrumbs; below that a folder tree on the left, a draggable splitter, and the selected folder's contents on the right. Rows are tagged `[DIR] [MDL] [SCN] [TEX] [SHD] [ - ]`.

| Item | Interaction |
| --- | --- |
| Folder | Single-click in the tree selects it; double-click in the contents pane drills in. |
| Model | Double-click spawns it in front of the camera; drag onto the Scene viewport spawns it where the drop lands; right-click for **Spawn in Scene**, **Assign to Selected Entity**, **Copy Path**. |
| Scene `.json` | Double-click loads it; right-click for **Load Scene**, **Set as Startup Scene**, **Copy Path**. Loading is disabled during Play. |
| Any other file | Right-click > **Copy Path**. |

Model loads are asynchronous: spawn/assign *request* the model and return immediately, and the toolbar shows `loading N model(s)...` while requests are in flight. The entity appears (undo-recorded) when the decode lands, at the position or target captured at request time. Cache hits complete inline in the same frame.

> **Gotcha — asset ops across Play.** The pending-op poll is gated off during Play. An op requested in edit mode that lands mid-play is deferred and applied to the *restored* edit scene afterwards. An op requested **during** Play is dropped at Stop — its intent died with the session.

**Validate** launches `AssetCooker.exe validate Exported` as a child process (crash and hang isolation: hostile assets take down the cooker, not the editor) and streams its stdout into the **Asset Validation** window, which reopens with the report and an exit-code line when the run finishes.

---

## Scene file operations (File menu)

Scene file I/O lives in the **File** menu on the custom title bar (Unity-style),
not in Settings: **New Scene**, **Open Scene…**, **Save Scene** (`Ctrl+S`),
**Save Scene As…**, **Save All** (`Ctrl+Shift+S`, scene + editor layout), and
**Set Current Scene as Player Startup** (writes `Exported/project.json`, the file
the Player reads at boot). All the scene-changing items are disabled during Play.
The **Edit** menu holds Undo/Redo (with the entry labels) and Clear History; the
**Window** menu toggles each panel's visibility.

**New Scene** re-seeds a default Main Camera + Ground and clears the selection,
undo history, Game-view director, in-flight model ops, and physics world, then
forces a CSM rebuild. Loading a scene (File menu or the Assets panel) does the
same clearing through `loadSceneFromFile_`.

> **Important:** wholesale scene replacement bypasses the departure-sphere
> dirty-caster flow. Without the forced rebuild, the old scene's shadows stay
> baked in cascades the new content never touches.

## Settings

A dockable window split into three tabs — **Rendering** (everything visual),
**Editor** (the tool itself), and **Audio** (the global mix).

### Rendering tab

**Quality** — a `Low` / `Medium` / `High` / `Custom` preset combo at the top.
Picking a tier calls `Renderer::ApplyQualityTier`, which fans out across AA, mesh
LOD + distance, projected-size culling, the depth pre-pass, shadow
cascades/resolution, and bloom. `Custom` leaves the individual controls below
untouched. See **[Post-processing & Quality Tiers](post-processing.md)**.

**Lighting** (collapsing header) merges the sun and the scene's direct light:

- *Direct Light*: `Dir` (drag), `Color`, `Intensity` (0–10).
- *Sun & Shadows*: a `Rotate Sun (Yaw/Pitch)` toggle (yaw −180–180 / pitch −89–89) or a direct `Sun dir` drag, plus `Use Sun Dir for Shading Light`; then the **Cascaded Shadows** controls — `CSM Enabled`, `Cascades` (1–4), `Base Resolution` (512–4096), `Split Lambda`, `Max Shadow Distance`, padding/margin/stability epsilons, `Update Budget`, dynamic-caster interval, acne biases + `Cull Front Faces`, per-cascade `PCF Radius`, **Force Rebuild CSM**, and a **CSM Debug** mode picker.

**Environment** — the sky/IBL source: a `Procedural sky` vs `HDRi file` picker
(an unreadable HDRi path falls back to the procedural sky, reported inline),
`Enable IBL`, `IBL Intensity` (0–4), `Exposure`, `Draw skybox`, `Sky brightness`,
and the procedural sky colours when that source is chosen.

**Post & Toggles** — anti-aliasing (FXAA), VSync, instancing, depth prepass, mesh
LOD + distance, and the projected-size **Cull tiny objects** control, plus a
**Post-process** tree with **Bloom**, **Ink outline**, **Colour grade**, and
**Vignette** (each with its own sliders when enabled). A **Physics** section
follows: a **Backend** combo (only what this build registered), `Gravity`, a live
`Bodies: N` count, and a warning for collider-less bodies; switching backend is
refused during Play.

> **Tip from the code:** the projected-size cull is the lever that actually
> speeds up wide/bird's-eye views — those are vertex/instance-bound, and
> shadows/fill are effectively free there.

> The old scene-wide **Materials** sliders were removed: they were overridden
> per-draw by each material's own values. Materials — including **PBR/Toon
> shading** — are authored per-object in the Inspector's Model section.

### Editor tab

**Time** — `Paused`, `Time Scale` (0–4), `Fixed Tick (Hz)` (15–240); gameplay
time only (editor camera ignores pause/time scale). **Input** — read-only
gamepad + axis diagnostics. **Layouts** — save/load named `.ini` window layouts.

### Audio tab

Shows the active **Backend** (`Miniaudio`, or `Null` when no device could be
opened — headless / no sound card, in which case everything runs but stays
silent) and a **Master volume** slider (0–1) scaling the whole mix. The change
applies live and is saved to `Exported/project.json` once the drag settles, so
the shipped player boots at the same volume. Per-source volume is separate — it
lives on the Audio Source component.

### Layouts

Type a name and press **Save Layout** to write `Layouts/<name>.ini` (the name is filtered to alphanumerics, `-`, `_`, and spaces). Saved layouts are listed with **Load** and **Delete** buttons. The session layout keeps auto-saving to `imgui.ini` on top of named ones.

> **Note:** loading a layout is deferred to *between* frames. `LoadIniSettingsFromDisk` re-applies settings to live windows through the settings handlers, which must run outside `NewFrame`/`Render`.

---

## Information

An auto-resizing window with a **Rendering Stats** header:

```
dt: 16.700 ms (59.9 FPS)
GPU: <GL_RENDERER string>
  3D submit:  x.xx ms
  editor UI:  x.xx ms
  swap/wait:  x.xx ms  (vsync ON)
Cascades: N, res: M
Draws / Instanced draws / Instances
Texture binds / VAO binds
Built items / Culled (frustum) / Culled (size) / Submitted
LOD 0/1/2
GPU draw calls
```

Two lines earn their place:

* **GPU** — the `GL_RENDERER` string, queried once. A hybrid laptop silently running on the Intel iGPU is roughly 4–5× slower than the dGPU, and this line is the fastest way to spot that.
* **The 3D submit / editor UI / swap breakdown** — which *third* of the frame is slow. GL is asynchronous, so 3D submission is usually small and the GPU wait (plus any vsync block) lands in **swap/wait**. A large **editor UI** number means the panels own the frame, not the renderer.

The panel deliberately reads the **Scene** view's render stats and is drawn *before* the Game view renders and overwrites them.

---

## Edit — undo and redo

The **Edit** window (`DrawEditHistory`) has **Undo** / **Redo** buttons, the `Ctrl+Z / Ctrl+Y` hint, and the full command list. Clicking a row rewinds or replays history to just after that entry; rows past the cursor are dimmed. `(initial state)` at the top rewinds everything. History is capped at `UndoHistory::kMaxEntries = 100`.

Keyboard: `Ctrl+Z` undo, `Ctrl+Shift+Z` and `Ctrl+Y` redo. They are deliberately inert in several situations:

* while typing in a text field (ImGui's own text-edit undo owns `Ctrl+Z` there),
* while a gizmo drag or a tracked widget edit is in flight (rewinding mid-manipulation corrupts history — ImGuizmo would stomp the undone transform from its drag-start anchors),
* while a drag-drop payload is in flight (undo could destroy the entity whose handle is mid-flight),
* while a popup or modal is open,
* during Play.

Continuous edits coalesce: a whole gizmo drag or `DragFloat` interaction becomes one entry, and edits that end up changing nothing are dropped. There is also a liveness protocol — `tickFrame` commits any edit whose widget stopped being submitted (entity deselected while a field was focused, dock tab switched, header collapsed), because its deactivation event can never be observed.

> **Gotcha:** undo/redo restore snapshots that overwrite transform matrices wholesale, so the departure pose never reaches the dirty-caster flow. Every undo, redo, and history jump therefore forces a full CSM rebuild on both renderers.

---

## Play mode

Press **Play** in the Scene toolbar or **Ctrl+P**. The toolbar turns into a red **Stop** button with the banner:

```
PLAYING — entity changes revert on Stop
```

(`PLAYING (paused) — ...` when the Time section's Paused box is ticked.)

What Play does (`startPlay_`):

1. cancels any half-open widget edit,
2. **snapshots the whole registry** into `playSnapshot_`,
3. disables undo recording,
4. resets the game clock so every session's first tick is deterministic,
5. builds native physics bodies from the **current edit-mode poses**, so play starts from exactly what you see,
6. enables the gameplay hooks (`FixedUpdate` / `Update`), which are off in edit mode.

What Stop does (`stopPlay_`): disables gameplay, destroys every physics body *before* the restore, restores the snapshot, re-enables undo recording, cuts the Game view's director back to the edit-mode camera, drops play-requested asset ops, and forces a CSM rebuild.

> ### IMPORTANT — Play snapshots, Stop restores
>
> **Anything you change to an entity during Play is discarded when you press Stop.** Play captures the registry and Stop rebuilds it from that capture, under the original entity handles. Moving something, editing a component, or spawning an entity mid-play does not survive. Undo cannot recover it either — play-mode changes are *discarded*, not undone, which is exactly why undo/redo is disabled while playing.
>
> The snapshot is a **closed list** of components (`EntitySnapshot` in `Editor/src/UndoHistory.h`): a component not tracked there is removed by the restore and never resurrected. Today it covers `Name`, `Transform`, `ModelComponent` (by asset path, presence tracked separately so an empty component survives), `AABB`, `MaterialOverrides`, `NoShadow`, `Parent`, `CameraComponent`, `RigidBody`, and the four colliders.
>
> Scene-*level* settings are **outside** the snapshot and do stick: lights, materials, rendering toggles, sun/shadow settings. Only registry contents revert.
>
> Because handles are restored, your selection and the entire undo history stay valid across a play session. The selection only drops if you had a play-created entity selected at Stop.

Physics bodies exist only for the duration of a play session, so the solver never disturbs your edit-mode poses. Physics steps on the fixed tick as a *subscriber*, so a game's own `Application::SetFixedUpdate` hook still composes with it.

> **Gotcha:** the `Ctrl+P` handler shares the same gates as undo/redo. It is deliberately blocked while a drag is in flight — toggling play mid-drag would snapshot/restore around a half-applied manipulation and leak play-pose transforms into the edit scene and history — and while a modal popup is open, since starting play with a modal up would soft-lock it (modal buttons inherit the play-mode disabled flag, modals cannot be Escape-closed, and the modal blocks clicking Stop).

---

## A typical session

1. The editor opens your startup scene. Check **Settings > Scene** for the boot status if you are unsure what loaded.
2. Frame the shot in the **Scene** panel: hold RMB to look, `WASD` to fly, wheel to zoom. Remember the viewport must be focused or hovered.
3. Drag a model from **Assets** onto the viewport, or `+ Create Entity` in **Scene Hierarchy**.
4. Position it with the gizmo, or type exact numbers into the **Inspector**'s Transform.
5. Add a `Rigid Body` and one collider from **Add Component**. Watch for the `No collider: this body will be skipped.` warning.
6. Give the scene a camera if it has none, and check the framing in the **Game** panel.
7. **Ctrl+P** to play, **Ctrl+P** again to stop — and remember Stop throws away everything the session did to entities.
8. **Save Scene** in **Settings > Scene**, then **Set Current File as Startup Scene** so the player boots into it.
