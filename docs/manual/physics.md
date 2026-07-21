# Physics

Cat Splat Engine simulates rigid bodies through a **backend-agnostic seam**. You author physics with a small set of ECS components (`RigidBody` plus one collider), and the actual solving is done by whichever physics library the build selected — Jolt, PhysX, or the built-in dependency-free "Simple" backend. Your scene data, your gameplay code, and the editor UI never mention a library type, so you can switch engines at runtime and compare results.

This page covers how the seam works, how to set up simulated objects, how to react to collisions and triggers, how to query the world with raycasts, and how to plug in a new physics library.

Source: `Engine/src/physics/`. Everything below is exported through `Engine/include/Engine.h`.

---

## The backend seam

### `IPhysicsBackend`

Every physics library implements exactly one interface, `IPhysicsBackend` (`Engine/src/physics/IPhysicsBackend.h`). Nothing outside a backend's own `.cpp` ever sees a library type — `<Jolt/...>` and `<PxPhysicsAPI.h>` are each included by exactly one translation unit, hidden behind a pimpl in `JoltPhysicsBackend.cpp` / `PhysXPhysicsBackend.cpp`.

The vocabulary the rest of the engine speaks lives in `Engine/src/physics/PhysicsTypes.h`: `BodyId`, `BodyType`, `ShapeType`, `ShapeDesc`, `MaterialDesc`, `BodyDesc`, `BodyState`, `RayHit`, `ContactEvent`, `PhysicsSettings`. That header is deliberately library-free.

Two contract points from the interface's own comments matter to users:

- `step()` is called from the **fixed tick** with a constant `dt`. Backends must not substitute their own variable timestep — determinism is the point.
- `BodyId` values are **opaque and backend-specific** (Jolt body ids and PhysX actor pointers are both stuffed into the same `uint64_t`). Never persist one to disk, and never compare ids issued by different backends.

### One active backend per world

`PhysicsWorld` (`Engine/src/physics/PhysicsWorld.h`) owns the active backend *and* the entity ↔ body mapping. It is the only place the ECS meets physics.

```c++
bool SetBackend(const std::string& name, const PhysicsSettings& settings = {});
const std::string& BackendName() const;
bool HasBackend() const;
```

`SetBackend` tears the old world down first, so two SDKs never hold bodies at the same time. If the name is unknown or the backend fails to initialize, it returns `false` and leaves the world **empty rather than half-built** — a state `tests/test_physics.cpp` asserts explicitly (`SwitchingBackendsTearsDownCleanly`).

### The registry

`PhysicsBackendRegistry` (`Engine/src/physics/PhysicsBackendRegistry.h`) is a name → factory map:

```c++
using Factory = std::function<std::unique_ptr<IPhysicsBackend>()>;

static void Register(std::string name, Factory factory);
static std::unique_ptr<IPhysicsBackend> Create(const std::string& name);
static bool IsRegistered(const std::string& name);
static std::vector<std::string> Available();   // registration order, for the UI
static void Clear();                           // tests
```

Registration is **explicit**, via `RegisterBuiltinPhysicsBackends()`, not via static initializers — self-registering statics inside a DLL are fragile, because the linker may drop an object file nothing references and the backend silently disappears from the list. The call is idempotent, so both the editor and the player can call it. Later registrations under the same name replace earlier ones, which is how a game can override a built-in backend with its own build.

### The three built-in backends

| Name | Availability | Triggers | Contact events | Notes |
|---|---|---|---|---|
| `Simple` | always | **no** (`supportsTriggers()` returns `false`) | yes | Dependency-free reference simulator |
| `Jolt` | when `CSE_WITH_JOLT` is defined | yes | yes | Full solver |
| `PhysX` | when `CSE_WITH_PHYSX` is defined | yes | yes | Full solver |

**Simple** is a real (if minimal) simulator, not a no-op: semi-implicit Euler with gravity and damping, plus resting contact against static `Plane` and `Box` bodies. Its documented limitations are *not* bugs: no dynamic-vs-dynamic collision, no rotation from collision response, no continuous detection, and no triggers. It exists so the engine still runs in a build with neither SDK, and so the conformance suite has a reference to check Jolt and PhysX against. Use Jolt or PhysX for real gameplay.

