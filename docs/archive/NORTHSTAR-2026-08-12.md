# NORTH STAR ROADMAP
### Cats Plat Studios — Game Engine Framework → deterministic rollback fighting game → paper case study

> **Partially superseded by [ARCHITECTURE.md](ARCHITECTURE.md) (2026-08-12).** This
> roadmap was written while cross-platform determinism was still treated as
> conditional on whether crossplay was in scope, and while the scripting language
> was assumed to be Lua. Both changed: cross-platform multiplayer is a hard
> requirement, and the scripting language is open. Where this document and
> ARCHITECTURE.md disagree on determinism, rollback, the gameplay core or the
> behaviour-authoring language, **ARCHITECTURE.md wins**. Sections 1, 2, 5, 6 and
> 7 here (inventory, testable definitions of done, research integration, what not
> to do, open questions) are unaffected and still current.

**Baseline: branch `audit-fixes-2026-08`, commit `8c5ad20`, 2026-08-12.** Every claim below is traceable to a file:line in this repo, to `C:/rw`, or to a measurement recorded during the audit. Where the seven specialists disagreed, the disagreement is named and my reading is given.

---

## 1. WHAT THIS ENGINE IS

**The honest pitch.** This is a 62,277-line C++17 engine with an EnTT ECS, an 11-pass forward renderer with 4-cascade shadow maps, three swappable physics backends behind one interface, a sandboxed Lua seam, a batched 2D renderer with stb_truetype text, a versioned JSON scene serializer, a dockable ImGui editor with undo/redo and play-in-editor, a standalone shipping player, and — its single largest subsystem — a retained-mode in-game UI toolkit with its own markup language, stylesheet cascade, yoga flexbox layout, and two-way data binding. It is covered by 993 GoogleTest cases across 50 test executables, a 0.83:1 test-to-engine line ratio, and documented by a 13-chapter, 9,625-line manual. It contains exactly one TODO marker in the entire first-party codebase (`tests/test_scene_details.cpp:65`).

**The website currently claims four things (11-pass renderer, CSM, ECS, dockable editor). All four are true and they describe roughly a fifth of the work.** Here is the defensible version.

| | Verified count | Evidence |
|---|---|---|
| First-party C++ | 238 files, 62,277 lines | `git ls-files`, tracked only |
| Engine / tests / editor / player / cooker | 30,395 / 25,329 / 6,118 / 359 / 76 | same |
| Shaders | 26 GLSL, 1,378 lines | same |
| Tests | 993 cases, 50 executables, 53 CTest entries | `ctest -N`; 50 `engine_test()` calls + 3 raw `add_test` |
| Verified run | `ctest -LE perf -j8` → 52/52 pass, 12.19 s | measured |
| Render passes | 11 registered | `Renderer.cpp:121,162,171,178,185,189,196,200,204,209,217` |
| Shadow cascades | up to 4 | `ShadowCSMPass.h:98` |
| Punctual lights | 16 | `Exported/Shaders/frag.glsl:36` |
| Serialized component types | 17 | enumerated from `SceneSerializer.cpp` |
| Physics backends | 3 real ones (Jolt 485 / PhysX 472 / Simple 269 lines) | `Engine/src/physics/backends/` |
| In-game UI | 12,455 lines, 42 files, 1,994-line manual chapter, 25 test executables | `Engine/src/ui/`, `docs/manual/ui.md` |
| Repo | 265 commits, 2024-10-26 → 2026-08-11 | `git log` |

**Publishable line, safe verbatim:**
> 62k lines of C++ across engine, editor, player and cooker · 993 tests in 50 executables · an 11-pass renderer with 4-cascade shadows · three swappable physics backends behind one interface · a retained-mode UI toolkit with its own markup, stylesheets and data binding · Windows verified.

**Three corrections to make before anything is published.**

1. **`README.md:62` and `:70` declare the in-game UI "🔲 not started."** It is the largest subsystem in the engine, it is render pass 11 (`Renderer.cpp:217`), and both shipping hosts install it (`PlayerMain.cpp:95`, `EditorApplication.cpp:558`). The README also never mentions `.cxml`, `.cstyle`, yoga, or Renderer2D anywhere. The README has drifted in the opposite direction from the website: the site undersells, the README misreports. Fix both from the table above.
2. **Do not write "53 test executables."** It is 50 executables producing 53 CTest entries.
3. **Do not list Linux as a supported platform.** `README.md:59` already says the right thing (🟡, "port phases 0–1: compiles under gcc/clang") — the specialist finding that called this an overclaim was refuted; the README and `docs/BUILDING_LINUX.md:12` agree. Just don't upgrade the wording. There is no Linux preset in `CMakePresets.json` (all four inherit `windows-base`, gated on `hostSystemName == Windows`), and the port has never been run on Linux hardware.

**One genuine process gap:** there is no CI of any kind. No `.github`, no pipeline file anywhere in the repo. 993 tests exist and nothing runs them automatically. For a project that intends to be a paper case study, this is the highest-leverage few hours available.

**What does not exist, at all:** anything fighting-game-shaped. A repo-wide grep for `comboprover|rollback|netcode|hitstun|hurtbox|frame data|cancel table` across `Engine/`, `Editor/`, `Player/`, `tests/` and `docs/` returns exactly one hit and it is a false positive (`tests/test_ui_stylesheet.cpp:295`, prose). There is no animation system of any kind — the live vertex format (`Model.h:37-43`) has no bone IDs or weights, and `Engine/src/core/Mesh.h` is 146 lines of fully commented-out LearnOpenGL-era skinning code that is not in any build. **This is a greenfield start from a mature general-purpose engine, not a partially-built fighting game.** That is a favourable position. Any plan that assumes otherwise is wrong.

---

## 2. THE NORTH STAR, MADE TESTABLE

**The target.** A Street Fighter 6-like fighting game: 3D characters on a 2D plane, two players, 60 Hz simulation, rollback netcode, character and move data authored as files rather than code, built on a framework the author can reuse for the next fighting game — and which proves the `C:/rw` combo-prover research by becoming its contribution #9, "an integration of the analysis into a working engine's editor."

Four properties. Each gets a definition of *done* that is a test, a number, or a demo — not an adjective.

### (a) Deterministic
**Done means three test tiers pass, in order:**
- **T1 — same process, same binary.** Run the sim N ticks from a fixed start state and a recorded input log, twice. Assert the two end states are **byte-identical** (not `EXPECT_NEAR`). *Today: no such test exists. `tests/test_fixed_timestep.cpp:33` documents the opposite property — `EXPECT_NEAR(calls, 60, 1); // float accumulation slack of one step`.*
- **T2 — rollback.** Run N ticks, snapshot, run M more, restore the snapshot, re-run the same M ticks with the same inputs, assert byte-identical. This is the actual rollback operation.
- **T3 — cross-machine.** CI emits a per-tick state hash file for a fixed input log on Windows and on Linux; the two files are diffed. *Only T3 settles the libm and physics-backend questions. Everything short of it is inference from source.*

### (b) Rollback
**Done means numbers, measured:**
- Save and restore are `memcpy` over a fixed-layout POD. Target: **≤ 5 µs per save/restore**, 8 restores plus 8× re-simulation inside one 16.6 ms frame.
- Reference points already measured: `memcpy` of a 64-byte POD block × 500 entities = **0.7 µs**. The editor's `UndoHistory::captureScene` at 500 entities = **907 µs** — 1,300× slower, and it is not the right thing anyway (below).
- A rolling state checksum per confirmed tick, plus a desync log naming the first divergent tick and field. Without this you will never find a desync.
- A `static_assert(std::is_trivially_copyable_v<T>)` over every simulation component type, so a `std::string` or `shared_ptr` cannot silently enter the snapshot.

### (c) Data-driven
**Done means:**
- A new character is added by dropping a JSON file into a directory. Zero recompiles, zero engine edits.
- A move's `startup`/`active`/`recovery` can be edited while a match is running and takes effect within one poll interval. The mechanism exists — `UIAssetDocument::Update` polls mtime+size at 0.25 s (`UIAssetDocument.cpp:179-193`, `UIAssetDocument.h:176`) — but is welded to one class and two file paths.
- `python -m comboprover <character.json>` runs on the engine's own character files **unchanged, with no export step**. `C:/rw/comboprover/importers/json_spec.py` reads every field via `raw.get(...)` and performs no unknown-key check, so the engine can superset the prover's schema and the Python reference still reads it.
- Unknown key in a character file is a **load error naming the key** — the `.cxml`/`.cstyle` policy (`UIMarkup.cpp:74-98`, `UIStyleSheet.cpp:346-353`), not the scene-JSON policy (which silently drops unknown fields, `SceneSerializer.cpp:132-292`).

