# Entities and Components

Verified: 2026-09-02 @ 78ea28b

Everything in a Cat Splat scene is an **entity**: an id with a bag of **components** attached to it. There is no `GameObject` base class — an entity *is* its components, and systems (rendering, physics, scripting, camera selection, serialization) find work by asking for entities that have a particular combination of them.

The ECS is [EnTT](https://github.com/skypjack/entt). `MyCoreEngine::Scene` owns the registry directly:

```c++
// Engine/src/core/Scene.h
class ENGINE_API Scene {
public:
    entt::registry registry;
    Entity createEntity();
    // ...
};
```

`registry` is public and you are expected to use it. The `Entity` wrapper is a convenience, not a wall.

## Creating entities

```c++
Entity Scene::createEntity();   // Engine/src/core/Scene.cpp
```

`createEntity` calls `registry.create()` and returns a wrapper bound to that handle and registry. A fresh entity has **no components at all** — not even a `Name` or a `Transform`. You add what you need:

```c++
using namespace MyCoreEngine;

Entity e = scene.createEntity();
e.addComponent<Name>(Name{ "Crate" });

Transform t{};
t.position = glm::vec3(0.f, 2.f, 0.f);
e.addComponent<Transform>(t);

if (auto model = assets.GetModel("Exported/Model/crate.obj")) {
    e.addComponent<ModelComponent>(ModelComponent{ model });
    e.addComponent<AABB>(generateAABB(*model));   // culling bounds
}
```

### The `Entity` wrapper

`Engine/src/core/Entity.h` is deliberately tiny:

```c++
Entity(entt::entity handle, entt::registry* registry);

template<typename T, typename... Args> T& addComponent(Args&&... args);
template<typename T> T& getComponent();
template<typename T> bool hasComponent() const;
operator entt::entity() const;
```

It stores a raw `entt::registry*` and implicitly converts to `entt::entity`, so you can pass an `Entity` anywhere a handle is expected.

> **Gotcha — flag components.** `addComponent<T>()` returns `T&`, but EnTT's `emplace` returns `void` for empty (tag) types like `NoShadow`. Add those through the registry instead:
> ```c++
> scene.registry.emplace<NoShadow>(e);
> ```
> (See the comment at `Editor/src/EditorApplication.cpp` in `createDefaultScene_`.)

There is no `removeComponent` on the wrapper. Use the registry: `scene.registry.remove<T>(e)`, `emplace_or_replace<T>(e, ...)`, `try_get<T>(e)`, `any_of<T>(e)`.

`Scene::ResetToDefaults()` clears every entity and restores scene-level settings. It does **not** clean up after you: stale entity handles you are holding (selection, undo history) and shadow-map state are the caller's problem.

## Component reference

Every component that exists, with its fields and defaults exactly as declared.

### Core components — `Engine/src/core/Components.h`

Five of the entries below are declared elsewhere: `ScriptComponent` in `Engine/src/script/ScriptComponent.h`, `AudioSourceComponent` and `AudioListenerComponent` in `Engine/src/audio/AudioComponents.h`, `UIDocumentComponent` in `Engine/src/ui/UIComponent.h`, and `NoShadow` in `Engine/src/core/Scene.h`. They are grouped here by role, not by header.

**`Name`**

| Field | Type | Default |
| --- | --- | --- |
| `value` | `std::string` | `"Entity"` |

Absence is meaningful: **no `Name` component means unnamed**, which is why the serializer writes no `"name"` key for such an entity and the Hierarchy shows `Unnamed [<id>]`. The editor never stores `Name{""}` — clearing the Inspector's Name field removes the component — because an empty name would read as *named* everywhere that tests for the component's presence.

**`Transform`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `position` | `glm::vec3` | `{0,0,0}` | LOCAL when parented, world otherwise |
| `rotation` | `glm::vec3` | `{0,0,0}` | Euler **degrees**, applied Y\*X\*Z |
| `scale` | `glm::vec3` | `{1,1,1}` | |
| `modelMatrix` | `glm::mat4` | identity | **always the WORLD matrix** |
| `dirty` | `bool` | `true` | see [Writing a Transform from code](#writing-a-transform-from-code) |

Helpers: `localMatrix()`, `updateMatrix()`, `getRight()`, `getUp()`, `getBackward()`, `getForward()`, `getGlobalScale()`. `getForward()` returns `-modelMatrix[2]` — the engine's identity orientation looks down −Z.

**`Parent`**

| Field | Type | Default |
| --- | --- | --- |
| `value` | `entt::entity` | `entt::null` |

The single parent link is the only source of truth for hierarchy; children lists are derived where needed.

**`ModelComponent`**

| Field | Type | Default |
| --- | --- | --- |
| `model` | `std::shared_ptr<MyCoreEngine::Model>` | null |

> **Important.** A `ModelComponent` with a null `model` is a legitimate authoring state (component added, no asset assigned yet) and must survive save/load, undo and play-stop restores. Every render path null-checks it — so must any code you write. `Scene::UpdateTransforms` does exactly this when deciding whether an entity is a shadow caster.

**`MaterialOverrides`**

| Field | Type | Default |
| --- | --- | --- |
| `byIndex` | `std::unordered_map<size_t, MyCoreEngine::MaterialHandle>` | empty |

Maps a mesh's material slot index to an override. `MaterialHandle` is `std::shared_ptr<Material>` (`Engine/src/core/Material.h`). `Scene::chooseMaterial_` looks up `mesh.MaterialIndex()` in this map and falls back to the mesh's shared material.

**`SkinnedPose`**

| Field | Type | Default |
| --- | --- | --- |
| `palette` | `std::vector<glm::mat4>` | empty |
| `valid` | `bool` | `false` |

The pose a skinned entity wears this frame — one matrix per joint, parent-first, `world × inverseBind`, exactly what `SamplePalette` produces ([Rendering](rendering.md), the skinning sections). **Derived, never authored, never saved**: the fight mode writes it from `GameState` every rendered frame, the renderer reads it, `SceneSerializer` never writes it (`SceneSerializer.SkinnedPoseIsDerivedAndNeverSaved`), and the Inspector shows it read-only with a debug-only clip scrub in edit mode. An entity whose pose is not `valid`, or whose palette is not sized for its model's skeleton, draws in its rest pose. A posed entity counts as a dynamic shadow caster every frame it carries a valid pose.

**`AABB`** (derives from `BoundingVolume`)

| Field | Type | Default |
| --- | --- | --- |
| `center` | `glm::vec3` | `{0,0,0}` |
| `extents` | `glm::vec3` | `{0,0,0}` |
| `min` | `glm::vec3` | `{0,0,0}` |
| `max` | `glm::vec3` | `{0,0,0}` |

`AABB` has **no default constructor** — build it with `AABB(min, max)`, `AABB(center, iI, iJ, iK)`, or (normally) `generateAABB(const Model&)`, which walks every mesh vertex. It is model-local; frustum tests transform it by `Transform::modelMatrix`. Without an `AABB`, an entity is not rendered at all: every draw path (forward, depth and shadow) iterates `view<ModelComponent, Transform, AABB>()`, so an entity missing the component is skipped entirely, and it is not tracked as a dynamic shadow caster either.

**`ScriptComponent`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `path` | `std::string` | `""` | script file, relative to `Exported/Scripts` |
| `enabled` | `bool` | `true` | a disabled script still loads (so errors surface) but never runs — no update, no fixed update, and no collisions |

Each entity gets its own instance with isolated globals, even when several
entities share one file. See [Lua scripting](gameplay-scripting.md#lua-presentation-and-tooling-only-never-the-simulation).

The checkbox is live during Play. Ticking it mid-session runs `OnStart` before
the first hook that instance receives, so `OnStart` keeps its promise ("once,
before the first update") no matter when the script was switched on.

**`AudioSourceComponent`** (in `namespace MyCoreEngine`)

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `clip` | `std::string` | `""` | sound file path relative to the working dir |
| `volume` | `float` | `1.0f` | 0..1 |
| `pitch` | `float` | `1.0f` | 1 = normal; also scales playback speed |
| `loop` | `bool` | `false` | |
| `spatial` | `bool` | `true` | 3D positioned vs. plain 2D |
| `playOnStart` | `bool` | `true` | begins on Play / Player boot |
| `minDistance` | `float` | `1.0f` | 3D: full volume within this radius. Must be **> 0** |
| `maxDistance` | `float` | `100.0f` | 3D: gain falls linearly from `minDistance` and reaches **zero** here |

Positioned from the entity's `Transform` (world matrix) each frame while
playing. `AudioWorld` is the only place the ECS meets audio; see the audio seam
under `Engine/src/audio/`.

3D sources use a **linear** attenuation model, so the two distances mean exactly
what they say: full volume inside `minDistance`, a straight ramp to silence at
`maxDistance`, and raising `maxDistance` makes a source carry further. (The
backend's default inverse model clamps the *distance* rather than the gain, so a
source never actually reaches zero and the control reads backwards.)

`minDistance` is clamped to at least `kMinAudibleDistance` (`0.01`, in
`AudioTypes.h`) and `maxDistance` to at least `minDistance + kMinAudibleDistance`
by the Inspector, the serializer *and* the backend. Zero is not a harmless value
here: every attenuation model divides by `minDistance`, so `0` silences the
source at every distance, with no warning.

**`AudioListenerComponent`** (in `namespace MyCoreEngine`)

An empty tag: presence marks the entity whose transform is the audio listener
(the "ears"), usually the camera. The first one found wins; with none, the host's
fallback listener is used — the rendering camera in the player, and the **Game**
camera in the editor, so Play and the shipped build hear the same mix. Add it
through the registry like other tags (`emplace` returns `void` for empty types).

**`UIDocumentComponent`** (in `namespace MyCoreEngine`)

| Field | Means |
|---|---|
| `markup` / `stylesheet` | project-relative `.cxml` / `.cstyle` paths, both hot-reloading |
| `sortOrder` | higher draws on top; ties break on entity order |
| `enabled` | off hides it and stops it consuming input |
| `interactive` | off for a decorative overlay that must not swallow clicks |
| `regionX` / `regionY` / `regionW` / `regionH` | the part of the UI surface this document occupies, as fractions (`0, 0, 1, 1` — the whole surface — by default). Normalized rather than pixels so a layout means the same thing at 1080p and 4K; layout runs at the region's size, so a sidebar's `width: 50%` is half the *sidebar*, and the document's rects are offset into place so painting, hit-testing and clipping all follow. Nonsense values are clamped rather than producing a negative or off-surface box (a zero-area region simply is not drawn), and all four serialize as a single `region` array |
| `scale` | how authored pixels become screen pixels: a `ui::UIScaleSettings` (`Engine/src/ui/UIScale.h`) of `mode` (`Constant` by default, or `ScaleWithScreen`), `reference` (`{1920, 1080}`), and `match` (`0` follows width, `1` follows height, in between blends the two in log space). `Constant` returns `1.0` before it looks at either of the others, so a document left on the default scales by exactly nothing. Computed from the **whole UI surface**, never from this document's region — a sidebar occupying a quarter of the screen must scale by how big the *screen* is, or it would shrink its own text on exactly the large display the feature exists to serve |

Attaches an in-game UI to the entity, so a scene declares its own interface
rather than the executable doing it. Driven by `UIWorld`, which every host runs
once per frame. Both paths go through the same containment gate as models,
scripts and clips — a rejected path is cleared and the component survives. See
**[In-game UI](ui.md#ui-as-scene-content)**.

**`LightComponent`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `type` | `LightType` | `Point` | `Point` or `Spot` |
| `color` | `glm::vec3` | `{1,1,1}` | linear colour |
| `intensity` | `float` | `10.0f` | much larger than the sun's, because punctual lights fall off with distance |
| `range` | `float` | `15.0f` | distance at which the light reaches zero; also the cull bound |
| `innerAngleDeg` | `float` | `20.0f` | spot only; full brightness inside this cone |
| `outerAngleDeg` | `float` | `30.0f` | spot only; zero outside. Clamped to be >= inner |
| `enabled` | `bool` | `true` | disabled lights are skipped entirely |

Position comes from the entity's `Transform`; a spot aims down its **-Z** axis.
These lights are unshadowed — the scene's sun is the only shadow caster. See
[Rendering](rendering.md) for the bounded-array behaviour when a scene has more
lights than the shader can hold.

**`CameraComponent`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `fovDeg` | `float` | `60.0f` | vertical FOV |
| `nearClip` | `float` | `0.1f` | must be > 0 |
| `farClip` | `float` | `1000.0f` | must be > `nearClip` |
| `priority` | `int` | `0` | highest enabled wins; ties go to the **lowest entity index** |
| `enabled` | `bool` | `true` | disabled cameras are never selected |

Position and orientation come from the entity's `Transform`, hierarchy included — a camera can be parented to anything. `FindActiveCamera(registry)` performs the selection; `SyncCameraFromEntity(registry, e, cam)` copies the world pose and lens into a `Camera`.

> **Important — near/far separation.** Use `MinFarClipFor(nearClip)` wherever you enforce `near < far`:
> ```c++
> inline float MinFarClipFor(float nearClip) {
>     return std::max(nearClip + 1e-3f, nearClip * 1.0001f);
> }
> ```
> A plain absolute epsilon is absorbed by float rounding above ~32k (`ULP(50000) ≈ 0.004 > 1e-3`), which lets `near == far` reach `glm::perspective` as a division by zero: NaN projection, silently black render.

**`NoShadow`** — declared in `Engine/src/core/Scene.h`, not `Components.h`. Empty tag; add it to skip an entity in shadow-map passes.

### Physics components — `Engine/src/physics/PhysicsComponents.h`

These are **pure data and backend-agnostic** by design: no `BodyId`, no native handle. The editor snapshots components wholesale for undo/redo and play-stop, and a restore resurrects entities via `registry.clear()` + `create(hint)` — a native body id stored in a component would survive that restore as a dangling value. The entity → body mapping lives only in `PhysicsWorld`.

**`RigidBody`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `type` | `BodyType` | `BodyType::Dynamic` | `Static`, `Kinematic`, `Dynamic` (`Engine/src/physics/PhysicsTypes.h`) |
| `mass` | `float` | `1.0f` | `<= 0` ⇒ backend computes it from the shape |
| `friction` | `float` | `0.5f` | |
| `restitution` | `float` | `0.0f` | bounciness |
| `linearDamping` | `float` | `0.05f` | |
| `angularDamping` | `float` | `0.05f` | |
| `isTrigger` | `bool` | `false` | reports overlaps, no collision response |
| `initialLinearVelocity` | `glm::vec3` | `{0,0,0}` | |

Needs a `Transform` **and** a collider. A `RigidBody` with no collider is skipped with a warning rather than simulated as a point.

**`BoxCollider`**

| Field | Type | Default |
| --- | --- | --- |
| `halfExtents` | `glm::vec3` | `{0.5, 0.5, 0.5}` |
| `offset` | `glm::vec3` | `{0,0,0}` |

**`SphereCollider`**

| Field | Type | Default |
| --- | --- | --- |
| `radius` | `float` | `0.5f` |
| `offset` | `glm::vec3` | `{0,0,0}` |

**`CapsuleCollider`**

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `radius` | `float` | `0.5f` | |
| `halfHeight` | `float` | `0.5f` | cylindrical part only, **excludes the caps** |
| `offset` | `glm::vec3` | `{0,0,0}` | |

**`PlaneCollider`**

| Field | Type | Default |
| --- | --- | --- |
| `offset` | `glm::vec3` | `{0,0,0}` |

Infinite horizontal plane; only meaningful on a `Static` body (ground).

> **Gotcha — one shape per entity.** If an entity carries several collider components, the first one found in the order **Box → Sphere → Capsule → Plane** wins. Multiple colliders are not compounded.

## The transform hierarchy

Two rules cover almost everything:

1. **`position` / `rotation` / `scale` are LOCAL** when the entity has a `Parent` that is valid and has a `Transform`. Otherwise they are world-space.
2. **`modelMatrix` is ALWAYS the world matrix.** The renderer, picking and culling consume it directly, so never write a local matrix into it.

`Scene::UpdateTransforms()` resolves the hierarchy root-down each frame: it derives a children adjacency map, walks from the roots, and for each node computes

```
world = parentWorld * localTRS
```

A node recomputes when **its own `dirty` flag is set OR any ancestor recomputed** — that is how children follow a moving parent. Entities caught in a `Parent` cycle are unreachable from every root and simply freeze; the editor refuses to create cycles, and the cycle handling is there to survive corrupt data.

### Helpers

```c++
// Engine/src/core/Scene.h  (namespace MyCoreEngine)
ENGINE_API bool IsSameOrDescendantOf(entt::registry& reg, entt::entity node,
                                     entt::entity ancestor);
ENGINE_API glm::mat4 ResolveWorldMatrix(entt::registry& reg, entt::entity e);
ENGINE_API void DecomposeTRS(const glm::mat4& m, glm::vec3& outPos,
                             glm::vec3& outRotDeg, glm::vec3& outScale);
ENGINE_API bool SetParentKeepWorld(entt::registry& reg, entt::entity child,
                                   entt::entity newParent);
```

`ResolveWorldMatrix` walks the `Parent` chain and multiplies **local TRS values**, not cached `modelMatrix` values — it is correct even when the caches are stale mid-frame (right after a gizmo drag, for example).

### Reparenting

Assigning `Parent` directly re-interprets the existing TRS as local to the new parent, so the entity visibly jumps. To reparent without moving anything on screen:

```c++
// keeps the child exactly where it is in the world
MyCoreEngine::SetParentKeepWorld(scene.registry, child, newParent);

// pass entt::null to make the child a root again
MyCoreEngine::SetParentKeepWorld(scene.registry, child, entt::null);
```

It returns `false` and changes nothing if the child is invalid or has no `Transform`, if the new parent is invalid or has no `Transform`, or if the link would close a cycle. On success it recomputes the local TRS via `DecomposeTRS`, sets `dirty = true`, and adds/removes the `Parent` component.

## The decompose gotcha

> **CRITICAL.** When you turn a matrix back into `Transform::position` / `rotation` / `scale`, you must use `MyCoreEngine::DecomposeTRS`. **Never use ImGuizmo's `DecomposeMatrixToComponents`.**

`Transform::localMatrix()` rebuilds rotation as `Y * X * Z`, and `DecomposeTRS` extracts with `glm::extractEulerAngleYXZ` to match — a decompose → rebuild round-trip is lossless for shear-free matrices. ImGuizmo uses a different euler order, so feeding its output into `Transform::rotation` silently re-orients any compound-rotated entity on *every* gizmo drag. The editor's gizmo path (`Editor/src/EditorApplication.cpp`) calls `DecomposeTRS` for exactly this reason.

`DecomposeTRS` assumes no shear and no negative scale.

## Writing a Transform from code

`UpdateTransforms` only revisits nodes whose `dirty` flag is set (or whose ancestor moved). If you change TRS values and forget the flag, `modelMatrix` keeps its old value and nothing on screen moves.

```c++
auto& t = scene.registry.get<Transform>(e);
t.position += glm::vec3(0.f, 1.f, 0.f);
t.dirty = true;              // <-- required, or the renderer never sees it
```

The physics write-back does the same thing after each fixed step (`Engine/src/physics/PhysicsWorld.cpp`): decompose the simulated pose into the local TRS, then set `dirty = true`.

Setting `dirty` on a parent is enough to move its whole subtree — the traversal propagates the recompute downward.

## Serialization

`MyCoreEngine::SceneSerializer` (`Engine/src/core/SceneSerializer.h` / `.cpp`) reads and writes JSON:

```c++
MyCoreEngine::SceneSerializer serializer(scene, assets);
serializer.Save("scenes/level1.scene");
serializer.Load("scenes/level1.scene");
```

`Load` parses and validates the whole file before touching the registry, so a bad file leaves the current scene intact and returns `false`. Current `kVersion` is `1`; a file whose `version` is `<= 0` or greater than `kVersion` is rejected.

### File shape

```json
{
  "version": 1,
  "settings": { "lightDir": [0.3, -1.0, 0.2], "pbrEnabled": true },
  "entities": [
    { "name": "Main Camera", "transform": { "position": [0,6,30], "rotation": [-11,0,0], "scale": [1,1,1] },
      "camera": { "fovDeg": 60.0, "nearClip": 0.1, "farClip": 1000.0, "priority": 0, "enabled": true } },
    { "name": "Crate", "parent": 0, "transform": { }, "model": "Exported/Model/crate.obj",
      "rigidBody": { "type": 2, "mass": 1.0 }, "boxCollider": { "halfExtents": [0.5,0.5,0.5], "offset": [0,0,0] } }
  ]
}
```

**One JSON key per component.** A key is written only if the component is present, and **a missing key means the component is absent** on load — there is no "default component". The mapping:

| JSON key | Component | Serialized fields |
| --- | --- | --- |
| `name` | `Name` | the string |
| `parent` | `Parent` | array index of the parent entity |
| `transform` | `Transform` | `position`, `rotation`, `scale` (matrix and `dirty` are derived) |
| `model` | `ModelComponent` | model source path; `""` = component present, no model |
| `noShadow` | `NoShadow` | `true` |
| `camera` | `CameraComponent` | `fovDeg`, `nearClip`, `farClip`, `priority`, `enabled` |
| `light` | `LightComponent` | `type`, `color`, `intensity`, `range`, `innerAngleDeg`, `outerAngleDeg`, `enabled` |
| `script` | `ScriptComponent` | `path`, `enabled` |
| `rigidBody` | `RigidBody` | `type` (int), `mass`, `friction`, `restitution`, `linearDamping`, `angularDamping`, `isTrigger`, `initialLinearVelocity` |
| `boxCollider` | `BoxCollider` | `halfExtents`, `offset` |
| `sphereCollider` | `SphereCollider` | `radius`, `offset` |
| `capsuleCollider` | `CapsuleCollider` | `radius`, `halfHeight`, `offset` |
| `planeCollider` | `PlaneCollider` | `offset` |
| `audioSource` | `AudioSourceComponent` | `clip`, `volume`, `pitch`, `loop`, `spatial`, `playOnStart`, `minDistance`, `maxDistance` |
| `audioListener` | `AudioListenerComponent` | `true` (an empty tag — presence is the whole state, like `noShadow`) |
| `uiDocument` | `UIDocumentComponent` | `markup`, `stylesheet`, `sortOrder`, `enabled`, `interactive`, `region` (a 4-element `[x, y, w, h]` array of surface fractions in `0..1`), and the three scale keys `uiScaleMode` (`0` Constant, `1` ScaleWithScreen), `uiScaleReference` (a 2-element `[w, h]`, default `1920x1080`) and `uiScaleMatch` (`0` follow width, `1` follow height, between = a **log-space** blend; default `0`). Every one of these keys is read with a default, so an absent key means "as before" and an older scene loads unchanged. Both paths are containment-checked on load |
| `materialOverrides` | `MaterialOverrides` | array of `{ slot, baseColor, emissive, metallic, roughness, ao, alphaMode, opacity, alphaCutoff, doubleSided, shadingModel, toonBands, toonSpecStrength, toonSpecSize, toonRimStrength }`; slots whose override is null are skipped, and the key is omitted entirely when nothing survives |

`AABB` is **not** serialized. It is derived data, regenerated from the model on load — and skipped entirely for models that loaded with zero meshes, whose bounds would be garbage. For a skinned model the regenerated box is its **pose bounds** (every frame of every clip), not its rest mesh. `SkinnedPose` is not serialized either, for the stronger reason above: a file that carried one would ship a pose the simulation did not produce.

### Parent links are array indices

> **Important.** Entities have no stable ids in the file. `parent` is the **index of the parent within the `entities` array**. Consequences:
>
> - Every entity occupies an object slot, even a component-less one, so indices stay aligned. A non-object array element still consumes a slot on load.
> - Entities are written in **creation order** (EnTT views iterate newest-first, so `Save` reverses them). This keeps relative entity indices stable across save/load cycles — which matters because camera priority ties are broken by lowest entity index, and that must not flip every time you save.
> - Parent links resolve in a second pass, so a child may appear before its parent. Links that would close a cycle are skipped with a `WARN::SCENE::LOAD` message.

### Values are clamped on load

Hand-edited or corrupt files are defended against rather than trusted:

- `fovDeg` clamped to `[1, 179]`; `nearClip` to at least `1e-3`; `farClip` to at least `MinFarClipFor(nearClip)`.
- `RigidBody::type` range-checked against `BodyType` (out of range ⇒ `Dynamic`); `friction` ≥ 0, `restitution` clamped to `[0,1]`, damping ≥ 0.
- `BoxCollider::halfExtents` ≥ `1e-3` per axis, `SphereCollider::radius`/`CapsuleCollider::radius` ≥ `1e-3`, `CapsuleCollider::halfHeight` ≥ `1e-4`. Zero extents degenerate every backend.
- `AudioSourceComponent::volume` clamped to `[0,1]`, `pitch` ≥ `1e-3`, `minDistance` ≥ `kMinAudibleDistance` (`0.01`) and `maxDistance` ≥ `minDistance + kMinAudibleDistance`. A `minDistance` of `0` is not merely odd: every attenuation model divides by it, so it silences the source at *every* distance.
- **Asset paths are containment-checked.** `model`, `audioSource.clip`, `uiDocument.markup` and `uiDocument.stylesheet` go through `PathIsContained` (`Engine/src/core/PathSandbox.h`) during the load itself: the rejected path is cleared and logged while the component survives, so the asset degrades gracefully instead of taking the load down. `script.path` and the environment's `hdriPath` pass the same gate later, at the point of use — `ScriptWorld::defaultResolve_` (the resolver used unless a host installs its own) and `IBLBaker::BakeFromFile`. There it reads as a failure to use the asset rather than a sanitised field: the script instance fails with `could not read script '<path>'`, and the baker refuses the file with `rejected HDRi path outside the project: '<path>'`, which the renderer logs before falling back to the procedural sky. Either way the stored path is left alone rather than cleared, so it round-trips through save/load unchanged. `PathIsContained` rejects absolute paths, drive/UNC roots and `..` in every case, so a hostile or hand-edited scene cannot reach outside the project.
- Material overrides clone the model's shared material first (so texture ids carry over), then apply the serialized scalars on top. Overrides are dropped if the model failed to load.

Because physics fields are plain engine enums and floats, a scene authored against Jolt loads under PhysX unchanged.

### After loading

A load calls `registry.clear()`, so **every entity handle you were holding is now invalid**. The editor's `loadSceneFromFile_` shows the full list of what must be reset: selection, undo history, camera-director handles, in-flight asset operations, the shadow cascades (wholesale replacement bypasses dirty tracking), the physics world's entity→body map, the script world's entity→instance map, and any audio voices still playing from the old scene.