### How the default is chosen

`DefaultPhysicsBackendName()` prefers a real engine when one is compiled in:

```c++
#ifdef CSE_WITH_JOLT
    return "Jolt";
#elif defined(CSE_WITH_PHYSX)
    return "PhysX";
#else
    return "Simple";
#endif
```

`InstallPhysics` (`Engine/src/physics/PhysicsInstall.h`) is the one-call setup shared by both hosts so "it works in Play" and "it works in the shipped game" can never drift apart:

```c++
inline Application::TickHandle InstallPhysics(Application& app, Scene& scene,
                                             PhysicsWorld& world,
                                             const std::string& backendName = {},
                                             const PhysicsSettings& settings = {});
```

It registers the built-ins, selects `backendName` (falling back to the default, then to `"Simple"`, so a bad name can never leave the app without physics), and subscribes the fixed-tick step.

> **Important:** `InstallPhysics` uses `Application::AddFixedUpdate` — a *subscriber* — never `SetFixedUpdate`. The single primary fixed-update slot is reserved for your game's own gameplay hook; taking it would silently replace your logic. Subscribers run after the primary slot, so gameplay applies forces on a tick and the simulation integrates them in the same tick.

### Selecting a backend in the editor

The editor's **Settings** panel has a *Physics* section (`Editor/src/EditorApplication.cpp`) with:

- a **Backend** combo populated from `PhysicsBackendRegistry::Available()` — the picker is honest, so a build without an SDK simply does not offer it;
- **Gravity**, wired to `PhysicsWorld::SetGravity` / `Gravity()`;
- a live **Bodies** count;
- a warning when `SkippedEntities()` is non-empty;
- a status line, plus `"Bodies build on Play."` / `"Simulating."`.

Switching backends is **refused mid-play** (`"Stop play before switching backend."`): bodies would vanish and every simulated pose would snap back.

---

## Components

Physics components are **pure data and completely backend-agnostic** — no `BodyId`, no native handle (`Engine/src/physics/PhysicsComponents.h`). This is deliberate: the editor snapshots components wholesale for undo/redo and play-stop, and a restore resurrects entities via `reg.clear()` + `create(hint)`. A native body id stored in a component would survive that restore as a dangling value pointing at a body the backend already destroyed. The entity → body mapping therefore lives only in `PhysicsWorld`.

### `RigidBody`

Marks an entity as simulated. Requires a `Transform` and a collider component.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `type` | `BodyType` | `Dynamic` | Static / Kinematic / Dynamic |
| `mass` | `float` | `1.0f` | `<= 0` ⇒ backend computes it from the shape |
| `friction` | `float` | `0.5f` | |
| `restitution` | `float` | `0.0f` | Bounciness |
| `linearDamping` | `float` | `0.05f` | |
| `angularDamping` | `float` | `0.05f` | |
| `isTrigger` | `bool` | `false` | Reports overlaps, no collision response |
| `initialLinearVelocity` | `glm::vec3` | `{0,0,0}` | Applied at build time |

In the Inspector, mass / damping / initial velocity are only shown for `Dynamic` bodies — those knobs mean nothing on a static body, so the UI does not offer controls that silently do nothing.

### The four colliders