### (d) Reusable
**Done means:**
- A second game builds against `Engine` with no edits to `Engine/`. Today `Engine.dll` ships one specific game's main menu: `Engine.h:78-79` exports `DemoUIContent.h` and `MenuUIContent.h`, and `MenuUIContent.h:44-45` hard-codes `playScenePath = "Exported/scene.json"`.
- No fighting-game type appears in `Engine`'s public headers; the fighter is a library that links `Engine`, not a branch inside it.
- The Editor's Play mode and the shipped Player run the *same* simulation code path. That property partly exists today — both hosts call the same `InstallPhysics`/`InstallScripting` helpers in the same order (`PlayerMain.cpp:178-188`, `EditorApplication.cpp:519-530`) — and is worth protecting deliberately.

---

## 3. WHAT BLOCKS IT TODAY

Seven specialists produced sixteen BLOCKING claims. After adversarial verification, they collapse to **three genuine blockers plus two conditional ones**. I am using the strict definition: *this makes deterministic rollback netcode impossible without changing it.*

### BLOCKER 1 — There is no simulation state, no tick index, and no snapshot. (months)
*determinism #1, rollback #2, cleanup #1 — all three verified true independently.*

There is no simulation tick counter anywhere in the engine. The only monotonic counters are render-side: `Renderer::frameIndex_` (`Renderer.h:200`, incremented inside `RenderFrame` at `Renderer.cpp:256`) and `ShadowCSMPass`. How far the world has advanced is expressed only as accumulated float seconds derived from `glfwGetTime()` (`Application.cpp:73-77`, `:181`, `:187`).

Simulation-visible state is spread across **five owners, of which one is the registry**:
- the `entt::registry` (`Scene.h:175`),
- `InputMap`'s masks, latches and phase counter (`InputMap.h:148-182`),
- `PhysicsWorld::entityToBody_`/`bodyToEntity_` (`PhysicsWorld.h:123-124`) plus the backend's opaque solver state,
- each Lua instance's `sol::environment` (`LuaScriptBackend.cpp:444`) — and worse, chunk-level locals that aren't even in that table (`bouncer.lua:9`, `local hits = 0`),
- `ScriptWorld::time_` (`ScriptWorld.h:144`) and `Application::fixedStep_`'s accumulator (`Application.h:290`).

Nothing in the engine can save or restore any of it. The only whole-state capture in the repo is the editor's `UndoHistory::captureScene`/`restoreScene` (`UndoHistory.cpp:259-277`), and it is emphatically not the seed for this:
- **Measured**: `sizeof(EntitySnapshot)` = 744 bytes; `is_trivially_copyable` = **false** (it holds `std::string`, an `unordered_map` of `shared_ptr`, and `AABB` is polymorphic — `is_polymorphic<AABB>` = true, `Components.h:286`). It can never be `memcpy`'d.
- **Measured**: capture 0.160 / 0.907 / 4.230 / 13.365 ms at 100 / 500 / 2000 / 5000 entities; restore 0.026 / 0.131 / 0.820 / 1.633 ms. Against `memcpy` of an equivalent POD block: 0.0001 / 0.0007 / 0.0024 / 0.0065 ms. **~1300–1600×.**
- It does not contain what rollback needs: `RigidBody` (`PhysicsComponents.h:21-30`) stores `initialLinearVelocity` and no live velocity at all, so a restore loses every simulated velocity.
- `restoreScene` calls `reg.clear()` and reloads models by asset path.
- It is a hand-maintained **closed list** and says so: *"apply() removes any tracked component whose flag is false, and never restores an untracked one, so a component missing here is destroyed by play-stop and undo"* (`UndoHistory.h:42-44`).

**One specialist called `EntitySnapshot`'s slowness MAJOR; the verifier downgraded it to INFO on the grounds that it runs exactly twice per play session (`EditorApplication.cpp:2324`, `:2398`) and is fine at that job. That is correct. My reading: `UndoHistory` is not broken and should not be changed. It is simply not the rollback substrate, and anyone who tries to grow it into one inherits a 744-byte non-copyable struct with a silent component-destroying restore.**

**Cost: 1–2 engineer-months** for the sim layer beside what exists.

### BLOCKER 2 — Input cannot be replayed, even in principle. (weeks)
*determinism #5, rollback #2 — verified.*

`Application.cpp:94` polls hardware once per **rendered frame**. `Application.cpp:187` then runs 0–8 fixed ticks off that single poll (`FixedTimestep.h:25-37`), so every tick in a frame sees byte-identical input — at 30 fps with a 60 Hz tick, two consecutive sim frames get the same input, which silently costs a frame of input precision in a genre where one frame decides matches.

Worse, the read is **destructive**: `InputMap::consumePressed` (`InputMap.cpp:128-153`) writes `servedPhase` and clears `latched`, so re-running tick N returns a **different answer** than the first run. `beginInputPhase()` (`InputMap.cpp:156-158`) increments a counter with no rewind. This is not an accident and it is not going to fix itself — `tests/test_input_map.cpp:255-268` asserts the behavior as an invariant.

There is no per-tick input record, no input serialization, and no way to hand the simulation a stored input instead of hardware state. The engine cannot answer "what did player 2 press on tick 4711," which is the first question rollback asks.

**The good news:** `InputMap`'s poll seams are already virtual for injection (`InputMap.h:141-145`) and the editor already subclasses them (`ImGuiInputMap.h:12-25`). The producer side is half-built.

**Cost: weeks.**

### BLOCKER 3 — The physics SEAM cannot restore state, so nothing reachable through it round-trips. (weeks — mostly to *remove* physics from the gameplay path)
*rollback #1 — verified, with a benchmark harness.*

> **Correction (2026-08-12).** This section was originally headed *"Jolt and
> PhysX cannot be part of a rollback simulation. Measured, not inferred."* That
> heading claimed more than the measurement supports and has been changed.
>
> What was measured is what is reachable **through `IPhysicsBackend`**, which
> exposes no save or restore at all. The engine's only restore is
> `PhysicsWorld::Rebuild()` = `Clear()` + `Build()`, which rebuilds bodies from
> AUTHORING components — and `RigidBody` (`PhysicsComponents.h:21-31`) carries
> only `initialLinearVelocity`, an authored starting value, with no live velocity
> field anywhere. So `Rebuild` could not round-trip a moving body under ANY
> physics engine; its 1.62–2.39 figure measures the seam discarding state, not a
> solver diverging.
>
> **Jolt's own `PhysicsSystem::SaveState` / `RestoreState` has never been run in
> this repository.** The 0.0074–0.0318 row is labelled "best case reachable
> through today's seam" and that label is exact: pose and velocity poked back in
> by hand, with warm-start impulses, contact manifolds, sleep state and island
> assignment all unreachable. That is a fact about the abstraction, not about
> Jolt.
>
> The recommendation below — take the rigid-body solver off the fighting game's
> gameplay path — is unchanged, because it never rested on this measurement. It
> rests on the arguments in [ARCHITECTURE.md](ARCHITECTURE.md) D2: a fighter
> needs authored per-frame motion and AABB overlap rather than a constraint
> solver, and integer arithmetic is bit-identical across platforms by the
> language rather than by a vendor flag. Do not re-derive D2 from the numbers
> in this table.

`IPhysicsBackend` (`IPhysicsBackend.h:21-82`) has no save/restore/serialize of any kind. The only restore the engine owns is `PhysicsWorld::Rebuild() { Clear(); Build(reg); }` (`PhysicsWorld.h:47`) — destroy and recreate every native body from authoring components.

**Measured, against the shipped x64-Release `Engine.lib`:**

| Test | Simple | Jolt | PhysX |
|---|---|---|---|
| Replay from scratch, same process, 60 ticks | 0.000000 | 0.000000 | 0.000000 |
| **Engine's actual restore path** (`Rebuild()` at tick 30, re-sim 30) | **1.62–2.39 units** | **1.62–2.39** | **1.62–2.39** |
| Best case reachable through today's seam (pose + lin + ang velocity in place), 32-box stack, 8 ticks | **0.00000000** | 0.0074 / 0.0146 / 0.0318 | 0.0028 / 0.0039 / 0.0151 |
| Fighting-shaped scene (2 capsules + 4 spheres + ground) | **0.00000000** | 0 → 4.9e-5 | 8.3e-6 → 1.1e-4 |

