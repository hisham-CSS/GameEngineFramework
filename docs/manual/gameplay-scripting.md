# Writing Gameplay

This page is about gameplay written in **C++**: logic installed into the running
`Application` through callbacks — one variable-rate `Update`, and a fixed-rate
tick that also drives physics. It covers the update model, how to install your
logic over a `Scene`, reading input, and reacting to collisions.

The engine also ships a **per-entity scripting layer**, which is the other way
to write behaviour: a `ScriptComponent` (script path + enabled flag), an
`IScriptBackend`/`IScriptHost` seam mirroring the physics one, and a Lua 5.4 +
sol2 backend driving `OnStart` / `OnUpdate` / `OnFixedUpdate` / `OnCollision` /
`OnDestroy` per scripted entity. It lives in `Engine/src/script/`, is owned by
`ScriptWorld`, and is installed into both hosts by `InstallScripting`
(`ScriptInstall.h`) exactly the way physics is. Use scripts when one entity
needs behaviour authored as data; use the C++ hooks below when you are writing a
system that runs for the whole scene. The Lua half of that seam is documented at the bottom of this page:
**[Lua: presentation and tooling only](#lua-presentation-and-tooling-only-never-the-simulation)**.

Everything here is available from the single umbrella header
(`Engine/include/Engine.h`), which pulls in `Application.h`, `Scene.h`,
`InputMap.h`, `FixedTimestep.h`, and the physics, scripting and audio seams —
each of which keeps its SDK (Jolt/PhysX, sol2/Lua, miniaudio) behind a backend
`.cpp`, so including this header never drags an SDK header into your build.

## The update model

`Application::RunLoop` (`Engine/src/core/Application.cpp`) runs one loop per
frame: poll input → camera/quit handling → job completions → **game update** →
`scene.UpdateTransforms()` → camera director → render → UI → present.

The game-update stage has two halves:

```c++
using UpdateFn = std::function<void(float /*dt*/)>;
void SetUpdate(UpdateFn fn);
void SetFixedUpdate(UpdateFn fn);
```

| Hook | Rate | `dt` it receives | Use it for |
| --- | --- | --- | --- |
| `SetUpdate` | once per rendered frame | the frame delta, already scaled by `timeScale()` | camera-facing, per-frame, frame-rate-tolerant work |
| `SetFixedUpdate` | zero or more times per frame | **always the fixed step**, never the frame delta | movement, forces, anything that must be deterministic |

### The accumulator, the cap, and dropped backlog

Fixed ticks come from `FixedTimestep` (`Engine/src/core/FixedTimestep.h`), a
plain accumulator:

```c++
template <typename Fn>
int advance(float dt, Fn&& fn, int maxSteps = 8);
```

Each frame the scaled frame delta is added to the accumulator, and the callback
runs once per whole step consumed. Two consequences matter to gameplay code:

- **The callback always gets `step_`**, the configured fixed step — not the
  frame delta, not a remainder. Multiplying a speed by the incoming `dt` inside
  a fixed tick is correct and stable by construction.
- **The step count is capped at 8 per frame** and, when the cap is hit with a
  backlog still pending, `accumulator_` is reset to `0`. The backlog is
  **dropped, not repaid**. This is deliberate: it prevents the spiral of death
  (sim falls behind → more steps → longer frame → further behind).

- **Game time skips after a long stall**, but the first line of defence is
  upstream: `Application::updateDeltaTime_` clamps the frame delta to `0.1 s`
  before it ever reaches the accumulator, so a level load, a debugger break or
  a window drag contributes at most 0.1 s of simulation. The 8-step cap is a
  second, independent guard, reached only when the clamped delta still exceeds
  8 steps (a high fixed rate, or `timeScale` > 1). Either way: never assume
  tick count is a wall-clock timer, and never accumulate gameplay state that
  assumes no tick was ever missed.

`Application::fixedAlpha()` exposes the leftover fraction in `[0,1)` if you want
to interpolate presentation between simulated states.

### One accumulator, both halves

`RunLoop` drives the primary slot and every subscriber from the *same*
`advance` call:

```c++
hasFixedConsumers = fixedUpdate_ || !fixedSubscribers_.empty();
if (hasFixedConsumers) {
    fixedSteps = fixedStep_.advance(gameDt, [this](float fixedDt) {
        // Each tick is its own consumption phase: every
        // consumer within it observes the same input edges.
        input_->beginInputPhase();
        if (fixedUpdate_) fixedUpdate_(fixedDt);
        for (auto& s : fixedSubscribers_) {
            if (s.fn) s.fn(fixedDt);
        }
    });
}
```

So gameplay and physics always see the same number of steps and can never drift
apart, and gameplay runs **before** the simulation within each tick — the Unity
ordering: apply forces on the tick, then integrate them in that same tick.

### Time controls

All of these are on `Application` and settable at runtime; the editor exposes
them under the **Time** section of the **Settings** panel (`EditorApplication::DrawTimeControls`).

```c++
void  resetGameClock();               // drop the partial accumulated step
void  setFixedTimestepHz(float hz);   // step = 1 / max(1, hz)
float fixedTimestepHz() const;
float fixedAlpha() const;
void  setTimeScale(float s);          // clamped to >= 0
float timeScale() const;
void  setPaused(bool p);
bool  paused() const;
```

The fixed rate is genuinely user-settable while the game runs — the editor
slider covers 15–240 Hz. `FixedTimestep::setStep` clamps to a 1e-4 s floor
(a 10 kHz ceiling) to guard against divide-by-zero. Write gameplay that reads
the step it is handed rather than hardcoding `1.0f/60.0f`.

When paused, `gameDt` is `0`: no fixed steps run, and `SetUpdate` is skipped
entirely (`RunLoop` only calls it when `gameDt > 0.f`). Camera and quit input
deliberately ignore pause and time scale, so you can still fly around a paused
scene.

`resetGameClock()` exists so every play session starts at the same phase — the
editor calls it in `startPlay_`.

## The primary slot vs. subscribers

There is exactly **one** primary fixed-update slot, and `SetFixedUpdate`
*overwrites* it. That slot is reserved for a game's own gameplay logic.

Engine and tooling systems that need the fixed tick use subscribers instead:

```c++
using TickHandle = uint32_t;
TickHandle AddFixedUpdate(UpdateFn fn);
void       RemoveFixedUpdate(TickHandle h);
```

Subscribers run **after** the primary slot, in registration order, and there can
be any number of them.

> **Important:** Physics installs itself with `AddFixedUpdate`, never
> `SetFixedUpdate` — see the comment at the top of
> `Engine/src/physics/PhysicsInstall.h`. If a system took the primary slot, it
> would *silently replace the game's logic*: no error, no warning, the gameplay
> hook simply stops being called. If you are writing a reusable system rather
> than the game itself, use `AddFixedUpdate` and hand the returned
> `TickHandle` back so it can be removed.

## Installing gameplay over a Scene

`InstallPhysics` (`Engine/src/physics/PhysicsInstall.h`) is the model to copy:
a free function that takes the `Application` and the `Scene`, wires the hooks,
and returns a handle. It is shared by both hosts so "works in Play" and "works
in the shipped game" cannot drift apart. Do the same for your game.

```c++
// Game/src/GameplayInstall.h
#pragma once
#include "Engine.h"

namespace MyGame {

    // Everything the tick needs, owned by the caller so it outlives RunLoop.
    struct GameplayState {
        float spinDegreesPerSecond = 90.f;
        int   hitsThisSession = 0;
    };

    inline void InstallGameplay(MyCoreEngine::Application& app,
                                MyCoreEngine::Scene& scene,
                                MyCoreEngine::PhysicsWorld& world,
                                GameplayState& state)
    {
        using namespace MyCoreEngine;

        // The PRIMARY slot: this is the game. Physics is already a
        // subscriber, so it integrates after this runs, in the same tick.
        app.SetFixedUpdate([&app, &scene, &state](float fixedDt) {
            auto& reg = scene.registry;
            auto& in  = app.input();

            // fixedDt is the fixed step, whatever rate the user picked.
            const float turn = state.spinDegreesPerSecond * fixedDt;
            const float drive = in.axis("MoveForward");

            auto view = reg.view<Name, Transform>();
            for (auto e : view) {
                auto& name = view.get<Name>(e);
                if (name.value != "Hero") continue;

                auto& t = view.get<Transform>(e);
                t.rotation.y += turn;
                t.position   += t.getBackward() * (-drive * 4.f * fixedDt);
                t.dirty = true;   // <-- see the Gotcha below
            }
        });

        // Per-frame work that does not need determinism.
        app.SetUpdate([&app](float dt) {
            (void)dt;
            if (app.input().wasPressed("Fire")) {
                // ... frame-rate-tolerant, presentation-side reactions
            }
        });

        world.OnCollision([&state](const PhysicsWorld::CollisionEvent& e) {
            if (e.phase == ContactPhase::Begin && !e.isTrigger) {
                ++state.hitsThisSession;
            }
        });
    }

} // namespace MyGame
```

Call it from your host right after the scene is loaded and physics is installed,
exactly where the player does its setup (`Player/src/PlayerMain.cpp`):

```c++
scene.UpdateTransforms();          // world matrices before bodies are built
InstallPhysics(*this, scene, physics_);
MyGame::InstallGameplay(*this, scene, physics_, gameplay_);
physics_.Build(scene.registry);
```

> **Important:** the lambdas outlive the call. Anything they capture by
> reference — the `PhysicsWorld`, your `GameplayState` — must outlive
> `RunLoop`. `PlayerApplication` makes this explicit by holding
> `PhysicsWorld physics_` as a member with the comment *"Outlives RunLoop: the
> fixed-tick subscriber captures it by reference."* A local in `Run()` that
> goes out of scope before the loop ends is a use-after-free.

### Gotcha: writing a Transform from a tick requires `dirty = true`

`Scene::UpdateTransforms` only revisits nodes marked dirty. `Transform`
(`Engine/src/core/Components.h`) carries the flag:

```c++
struct Transform {
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation{ 0.0f, 0.0f, 0.0f }; // Euler degrees (Y*X*Z)
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
    glm::mat4 modelMatrix{ 1.0f };          // always the WORLD matrix
    bool dirty = true;
    // ...
};
```

If you mutate `position`/`rotation`/`scale` from a tick and forget
`t.dirty = true`, `modelMatrix` is never recomputed — the renderer, culling,
picking and the camera director all keep using the stale world matrix, and your
object appears frozen even though its data changed. Physics itself has to do
this: `PhysicsWorld::Step` sets `t->dirty = true` after writing back each
simulated pose, with the comment *"without this the simulated pose never reaches
modelMatrix or the renderer."*

Two related facts:

- `position`/`rotation`/`scale` are **local** when the entity has a `Parent`,
  and world-space otherwise. `modelMatrix` is always the world matrix.
- Rotation is Euler degrees composed `Y*X*Z`. Match that if you build matrices
  by hand.

### Which entities you may move

`PhysicsWorld::Step` reads simulated poses back into `Transform` for
**Dynamic** bodies every tick, so writing a Dynamic entity's Transform from
gameplay is overwritten immediately. Per `RigidBody::type`
(`Engine/src/physics/PhysicsComponents.h`):

| `BodyType` | Transform ownership |
| --- | --- |
| `Dynamic` | the simulation writes it back each step — don't drive it from Transform |
| `Kinematic` | gameplay/animation drives it; `Step` pushes your pose *into* the backend |
| `Static` | authored; neither pushed nor read back |

Entities with no `RigidBody` at all are yours to move freely.

## Reading input

`InputMap` (`Engine/src/core/InputMap.h`) is a named, rebindable mapping.
Digital **actions** bind to keys, mouse buttons and gamepad buttons (multiple
bindings are OR'd); analog **axes** bind to key pairs and/or gamepad axes
(contributions summed, clamped to `[-1, 1]`, and a **per-axis scaled deadzone**
on stick input: each gamepad axis is thresholded against its own magnitude and
the remainder is rescaled back out to full range. Deliberately *not* the radial
deadzone a stick usually gets — an axis here is a NAME, and the two halves of
the left stick are two independent names (`MoveRight` is LEFT_X, `MoveForward`
is LEFT_Y), each free to also carry key pairs, so there is no point at which the
map knows which two names form a pair).

```c++
void bindKey(const std::string& action, int glfwKey);
void bindMouseButton(const std::string& action, int glfwMouseButton);
void bindGamepadButton(const std::string& action, int glfwGamepadButton);
void clearAction(const std::string& action);

void bindAxisKeys(const std::string& axis, int positiveKey, int negativeKey);
void bindGamepadAxis(const std::string& axis, int glfwGamepadAxis, bool inverted = false);
void clearAxis(const std::string& axis);

void  setGamepadDeadzone(float dz);
float gamepadDeadzone() const;

// `sources` is a mask of InputSource (Src_Key, Src_Mouse, Src_Pad, and the
// combinations Src_KeyboardMouse and Src_Any). It answers "is this action down
// ON THIS KIND OF DEVICE", edges included, which is what lets the UI show pad
// prompts or keyboard prompts based on what was last used.
bool  isDown(const std::string& action, std::uint8_t sources = Src_Any) const;
bool  wasPressed(const std::string& action, std::uint8_t sources = Src_Any) const;
bool  wasReleased(const std::string& action, std::uint8_t sources = Src_Any) const;
float axis(const std::string& axis) const;          // clamped to [-1, 1]

bool gamepadConnected() const;
```

Querying an unbound name returns `false` / `0` — a typo is silent, not a crash.
The default gamepad deadzone is `0.15f`.

You do **not** call `update()` yourself in gameplay: `RunLoop` polls the map
once per frame before any hook runs. Just query it through `app.input()`.

### Default bindings

`BindDefaultActions(InputMap&)` installs these, and `Application` re-applies
them whenever a new map is installed via `installInput`:

| Name | Kind | Bound to |
| --- | --- | --- |
| `MoveForward` | axis | `W`/`S`, `Up`/`Down`, left stick Y (inverted) |
| `MoveRight` | axis | `D`/`A`, `Right`/`Left`, left stick X |
| `LookX` | axis | right stick X |
| `LookY` | axis | right stick Y (inverted) |
| `Quit` | action | `Escape`, gamepad `Back` |
| `Jump` | action | `Space`, gamepad `A` |

...and ten more, for driving a UI without a mouse. They are installed by the
same call, so they are present in every host:

| Name | Kind | Bound to |
| --- | --- | --- |
| `UIConfirm` | action | gamepad `A` |
| `UIBack` | action | gamepad `B` |
| `UIPagePrev` / `UIPageNext` | action | left / right bumper |
| `UINavUp` / `UINavDown` / `UINavLeft` / `UINavRight` | action | d-pad, **and** `W`/`S`/`A`/`D` |
| `UINavX` / `UINavY` | axis | left stick |

`UINavUp`/`Down`/`Left`/`Right` deliberately carry the same `W`/`S`/`A`/`D` keys
as `MoveForward`/`MoveRight`, because the same keys drive the camera and the UI
in different contexts; the host decides which is listening. Arrow keys are
**not** bound here — the UI takes those directly as `UIKey` events, which is why
a prompt asking for a direction names the arrows rather than WASD.

The first four axes drive the built-in free-fly camera. `Jump` has no engine-side
consumer — it exists so gameplay and scripts have one conventional action
bound out of the box. Add your own names in your install function:

```c++
auto& in = app.input();
in.bindMouseButton("Fire", GLFW_MOUSE_BUTTON_LEFT);
in.setGamepadDeadzone(0.2f);
```

> **Querying an unbound name is silent.** `isDown` / `wasPressed` return
> `false` and `axis` returns `0` for a name nothing has bound, with no log
> line — deliberately, so unconfigured input cannot kill a frame. The cost is
> that a typo looks exactly like "the key isn't pressed". Lua scripts get a
> warn-once for this; C++ callers should check `hasAction` / `hasAxis` when
> debugging input that does nothing.
>
> **Unbound is only one of two silent causes.** A *suppressed* map reads
> neutral from every query as well: `isDown` / `wasPressed` / `wasReleased`
> return `false`, `axis` returns `0`, and `consumePressed` returns `false`
> **without** consuming the latch — a suppressed reader must not eat a press
> that a legitimate consumer would otherwise get. `RunLoop` suppresses the map
> around the gameplay hooks *only* (`InputMap::setSuppressed`), whenever
> `gameplayInputEnabled()` is false, which is why the editor's fly camera keeps
> working off the same named axes while the game receives nothing. The editor
> drives that flag from **Game-surface** focus — `playing_ && gameSurfaceFocused_`
> — so gameplay stops reading input the moment you click away from the game
> image.
>
> `hasAction` / `hasAxis` look only at the binding tables, so they still answer
> `true` while suppressed — and the Lua warn-once, which is gated on them, stays
> quiet too. To tell the two cases apart, check `app.input().suppressed()` (or
> equivalently `app.gameplayInputEnabled()`) alongside `hasAction` / `hasAxis`.

### Reading edges from the fixed tick

`wasPressed` / `wasReleased` are scoped to a rendered **frame**, and the fixed
tick does not run once per frame: above the fixed rate most frames run zero
ticks, and a stalled frame runs several in a row. Reading `wasPressed` from a
fixed-tick hook therefore **misses** most presses and **multiplies** the rest.

Use `consumePressed(action)` instead — it latches the press and holds it until
a tick consumes it:

```c++
app.AddFixedUpdate([&](float dt) {
    if (app.input().consumePressed("Jump")) { /* exactly once per press */ }
    // Levels are fine to read directly on the tick:
    body.velocity.x = app.input().axis("MoveRight") * speed;
});
```

The claim is scoped to a **phase** (one fixed tick, or one variable update),
not to a caller: every consumer inside a phase sees the same answer, so N
entities all reacting to one jump all get it. Reading the same action from
both a per-frame and a fixed-tick hook is unsupported — those are different
phases, and whichever runs first wins.

## Reacting to physics collisions

`PhysicsWorld` (`Engine/src/physics/PhysicsWorld.h`) resolves backend contacts
back to ECS entities and fans them out:

```c++
struct CollisionEvent {
    ContactPhase phase = ContactPhase::Begin;
    entt::entity a = entt::null;
    entt::entity b = entt::null;
    bool isTrigger = false;
    glm::vec3 point{ 0.f };
    glm::vec3 normal{ 0.f };
    float impulse = 0.f;
};
using CollisionCallback = std::function<void(const CollisionEvent&)>;
using ListenerHandle = uint32_t;

ListenerHandle OnCollision(CollisionCallback cb);
void RemoveCollisionListener(ListenerHandle h);
void ClearCollisionListeners();
bool BackendReportsContacts() const;
```

`ContactPhase` is only `Begin` and `End` — the transitions, not a per-frame
"still touching" stream. That is what gameplay reacts to and the only thing
every backend reports consistently.

```c++
world.OnCollision([&scene](const MyCoreEngine::PhysicsWorld::CollisionEvent& e) {
    using namespace MyCoreEngine;
    if (e.phase != ContactPhase::Begin) return;

    if (e.isTrigger) {
        // overlap only: no collision response was generated
        return;
    }
    if (e.impulse > 5.f) {
        // hard hit at e.point with surface normal e.normal
    }
});
```

Things the source is explicit about:

- Listeners fire from `Step()`, **after** the backend has finished simulating
  and after transforms have been written back, so they run single-threaded on
  the fixed tick and may safely touch the registry — even though Jolt reports
  contacts from job threads.
- **Adding or removing a body from inside a listener is unsafe** (it mutates
  the map being iterated). Defer that work to the next tick, e.g. by pushing
  onto a queue your `SetFixedUpdate` drains.
- `point` and `normal` are only meaningful on `Begin`; some backends have no
  manifold left by the time a pair separates. `impulse` is `0` on `End`.
- `impulse` is in newton-seconds and scales with mass and closing speed — the
  right value to drive impact audio, damage and particle intensity. Its
  **fidelity differs by backend**, deliberately surfaced rather than hidden:
  PhysX reports the solver's actually-applied impulse, while Jolt's contact
  callback runs before the solver, so its value is an estimate. Tune thresholds
  against the backend you ship.
- Contact reporting is opt-in at the seam: `IPhysicsBackend::supportsContactEvents()`
  defaults to `false`. Every backend you can currently select — `Simple`, `Jolt`
  and `PhysX` (the latter two when compiled in) — overrides it to `true`, so
  `BackendReportsContacts()` guards against a future backend, or against no
  backend being set at all (a world whose `SetBackend()` failed), rather than
  against anything shipping today.

For polling rather than events, `PhysicsWorld` also offers raycasts:

```c++
RayHit hit{};
if (world.Raycast(origin, direction, 100.f, hit)) {
    const entt::entity e = world.EntityFromHit(hit);
    // hit.point, hit.normal, hit.distance
}
```

`direction` need not be normalized.

## The gameplay gate

Your hooks only tick while gameplay is enabled:

```c++
void setGameplayEnabled(bool on);
bool gameplayEnabled() const;
```

- **The Player is always playing.** `gameplayEnabled_` defaults to `true` and
  `PlayerMain.cpp` never touches it: ticks run from frame one, and physics
  bodies are built right after the scene loads because there is no Play
  transition.
- **The editor gates on Play.** `EditorApplication::Run` calls
  `setGameplayEnabled(false)` at startup, so edit mode is static and gameplay
  can never mutate the scene you are authoring. `startPlay_` calls
  `resetGameClock()`, rebuilds physics bodies from the current edit-mode poses,
  then enables the gate; `stopPlay_` disables it, clears the bodies, and
  restores the pre-play scene snapshot. `Ctrl+P` toggles play/stop.

When the gate is off, the entire game-update stage is skipped — both the fixed
steps and `SetUpdate`. Camera input, rendering and the UI keep running.

There is a **second, independent gate**, and it governs input rather than
ticking:

```c++
void setGameplayInputEnabled(bool on);
bool gameplayInputEnabled() const;
```

When it is off the hooks still run — they just read nothing. `RunLoop` wraps
*only* the gameplay hooks in `input_->setSuppressed(!gameplayInput_)`, so every
`isDown` / `wasPressed` / `wasReleased` / `axis` / `consumePressed` call inside
them returns `false` / `0`. The editor's own fly-camera block reads the same
named axes just above that window, so the Scene view keeps working while the
game receives nothing. Polling and edge bookkeeping continue underneath, so
turning input back on does not manufacture an edge from a key that was already
held — and a press made while input was off is dropped rather than queued, so
regaining focus cannot fire a jump from a keystroke typed into the Scene view.

The Player leaves it on. The editor sets it to `playing_ && gameSurfaceFocused_`:
gameplay reads input only while the Game panel's *render surface* holds the
keyboard — not merely the panel, and not one of its toolbar widgets — and
`stopPlay_` (and closing or collapsing the panel) turns it off again.

> **Important:** because Stop restores a registry snapshot (`reg.clear()` plus
> `create(hint)`), any entity handle your gameplay state cached during a play
> session is stale afterwards. Keep per-session state in something you reset on
> install, and re-resolve entities from the registry rather than caching handles
> across a play/stop boundary. This is the same hazard that keeps native body
> ids out of the physics components entirely (`PhysicsComponents.h`).

## Checklist for a new gameplay module

1. Write an `InstallX(Application&, Scene&, ...)` free function in a header,
   mirroring `Engine/src/physics/PhysicsInstall.h`.
2. Use `SetFixedUpdate` / `SetUpdate` **only** if you are the game — each is a
   single primary slot that *overwrites*. A reusable system uses
   `AddFixedUpdate` / `AddUpdate` instead and hands back the returned
   `TickHandle` so the host can `RemoveFixedUpdate` / `RemoveUpdate` it. (A
   module that subscribes to both rates gets two handles; `InstallScripting`
   returns the fixed-tick one and lets the per-frame subscription live for the
   life of the application.)
3. Store anything captured by reference as a member of the host application so
   it outlives `RunLoop`.
4. Bind your input names once at install time; query them through
   `app.input()` inside the hooks.
5. Set `dirty = true` on every `Transform` you write.
6. Call the same install function from both `PlayerMain.cpp` and
   `EditorApplication.cpp` so Play and the shipped build cannot diverge.

---

# Lua: presentation and tooling only, never the simulation

A script runs **per entity** and is edited as a data file, where the C++ hooks
above run for the whole scene. Two examples ship in `Editor/src/Exported/Scripts`:
`spinner.lua` (rotation) and `bouncer.lua` (collisions and input).

**Nothing authoritative is ever authored in Lua**, and that is a decision with
teeth rather than a style preference ([ARCHITECTURE.md](../ARCHITECTURE.md) D7).
Non-string table keys in Lua 5.4 hash by **address**, so the most natural line a
modder can write — `for e, box in pairs(t)` — enumerates in heap-address order,
and Windows and Linux will never agree. There is no compile error, no runtime
error, and no way for the host to detect it. Use Lua for presentation, editor
tooling and asset pipelines; the simulation is authored data executed by an
integer kernel ([DETERMINISM.md](../DETERMINISM.md) §1).

### Attaching a script

1. Select an entity.
2. **Add Component → Script**.
3. Set **File** to a path relative to `Exported/Scripts`, e.g. `spinner.lua`.
4. Press **Play**.

Two examples ship in `Editor/src/Exported/Scripts`: `spinner.lua` (rotation)
and `bouncer.lua` (collisions and input).

Scripts load when Play starts and are destroyed on Stop, so a script can never
disturb the edit-mode scene you are looking at. The **Enabled** checkbox stops
a script running while still loading it — so syntax errors surface in the
Inspector immediately, rather than the first time you remember to tick the box.

### Lifecycle hooks

Define any subset; a missing hook costs nothing.

| Hook | When |
|------|------|
| `OnStart()` | Once, before the first update |
| `OnUpdate(dt)` | Every rendered frame |
| `OnFixedUpdate(dt)` | Every fixed physics tick, after the simulation step |
| `OnCollision(c)` | A contact involving this entity |
| `OnDestroy()` | Scene teardown or Stop |

Put physics work in `OnFixedUpdate`. Applying an impulse in `OnUpdate` makes
the force depend on framerate.

### The script API

`self` is the entity the script is attached to.

```lua
function OnUpdate(dt)
    local p = self:position()
    self:setPosition(vec3.new(p.x, p.y + dt, p.z))
end
```

**Entity** — `valid()`, `name()`, `position()`, `rotation()`, `scale()`,
`setPosition(v)`, `setRotation(v)`, `setScale(v)`, `translate(v)`, `rotate(v)`,
`applyImpulse(v)`, `setVelocity(v)`.

Rotation is Euler degrees. `applyImpulse` and `setVelocity` need a RigidBody
and wake a sleeping body; without one they do nothing.

**Globals** — `vec3.new(x,y,z)` (with `+`, `-`, `*`, `length()`,
`normalized()`), `find(name)`, `raycast(origin, dir, maxDistance)`,
`time()`, `log/logWarn/logError(msg)`, and `print` (routed to the engine log,
since a shipped game has no console).

`input.down(action)` and `input.pressed(action)` read **actions**;
`input.axis(axis)` reads **axes**. Both take names from the InputMap rather
than key codes, so scripts survive rebinding — but the two are separate name
spaces. Bound by default: the actions `Jump` (Space / gamepad A) and `Quit`
(Escape / gamepad Back), and the axes `MoveForward` and `MoveRight` (W/S,
A/D, the arrow keys, left stick) plus `LookX`/`LookY` (right stick — gamepad
only; mouse look is handled by the Application, not the InputMap). The same
call also installs ten UI-navigation names — the actions `UIConfirm`,
`UIBack`, `UIPagePrev`, `UIPageNext` and `UINavUp`/`Down`/`Left`/`Right`
(d-pad **and** W/S/A/D), plus the axes `UINavX`/`UINavY` (left stick) — which
are there for the in-game UI but are perfectly ordinary names a script may
read. See [Default bindings](#default-bindings) for the
full table. Anything
else you must bind yourself, and a name in the wrong space counts as unbound:
`input.down("MoveForward")` warns once and reads false.

`input.pressed()` is safe to call from `OnFixedUpdate`. It reports a latched
press rather than a frame-scoped edge, so one physical press fires **exactly
once** even though the fixed tick runs zero times on some frames and several
times on others — and every entity reacting in that tick sees it, so putting
the same jump script on ten objects jumps all ten. Do not read the same action
from both `OnUpdate` and `OnFixedUpdate`; whichever runs first claims it.

Querying an action nobody bound warns **once** and reads as false, rather than
silently doing nothing forever.

> **In the editor, gameplay reads input only while the game *surface* has the
> keyboard** — click inside the rendered image, and a highlight border is drawn
> around it. Clicking the panel's own widgets (the camera picker, the `Blend`
> field) hands the keyboard back to the editor even though the panel stays
> focused, and the toolbar's `Input: game` label reports **surface** focus too —
> the same flag the border and the gameplay gate read, so the label and the
> border now agree. This is what lets you fly
> the Scene view with the same keys while a scene is playing. The shipped
> player always has input. See
> [Who owns the keyboard](editor.md#who-owns-the-keyboard).

`raycast` returns `nil` on a miss, or a table with `entity`, `point`,
`normal`, `distance`.

`OnCollision(c)` receives `c.other`, `c.phase` (`"begin"`/`"end"`),
`c.isTrigger`, `c.point`, `c.normal`, `c.impulse`.

Calling anything on an entity that no longer exists is a safe no-op, not a
crash — scripts hold entity references across frames and objects get destroyed.

### When a script breaks

A script is user content and will be broken regularly, so a failure never
takes down the editor:

- The error is logged **once**, with the file and line.
- That script instance is disabled and never called again.
- Every other script keeps running.
- The message appears in the Inspector next to the file that caused it.

Runaway loops are handled too. Each callback runs under an instruction budget
(`ScriptSettings::instructionLimit`, default 2,000,000) and a wall-clock budget
(`ScriptSettings::callbackDeadlineMs`, default 1,000 ms); `while true do end`
is aborted and the script disabled instead of freezing the editor — even when
the loop is wrapped in `pcall`. See [Security](#security) for the full set of
sandbox limits.

### Isolation

Every entity gets its **own** global environment, even when two entities share
a file. This is deliberate — `counter = 0` at file scope is per-object state,
not shared state.

### Security

The sandbox is the trust boundary for the "run scripts you did not author" case
(mods, workshop content). In the default (untrusted) configuration:

- **`io`, `os`, `package`, `debug` are not loaded** — no filesystem or process
  control.
- **`load`, `loadstring`, `loadfile`, `dofile` are removed.** `load` accepts
  *unverified binary bytecode*, which Lua 5.4 explicitly does not validate — a
  single `load(bytes)()` is a memory-corruption primitive. `loadfile`/`dofile`
  additionally read and run arbitrary files. Withholding `io` means nothing
  while these remain, so they go too.
- **`coroutine` is not loaded.** The instruction-limit hook is per-thread and a
  new coroutine starts unhooked, so a loop inside one would run unbounded.
- **Memory is capped** (`ScriptSettings::memoryLimitBytes`, default 256 MB). A
  single `string.rep("x", 2^31)` allocates in one C call the instruction hook
  cannot see; the cap turns that from a host crash into a script error.
- **Instruction budget** (`instructionLimit`, default 2,000,000) aborts runaway
  loops. The abort survives being wrapped in a single `pcall`.
- **Wall-clock budget** (`callbackDeadlineMs`, default 1,000 ms) bounds the one
  case the instruction budget cannot: a runaway loop wrapped in `pcall` *and*
  looped around. A Lua error cannot cross a `pcall`, so the inner `pcall`
  swallows every instruction-limit abort and the outer loop retries forever.
  Once the time budget is blown, the sandbox's `pcall`/`xpcall` re-raise
  instead of returning, so the abort climbs out past every `pcall` level and
  the callback ends. Generous by design — the instruction budget already caps a
  callback at ~1–2 ms of work, so only a true runaway reaches it.
- **Asset paths from a scene file are containment-checked** — absolute paths,
  drive/UNC roots, and `..` are rejected, so a hostile scene cannot point outside
  the project. This covers script paths, model paths, the environment's HDRi
  path, a UI document's `.cxml`/`.cstyle` paths, and **audio clip paths** (a clip
  flows straight into miniaudio's WAV/MP3/FLAC/OGG decoders, which parse
  attacker-controlled binary). Model, audio-clip and UI paths are checked at scene
  load: a rejected path is cleared and logged, leaving the component in place, so
  the asset degrades gracefully rather than failing the load. A script path is
  checked later, when the source is resolved — the path is kept and only that one
  script instance fails, with `could not read script '<path>'` shown in the
  Inspector. The HDRi path is checked at bake time: rejection is logged, the path
  is kept, and the environment falls back to the procedural sky.

`ScriptSettings::allowUnsafeLibraries` opts back into the full language
(io/os/package/debug, the loaders, coroutines, and `require` from the script
directory) for **trusted** content. Nothing ships with it on: the editor and the
player both build `ScriptSettings` with only `scriptDirectory` set, so the editor
runs the same untrusted sandbox a shipped game does — a host that knows its
scripts are trusted has to turn it on itself.

The wall-clock budget assumes `pcall`/`xpcall` are the only error boundaries a
script can reach, which holds in the default sandbox (no coroutines, no `load`,
no `debug`) — the configuration both hosts actually run. Turning on
`allowUnsafeLibraries` reopens `debug` and coroutines, so trusted content can
defeat the guards — by design; the budget is a limit for *untrusted* scripts,
which is what both hosts (the editor and the shipped Player) run today.

### Adding another language

The seam is the same shape as [physics](physics.md):

- `IScriptBackend` — the engine calls into scripts (load, lifecycle hooks).
- `IScriptHost` — scripts call into the engine (transform, physics, input).
- `ScriptBackendRegistry` — name → factory, with explicit registration.
- `ScriptWorld` — the only place the ECS meets scripting.

A new language implements `IScriptBackend` and reuses the existing host, so
the capability set is written once. Each backend is compiled into its own
**static** library by the `cse_add_isolated_backend` helper, which links the SDK
into that library privately and then links the library into `Engine` (that is how
the registry reaches `LuaScriptBackend`). Because it is `STATIC` rather than
`OBJECT`, CMake records the private SDK dep as `$<LINK_ONLY:...>`, so `Engine`
inherits the SDK's `.lib` for linking but none of its INTERFACE compile
definitions or include directories — which is what keeps them from leaking
engine-wide.

A dependency-free `Null` backend is always registered, so a build without any
language runtime still loads and plays scenes — scripted entities simply do
nothing.

### The limits are the design

Scripts cannot add or remove components, spawn or destroy entities, or read
materials and lights; there is no hot reload (editing a `.lua` requires
Stop/Play); the default sandbox has no coroutines; and each instance recompiles
its chunk, which is fine for dozens of scripted objects and would want a shared
cache for thousands. None of these is scheduled, and that is deliberate — Lua is
not on the path to the fighting game, so growing it costs the thing that is.

Isolation prevents accidental collisions between scripts, not deliberate ones.
The sandbox boundary is between *the host and untrusted content*, not between
one script and another.