An entity uses the **first** collider found in this order: `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `PlaneCollider`. Multiple colliders on one entity are **not compounded** — the editor's Add Component menu enforces this by only offering a collider when the entity has none.

| Component | Fields | Defaults |
|---|---|---|
| `BoxCollider` | `glm::vec3 halfExtents`, `glm::vec3 offset` | `{0.5,0.5,0.5}`, `{0,0,0}` |
| `SphereCollider` | `float radius`, `glm::vec3 offset` | `0.5f`, `{0,0,0}` |
| `CapsuleCollider` | `float radius`, `float halfHeight`, `glm::vec3 offset` | `0.5f`, `0.5f`, `{0,0,0}` |
| `PlaneCollider` | `glm::vec3 offset` | `{0,0,0}` |

`CapsuleCollider::halfHeight` is the **cylindrical part only** and excludes the caps; capsules are Y-up. (The PhysX backend rotates its shape at creation time because PhysX capsules are X-aligned by default — every backend here presents the same Y-up convention.)

`PlaneCollider` is an infinite horizontal plane and is only meaningful on a `Static` body. Jolt has no infinite-plane primitive, so the Jolt backend substitutes a very large, very thin static box positioned so its *top* sits at the requested height.

All of these round-trip through the scene file under the JSON keys `rigidBody`, `boxCollider`, `sphereCollider`, `capsuleCollider`, `planeCollider` (`Engine/src/core/SceneSerializer.cpp`).

> **Gotcha — a RigidBody with no collider is skipped.** It has no volume; simulating it would be an invisible point mass. `PhysicsWorld::Build` records the entity in `SkippedEntities()` instead of silently doing nothing, the Inspector shows `"No collider: this body will be skipped."` on the body, and Settings shows `"N body(s) skipped - no collider"`. The same list also collects entities whose shape the active backend does not support, and bodies the backend refused to create.

> **Gotcha — colliders are scaled by the entity's Transform scale.** Shapes are authored in local units but simulate in world space, so `Build` bakes the entity's world scale into the shape: box half-extents multiply component-wise; a sphere's radius multiplies by the largest of the three scale axes; a capsule's radius multiplies by `max(scale.x, scale.z)` and its half-height by `scale.y`; `Plane` is unaffected. The collider `offset` is scaled too. A 300× scaled ground box really is 300× wide in the simulation.

---

## Body types and which way transforms flow

`BodyType` decides the direction data travels each fixed step (`PhysicsWorld::Step`):

| Type | Meaning | Transform flow |
|---|---|---|
| `Static` | Never moves; cheapest (level geometry) | Neither direction — authored once at build |
| `Kinematic` | Moved by code/animation, pushes dynamics, ignores forces | **Transform → physics**, pushed in before the step |
| `Dynamic` | Fully simulated | **Physics → Transform**, read back after the step |

`Step` runs in four phases, in this order:

1. Push **kinematic** poses into the backend — they are driven by gameplay/animation and must move the simulation, not be overwritten by it.
2. `backend->step(fixedDt)`.
3. Read **dynamic** poses back into `Transform`. Static bodies are authored and kinematic ones are driven, so neither is read back.
4. Fan out collision/trigger events — deliberately last, so listeners see transforms that already reflect this step.

On read-back, the simulated pose is rebuilt into a world matrix that preserves the entity's authored scale, converted to local space when the entity is parented (via `ResolveWorldMatrix` of the parent), decomposed with `DecomposeTRS`, and then `Transform::dirty` is set. That last flag is load-bearing: `UpdateTransforms` only revisits dirty nodes, so without it the simulated pose would never reach `modelMatrix` or the renderer.

---

## Lifecycle: when bodies exist

```c++
void Build(entt::registry& reg);
void Clear();
void Rebuild(entt::registry& reg);   // Clear + Build
bool IsBuilt() const;
```

- **In the editor**, native bodies exist only for a play session. `startPlay_` calls `physics_.Rebuild(scene.registry)` so play starts from exactly the poses the author sees; `stopPlay_` calls `physics_.Clear()` **before** restoring the pre-play snapshot. Edit mode is static.
- **In the player**, there is no Play button, so bodies are built right after the scene loads: `scene.UpdateTransforms(); InstallPhysics(*this, scene, physics_); physics_.Build(scene.registry);` (`Player/src/PlayerMain.cpp`).

`Rebuild` **must** run after any bulk registry restore — play-stop, undo, redo, history jump, scene load, New Scene. Those paths call `reg.clear()` and resurrect entities via `create(hint)`, so every cached entity → body pair is stale even though the handles look identical. The editor also calls `physics_.Clear()` when a new scene replaces the old one.

> **Gotcha — never trust `Transform::modelMatrix` right after a scene load.** It is a *cache* that `UpdateTransforms` fills; immediately after a load every `Transform` is dirty and its cached matrix is still identity. `Build` therefore resolves the world pose from the local TRS chain (`ResolveWorldMatrix`) whenever `t.dirty` is set, and only uses `modelMatrix` when it is known-clean. This was a real shipped bug: a 300×-scaled ground at y=-3 became a 1×1 box at the origin and everything fell straight through it — in the player but not the editor, which had already ticked for frames before Play. The regression test is `PhysicsWorldTest.BuildUsesWorldPoseEvenWithDirtyTransforms` in `tests/test_physics.cpp`.

---

## Collision and trigger events

### The model: transitions only

The API surfaces **Begin/End transitions**, not a per-step "still touching" stream:

```c++
enum class ContactPhase {
    Begin, // the pair started touching this step
    End    // the pair stopped touching this step
};
```

That is what gameplay actually reacts to, and every backend can report it consistently — "persisted" semantics differ enough between libraries to leak through the seam. A body resting on the ground reports `Begin` once and then nothing, which `PhysicsConformance.ContactEventsAreClearedEachStep` asserts for every backend.

### `PhysicsWorld::CollisionEvent`

The backend-level `ContactEvent` carries opaque `userData`; `PhysicsWorld` resolves both sides to ECS entities and hands you this:

| Field | Type | Meaning |
|---|---|---|
| `phase` | `ContactPhase` | `Begin` or `End` |
| `a`, `b` | `entt::entity` | The two entities; either may be `entt::null` |
| `isTrigger` | `bool` | True when either side is a trigger/sensor |
| `point` | `glm::vec3` | Representative world contact point — meaningful only on `Begin` |
| `normal` | `glm::vec3` | Representative world normal — meaningful only on `Begin` |
| `impulse` | `float` | Impact strength in N·s; `0` on `End` |

`point`/`normal` are only meaningful on `Begin`: an `End` event fires when the pair separates, and some backends have no manifold left to report by then.

An entity handle may come back `entt::null` because an `End` event can name a body whose entity was destroyed during the very step that produced the event. `PhysicsWorld` validates each side against the registry and drops the event only if *both* sides are unresolved — so **always check before using a handle**.

### Subscribing

```c++
using CollisionCallback = std::function<void(const CollisionEvent&)>;
using ListenerHandle = uint32_t;