The residual on Jolt/PhysX is warm-start impulses, contact manifold cache, sleep/activation and island assignment — state the interface cannot reach. **For rollback, nonzero is nonzero.** `Rebuild()` is measurably a desync generator and must never be used as a restore.

**The specialists disagreed here and it matters.** The rollback specialist measured desync and called it BLOCKING. The determinism verifier called it MINOR, on the grounds that `PhysicsWorld::Build` populates `entityToBody_` only from `reg.view<RigidBody, Transform>()` (`PhysicsWorld.cpp:96`), so a scene with no `RigidBody` components leaves the map empty and the solver contributes **zero bits** to gameplay state even while subscribed to the tick.

**My reading: both are right and they point the same direction.** Physics is blocking *if it is on the gameplay path* and inert *if it is not*. The decision — not the code — is what resolves it. **Take the rigid-body solver off the fighting game's gameplay path entirely.** A fighter needs authored per-frame motion, axis-aligned box overlap between hit/hurt/pushboxes, and a fixed pushback rule. It does not need a constraint solver.

Two things worth knowing if physics ever *must* stay: Jolt already ships `PhysicsSystem::SaveState`/`RestoreState` with `StateRecorderImpl` and a ready-made `IsEqual` desync detector — the seam simply doesn't expose it. PhysX ships only `PxSerialization` (asset collections, not a per-frame solver snapshot); until someone tests it, treat PhysX as a non-rollback backend and say so in the docs. And `SimplePhysicsBackend`'s entire state is a POD `unordered_map<uint64_t, Body>` (`SimplePhysicsBackend.h:55-70`) that round-trips **exactly** in every configuration tested — it is the honest fallback if Jolt integration slips.

**Cost: weeks — and most of that is writing the integer AABB resolver, not removing physics.** Removing it is `InstallPhysics` becoming opt-in. Note that today the shipped Player installs physics unconditionally and discards the returned `TickHandle` (`PlayerMain.cpp:178`), so there is no supported opt-out in the shipped runtime. That is a small change to make before rollback work starts.

### CONDITIONAL 1 — Lua, if gameplay is ever authored in it. (months if allowed; days to prevent)
*determinism #3 (BLOCKING) vs rollback #3 and datadriven #1 (both downgraded to MAJOR by their verifiers).*

The facts are not in dispute and every citation verified. `ScriptInstall.h:94-96` subscribes `ScriptWorld::FixedUpdate` to the fixed tick. Each instance's mutable state lives in a `sol::environment` (`LuaScriptBackend.cpp:444`) that nothing in the engine can read, copy or write — `IScriptBackend.h:49-87` has no snapshot, serialize, or restore on it. `SceneSerializer.cpp:183-186` states the policy outright: *"script STATE lives in the language runtime and is deliberately not persisted."* The engine's only existing script restore path is `Clear()` + `destroyAllScripts()` + re-run the chunk. `sol::lib::math` is opened unconditionally (`LuaScriptBackend.cpp:400`), and Lua 5.4.7 auto-seeds `math.random` from time and the `lua_State` address at library-open time, and randomizes its string-hash seed per state (which randomizes `pairs()` order over string keys run to run). Two smaller cuts: the runaway guard aborts on a **wall-clock** deadline (`LuaScriptBackend.cpp:183-185`, `ScriptTypes.h:54` default 1000 ms), so the same script can be killed on a slow machine and not on a fast one and then never called again (`ScriptWorld::fail_`, `ScriptWorld.cpp:337-348`); and `ScriptWorld::time_ += dt` accumulates the **variable frame delta** (`ScriptWorld.cpp:392-393`) and is exposed to every script as the global `time()` (`LuaScriptBackend.cpp:346`). The shipped example teaches the wrong pattern on purpose: `spinner.lua` mutates persistent entity state from a wall-clock dt in `OnUpdate`.

**My reading: this is MAJOR today and BLOCKING the day someone writes a move in Lua.** The verifiers are right that Lua is opt-in per entity (`ScriptWorld::Build` walks `reg.view<ScriptComponent>()`, `ScriptWorld.cpp:262`) and that a scene with no `ScriptComponent` pays nothing. But "the only recompile-free gameplay-authoring path" is also right, `CSE_ENABLE_LUA` defaults ON (`Engine/CMakeLists.txt:250`), and reaching for Lua to script moves is the natural instinct. **The fix is a scoping decision that costs nothing now and is expensive to discover in month nine.** Write it in `docs/MAINTENANCE.md` as an invariant, and enforce it mechanically.

Two cheap hardenings worth doing regardless: set `callbackDeadlineMs = 0` for any shipped/networked configuration (keep the deterministic `instructionLimit = 2'000'000` guard, `ScriptTypes.h:42`), and drive script time from a tick index rather than `time_ += dt`.

Also record, for whenever runtime spawns are wanted: `ScriptWorld::order_` is frozen at `Build` time (`ScriptWorld.cpp:256-308`) and `Update`/`FixedUpdate` iterate only it, so an entity created mid-match with a `ScriptComponent` **never ticks, silently**. And there is no create/destroy in the host surface at all (`IScriptHost.h`).

### CONDITIONAL 2 — Float pose math and libm transcendentals, if Windows↔Linux crossplay is in scope. (weeks)
*determinism #4 — reported BLOCKING, downgraded to MAJOR by the verifier. I agree with the downgrade.*

Gameplay pose is float euler degrees (`Components.h:98-100`). `Transform::localMatrix()` builds the matrix with three `glm::rotate` calls, each evaluating `cos`/`sin` (`Components.h:105-113`; `glm/ext/matrix_transform.inl:21-22`). `MyCoreEngine::DecomposeTRS` calls `glm::extractEulerAngleYXZ` (`Scene.cpp:96`), which evaluates `atan2` three times plus `sin`, `cos` and `sqrt` (`glm/gtx/euler_angles.inl:719-724`), and `PhysicsWorld.cpp:243` runs it **unconditionally for every dynamic body every tick**. IEEE-754 pins `+ - * / sqrt` exactly and says nothing about transcendentals; MSVC's UCRT and glibc's libm differ in the last bits.

**Why this is MAJOR and not BLOCKING:** rollback's actual mechanic is save/restore/re-simulate on **one binary in one process**, where `sin`/`cos`/`atan2` are pure functions returning identical bits. Local re-simulation is bit-exact on this code as written. What libm variance breaks is peer agreement across *differing toolchains* — crossplay and cross-platform replay files. A per-platform-matched rollback build works today.

**The verifier is also right that this is not the controlling cause.** `PhysicsWorld.cpp:205` steps into Jolt/PhysX; converting `Transform` to fixed-point while the sim still steps through that line buys nothing. And a more direct cross-build hazard sits four lines away: `PhysicsWorld::Step` iterates `std::unordered_map` (`PhysicsWorld.h:123`, iterated at `:186` and `:210`), and readback order is **observable** — at `:236-239` a parented dynamic body converts world→local through its parent's *live* TRS, and `:246` sets `dirty = true` after each writeback, so whether a child sees its parent's old or new pose depends purely on which the hash map yielded first.

**Both mitigating facts are real and worth protecting:** there is no `/fp:fast`, `-ffast-math`, `/arch:AVX` or `-march=` anywhere in `CMakePresets.json`, any `CMakeLists.txt`, or `cmake/` (grep, zero matches). The configured flags are `/DWIN32 /D_WINDOWS /EHsc` and `/Zi /O2 /Ob1 /DNDEBUG` (`CMakeCache.txt:31,43`), so MSVC's default `/fp:precise` at the SSE2 baseline, no FMA contraction. GLM auto-selects SSE2 on x64 (`glm/simd/platform.h:389`). **These are one careless commit away from being lost and belong in `docs/MAINTENANCE.md` with a CI grep.**

### The dependency order
```
BLOCKER 2 (input as data)  ──┐
                             ├──> BLOCKER 1 (GameState + tick + snapshot) ──> rollback session
BLOCKER 3 (physics off path)─┘                    │
                                                  └──> T1/T2 determinism tests
CONDITIONAL 1 (Lua ban)  — decide before any gameplay is authored (free today)
CONDITIONAL 2 (fixed-point) — decide with the crossplay question; do it inside BLOCKER 1 if yes
```

---

## 4. THE ROADMAP

Ordered by dependency, with the de-risking work first. Milestones marked **[PAPER]** are prerequisites for the case study.

