# Entities and Components

Everything in a Cat Splat scene is an **entity**: an id with a bag of **components** attached to it. There is no `GameObject` base class and no scripting component — an entity *is* its components, and systems (rendering, physics, camera selection, serialization) find work by asking for entities that have a particular combination of them.

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

**`Name`**

| Field | Type | Default |
| --- | --- | --- |
| `value` | `std::string` | `"Entity"` |

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

**`AABB`** (derives from `BoundingVolume`)

| Field | Type | Default |
| --- | --- | --- |
| `center` | `glm::vec3` | `{0,0,0}` |
| `extents` | `glm::vec3` | `{0,0,0}` |
| `min` | `glm::vec3` | `{0,0,0}` |
| `max` | `glm::vec3` | `{0,0,0}` |

`AABB` has **no default constructor** — build it with `AABB(min, max)`, `AABB(center, iI, iJ, iK)`, or (normally) `generateAABB(const Model&)`, which walks every mesh vertex. It is model-local; frustum tests transform it by `Transform::modelMatrix`. Without an `AABB`, an entity is not rendered at all: every draw path (forward, depth and shadow) iterates `view<ModelComponent, Transform, AABB>()`, so an entity missing the component is skipped entirely, and it is not tracked as a dynamic shadow caster either.

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
| `rigidBody` | `RigidBody` | `type` (int), `mass`, `friction`, `restitution`, `linearDamping`, `angularDamping`, `isTrigger`, `initialLinearVelocity` |
| `boxCollider` | `BoxCollider` | `halfExtents`, `offset` |
| `sphereCollider` | `SphereCollider` | `radius`, `offset` |
| `capsuleCollider` | `CapsuleCollider` | `radius`, `halfHeight`, `offset` |
| `planeCollider` | `PlaneCollider` | `offset` |
| `materialOverrides` | `MaterialOverrides` | array of `{ slot, baseColor, emissive, metallic, roughness, ao }` |

`AABB` is **not** serialized. It is derived data, regenerated from the model on load — and skipped entirely for models that loaded with zero meshes, whose bounds would be garbage.

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
- Material overrides clone the model's shared material first (so texture ids carry over), then apply the serialized scalars on top. Overrides are dropped if the model failed to load.

Because physics fields are plain engine enums and floats, a scene authored against Jolt loads under PhysX unchanged.

### After loading

A load calls `registry.clear()`, so **every entity handle you were holding is now invalid**. The editor's `loadSceneFromFile_` shows the full list of what must be reset: selection, undo history, camera-director handles, in-flight asset operations, the shadow cascades (wholesale replacement bypasses dirty tracking), and the physics world's entity→body map.