ListenerHandle OnCollision(CollisionCallback cb);
void RemoveCollisionListener(ListenerHandle h);
void ClearCollisionListeners();
bool BackendReportsContacts() const;
```

Listeners fire from `Step()`, **after** the backend has finished simulating, so they run single-threaded on the fixed tick even though Jolt reports contacts from its job threads. That means a listener may safely touch the registry.

> **Important:** adding or removing a *body* from inside a listener is still unsafe — it would mutate the map being iterated. Defer that work to the next tick. Unsubscribing a *listener* from inside a listener is fine: dispatch iterates a copy of the listener list.

### Worked example

```c++
#include "Engine.h"

using namespace MyCoreEngine;

// Play a thud when something lands hard, and open a door when the player
// walks into its trigger volume.
void HookUpImpactAudio(PhysicsWorld& world, Scene& scene)
{
    if (!world.BackendReportsContacts()) {
        // e.g. a backend with no contact reporting: nothing to listen to.
        return;
    }

    world.OnCollision([&scene](const PhysicsWorld::CollisionEvent& e) {
        // End events carry no manifold and no impulse.
        if (e.phase != ContactPhase::Begin) return;

        // Either side can be null when an entity died during the step.
        if (e.a == entt::null || e.b == entt::null) return;

        if (e.isTrigger) {
            // Overlap, no collision response.
            OnEnteredVolume(e.a, e.b);
            return;
        }

        // impulse is in newton-seconds: it scales with mass and closing
        // speed, so it is the right value to drive impact intensity from.
        if (e.impulse > 2.0f) {
            PlayThud(e.point, /*loudness*/ e.impulse);
        }
    });
}
```

Remember the handle if you ever need to detach:

```c++
const auto h = world.OnCollision(cb);
// ...later...
world.RemoveCollisionListener(h);
```

### Triggers

Set `RigidBody::isTrigger` to make a body report overlaps without any collision response — dynamics fall straight through it. `Build` only honours the flag when the active backend advertises support:

```c++
desc.isTrigger = rb.isTrigger && backend_->supportsTriggers();
```

> **Gotcha — the Simple backend has no trigger support.** `SimplePhysicsBackend::supportsTriggers()` returns `false`, so a trigger authored in your scene simply becomes an ordinary body under `Simple` (it will *block*, not overlap). Test trigger volumes under Jolt or PhysX. The conformance test `TriggerReportsOverlapWithoutBlocking` skips backends that declare no trigger support rather than asserting around them.

### Contact impulse: comparable within a backend, not across backends

`CollisionEvent::impulse` is the impact strength along the normal, in newton-seconds (kg·m/s). It scales with mass and closing speed, which makes it the value to drive impact audio, damage, and particle intensity from. It is `0` on `End` events.

**The fidelity genuinely differs by backend, and the engine surfaces that rather than hiding it:**

- **PhysX** reports the *solver's actual applied impulse*. `onContact` runs post-solve, and the backend sums `pts[k].impulse.magnitude()` over the manifold points.
- **Jolt** invokes its contact callback *before the solver runs*, so the applied impulse does not exist yet. The backend estimates the impulse needed to cancel the closing velocity, `j = closingSpeed / (1/m1 + 1/m2)`. Static bodies have inverse mass 0, which correctly collapses this to the moving body's own mass.
- **Simple** reports the impulse needed to cancel the downward velocity on landing, `|v.y| * mass`.

All three are the right order of magnitude and **monotonic in impact speed** — a 20 m drop always reports a larger impulse than a 1.5 m drop, which is the property `PhysicsConformance.ContactImpulseScalesWithImpactSpeed` pins down for every backend. But **do not compare absolute impulse values across backends**, and do not hard-code a damage threshold that you expect to mean the same thing under Jolt and PhysX. Tune thresholds per backend, or normalize against a known reference impact.

---

## Raycasts

```c++
bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
             float maxDistance, RayHit& out) const;