### Cheap wins — do these in the gaps (hours to days, any time)
These are independent of everything else and several will save real time later.

| Work | Why | Effort |
|---|---|---|
| **Frame-step button in the editor** — set timeScale 0, add a one-shot "run exactly one fixed tick" flag consumed in `RunLoop`. Today the entire time control is Paused / Time Scale / Fixed Tick Hz (`EditorApplication.cpp:1348-1360`). | Highest value-per-hour item in this entire document. Nothing about a frame-exact game is debuggable without it. | hours |
| **Fix the `Exported/` overwrite.** `cmake/stage_runtime_assets.cmake:19-25` copies every *subdirectory* of source `Exported/` into the runtime tree on **every build**; only top-level `*.json` gets the seeded-only + SHA256-warn treatment (`:27-49`). `hud.cxml:45-48` documents the resulting "edit the staged copy, copy your changes back" workflow. | This will lose work daily for a project whose central activity is iterating frame data against a running game. The mechanism already exists twenty lines away; it is applied to the wrong set. | hours |
| **CI.** GitHub Actions building `x64-relwithdebinfo-tests` and running `ctest -LE perf`. Exclude the `perf` label — it is already `RUN_SERIAL` + labeled (`tests/CMakeLists.txt:151-157`). | Converts 993 tests from a private discipline into a public citable fact. vcpkg binary caching is the only fiddly part. | days |
| **Fix `README.md:62`, `:70`, and the Tests row at `:60`.** | The inventory in §1 is the correct source. | hours |
| **Order the physics iteration.** Replace `entityToBody_` iteration with a `std::vector<pair<entity, BodyId>>` built entity-index-ascending; keep the hash map for lookups. Same for `SimplePhysicsBackend` (`SimplePhysicsBackend.h:68`, ties at `.cpp:103` and `:198-256` resolved by hash order). | Cheap; removes a real cross-build hazard; makes Simple a usable conformance oracle. | hours |
| **`FXAA_CostIsSmall` (`test_perf_render.cpp:335`)** asserts an absolute 1.0 ms bound on the *difference of two independently-measured medians*, whose measured run-to-run noise is ±1 ms (five clean runs: −1.09 to +0.51). Make it relative to `off.medianMs`, or record-and-report. | The broader "perf tests are flaky" claim was refuted — five clean runs on the reference RTX 3050 passed 8/8 with 25–55% headroom. This one assertion is genuinely under the noise floor. | hours |
| **`AssetIndex::classify`** (`AssetIndex.cpp:18-28`) doesn't recognise `.cxml`, `.cstyle` or `.lua` — the engine's own authored formats classify as `Other`. Add them plus the character extension. | One-function change, immediate authoring payoff. Note a bare `.json` classifies as `SceneJson`, so a character file **must** get its own extension. | hours |
| **Extract `FileWatch`** from `UIAssetDocument::stampOf`/`Update` (`UIAssetDocument.cpp:12-24`, `:179-193`) — mtime **and** size, 0.25 s poll. | ~30 correct lines welded to one class. Live-reloading frame data against a running match is the highest-value authoring feature this project can ship. | hours |
| **Delete dead code**: `Engine/src/core/Mesh.h` (146 lines, 100% commented out, not in any build), `EventBus.h`/`Event.h` + its self-referential test (zero production callers, and a process-wide mutable singleton is exactly what a rollback sim must not have), `Scene::RenderShadowDepth` (`Scene.h:207`, zero callers), `BoundingVolume`/`SquareAABB`/`Sphere`/`generateSphereBV` (~90 dead lines — and deleting them makes `AABB` trivially copyable and removes a vtable pointer, i.e. a raw address, from a snapshotted component). | Safe deletions, nothing links against them. | hours |
| **Move `DemoUIContent`/`MenuUIContent` out of `Engine.h`** (`:78-79`) into a sample target both executables link. | Required for property (d). The seam is already `Install*` functions taking a `UIWorld&`. | days |
| **`namespace MyCoreEngine` for components**, and delete `Components.h:15` (`using namespace MyCoreEngine;` — at file scope, in a header included by everything). `Name`, `Transform`, `Plane`, `Sphere`, `Frustum`, `AABB` are all in the **global** namespace. | A fighter wants `State`, `Frame`, `Box`, `Input`, `Player`, and `comboprover.hpp` is a single header that will land in this same global namespace. Large mechanical diff, purely syntactic, gets harder every week. Do it before vendoring the prover. | days |

---

### Phase 0 — The determinism harness **[PAPER prerequisite]**
**Goal:** make reproducibility a thing the build asserts, before writing any of the fixes it is meant to verify.

**Work.** Add `uint64_t tick_` owned by the simulation and make it the only clock gameplay reads. Write the T1 test (§2a): run the sim twice from the same state and input log, assert byte-identical end state. Add a state-hash function and a per-tick hash log. Wire it into CI.

**Why first.** This test is what tells you whether every later fix worked. It will immediately expose the ordering, cache and clock hazards below without anyone having to reason about them.

**Note on feasibility:** one specialist claimed the fixed tick can only run inside a windowed rendering loop. **That was refuted.** `tests/test_physics.cpp:457-460` already steps `world.Step(scene.registry, 1.f/60.f)` + `scene.UpdateTransforms()` sixty times headless with no window, and `tests/test_scripting.cpp:449-451` does `beginInputPhase(); world.FixedUpdate(reg, 1.f/60.f)` the same way. The tick's payload is free functions of `(registry, dt)`. What *is* true is that `Application`'s **composed** tick ordering (primary slot + subscribers, `Application.cpp:191-194`) is reachable only from `RunLoop`, and `Application` needs a window (`Application.h:270`, `Window.h:13-22`). A `StepSimulation(Scene&, float)` seam is worth extracting so the ordering isn't duplicated by hand — but it is an ergonomics fix, not a blocker.

**Done when:** T1 passes in CI on every commit. **Effort: 1–2 weeks.**

### Phase 1 — Input as data
**Goal:** the simulation reads `inputs[tick]` and nothing else.

**Work.** Define `PlayerInput` (button bitmask + quantized 9-way direction, ~2 bytes) and `TickInputs { uint32_t tick; PlayerInput p[2]; }`. A ring buffer of the rollback window plus a confirmed-tick marker. `InputMap` becomes a producer sampling **at the tick rate, not the frame rate** — which also fixes the precision loss at low frame rates. Quantize analog axes to integers at the sampling boundary so a stick value is identical on both peers by construction (`InputMap::axis` currently returns a raw float sum, `InputMap.cpp:118-122`). Use the existing virtual poll seams (`InputMap.h:141-145`) for a `ReplayInputMap`. Leave the latch/phase machinery outside the simulation entirely — it is frame-scoped bookkeeping, and `test_input_map.cpp:255-268` asserts its current behavior, which is correct *for the editor*.

Also fix the deadzone for this path: it is per-axis and explicitly **not** radial (`InputMap.h:35-42`), so diagonals register on a different threshold than cardinals — which is not what a clean 2→3→6 quarter-circle needs. And note the documented hazard at `InputMap.h:160-166`: two press-release cycles inside a single zero-tick window (~14 ms at 144 fps) collapse into one press. A piano/plink input is exactly that.

**Done when:** a recorded input log replays to an identical per-tick hash log. **Effort: 3–4 weeks.**

### Phase 2 — The simulation layer **[PAPER prerequisite]**
**Goal:** one flat POD `GameState` that is the entire authoritative simulation, saved and restored by `memcpy`.

