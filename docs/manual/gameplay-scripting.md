# Writing Gameplay

Cat Splat Engine has no script component and no scripting VM. Gameplay is C++
installed into the running `Application` through callbacks: one variable-rate
`Update`, and a fixed-rate tick that also drives physics. This page covers the
update model, how to install your logic over a `Scene`, reading input, and
reacting to collisions.

Everything here is available from the single umbrella header
(`Engine/include/Engine.h`), which pulls in `Application.h`, `Scene.h`,
`InputMap.h`, `FixedTimestep.h` and the physics core.

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
fixedStep_.advance(gameDt, [this](float fixedDt) {
    if (fixedUpdate_) fixedUpdate_(fixedDt);
    for (auto& s : fixedSubscribers_) {
        if (s.fn) s.fn(fixedDt);
    }
});
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
(contributions summed, clamped to `[-1, 1]`, radial deadzone on stick input).

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

bool  isDown(const std::string& action) const;
bool  wasPressed(const std::string& action) const;  // went down this frame
bool  wasReleased(const std::string& action) const; // went up this frame
float axis(const std::string& axis) const;          // clamped to [-1, 1]

bool gamepadConnected() const;
```

Querying an unbound name returns `false` / `0` — a typo is silent, not a crash.
The default gamepad deadzone is `0.15f`.

You do **not** call `update()` yourself in gameplay: `RunLoop` polls the map
once per frame before any hook runs. Just query it through `app.input()`.

### Default bindings

`Application::bindDefaultInput_` installs these (and re-applies them whenever a
new map is installed via `installInput`):

| Name | Kind | Bound to |
| --- | --- | --- |
| `MoveForward` | axis | `W`/`S`, `Up`/`Down`, left stick Y (inverted) |
| `MoveRight` | axis | `D`/`A`, `Right`/`Left`, left stick X |
| `LookX` | axis | right stick X |
| `LookY` | axis | right stick Y (inverted) |
| `Quit` | action | `Escape`, gamepad `Back` |

These drive the built-in free-fly camera. Add your own names in your install
function:

```c++
auto& in = app.input();
in.bindKey("Jump", GLFW_KEY_SPACE);
in.bindGamepadButton("Jump", GLFW_GAMEPAD_BUTTON_A);
in.bindMouseButton("Fire", GLFW_MOUSE_BUTTON_LEFT);
in.setGamepadDeadzone(0.2f);
```

> **Gotcha — edge queries and the fixed tick.** `wasPressed` / `wasReleased`
> are computed once per *frame*, in `InputMap::update`. A frame that consumes
> three fixed steps will report the same press as `wasPressed` in all three,
> and a frame that consumes zero steps will never show it to the tick at all.
> Read edges in `SetUpdate` (or latch them there and consume the latch in the
> tick); read `isDown` / `axis` levels in the fixed tick.

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
- Not every backend reports contacts at all. Check `BackendReportsContacts()`
  before relying on events.

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

> **Important:** because Stop restores a registry snapshot (`reg.clear()` plus
> `create(hint)`), any entity handle your gameplay state cached during a play
> session is stale afterwards. Keep per-session state in something you reset on
> install, and re-resolve entities from the registry rather than caching handles
> across a play/stop boundary. This is the same hazard that keeps native body
> ids out of the physics components entirely (`PhysicsComponents.h`).

## Checklist for a new gameplay module

1. Write an `InstallX(Application&, Scene&, ...)` free function in a header,
   mirroring `Engine/src/physics/PhysicsInstall.h`.
2. Use `SetFixedUpdate` **only** if you are the game. Otherwise
   `AddFixedUpdate` and return the `TickHandle`.
3. Store anything captured by reference as a member of the host application so
   it outlives `RunLoop`.
4. Bind your input names once at install time; query them through
   `app.input()` inside the hooks.
5. Set `dirty = true` on every `Transform` you write.
6. Call the same install function from both `PlayerMain.cpp` and
   `EditorApplication.cpp` so Play and the shipped build cannot diverge.