entt::entity EntityFromHit(const RayHit& hit) const;
```

`direction` need not be normalized. `Raycast` returns `false` when nothing is hit (and when the world has no backend, in which case `out` is reset to a default `RayHit`).

`RayHit` carries `hit`, `body`, `point`, `normal`, `distance`, and `userData`. `userData` mirrors the `BodyDesc::userData` of the body that was hit — the engine stores the entity handle there, which is how a query maps back to the ECS without the backend knowing what an entity is.

Prefer `EntityFromHit` over reading `userData` yourself: it looks the hit's `BodyId` up in `PhysicsWorld`'s own body → entity map and returns `entt::null` for a miss or an unknown body.

```c++
// Click-to-pick: shoot a ray and find out which entity it landed on.
RayHit hit{};
if (world.Raycast(rayOrigin, rayDir, 1000.f, hit)) {
    const entt::entity picked = world.EntityFromHit(hit);
    if (picked != entt::null) {
        selected = picked;
        SpawnDecal(hit.point, hit.normal);
    }
}
```

Note that the `Simple` backend approximates a capsule by its bounding sphere for raycasts.

## Other world controls

```c++
void      SetGravity(const glm::vec3& g);
glm::vec3 Gravity() const;
size_t    BodyCount() const;
const std::vector<entt::entity>& SkippedEntities() const;
```

`SetGravity` updates the stored settings even when no backend is active, so the value is kept while the world has no backend. It does **not** survive a backend switch: `SetBackend` replaces the stored settings with the ones passed to it (default gravity when omitted), so re-apply your gravity afterwards. `Gravity()` reads from the backend when there is one, otherwise from the stored settings.

`PhysicsSettings` (passed to `SetBackend` / `InstallPhysics`) is deliberately small and generic — anything only one library understands belongs in that library's backend:

| Field | Default | Meaning |
|---|---|---|
| `gravity` | `{0, -9.81, 0}` | |
| `maxBodies` | `8192` | Sizing hint; backends that pre-allocate use it |
| `maxBodyPairs` | `8192` | Sizing hint |
| `maxContactConstraints` | `4096` | Sizing hint |
| `workerThreads` | `0` | `0` = single-threaded, the deterministic default |

`workerThreads` defaults to 0 because the engine's own `JobSystem` owns the machine's cores and physics runs on the fixed tick; under Jolt, 0 workers means everything runs on the calling thread.

---

## Adding a new backend

Adding a physics library means **one `IPhysicsBackend` subclass, one registration line, and one `find_package` block**. Nothing else in the engine changes.

### 1. Subclass `IPhysicsBackend`

Put every SDK type behind a pimpl so the header stays library-free, exactly as `JoltPhysicsBackend.h` and `PhysXPhysicsBackend.h` do:

```c++
// Engine/src/physics/backends/MyPhysicsBackend.h
#pragma once
#include "../IPhysicsBackend.h"
#include <memory>