**Work.**
- Positions and velocities in fixed-point (e.g. 1/256 units, int32). Facing as an integer, not free rotation. The prover already models everything in integer frames and integer resources (`C:/rw/engine/comboprover.hpp` — `Move` fields are `int` startup/active/recovery, resources are `std::vector<int>`), so integer sim state is the representation the proof obligation is already stated in.
- Flat transforms — no parent chain in the sim. Two characters and their hitboxes do not need a scene graph, and a flat representation removes the `Transform::modelMatrix` staleness class entirely. (`Scene::UpdateTransforms` is the only writer of `modelMatrix` and the only place `dirty` is cleared, `Scene.cpp:314`, and `Application.cpp:228` runs it **after** the whole fixed loop — so a node whose *ancestor* moved during an earlier tick of the same frame reads a stale cached world matrix, and whether that happens depends on wall clock. `docs/MAINTENANCE.md` already names this trap and records that *"This has now bitten four times."*)
- Hit resolution as integer AABB overlap with an **explicit documented order**: pushbox separation → throws → strikes → projectiles. That ordering rule is what makes the prover integration meaningful, since the analysis assumes a deterministic hit outcome per frame. Do not extend `IPhysicsBackend` for this — it offers exactly one spatial query, `raycast` (`IPhysicsBackend.h:55-56`), no overlap or box-cast, no collision layers in `BodyDesc` (`PhysicsTypes.h:57-74`), and contact events documented as arriving *"in no particular order"* (`IPhysicsBackend.h:60-61`).
- Hitstun, blockstun, juggle budget, drive/meter, air actions as **named integer resources**. Every one of them is a ranking function in the research's sense, so implementing them as explicit monotonically-spent resources is simultaneously the gameplay work and the thing that makes the character provably terminating.
- A stable serialized `SimId` as the only identity the sim, scripts, network and save files ever see. Today identity leaks two ways: `ScriptWorld.cpp:18-31` hands Lua `entt::to_integral(e)` — *"Round-trips the FULL handle including entt's version bits"* — and `SceneSerializer.cpp:117-127` writes parent links as **array position**, kept meaningful only by a `std::reverse` on save (because entt views iterate newest-first, `sparse_set.hpp:606-609`). You cannot put an entt handle on the wire.
- A deterministic spawn queue drained at a fixed point in the tick, with `SimId`s from a counter that is part of the snapshot. Do **not** solve runtime spawns by making iteration follow entt creation order — pool order is permuted by every component add/remove (`deletion_policy::swap_and_pop`, `sparse_set.hpp:403`).
- Presentation/simulation boundary written down as an invariant: **simulation** = the POD state + tick index + input ring. **Presentation** = camera blends, UI documents, audio voices, render settings, CSM cascades — explicitly not snapshotted, derivable from sim state each frame. Enforce with `static_assert(std::is_trivially_copyable_v<T>)` over the sim state.

**Done when:** T2 passes — snapshot, run M ticks, restore, re-run, byte-identical — and save/restore is measured under 5 µs. **Effort: 6–10 weeks. This is the largest single item in the plan.**

### Phase 3 — Character data and the frame-indexed clip player **[PAPER prerequisite]**
**Goal:** a character is a file; the file is the prover's schema.

**Work.** One `.fchar` JSON per character (its own extension so `classify()` can get a `Kind::Character` — a bare `.json` cannot, see cheap wins). Its **top level IS `C:/rw/comboprams/importers/json_spec.py`'s schema, unmodified**: `name`, `moves[{id,startup,active,recovery,hitstun,damage,effect,guard,…}]`, `cancels[{from,to,delay|kind:link,on,effect,guard}]`, `resources[{name,initial,floor,ceiling}]`, `decay`, `starters`, `scaling`. Engine-only concerns go in **sibling keys the prover ignores**: per-frame hitbox/hurtbox/pushbox sets, the animation track, hit effects (hitstop, shake, sfx, vfx), and the motion-input command table. `comboprover::Move` has no opinion about any of them.

Strict parsing: **unknown key is a load error naming the key.** A typo'd `startup` that silently reads as 0 is exactly the `nmae="healthFill"` bug the UI markup layer already refuses (`UIMarkup.cpp:72`), with worse consequences. Version gate plus an explicit migration list from day one — frame data outlives tools and mods will be written against whatever shipped. (Contrast the scene format: `kVersion = 1` since the commit that introduced it, `SceneSerializer.h:72`, with the only real migration keying off the *absence* of a field rather than the version, `SceneSerializer.cpp:600-604`.)

Add one consistency check the prover cannot make, because it is where the two halves will drift: **assert `startup + active + recovery` equals the authored frame count of the move's box/animation track, and fail the load naming both numbers.**

The runtime half: a **frame-indexed** clip player. Animation is sampled from the state machine's `frameInState`, never from wall-clock dt — that is what makes the animation, the hitbox timeline, and the research's `startup/active/recovery` triple the same clock. This is the largest missing engine subsystem, larger than the data format. If characters are 3D (see Open Question 2), skinning is also needed: extend `Vertex` (`Model.h:37-43`) with `ivec4 boneIds` + `vec4 weights`, add a bone-palette uniform/SSBO and a skinning branch in the forward shader, import via Assimp's `aiAnimation` (already a dependency). **Build the frame-indexed sampler first; the skinning is a renderer feature that hangs off it.**

Also here: a JSON input-binding file loaded into `InputMap` at boot (bindings are code today — `InputMap.h:56-67` exposes only imperative binders, and `ProjectSettings.h:14-25` carries exactly `startupScene` and `masterVolume`), and a data-driven command table (`{name, motion:"236", button:"P", window:12, priority:3}`) matched **backwards** over the input ring. Do not try to express motion inputs as `InputMap` actions — that abstraction is per-frame boolean with no place to put a sequence.

**Done when:** `python -m comboprover Editor/src/Exported/Characters/*.fchar` runs on the engine's own files with no export step, and a move's frame data edited on disk takes effect in a running match within 0.25 s. **Effort: 8–12 weeks (skinning adds ~4).**

### Phase 4 — Authoring and debugging tools
**Goal:** make frame data inspectable, because the rest of the project is spent iterating it.

**Work.** Debug draw — there is **no line/box primitive anywhere**: grep for `GL_LINES`/`GL_LINE_STRIP`/`glLineWidth` across `Engine/` and `Editor/` returns zero hits. `Renderer2D` could serve; its world-space mode (`Renderer2D.h:70` `BeginWorld`) has no production caller, only `tests/test_renderer2d.cpp:160`. Then: hitbox overlay driven from the same authored boxes the sim tests, on-screen input display reading the input ring (a training feature, a debugging tool, and a paper figure for the price of one), replay record/playback (falls out of the input ring + a start-state hash), training mode.

**Done when:** a replay reproduces bit-for-bit — which is also your standing determinism regression test. **Effort: 4–6 weeks** (frame-step already landed in cheap wins).

### Phase 5 — The combo prover in the editor **[PAPER — this is contribution #9]**
See §5. **Effort: 3–5 weeks** once Phase 3's asset exists.

### Phase 6 — Rollback session and netcode
**Goal:** two peers, 60 Hz, up to ~8 frames of rollback.

**Work.** Do not start this before Phases 0–2. GGPO-style rollback is a thin layer over `Simulate(GameState&, TickInputs)` + `memcpy` save/restore. Split `RunLoop` so the session decides which tick to simulate next rather than a float accumulator: today `FixedTimestep::advance` caps at 8 steps and **zeroes the accumulator**, discarding backlog (`FixedTimestep.h:33-35`), and `Application.cpp:142` resets it on every scene swap while `:180` skips gameplay entirely on the swap frame — so tick alignment across peers cannot survive a scene transition.

Budget for the parts people underestimate: a **state checksum per confirmed frame plus a desync log** (without it you will never find the divergence), and **suppressing one-shot side effects during re-simulation**. Today nothing distinguishes a replayed tick from a confirmed one: physics contacts dispatch from inside the fixed tick into `ScriptWorld::DispatchCollision` (`PhysicsWorld.cpp:249-251`, `ScriptInstall.h:57-64`), and `AudioWorld::PlayOneShot` (`AudioWorld.cpp:119-127`) is fire-and-forget with no dedup. A 7-frame rollback replays 7 ticks of hooks and fires every hit sound up to 8 times. Give the tick a phase (PREDICTED vs CONFIRMED), queue presentation events keyed by tick, drain only on first confirmation, dedup by `(tick, entity, event)`.

Pin the simulation-relevant configuration in content and check it at connect: backend name, fixed-tick Hz, gravity, engine build hash. Today none of it is pinned — `DefaultPhysicsBackendName()` is a pure preprocessor decision, `find_package(... QUIET)` silently disables a backend when the SDK is absent, and nothing in `SceneSerializer.cpp` writes the backend name into the scene. Two peers built on machines with different vcpkg states would run **three entirely different solvers with no error**. Also lock the fixed rate: `setFixedTimestepHz` is user-editable 15–240 Hz at runtime (`Application.h:232`, `EditorApplication.cpp:1359`) — a slider that silently changes every frame count in your frame data is a trap.

**Done when:** T3 passes, and two instances play a match with an injected 100 ms / 5% loss link with zero desyncs over 10 minutes. **Effort: 2–3 months.**

