# Lua Scripting

Scripting lets you attach behaviour to an entity without rebuilding the
engine. Where [Writing Gameplay](gameplay-scripting.md) covers C++ hooks that
run for the whole scene, a script runs **per entity** and is edited as a data
file.

> **Status: prototype.** The seam and the Lua backend are complete and tested,
> but see [What is not built yet](#what-is-not-built-yet) before planning
> around it.

## Attaching a script

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

## Lifecycle hooks

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

## The script API

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

`input.down(action)`, `input.pressed(action)`, `input.axis(axis)` take **action
names** from the InputMap, not key codes, so scripts survive rebinding.
`Jump` (Space / gamepad A), `Quit`, `MoveForward` and `MoveRight` are bound by
default; anything else you must bind yourself.

`input.pressed()` is safe to call from `OnFixedUpdate`. It reports a latched
press rather than a frame-scoped edge, so one physical press fires **exactly
once** even though the fixed tick runs zero times on some frames and several
times on others — and every entity reacting in that tick sees it, so putting
the same jump script on ten objects jumps all ten. Do not read the same action
from both `OnUpdate` and `OnFixedUpdate`; whichever runs first claims it.

Querying an action nobody bound warns **once** and reads as false, rather than
silently doing nothing forever.

> **In the editor, gameplay reads input only while the Game panel is
> focused** — click it, and the toolbar shows `Input: game`. This is what lets
> you fly the Scene view with the same keys while a scene is playing. The
> shipped player always has input.

`raycast` returns `nil` on a miss, or a table with `entity`, `point`,
`normal`, `distance`.

`OnCollision(c)` receives `c.other`, `c.phase` (`"begin"`/`"end"`),
`c.isTrigger`, `c.point`, `c.normal`, `c.impulse`.

Calling anything on an entity that no longer exists is a safe no-op, not a
crash — scripts hold entity references across frames and objects get destroyed.

## When a script breaks

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

## Isolation

Every entity gets its **own** global environment, even when two entities share
a file. This is deliberate — `counter = 0` at file scope is per-object state,
not shared state.

## Security

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
- **Script and HDRi paths from a scene file are containment-checked** — absolute
  paths, drive/UNC roots, and `..` are rejected, so a hostile scene cannot point
  a script or environment path outside the project.

`ScriptSettings::allowUnsafeLibraries` opts back into the full language
(io/os/package/debug, the loaders, coroutines, and `require` from the script
directory) for **trusted** content — the editor sets it; a shipped game running
downloaded scripts should not.

The wall-clock budget assumes `pcall`/`xpcall` are the only error boundaries a
script can reach, which holds in the default sandbox (no coroutines, no `load`,
no `debug`). Turning on `allowUnsafeLibraries` reopens `debug` and coroutines,
so trusted content can defeat the guards — by design; the budget is a limit for
*untrusted* scripts, and a generous one for the editor's own.

## Adding another language

The seam is the same shape as [physics](physics.md):

- `IScriptBackend` — the engine calls into scripts (load, lifecycle hooks).
- `IScriptHost` — scripts call into the engine (transform, physics, input).
- `ScriptBackendRegistry` — name → factory, with explicit registration.
- `ScriptWorld` — the only place the ECS meets scripting.

A new language implements `IScriptBackend` and reuses the existing host, so
the capability set is written once. Backends are compiled into their own
static library and never linked into `Engine` directly, which keeps SDK
compile definitions from leaking engine-wide.

A dependency-free `Null` backend is always registered, so a build without any
language runtime still loads and plays scenes — scripted entities simply do
nothing.

## What is not built yet

- **Hot reload.** Editing a `.lua` file requires Stop/Play.
- **No component access beyond transform + physics.** Scripts cannot add or
  remove components, spawn entities, or read materials and lights.
- **No coroutine scheduler.** The `coroutine` library is available, but the
  engine does not resume anything for you.
- **One environment per instance, chunk recompiled per entity.** Fine for
  dozens of scripted objects; a shared-chunk cache is the obvious next step
  if thousands are needed.
- **Scripts are not sandboxed from each other.** Isolation prevents accidental
  collisions, not deliberate ones.