namespace MyCoreEngine {

    class MyPhysicsBackend final : public IPhysicsBackend {
    public:
        MyPhysicsBackend();
        ~MyPhysicsBackend() override;

        const char* name() const override { return "MyPhysics"; }

        bool initialize(const PhysicsSettings& settings) override;
        void shutdown() override;

        BodyId createBody(const BodyDesc& desc) override;
        void   destroyBody(BodyId id) override;
        void   destroyAllBodies() override;
        size_t bodyCount() const override;

        void step(float fixedDt) override;

        bool getBodyState(BodyId id, BodyState& out) const override;
        void setBodyTransform(BodyId id, const glm::vec3& position,
                              const glm::quat& rotation) override;
        void setLinearVelocity(BodyId id, const glm::vec3& v) override;
        void setAngularVelocity(BodyId id, const glm::vec3& v) override;
        void applyImpulse(BodyId id, const glm::vec3& impulse) override;
        void wakeBody(BodyId id) override;

        bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                     float maxDistance, RayHit& out) const override;

        void      setGravity(const glm::vec3& g) override;
        glm::vec3 gravity() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace MyCoreEngine
```

Contact events are optional. Override both `contactEvents()` and `supportsContactEvents()` (which default to an empty list and `false`) if your library can report them, and accumulate events on your own lock — Jolt, for example, invokes contact callbacks concurrently from its job threads. Collecting during the step and exposing only afterwards is what lets listener code stay single-threaded. Clear the buffer at the **start** of each `step()`.

Also override `supportsShape(ShapeType)` and `supportsTriggers()` (both default to `true`) if your library cannot do something, so the engine can warn instead of silently simulating the wrong thing.

`initialize()` may fail — return `false` and leave the object safely destructible.

### 2. Register it

In `RegisterBuiltinPhysicsBackends()` (`Engine/src/physics/PhysicsBackendRegistry.cpp`), guarded by your own compile definition:

```c++
#ifdef CSE_WITH_MYPHYSICS
    PhysicsBackendRegistry::Register("MyPhysics", [] {
        return std::unique_ptr<IPhysicsBackend>(new MyPhysicsBackend());
    });
#endif
```

Games can also call `PhysicsBackendRegistry::Register` themselves at startup without touching the engine at all.

### 3. Add a `find_package` block

In `Engine/CMakeLists.txt`, follow the existing pattern:

```cmake
option(CSE_ENABLE_MYPHYSICS "Build the MyPhysics backend" ON)

if (CSE_ENABLE_MYPHYSICS)
    find_package(MyPhysics CONFIG QUIET)
    if (MyPhysics_FOUND)
        cse_add_physics_backend(EnginePhysicsMyPhysics
            src/physics/backends/MyPhysicsBackend.cpp
            MyPhysics::MyPhysics CSE_WITH_MYPHYSICS)
        message(STATUS "Physics: MyPhysics backend ENABLED")
    else()
        message(STATUS "Physics: MyPhysics not found - backend disabled")
    endif()
endif()
```

> **Important — use the `cse_add_physics_backend` helper; do not link an SDK straight into `Engine`.** Each SDK gets its own **STATIC** library. Imported SDK targets propagate INTERFACE compile definitions to every consumer source file, and Jolt's include `_HAS_EXCEPTIONS=0`. Linking Jolt directly into `Engine` rebuilt the *entire engine* without exception support, which turned the `std::filesystem` throw that `AssetIndex` relies on (non-codepage filenames) into a `0xC0000409` fast-fail and would have silently broken every other `try`/`catch` in the engine. `STATIC` rather than `OBJECT` is load-bearing: for a static library CMake records PRIVATE deps as `$<LINK_ONLY:...>`, so `Engine` inherits the SDK's `.lib` for linking but not its INTERFACE compile definitions or include directories.

Once your backend is registered it appears in the editor's picker automatically, and `tests/test_physics.cpp` runs the whole conformance suite against it — backends are discovered at runtime via `PhysicsBackendRegistry::Available()`. If your library disagrees with the others about what "a 1 kg box dropped onto a ground plane" does, that suite says so.