### Phase 7 — The game
Match flow (round start, KO, transitions, rematch) as an explicit state machine **on the sim side**, with UI merely binding to it, so round state rolls back with everything else. Character select needs one contained engine change: **per-player nav scopes in `UIWorld`**, because today there is one nav focus per document and *"two players cannot drive two cursors through one menu"* (`docs/manual/ui.md:1983`). Audio needs a low-latency device — `ma_engine_init(nullptr, ...)` (`MiniaudioBackend.cpp:26`) takes device defaults, and `AudioSettings` carries exactly `masterVolume` (`AudioTypes.h:12-14`).

One shipping hazard to treat as a release blocker for this genre specifically: the 2026-07 audit recorded **12.6 FPS on the iGPU vs 58.3 on the discrete card** (`docs/ENGINE_AUDIT_2026-07.md:30-31`). In a game where a dropped frame is a lost match, defaulting to the wrong GPU is fatal. Scene scale itself is not a concern — 14.6 ms at 12.3k instances / 1080p on an RTX 3050 (`:250`), and two characters plus a stage is orders of magnitude below that.

---

## 5. THE RESEARCH INTEGRATION

**Vendor, don't submodule, don't port.** `C:/rw` is not a git repository, so a submodule isn't available today. Copy `comboprover.hpp` to `Engine/src/thirdparty/comboprover.hpp` **unmodified**, with a header comment recording the upstream path, a content hash and the date, and a rule in `docs/MAINTENANCE.md` that it is never edited in place — fixes go upstream and come back as a re-vendor. It is standard-library-only and header-only, which matters for the Linux goal. **A fork would destroy the paper's own claim:** contribution #9 is only evidence if the engine ran the *published* implementation rather than a rewrite of it. Include it from exactly one `.cpp` and expose an engine-owned `ComboReport AnalyseCharacter(const CharacterAsset&)` — keeping a third-party type out of the ABI means the header can be updated without an Engine ABI change.

**Where it plugs in: both places.**

**The editor panel.** `analyse()` is pure CPU — no GL, no registry, no ImGui — which is exactly `JobSystem`'s contract for worker-thread work (`JobSystem.h:22-37`), so it runs on `submit(work, onComplete)` with the report landing on the main thread. **Do not call it from the ImGui frame.** Debounce 250–300 ms after the last keystroke and keep showing the previous verdict greyed with a "recomputing" marker.

**Why the debounce matters — the header's own performance claim does not survive realistic budgets.** `comboprover.hpp:9-13` says *"well under a millisecond, so the editor can re-run it on every edit."* Measured here (MSVC /O2): 30 moves / 120 cancels / juggle budget 6 → **0.23 ms**; 60 / 480 / budget 20 → **3.78 ms**; 40 moves fully connected / 1600 cancels / budget 12 → **3.95 ms**; 80 / 6400 / 12 → **27.8 ms**. The 60/480/20 case in a Debug build → **43.3 ms**, and the editor is what developers run in Debug. The driver is visible in `explored`: budget 6 explores 252 configurations, budget 20 explores 1092. State is `(move, hits, resource vector)` so the space scales with the **product of resource ranges**, not move count — and a drive gauge, a super meter and a juggle counter multiply three ranges together. Pass a `limit` of a few thousand for the interactive path so a pathological in-progress edit degrades to UNRESOLVED in milliseconds; use the full 200000 default only for the cooker gate. Give designers a quantisation rule in the authoring docs: **meter as 0–6 bars, not 0–6000 points.** And soften the claim in the header comment — the measured numbers are excellent for a build gate and fine for a debounced panel, and an overstated claim is what a TOG reviewer checks.

**The cooker gate.** The seam already has the right shape: line-oriented `OK/WARN/ERR/DONE` on stdout, exit 0 clean / 1 errors / 2 bad usage (`CookerMain.cpp:9-14`), a documented fail-closed stance (`:42-50`), one `validate <root>` command. The editor already spawns it out-of-process, drains stdout on a reader thread, keeps the handle so a hung child can be killed, and reaps on the main thread (`EditorApplication.cpp:2273-2303`). Extend `ValidateAssetTree` with a character pass: **INFINITE → ERR** (build fails); dead cancels and unreachable moves → WARN. Allow an explicit per-character waiver stored **in** the `.fchar` (`"allowInfinite": "<reason>"`) so a knowingly-broken WIP character is a reviewable diff rather than a disabled check. Add a separate `AssetCooker combos <root> --json` verb emitting one machine-readable record per character — same binary, different verb, so CI gets the gate and the paper gets the corpus.

**Three pre-integration requirements.** Two audit findings about the prover header were withdrawn as *engine* findings (correctly — they point at code outside this repo, reachable from nothing here). They survive as requirements for whoever writes the panel:

1. **Validate before calling.** `analyse()` indexes `character.moves[edge.from]`/`[edge.to]` and `starters` with **no bounds check** (`comboprover.hpp:337-341, :409-411, :429-433, :657-658`), and `addMove`/`addCancel` are bare `push_back`s. A probe with a dangling cancel returned garbage at /O2 and `abort()`ed in a Debug build — which is precisely the state a live editor is in the moment a designer deletes a move. The Python reference enforces these checks (`model.py:113-121`, `:160-162`, `:343-357`); the C++ port dropped them. Validate every `from`/`to`/`starter` in range and `startup >= 1`, `active >= 1`, `recovery >= 0`, `hitstun >= 0`, `delay >= 0`, and show the validation error instead of a verdict.
2. **Never report a trivial pass as a proof.** A character with zero moves returns `TERMINATING`, *"because every reachable position leads eventually to a dead end,"* `explored=0`. The engine's own precedent is right there: `runValidate` refuses a missing asset root because *"a validation gate must FAIL CLOSED"* (`CookerMain.cpp:42-43`), and `ValidateAssetTree` raises "model imports but contains no meshes" to ERR because *"lenient importers accept garbage as an empty scene"* (`AssetValidator.cpp:99-104`). The research names the same failure mode for its corpus survey (`C:/rw/README.md:113-116`). Classify three cases in the wrapper — PROVED (moves > 0, usableCancels > 0, not capped) / TRIVIAL / UNRESOLVED (`capped`) — using data the `Result` already exposes; in the cooker, TRIVIAL and UNRESOLVED are both ERR.
3. **Port `_binding_guards` from the reference.** The C++ `Result::rankingOrder` is a list of resource *indices* only (`comboprover.hpp:203-205`), rendered as *"ends because these run down, in order: juggle"* — with nothing about which move carries the load-bearing check. The Python reference already ships the paper's promised output: `ranking.py:124-152` `_binding_guards` produces `"juggle is checked before airslash, launcher"` in a single O(edges × |order|) pass. The C++ is a **lossy port**, and contribution 4 is literally *"a ranking function naming the load-bearing guards."* ~20 lines upstream plus a field on `Result`.

Two more gaps between the paper's claims and its own C++ artifact, worth fixing upstream while you are in there: `Result::prefix` **omits the opening move** (the fill loop runs `for (int i = 1; ...)`, `comboprover.hpp:511-513`), so a reported infinite is not a performable reproduction; and there is **no loop-closes self-check** in the C++ (`:543-549` returns with none), while `docs/00-research-program.md:76-79` claims *"Every reported infinite is verified to close before it is reported"* — true of `termination.py:92-104`, false of the header. Also add per-step slack, which the Python computes and the scope limits promise (`termination.py:108-124`, `00-research-program.md:149-151`): zero slack means a one-frame-exact input every repetition forever, which is an infinite in the model and nearly impossible in a human's hands. One shared-model caveat to surface in the UI, not to fix: dead cancels and worst-case bounds are computed at the **settled** hit count, so a link that connects on hits 1–2 and dies on hit 3 is reported as *"short by 11"* — flatly wrong as designer-facing text. Never surface `deadCancels` with the word "never"; compute the largest hit index at which each dead edge still connects and say *"connects on hits 1–2, dead from hit 3."*

**What the designer sees.** Never a bare verdict. On INFINITE: the reproduction as a performable string **starting from the opening move** (`opener > a > b > d > [e > d] ×∞`), per-step slack in frames, the loop's net resource change at the closing step, and "cut one of these to break it" — loop edges ranked by re-running `analyse()` with each disabled. On TERMINATING: the certificate as prose in the designer's vocabulary — *"ends because juggle runs down; the guards doing the work are launcher and airslash"* — plus "at most H hits, F frames to the last hit, D damage" with the decay caveat. Always: dead cancels phrased honestly, unreachable moves, a coverage line. Every move and cancel named is click-to-select, so the panel is a navigation surface into the character asset rather than a wall of text.

