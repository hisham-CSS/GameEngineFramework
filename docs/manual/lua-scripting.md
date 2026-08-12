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
read. See [Default bindings](gameplay-scripting.md#default-bindings) for the
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

## Adding another language

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

## What is not built yet

- **Hot reload.** Editing a `.lua` file requires Stop/Play.
- **No component access beyond transform + physics.** Scripts cannot add or
  remove components, spawn entities, or read materials and lights.
- **No coroutines in the default sandbox.** The `coroutine` library is *not*
  loaded for untrusted scripts (the instruction-limit hook is per-thread and a
  new coroutine would start unhooked). It only reopens under
  `allowUnsafeLibraries`, and even then nothing schedules or resumes for you.
- **One environment per instance, chunk recompiled per entity.** Fine for
  dozens of scripted objects; a shared-chunk cache is the obvious next step
  if thousands are needed.
- **Scripts are not sandboxed from each other.** Isolation prevents accidental
  collisions, not deliberate ones.