**What the paper gets, in the order it becomes possible.**
1. **The reproduction harness** — the highest-value artifact. A headless deterministic replay that takes a reported witness and drives the character's own state machine frame by frame, asserting the defender never leaves hitstun for N loop iterations. This is the direct test of the abstraction's central falsifiable claim (*"If predicted infinites systematically fail to reproduce in a real engine, the abstraction is wrong,"* `00-research-program.md:113-120`), and this engine can do it more convincingly than Ikemen GO because the ground truth is **the same file the analyser read**. Gates on Phases 0–3.
2. **The bounds dual** — for every character proved TERMINATING, an in-engine bounded search up to `maxHits + k` asserting no combo exceeds the reported bound. This is what would have caught the settling-decay under-estimate before a reviewer did.
3. **Authoring telemetry** — log every editor analysis run (content hash, move/cancel counts, resource ranges, `explored`, wall-clock ms, verdict, and whether the verdict **changed**) to newline-JSON. Cross-referenced against the character file's git history that yields the paper's strongest sentence: *how many infinites were caught at authoring time, at what median latency, and how long they survived before the tool existed.* **Instrument this from the panel's first day** — it is worthless retroactively.
4. **Gate telemetry** — the cooker's per-character record run in CI, giving build-failure counts over the project's life.

Also ship one differential test as a 51st engine test: run the examples in `C:/rw/examples` through the vendored header and assert verdicts, bounds and dead-cancel shortfalls against values recorded from the Python reference. It is the paper's evidence that the port and the reference agree — currently *asserted* (`engine/example.cpp:8-10`) but exercised only over six hand-built characters that never touch the divergent paths. One known divergence to encode: C++ `meetsGuard` iterates `min(have.size(), guard.size())` (`comboprover.hpp:256-262`) so a guard naming an unknown resource is silently **ignored**, while Python `satisfies` (`model.py:42-48`) makes the move **unusable**. Both are silent; the directions differ in danger. Resolve resource names to indices at import and reject an unknown name as an asset error.

---

## 6. WHAT NOT TO DO

This section exists to protect the schedule. Each of these is attractive and each would cost months.

**Do not make Jolt or PhysX rollback-capable.** Measured desync on both through every path today's seam allows. Adding `saveState`/`restoreState` to `IPhysicsBackend` is correct work *for the general engine* (Jolt already implements it), but it is not on the fighting game's critical path and it will absorb weeks. Fighting-game motion is authored per-frame data and AABB overlap. If some future title needs solver rollback, `SimplePhysicsBackend`'s POD `unordered_map<uint64_t, Body>` round-trips exactly and is the natural default.

**Do not build a general Lua VM snapshot.** Closures capture upvalues, `LuaEntity` userdata carries a raw `IScriptHost*`, metatables and coroutines have no portable serialization — and the shipped `bouncer.lua:9` puts its state in a chunk-level `local`, which is a closure upvalue and would be invisible even to a hypothetical environment-table walker. This is the months-long fight. The table-interpreter route — declarative move data interpreted by deterministic C++ — is days.

**Do not extend `EntitySnapshot` into a rollback snapshot.** 744 bytes, not trivially copyable, polymorphic member, 1300–1600× a `memcpy`, and a closed list whose own comment promises silent destruction of anything you forget. It is a good editor undo facility doing a job it does well twice per play session. Leave it alone.

**Do not host character data in `UIDataSource`.** The UI stack is the obvious thing to reuse and it is the wrong shape: `UIValue::Kind` is `{None,Bool,Int,Number,Length,Color,String}` — no object, no array (`UIValue.h:30`); `UIRecord` is a flat `vector<pair<string, UIValue>>` (`UIDataSource.h:34-61`); a list cannot contain a list. A move (N frames, each with a box set) is two nesting levels beyond what the model expresses. `repeat-count` is mandatory and hard-capped 1..64 (`UIMarkup.cpp:735-752`); tabs cap at 32. Use `nlohmann::json` (already a dependency) for the data and reuse the UI stack's **discipline** instead: strict allow-list, whole-file staged commit with last-good-on-failure, diagnostics quoting what the author typed. That discipline is the genuinely valuable asset and it is a pattern, not a library.

**Do not adopt MUGEN CNS as the authoring format.** The research's own importer documents why: a real character's cancels don't live in its attack states at all but in `[Statedef -1]` as trigger expressions over `stateno`/`movecontact`/`power`, routed through user variables — 1123 lines of importer for partial coverage of a side-effecting trigger interpreter, and it *"does not evaluate helpers, the opponent's state, or randomness, and it does not pretend to."* CNS is the right **corpus** format (that is contribution #7) and the wrong **authoring** format.

**Do not build a general time-based animation system with blend trees and state graphs.** Fighting animation is indexed by integer frame from the state machine, not by wall-clock time. A general animator is months and you would then have to fight it to get frame-exactness back. Build the frame-indexed sampler.

**Do not build a 2D sprite pipeline.** `Renderer2D` is capable — world and screen projections, atlas sub-rects, rotated sprites, sort layers, clip rects, SDF rounded boxes, stb_truetype text — but for an SF6-like the perspective forward renderer is the right tool, not overkill. Use `Renderer2D` for HUD, hitbox overlay, input display, damage numbers. A `SpriteComponent` and a world-space 2D pass are a Phase-8 decision *if* a 2D title is ever wanted.

**Do not build `cook-textures`/`cook-meshes`/`pack`.** For a moddable fighting game, loose files are arguably correct — it is what the MUGEN ecosystem runs on. Make that a written decision rather than an unfinished TODO in `CookerMain.cpp:20-22`. (Do fix path-keyed sidecar identity eventually — `ImportSettings.h:18-22` notes GUIDs are unimplemented and moving an asset silently orphans its settings — but not before the fighter runs.)

**Do not do the `Scene` god-object split as a prerequisite.** `Scene` genuinely fuses simulation and GL draw-list building (`Scene.cpp:1` includes `glad.h`; `Scene.h:424` owns a `GLuint instanceVBO_`; `UpdateTransforms` gathers `dirtyCasters_` mid-hierarchy-walk at `Scene.cpp:301-321` and rebuilds a children adjacency map from scratch every call at `:273`). It is real debt and worth fixing eventually. **But the fighting sim routes around it entirely** by owning its own flat state, so this is not on the critical path and starting with it would burn a month before the first determinism test exists.

**Do not chase editor UI test coverage.** ~5,485 of 6,118 editor lines are untested and most of that is ImGui presentation. Two targeted gaps are worth closing cheaply: `Subprocess` (235 lines of pure I/O plumbing, load-bearing for the Linux port, zero tests) and a headless smoke test that the Player boots a saved scene and renders one frame — which guards the shipped-player-contract invariant that a camera-less scene silently falls back to free-fly.

**Do not start networking before Phase 2 passes T2.** Every hour spent on transport before `Simulate(GameState&, TickInputs)` and `memcpy` save/restore exist is an hour spent on the thin part of the problem.

**Do not add renderer features.** No camera roll, no extra post passes, no new quality tiers, until the game runs. The renderer is the largest piece of already-reusable work in the repository for this goal and it is done enough.

---

## 7. OPEN QUESTIONS

> **ALL SEVEN ANSWERED. See [ADR-002](ADR-002-open-decisions.md).**
> **Q1** (a) crossplay in scope — so fixed-point is mandatory, not advisable.
> **Q2** (a) 3D on a 2D plane — 2D is *more* art, not less, and the 11-pass
> renderer already exists; skinning is ~4 weeks and `Mesh.h:30,:139` show it was
> started and abandoned. **Q3** (a) Lua banned from the simulation.
> **Q4** (b) flat `GameState` — sub-5 µs `memcpy` versus ~907 µs. **Q5** (a)
> verbatim superset, settled by *measurement*: the prover reads our files
> unmodified and stripping every `engine` key changes no verdict. **Q6** (a) yes,
> and it becomes a CI test — same input log through both hosts, compare the hash.
> **Q7** publish the inventory now; publish nothing about rollback until it runs.

Each of these is a choice only the author can make. Each is stated as named options with consequences, because the answer changes the plan.

**Q1 — Is Windows↔Linux crossplay in scope, or only cross-platform *builds*?**
- **(a) Crossplay in scope.** Fixed-point simulation becomes mandatory, not merely advisable; the T3 cross-toolchain hash test becomes a release gate; the physics backend at `PhysicsWorld.cpp:205` must be off the gameplay path (which is already the recommendation); and `unordered_map` iteration anywhere in sim code becomes a defect rather than a hazard. **Adds weeks to Phase 2 and makes CI mandatory.**
- **(b) Matches are always same-platform, same-binary.** libm variance drops from MAJOR to a residual risk (CPU-dispatched libm variants — glibc ifunc, UCRT feature dispatch — across different CPUs on the same OS), which a two-machine hash test settles cheaply. **The fixed-point work is still recommended** for the prover alignment, but it stops being a gate.

*My reading:* the memory note says the goal is to *support* Linux and Windows, which is not the same as requiring crossplay. Answer (b) unless there's a reason not to — but write the answer down, because two specialists disagreed on the severity of an entire class of findings purely because this was unstated.

**Q2 — 3D characters on a 2D plane (SF6-like), or 2D sprites?**
- **(a) 3D.** Skeletal skinning is required and is currently *absent at the vertex level* — `Model.h:37-43` has five attributes and no bone IDs or weights, and `Model.cpp:112-124` binds exactly those five. Adds roughly 4 weeks to Phase 3 (vertex format, bone palette, shader branch, Assimp `aiAnimation` import). The 11-pass forward renderer is otherwise exactly right, and scene cost is trivially within budget.
- **(b) 2D sprites.** `Renderer2D` already has atlas sub-rects and the world-space projection, but there is no `SpriteComponent`, no atlas file format, and no world-space 2D scene pass. Cheaper animation, more art.

*My reading:* the north star says "SF6-like," which means (a). But the frame-indexed clip player is the same code either way — build it first, and the skinning decision can be deferred by weeks without cost.

**Q3 — Is Lua allowed to touch gameplay at all?**
- **(a) Banned from the simulation.** Costs nothing today (Lua is opt-in per entity and the sim core never calls `ScriptWorld` — `Engine/src/core/Scene.cpp` contains zero references to it). Write it into `docs/MAINTENANCE.md` and enforce it with a CI grep. Lua stays for editor tooling, stage events, menus, non-authoritative presentation.
- **(b) Allowed.** Requires `IScriptBackend::snapshot`/`restore`, a deterministic table-iteration guarantee stock Lua does not give you, replacing `math.random` with a snapshot-owned PRNG, and swapping the wall-clock callback deadline for the instruction budget. **Months, and probably still not sound.**

*My reading:* (a), decided now, in writing. This is the cheapest decision in the document and the most expensive one to reverse late. Also rewrite `spinner.lua` to `OnFixedUpdate` so the shipped reference material stops teaching the frame-rate-dependent pattern that `docs/manual/gameplay-scripting.md:41-42` already warns against.

**Q4 — Does the fighting simulation live in the EnTT registry, or in its own flat `GameState`?**
- **(a) In the registry.** Reuses the editor, Inspector, serializer and existing tooling. Costs: eight hand-maintained edits per new component across `SceneSerializer` (save + load), `InspectorPanel` (section + Add-Component entry), `UndoHistory` (field + capture + apply + equality), with **silent data loss** as the failure mode for three of them; plus inherited ordering hazards (packed order permuted by every add/remove, `sparse_set.hpp:403`) and the `Transform::modelMatrix` staleness class. The eight-edit tax is real but currently unbroken and partly test-covered — this was reported as MAJOR and correctly downgraded to MINOR by its verifier, who found that every component in use today *is* in the snapshot list and that `docs/MAINTENANCE.md:122-130` already documents the checklist.
- **(b) Flat `GameState` beside the registry**, with an ECS mirror written for rendering just before `Scene::UpdateTransforms()`. `memcpy` is the entire save/restore. Costs new editor tooling for the fighting data.

*My reading:* **(b), decisively.** It is what every shipped rollback title does, it is what the measured numbers demand (sub-5 µs vs 907 µs), and it makes the trivially-copyable `static_assert` enforceable. But if you go (b), still land a component registry (`{name, toJson, fromJson, snapshot, restore, drawInspector}`) for the *non-simulation* components, because it retires the entire class of bug the three `UndoHistory.h` warning comments describe — and land it with a test that walks the registry and asserts every registered type survives both a serializer round-trip and a capture/restore cycle.

**Q5 — Is the character format the prover's schema verbatim, or engine-native with an exporter?**
- **(a) Verbatim superset.** `json_spec.loads()` reads every field via `raw.get(...)` with no unknown-key check, so the engine can add its own keys and the Python prover reads the file **unchanged**. No export step, no translation layer. The CI gate is literally `python -m comboprover <character.json>`, and the editor's live verdict is `comboprover.hpp` reading a `Character` built from the same object the runtime already parsed. Contribution #9 for close to free.
- **(b) Engine-native + exporter.** Freedom in the format, at the cost of a translation layer that will drift, and a weaker paper claim (the analysed artifact is no longer the shipped artifact).

*My reading:* **(a).** This is the single highest-leverage design decision in the document. If the importer needs cleverness, the asset format is wrong.

**Q6 — Does the Editor's Play mode have to be bit-identical to the shipped Player?**
- **(a) Yes.** Extract the game-side ownership out of `EditorApplication` into a shared `PlayModeHost` that both hosts construct. Today ~90 lines of UI/menu host setup are written twice (`PlayerMain.cpp:208-319` vs `EditorApplication.cpp:543-627`) and the two copies **already differ** in ways that matter — the Player polls the pointer from GLFW and gates nav unconditionally; the editor gates on `playing_ && gameSurfaceFocused_ && !editorCameraHasTheKeys`. The `Install*` family's own comment says why this matters: *"so 'it works in Play' and 'it works in the shipped game' can never drift apart."*
- **(b) No** — accept that Play is an approximation and only the Player is authoritative. Cheaper now; means every desync investigation starts by asking which host you were in.

*My reading:* (a), and it is days of work, not weeks, because the seam already exists.

**Q7 — Publish the inventory now, or after Phase 0?**
- **(a) Now.** The §1 numbers are verified and defensible today. The engine is genuinely impressive as a general-purpose engine and undersold by a factor of five.
- **(b) After Phase 0.** Adds "deterministic simulation, verified in CI" to the pitch, which is the claim that makes a reviewer take the fighting-game direction seriously.

*My reading:* publish now with the §1 wording, and add the determinism claim the day CI is green. Do **not** publish anything about rollback, determinism, or the research integration until T1 passes — a claim that outruns the code is exactly what a reviewer checks first, and this project's biggest asset right now is that its documentation is unusually honest.

---

## Appendix — the invariants worth writing into `docs/MAINTENANCE.md` today

These are properties that are **true right now** and each is one well-meaning commit from being lost. Several were established only by grep during this audit, meaning nothing currently protects them.

1. **No RNG anywhere.** Grep for `rand()`/`std::rand`/`mt19937`/`random_device`/`srand`/`uniform` across `Engine/src`, `Editor/src`, `Player/src` returns **zero hits**. Any future RNG must be a snapshot-owned counter-based generator.
2. **No fast-math.** No `/fp:fast`, `-ffast-math`, `/arch:AVX`, `-march=`, `-ffp-contract`, or `GLM_FORCE_*` in `CMakePresets.json`, any `CMakeLists.txt`, or `cmake/`.
3. **Physics worker threads stay at 0.** `PhysicsTypes.h:143`, honoured at `JoltPhysicsBackend.cpp:252-256`, one collision step per fixed dt at `:386`. The comment there already says this is what the engine wants for determinism, and it is true.
4. **No wall-clock reads on the simulation tick.**
5. **No transcendentals on the simulation tick.**
6. **No `unordered_*` iteration in simulation code.**
7. **Ordering keys use the entity index, never the raw handle.** Already an invariant; `CameraDirector.cpp:23` obeys it with `entt::to_entity` and an explanatory comment, while `UIWorld.cpp:189` breaks ties on `entt::to_integral` — the raw handle, version bits included — which is the documented mistake.
8. **The JobSystem contract holds:** workers never touch the registry or GL (`JobSystem.h:23-27`), completions run on the main thread, and `Renderer.cpp` contains no registry access at all, so rendering cannot feed the simulation.
9. **Asset load order does not reach the simulation:** physics builds from authored collider components (`PhysicsWorld.cpp:64-90`), never from loaded mesh AABBs.
10. **The physics components are deliberately free of runtime handles** (`PhysicsComponents.h:5-10`) — which is exactly the property a POD snapshot needs, and the reason to keep it.

A CI grep for `/fp:fast`, `rand(`, `steady_clock` and `unordered_` under a `sim/` directory would enforce most of this mechanically, for about an hour of work.