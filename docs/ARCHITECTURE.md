# ARCHITECTURE DECISION DOCUMENT
## A deterministic, rollback-capable, cross-platform, data-driven fighting-game framework

**Status:** Accepted, **amended by [ADR-001](ADR-001-fighting-core.md)**. **Date:** 2026-08-12. **Supersedes:** nothing. **Baseline:** commit `8c5ad20`, branch `audit-fixes-2026-08`.

Four specialists investigated and a principal engineer attacked each recommendation. Where they disagreed I adjudicated, and I re-verified every disputed fact against the sources myself. Verifications I ran this session are marked **[V]**.

> **Phase 0 has since been RUN, and it amended five sections of this document.**
> D2, D7 and D8 survive; one prescribed fix in Phase 0 and one in §5.2 were
> **refuted by measurement and struck**; D8 gains a new hard rule; Phase 5's scope
> grew. Amendments are marked inline in the affected sections and are quoted like
> this. Read [ADR-001](ADR-001-fighting-core.md) before acting on Phase 0, D8,
> Phase 5, §5.2 or §5.3 — **two of the instructions this document originally gave
> would have fabricated an infinite combo.**

---

## 0. THE ONE-PARAGRAPH SUMMARY

The authoritative fighting-game simulation is a **fixed-size POD struct of integers**, ~2.8 KB, simulated by a **tick-driven integer kernel** that includes no float, no Jolt, no EnTT, no Lua, and no libm. It is snapshotted by `memcpy` into a 128-slot ring, rolled back by `memcpy` back, and re-simulated up to 8 ticks in ~300-500 µs (2-3% of a frame). Cross-platform bit-identity is a property of integer arithmetic, not of a build flag — so **no Jolt overlay port, no NEON patch, and no ARM CI leg is on the critical path.** Character behaviour is authored data whose schema is a superset of `comboprover::Character`, so the combo prover reads the shipping game through a ~200-line total adapter instead of a 1123-line heuristic importer. EnTT stays as the presentation registry; Jolt stays for cosmetics, props, and the framework's non-fighting games. The first phase writes no engine code: it transcribes three real characters into the schema and runs the prover on them, because "does the declarative model fit a real fighting game, and does the prover answer usefully" is the only unknown that can invalidate everything else.

---

# 1. THE DECISIONS

## D1 — The simulation / presentation split

**Decision.** Authoritative simulation state lives in one `struct GameState` outside the EnTT registry: POD, trivially copyable, fixed-capacity dense arrays, no pointers, no `std::string`, no `std::vector`, no virtuals. Everything in the registry, every renderer/camera/audio/UI object, and `Transform::modelMatrix` is presentation, derived from `GameState` each rendered frame and never read back.

**Why.** Not performance — **correctness**. The registry structurally cannot be snapshotted:

- `Name` (`Components.h:17-20`), `ModelComponent` (`:85-87`) and `MaterialOverrides` (`:90-92`) are not trivially copyable.
- `AABB` (`Components.h:286`) derives from the polymorphic `BoundingVolume` (`:181-196`), so it carries a **vtable pointer** — a process address that must never enter a snapshot or a wire.
- EnTT packed order is permuted by `swap_and_pop` on every component removal (`sparse_set.hpp:253`), so any loop over a view is order-unstable across a restore.
- EnTT recycles indices with bumped versions on `clear()`+`create(hint)`, which is exactly why the editor hierarchy jumps from `1..401` to `1048577..`.

I explicitly **reject** the performance framing both the rollback specialist and I initially reached for. The specialist's own sizing said full-registry snapshot is 0.003% of a frame, and the critic recomputed `entt::snapshot` at 12-40 µs, not 30-80 µs. **The critic is right and the perf argument is void.** State the correctness case alone; it is the one that holds.

**What it trades away.** The Inspector, the scene serializer, and undo/redo all work on EnTT components for free. `GameState` gets none of that until we build it an authoring path, and we now maintain two notions of "an entity" joined by a reconciler. Mitigation that inverts this cost: a ~100-line reflection table over `GameState` (`{name, offset, type, count}`) yields the Inspector view, the JSON serializer, **and** the per-field desync log from one artifact. The table is cheaper than the desync log alone.

**Revisit if.** A gameplay feature genuinely requires emergent rigid-body interaction inside combat — throwable physics objects whose contacts decide hits. Then D2 changes, and this follows.

---

## D2 — What the gameplay core is built out of

**Decision.** The authoritative core is **plain `int32` in fixed sub-units (1 pixel = 256 sub-units)**, with **no general fixed-point type and no general fixed-point multiply**. Positions, velocities, and hitboxes are integers. Every genuine scaling operation goes through one helper with one documented rounding rule. Jolt is not in the authoritative path.

```cpp
// The ONLY place two non-frame-count quantities are multiplied.
// Round-half-away-from-zero: symmetric about 0, so a mirrored
// character loses the same LSB moving left as moving right.
constexpr int32_t scaleBy(int32_t v, int32_t num, int32_t den) {
    const int64_t p = (int64_t)v * num;
    const int64_t h = (den >> 1);
    return (int32_t)((p >= 0) ? (p + h) / den : (p - h) / den);
}
```

**Why.** Three independent arguments, in descending strength:

1. **Bit-identity is a property of the language, not of a vendor.** Integer add, shift, compare, and AABB overlap are exact on x86-64, AArch64, MSVC, GCC, and Clang. No flag, no patch, no CI matrix is required to *believe* it. Contrast the float path: `JPH_CROSS_PLATFORM_DETERMINISTIC` disables FMA on x86 only — the ~20 `vmlaq_f32` sites in `Mat44.inl` (259-261, 281-282, 303-305, 325-326, 371-372), `DMat44.inl` (85-86, 203-204, 240-241), `Vec3.inl:261` and `Vec4.inl:245` are unguarded, and `Mat44::operator*` is on the path of every body transform. The flag as shipped is **not sufficient for x86↔ARM**.
2. **The decision procedure needs the discrete automaton to be the game.** `comboprover.hpp` decides over `(move index, frame in move, integer resource vector, hits so far)`. A continuous rigid-body simulation is not analysable by it. Contribution #9 degrades from "the analysis decides this game" to "the analysis decides a model of this game" the moment the authoritative layer is continuous.
3. **The 8× re-simulation budget stops being a measurement.** An integer kernel is ~30-60 µs/tick (see D4); nobody has ever measured a Jolt step in this engine.

**Adjudication — the specialist's headline argument is false and I am striking it.** "Every combat-relevant quantity is already an integer" does not survive reading the file **[V]**: `Move::damage` is `float` (`comboprover.hpp:127`), `Character::scaling` is `std::vector<float>` (`:156`), `Decay::ratio` and `Decay::table` are float (`:71,:73`), and `Decay::hitstun` (`:76-95`) does **repeated float multiplication then `static_cast<int>`**. The correct claim is narrower and still sufficient: *every quantity that the simulation integrates over time is an integer, and the handful that are not (damage, scaling, decay ratios) are single multiplies at authoring-time boundaries that we quantize at load.* See D8 for the consequence — this is the seam where the engine and the paper can silently disagree by one frame of hitstun, which is exactly the difference between `Terminating` and `Infinite`.

**Adjudication — Q16.16 vs plain sub-units.** The critic's simpler option wins. A 2D fighter almost never multiplies two positions; it adds velocity to position and compares rectangles. A general `Fixed` type buys an overflow surface, a division-vs-shift rounding mismatch (`>>` rounds toward −∞ while `/` truncates toward zero, which makes the sim **not mirror-symmetric** and accumulates), and a C++17 implementation-defined arithmetic-right-shift question (`CMakeLists.txt:12` is `CMAKE_CXX_STANDARD 17` **[V]**). Delete the type; keep one helper.

**What it trades away.** We own a simulation Jolt would have given us. Jolt's decade of edge cases stops helping the moment gameplay wants a slope or a wall-bounce with non-axis-aligned normals. Two collision systems live in the tree with a standing temptation to wire a gameplay object into the wrong one. And the two features that motivated the dependency choices are retired **for this game**: `PhysicsSystem::SaveState` and `entt/entity/snapshot.hpp` are both unused by the rollback loop. **Say this out loud rather than softening it** — Jolt and EnTT are not being replaced, but neither is doing the job it was chosen for here. Both remain load-bearing for the framework's non-fighting games and for late-join (D5).

**Revisit if.** A shipped title needs solver-quality contact response inside combat, or the character controller starts accreting a general collision solver of its own. The second is the real failure mode; watch for it.

---

## D3 — Cross-platform determinism mechanics

**Decision.** Determinism is enforced by four mechanisms, in this order of load-bearing-ness:

1. **The integer kernel** (D2). Does 95% of the work.
2. **A grep gate in CI** on every generated `build.ninja` under `out/build`, *including* `vcpkg_installed`, failing on `/fp:fast`, `-ffast-math`, `-Ofast`, `-ffp-contract=fast`, and asserting the expected `/arch:` flags and `JPH_USE_*` defines.
3. **`cse_fp_strict`**, an INTERFACE target linked by every simulation-adjacent target: `/fp:precise /fp:except-` on MSVC, `-ffp-contract=off` elsewhere, plus a `FATAL_ERROR` if `CMAKE_CXX_FLAGS` ever matches `fast-math|/fp:fast`.
4. **A cross-toolchain hash test** (Phase 1), which is the only thing that converts any of the above from an assertion into a fact.

**We are NOT flipping `JPH_CROSS_PLATFORM_DETERMINISTIC` on the critical path.**

**Why the grep gate is item 2 and not item 6.** It is the highest value-per-hour item in the entire investigation. It would have caught, automatically, two things that no amount of reading `CMakeLists.txt` or the vcpkg manifest could reveal:

- **The linked Jolt is compiled with `/fp:fast` on all 139 translation units.** `Build/CMakeLists.txt:170-176` appends it at directory scope when the determinism flag is off, and never writes it back to the cache — so `CMakeCache.txt` shows only `/nologo /DWIN32 /D_WINDOWS /utf-8 /GR /EHsc /MP`. The established fact "the engine build sets no `/fp:fast` anywhere" is true of the engine's own TUs and **false of the physics library it links**.
- **Windows Jolt and Linux Jolt are different libraries.** `Jolt.cmake:614` gates the entire x86 block on `CMAKE_VS_PLATFORM_NAME`, which is empty under vcpkg's Ninja generator, so Windows gets SSE2 baseline and zero `JPH_USE_*` defines. Linux takes the `CMAKE_SYSTEM_PROCESSOR` branch at `:636` and gets `-mavx2 -mfma` with `JPH_USE_AVX2`/`JPH_USE_FMADD`. Same manifest line, two numerically different libraries. `JPH_VERSION_ID` does **not** encode instruction set (`Core.h:68,71`), so a connect-time handshake cannot catch it.

**Adjudication — the RelWithDebInfo abort is not real. I verified this myself [V].** The xplat specialist predicted `RegisterTypes()` would `std::abort()` in the committed RelWithDebInfo presets because `Jolt.cmake:519-520` gates `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` on `$<CONFIG:Debug,Release>`. It does not. vcpkg's toolchain sets `CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;None;"`, and CMake evaluates the genex against the *mapped* config. `out/build/x64-RelWithDebInfo/build.ninja` reads:

```
DEFINES = -DCSE_WITH_JOLT -DENGINE_DLL_EXPORTS -DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED -DJPH_OBJECT_STREAM ...
```

The define is present on the consumer. There is no live bug and no experiment to run. **The critic was right; delete the claim.**

**Adjudication — no Jolt overlay port, no NEON patch, no ISA pin.** Under D2, Jolt computes hit sparks, debris, ragdolls, and level props: things that by construction cannot desync a match. The proposal on the table was to ship a **locally patched physics engine**, eat an estimated 1.5-2.5× on physics throughput (the SSE4.1 `#else` branches are scalar C, not SSE2 — 37 `JPH_USE_SSE4_1` sites across `Vec3.inl`, `Vec4.inl`, `UVec4.inl`, `Mat44.inl`), and maintain a NEON macro patch across every upstream upgrade for five years — **to make hit sparks bit-identical across architectures.** That is a bad trade and it should never have been week 2. Same-binary determinism, which `PhysicsSettings::mDeterministicSimulation` (default true, `PhysicsSettings.h:93`) plus `workerThreads = 0` (`PhysicsTypes.h:143`) already gives today, is sufficient for cosmetics that are re-derived from confirmed state anyway.

**The overlay port is still worth one engineer-day, later, as framework work** — a deterministic Jolt is an asset for the framework's non-fighting networked games. It is gated behind: (a) a measured `PhysicsSystem::Update` cost on a representative scene, and (b) the existing 12-case `PhysicsConformance` suite passing. If either fails, drop it having lost a day.

**What it trades away.** We give up ever running the fighters through Jolt without redoing this analysis. We accept that the framework's *other* games do not get cross-platform determinism for free.

**Revisit if.** D2 is revisited, or a second title on this framework needs networked rigid-body physics. Then the overlay port, the NEON patch, and the ARM CI legs all come back, and the grep gate must be extended to assert `/arch:` parity.

---

## D4 — The rollback snapshot design

**Decision.**

| Item | Value |
|---|---|
| Snapshot unit | `GameState`, one `memcpy` |
| Size | 2 chars × 320 B + 32 projectile slots × 64 B + 128 B globals = **~2.8 KB** |
| Ring | **128 slots**, sized to the protocol's max prediction window, not to rollback depth. 360 KB resident. |
| Save cost | ~0.3 µs (L2-resident, 10 GB/s) |
| Restore cost | ~0.3 µs |
| Re-simulation | ≤8 ticks × 30-60 µs = **240-480 µs**, i.e. **1.4-2.9% of a 16.67 ms frame** |
| Checksum | FNV-1a over the 2.8 KB, ~0.3-1 µs, every tick |
| Wire | 4 B input/tick + last 8 inputs redundantly = **~1.9 KB/s**, plus a 4 B checksum every 8 ticks |

`GameState` contains, and this is exhaustive: tick counter, RNG state, per-slot occupancy and generation counters, per-character `{posX, posY, velX, velY, facing, stateId, stateFrame, animId, animFrame, health, meter, juggle, stun, hitstun, blockstun, hitstop, comboHits, gravityScale, flags, alreadyHitBits, var[32], fvar[16]}`, per-projectile the same shape narrowed, and globals `{roundTimer, stageMinX, stageMaxX, cameraX, nextGeneration}`.

**Ring exhaustion is defined behaviour: STALL, never drop.** If a correction targets a tick older than the ring, or the local sim is more than `kMaxPrediction` ticks ahead of the last confirmed remote input, the session runs **zero ticks this frame**. Running zero ticks is a legitimate hitch; dropping a tick is an unrecoverable desync. This is the single most common way a first rollback implementation fails in the field and it was absent from every proposal.

**Adjudications.**

- **`entt::snapshot` is rejected for the rollback loop** and retained for the late-join / spectator handshake, where `continuous_loader`'s entity remapping is exactly right. It is a save-file and network-join API, not a 60 Hz API.
- **`PhysicsWorld::Rebuild` is not a restore and can never be made into one.** `PhysicsWorld.h:47` is `Clear()+Build()`; `RigidBody` has no velocity field (`PhysicsComponents.h:21-30`), so velocity, sleep state, warm-start impulses, contact manifolds and island assignment are all discarded, and `destroyAllBodies` frees in `std::unordered_set` hash order (`JoltPhysicsBackend.cpp:369`) so the next `Build` assigns different `BodyID`s on a different STL. The measured "desync" recorded in `docs/NORTHSTAR.md:117-139` measured `Rebuild`, which could not possibly have round-tripped. **That report does not indict Jolt's `SaveState` path, which has never been tested in this repo.** Correct the doc.
- **Jolt is not snapshotted at all.** If it ever is, note the asymmetry the rollback critic found: `BodyManager::RestoreState` iterates the *stream*, not the world (`BodyManager.cpp:824-849`), so a body created after the snapshot survives the restore as a live ghost. Pooled bodies must be `DeactivateBody`'d, never `RemoveBody`'d, because removal takes them out of the broad phase and therefore out of `SaveState`'s filter (`:759`).
- **Rollback side effects are suppressed by a `Phase` parameter, never a global.** `Simulate(GameState&, TickInput, Phase)`. Audio, VFX, camera shake, contact events and logging are **appended to a per-tick event buffer inside `GameState`**, and presentation drains only confirmed ticks, deduplicating by `(tick, SimId, eventId)`. Otherwise a 7-frame rollback plays the hit sound 8 times. This is free if designed in from line one and an audit of every call site if retrofitted.

**What it trades away.** A fixed-capacity projectile pool. 32 slots is a hard cap; overflow drops the oldest, deterministically. An unbounded spawn list would break the fixed-size-snapshot property, so it is forbidden.

**Revisit if.** Snapshot exceeds ~64 KB (then reconsider delta encoding), or a design needs more than 32 simultaneous non-character actors.

---

## D5 — Entity identity

**Decision.** `SimId` is a packed `uint32`: low 16 bits are a **slot index** into the dense array, high 16 bits are a **generation counter**. Both live inside `GameState`, so both roll back. Slot allocation is a **linear scan for the lowest free slot** — 32 compares, no free list, nothing extra to snapshot, deterministic by construction. Authored entities occupy slots `[0, N)` assigned by file order; runtime spawns take `[N, 32)`. `entt::entity` never crosses into `Simulate`.

**Why.** The spawn-identity mechanism must be *inside the snapshot* or it cannot replay. A rollback to tick N−3 restores the occupancy bitmap and generation counters to their N−3 values, so re-simulation reissues exactly the same `SimId`s. Do not hash the spawn site; do not use a process-global counter.

**Adjudication — the reconciler must not leak EnTT handles into visible ordering.** The rollback specialist proposed changing `UIWorld.cpp:189` from `to_integral` to `to_entity`. That is wrong and makes it worse: under D1 the reconciler creates and destroys presentation entities per frame, and `registry.create()` recycles indices from a free list whose history differs between a peer that rolled back and one that did not. Stripping the version bits removes the only thing making the value monotone. **Presentation ordering ties must be broken on `SimId`, full stop.** Two sites:

- `CameraDirector.cpp:23` — priority ties on `entt::to_entity(e)`.
- `UIWorld.cpp:189` — draw order on `entt::to_integral(a)`.

Two spectators of the same match can otherwise see a different camera win a tie.

**Also fixed by this decision.** `SceneSerializer.cpp:115-128` currently encodes parent links as array position and keeps them meaningful with a `std::reverse`; authored `SimId`s replace that. `BodyDesc::userData` carries `SimId` instead of `entt::to_integral(e)` (`PhysicsWorld.cpp:160`). And the editor's one-line fix, changing only the *second* argument (the first is the ImGui ID and correctly wants full uniqueness):

```cpp
// SceneHierarchyPanel.cpp:84-85
GetEntityLabel(reg, e), (unsigned)entt::to_entity(e));
```

**What it trades away.** A hard cap on simultaneous sim entities and a break in the current save-file format.

**Revisit if.** The cap becomes limiting. Widen the slot field, never remove the cap.

---

## D6 — The input path

**Decision.** `Simulate` takes input **as a value parameter** and never queries `InputMap`.

```cpp
struct PlayerInput { uint16_t buttons; int8_t dir; uint8_t pad; };  // 4 B
struct TickInput   { PlayerInput p[2]; };                           // 8 B
```

`dir` is **9-way numpad notation, quantized at the sampling boundary** — before anything the network or the sim sees — so the float deadzone (`InputMap.cpp:247-252`) and per-pad float variance stop being cross-peer concerns. A fighting game has no analog input.

The input ring is 256 ticks × 8 B = 2 KB, indexed by tick, **separate from `GameState`** (it is append-only and never mutated by the sim). Three arrays: `local[]`, `remote[]`, and a `confirmed` bitmask. Remote is predicted by repeating the last confirmed input; on arrival, if `remote(N) != predicted(N)`, tick N is the rollback target.

**The biggest omission in the entire investigation, now closed.** `InputMap` contains a **phase latch** — `consumePressed` serves a press exactly once per phase (`InputMap.h:87-108`), `beginInputPhase` runs per tick (`Application.cpp:190`), `clearPressLatches` drops unconsumed latches (`:116-119`), and `ScriptWorld.cpp:169-176` calls `consumePressed` so the *first* script to ask sees `true` and every later one sees `false`. That is hidden, order-dependent, consuming mutable state living **outside the snapshot, in the one subsystem rollback depends on most.** Re-simulating a tick either double-consumes a press or sees nothing.

**Adjudication — the latch is not sloppiness and "stop using it" is not the fix.** `InputMap.h:88-93` documents the real problem: `wasPressed` is scoped to a *rendered frame* while the fixed tick may run zero or several times per frame. The structural fix:

- The session clock decides how many ticks run. Each tick consumes exactly one **immutable** `TickInput` record from the ring, sampled once when the tick is scheduled.
- Between ticks, the producer OR-accumulates a sticky "pressed since last tick" mask so a tap during a 3-frame stall is not lost. That mask is **producer-side, local-only, never re-read during re-simulation** — it therefore never needs to be snapshotted.
- `InputMap` keeps its latch machinery unchanged for the editor and menus, where it is correct and pinned by `test_input_map.cpp`. It becomes a *producer* into the ring, nothing more.

**Break the UI coupling.** `Application.cpp:105` calls `input_->clearPressLatches()` when the UI wants text — one line, one mechanism (the specialist described this as a block gating gameplay; it is not, and locating it precisely makes the fix a one-liner). While a network session is live, the producer ignores UI capture entirely. Pausing becomes a session protocol message, not a local `paused_`/`timeScale_` (`Application.cpp:181`), because a local pause changes how many ticks run and the peer will not agree.

**What it trades away.** No analog movement, ever. No mouse in the authoritative sim.

**Revisit if.** A framework title needs analog gameplay input. Then quantize to a fixed integer grid at the boundary and keep everything else.

---

## D7 — The behaviour-authoring language

**Decision.** Behaviour is **authored data**: a JSON character format whose schema is a **strict superset** of `comboprover::Character`, plus a small trigger expression language in the MUGEN-CNS tradition, evaluated by a tree-walking integer interpreter. **Lua is removed from the simulation path.** The escape hatch is **named C++ effects** registered by the engine and invoked from data as opaque rows.

**Why Lua goes.** Four determinism failures in the vendored 5.4.7, one of which has no fix:

1. String hash seed is randomised per process — `lstate.c:71-78`, `time(NULL)` plus three ASLR'd addresses. Mitigable via `-Dluai_makeseed(L)=<const>` (the guard at `:58` allows it), but only through an overlay port.
2. **Non-string table keys hash by address, and this cannot be fixed.** `ltable.c:159-185` `mainpositionTV` does `hashpointer` for lightuserdata, C functions, tables, userdata and closures, via `point2uint` (`llimits.h:91`). `LuaEntity` is a userdata (`LuaScriptBackend.cpp:301`), so the most natural line a modder can write — `for e, box in pairs(activeHitboxes)` — enumerates in heap-address order. Windows PE and Linux ELF will never agree. **No compile error, no runtime error, no way for the host to detect it, and every mod is a fresh chance to reintroduce it.**
3. The whole `math` library routes to libm (`lmathlib.c:41-191`), and `^` is `pow` (`llimits.h:340-341`) — undoing exactly what Jolt refuses to do (`Trigonometry.h:9`).
4. `math.random` auto-seeds from `time(NULL)` + `(size_t)L` at library open (`lmathlib.c:628-631`).

**Adjudication — the snapshot-cost argument against Lua is wrong twice over, and it does not matter.** The specialist said Lua's heap can't be cheaply snapshotted; the critic showed it can (the backend already owns the allocator at `LuaScriptBackend.cpp:388`, so an arena makes it a ~30 µs `memcpy`). Then the critic found the real cost the specialist missed: `ScriptWorld.h:9-12` states that `Rebuild()` **must** run after any bulk registry restore, and `Rebuild` recompiles every script through `safe_script` (`LuaScriptBackend.cpp:449`) — 1-4 ms per rollback event, 6-24% of the budget. **Both are moot.** Reject Lua on failure 2, which is unenforceable, not on cost, which is arguable.

**The sunk cost of removing Lua is approximately zero: the entire Lua content footprint of this repository is two files [V]** — `Editor/src/Exported/Scripts/bouncer.lua` and `spinner.lua`. Lead with that.

**Adjudication — "the prover reads the shipping game with no importer at all" is FALSE. Striking it.** `comboprover` consumes exactly two guard/effect forms **[V]**: `meetsGuard` is componentwise minimum on an integer vector (`comboprover.hpp:256-262`) and `applyEffect` is componentwise addition (`:249-254`). A trigger language with `var(1) == 1` or `stateno != [3050,3100)` — both taken from real MUGEN characters (`mugen_cns.py:20-24,34-40`) — is expressible as neither. The abstraction step is real and lossy, which is precisely why the plan itself specifies keeping `triggers.py:56-64`'s three-valued `DOUBTFUL` set. If the format were the prover's format, three-valued evaluation would be dead code.

**The honest claim, which is still the strongest reason to do this:** *the importer becomes a ~200-line total function over our own schema, whose every lossy step is documented with a soundness argument, instead of a 1123-line heuristic over a foreign format that its own docstring admits cannot evaluate helpers, opponent state, or randomness.*

**What `IScriptHost` does not have.** Verified **[V]**: log, name, transform get/set, impulse, linear velocity, raycast, three input queries, `timeSeconds`. That is it. There is no animation system with per-frame events, no hitbox/hurtbox authoring or storage, no hit detection with priority and trade resolution, no hitstop, no pushback, no juggle or proration bookkeeping, and no state-machine runtime. **This is why the schedule in §3 is months, not weeks** — the DSL is not the expensive part; the combat systems it drives do not exist.

**What survives from the existing seam.** `ScriptWorld`'s lifecycle, registry, `PathSandbox` containment (`ScriptWorld.cpp:246`) and report-once error policy (`:337-348`) are worth keeping verbatim. `IScriptBackend`'s five-hook, `dt`-carrying interface (`:62-81`) does not — a fighting-game state machine wants `step(SimId, frame)` with no `dt` at all. Being honest matters here: the seam makes the swap **supported**, not **free**.

**What it trades away, deliberately and permanently.** No loops, no recursion, no allocation, no strings, no closures, no unbounded collections in authored content. Some behaviour that would be five lines of Lua becomes a C++ effect plus a schema field. Every one of those constructs is a way for a mod to desync a match, and their absence is what makes the prover's answer a decision rather than a guess.

**Revisit if.** More than ~20% of moves in a real character need a C++ escape hatch (see Phase 0). That means either the vocabulary is wrong (fixable) or the approach is (not).

---

## D8 — Where the floats live, and the quantization rule

**Decision.** The authored schema stores integers. The three float quantities in the research model are quantized **at load, on both peers, by one specified rule**:

| Quantity | Source | Stored as |
|---|---|---|
| `Move::damage` (`comboprover.hpp:127`) | float | `int32` in 1/100 units |
| `Character::scaling[]` (`:156`) | `vector<float>` | `int32` in 1/1000 units |
| `Decay::ratio` / `Decay::table[]` (`:71,:73`) | float | `int32` in 1/1000 units |

`Decay::Kind::Multiplicative` (`:82-88`) does repeated float multiplication then `static_cast<int>` **[V]**. The engine must implement decay with `scaleBy` (D2), and the adapter must feed the prover values that produce the *same* truncation. **For a decision procedure whose output hinges on whether `hitstun >= startup - advantage`, a one-frame disagreement between the engine and the prover is exactly the difference between `Terminating` and `Infinite`.** This is the single most likely way the paper's case study ends up certifying a game that is not the game. It is also cheap to close: prefer `Decay::Kind::Table` with integer multipliers, and add an assertion in the adapter that engine-computed and prover-computed hitstun agree for all `(move, hitsSoFar)` up to `settlingIndex`.

**Adjudication.** Neither specialist raised the float→fixed load-time quantization rule at all, and it is a start-of-match divergence source that no amount of rollback correctness repairs.

> **AMENDED by [ADR-001](ADR-001-fighting-core.md). The hitstun gap above is closed outright rather than mitigated, and one new hard rule is added.**
>
> MUGEN 1.0 has **no global hitstun decay** — every `HitDef` states an absolute
> `ground.hittime` and nothing reduces it as a combo runs. So `decay.kind: "none"`
> is the truthful transcription, and like `"linear"` it is pure integer arithmetic
> in both implementations (`model.py:257-259`, `comboprover.hpp:74-75`). There is
> then no float multiplication to disagree about. **Forbid
> `Decay::Kind::Multiplicative` in the schema** rather than working around it.
>
> **NEW RULE: `decay.floor` must never exceed the smallest hitstun in the file,
> asserted at load.** Both implementations compute `max(floor, base − step·n)`, so
> a floor *above* a move's base hitstun **raises** it. This is not hypothetical:
> the draft house rule (linear, step 2, floor 10) exceeded `stand_lp`'s authored
> `ground.hittime` of 9 and **fabricated an infinite** in our own first draft.
> Found by measurement, not review.
>
> Also measured: a decay rule invented for a character that has none *deletes the
> character*. Both implementations evaluate cancel edges at the **settled**
> hitstun (`comboprover.hpp:336-339`), so a 2-frames-per-hit rule collapsed 128 of
> 134 real cancels to dead. Authoring decay you did not derive from source is the
> single most dangerous mistake this schema permits.

---

## D9 — Named choices where I am genuinely torn

> **ANSWERED. See [ADR-002](ADR-002-open-decisions.md).** In brief: **A** adopt
> GekkoNet, but it is **not in vcpkg** — adopting means vendoring, and the
> two-day spike is now a gate. **B** data-only first, confirmed by Phase 0's
> measurement (a DSL would have addressed 1.7% while 39% was missing struct
> fields). **C** abort and name the frame, plus write a repro artifact. **D** keep
> Jolt — and make the D2 boundary a **link error** by giving the gameplay kernel
> its own target that links no Jolt, EnTT or Lua, because today proved a rule the
> build enforces beats a rule in a document.

**CHOICE A — Write the rollback session layer, or adopt one.**
*Recommended default: **adopt GekkoNet** (MIT, C, header-light, designed for exactly this), and own only `(state, inputs) -> state`.*
Input delay, prediction, confirm frames, frame-advantage/rift adjustment, packet loss, disconnect handling and desync detection are 6-12 weeks of the riskiest code in the project, and none of it is differentiating. The requirement says "GGPO-style" and no proposal on the table mentioned GGPO once. Wrap it behind a thin `ISession` so it can be replaced; **do not** write `GameState` into the session layer's types, or game #2 forks it. If GekkoNet turns out to be a poor fit, fall back to writing it — but decide after a two-day spike, not on principle.

**CHOICE B — Trigger DSL in Phase 3, or data-only first.**
*Recommended default: **data-only first.*** Phase 3 ships frame data + cancel edges + per-frame boxes with **no expression language** — the exact `comboprover` fragment and nothing more. The trigger language lands in Phase 5, after Phase 0 has told us which conditions real characters actually need. Building the parser before the transcription exercise is designing a language from imagination.

**CHOICE C — Desync response: resync, or abort.**
*Recommended default: **abort the match, name the frame**.* The xplat critic proposed periodic authoritative state resync, which would demote determinism from a correctness requirement to an optimization. That is genuinely attractive for a server-authoritative game — and wrong for 2-player P2P fighting, where there is no authority, a resync hitch costs a round, and an integer kernel makes determinism free anyway. **Keep the detection half, drop the correction half:** exchange a 4-byte checksum every 8 ticks, and on mismatch stop the match and report "desync at tick 1847, field `p[1].hitstun`". Silent correction in a fighting game is worse than a stop.

**CHOICE D — Does the framework keep Jolt in the fighting-game build?**
*Recommended default: **yes**, `CSE_WITH_JOLT` stays on, cosmetics only.* Ragdolls on knockdown, debris and stage props are worth having and cannot desync anything. The cost is one more subsystem in the process and the standing temptation named in D2.

---

# 2. WHAT WE ARE NOT DOING

Named so they are not re-proposed. Each with the reason and the condition under which it comes back.

| Rejected | Why | Comes back if |
|---|---|---|
| **A vcpkg overlay port flipping `CROSS_PLATFORM_DETERMINISTIC`, on the critical path** | Jolt is not in the authoritative sim. Costs 1.5-2.5× physics throughput and a permanent patch set to make cosmetics bit-identical. | D2 is revisited, or a second title needs networked physics. Then it is one engineer-day of framework work. |
| **Patching Jolt's ~20 unguarded `vmlaq_f32` NEON sites** | Real upstream gap, correctly diagnosed. Shipping a locally patched physics engine that upstream has never validated for x86↔ARM is a bet we do not need to make. The proposed `#undef vmlaq_f32` macro is also unsafe: `JPH_CROSS_PLATFORM_DETERMINISTIC` is exported PUBLIC (`Jolt.cmake:539-540`), so it redefines an ACLE intrinsic in the consumer TU. | Same as above. If it does come back, patch `sFusedMultiplyAdd` and rewrite the 18 call sites — no macro. |
| **Pinning Jolt's ISA down to SSE2** | The mechanism is one-sided and broken: `-DUSE_AVX2=OFF` etc. are consumed by the `Jolt.cmake:614` block that never executes under Ninja on Windows. They only bind on Linux. If it ever must be pinned, pin *up* (`/arch:AVX2` / `-mavx2`) and let `Core.h:132-149` derive the defines from compiler macros. | Never, as written. |
| **Vendoring Jolt as a git submodule + `add_subdirectory`** | Its headline benefit is false: `add_subdirectory` does not fix `CMAKE_VS_PLATFORM_NAME` being empty under Ninja. Its other benefit (the `$<CONFIG>` genex) addressed a bug that does not exist **[V]**. And Jolt's `Build/CMakeLists.txt` mutates `CMAKE_CXX_FLAGS` at directory scope — this repo has already been burned by that class of contamination (`Engine/CMakeLists.txt:192-199`). | Never. |
| **`entt::snapshot` / `continuous_loader` in the rollback loop** | It is an archive-callback loop (~12-40 µs/save, allocating, linear in scene size), and it cannot capture `Name`, `ModelComponent`, `MaterialOverrides` or `AABB`'s vtable. It is the right tool for late-join, which we keep. | Never for the loop. Used in Phase 7 for spectate/join. |
| **`PhysicsWorld::Rebuild` as a rollback restore** | It is `Clear()+Build()`. Not a restore; a re-roll. Discards velocity, sleep state, warm-start impulses, contact cache and island assignment, and reassigns `BodyID`s in hash order. | Never. |
| **Hardening Lua into determinism** (seeded hash overlay port, stripped `math`, arena allocator, `pairs` ban) | 3-6 weeks for a forked language no tooling works against, and rule "no `pairs` over pointer-keyed tables" is **unenforceable from the host**. Silent Windows-vs-Linux desync with no error. | Never in the simulation. Lua may live *outside* the sim — editor tooling, asset pipelines, build scripts — where the seam and the 521-line backend remain an asset. |
| **AngelScript / Wren / QuickJS** | All keep the structural problem: a GC'd heap, their own hash-ordering and libm questions, none readable by the prover, moddability no better than Lua. They change which nondeterminism bugs you find and in what order, not the answer. | Never. |
| **WASM (wasm3 / WAMR)** | *Deferred, not rejected.* Genuinely good determinism properties — math compiles into the module, snapshot is `memcpy(linear_memory) + globals`. But neither is in this vcpkg checkout (only `wasmedge`, LLVM-backed, wrong fit), and "edit a text file" becomes "install a toolchain and compile", losing the non-programmer modder. And the prover cannot read a `.wasm`. | A mod author needs logic the DSL genuinely cannot express. It slots in as another `effect` implementation without breaking anything. |
| **A custom bytecode VM for triggers** | Premature. Corrected evaluation estimate is ~30 µs/tick for 2 characters (the 200 µs figure assumed every projectile carries trigger mass), so ~0.3 ms across a full rollback. And a hand-rolled VM accretes opcodes until it is a general-purpose language with none of Lua's testing. | Profiling on real characters says the tree walk exceeds ~500 µs per rollback event. It is an optimization behind the same interface, not an architecture. |
| **Writing the rollback session / transport layer from scratch** | See CHOICE A. | The GekkoNet spike fails. |
| **Periodic authoritative state resync** | See CHOICE C. | The framework builds a server-authoritative title. |
| **Making the gameplay core a fourth `IPhysicsBackend`** | Would force it through a float API (`PhysicsTypes.h:44-81`) and drag `PhysicsWorld`'s `entt` map and the `DecomposeTRS` round-trip into the rollback path. | Never. |
| **A general `Fixed` type with operator overloads, Q16.16 or Q32.32** | See D2. Buys an overflow surface, a mirror-asymmetry bug, and a C++17 arithmetic-shift question, for a game that rarely multiplies two positions. | Never for this game. |
| **Extending `SimplePhysicsBackend` into the gameplay core** | Float glm throughout, iterates `unordered_map` (`SimplePhysicsBackend.cpp:70,:82`), Y-up 3D, and its own header declares "no dynamic-vs-dynamic collision" (`:10-12`) — the *primary* interaction in a fighting game. | Never. But fix its hash-order tie-break regardless (§6). |
| **`Application`'s `FixedTimestep` driving the sim** | `FixedTimestep::advance` caps at 8 steps then **zeroes the accumulator, dropping the backlog** (`FixedTimestep.h:33-35` **[V]**, and `Tests/test_fixed_timestep.cpp:41-44` *asserts* that behaviour). A dropped tick in lockstep is an unrecoverable desync. | Never for the sim. `FixedTimestep` stays for the editor and single-player. |

---

# 3. THE BUILD ORDER

Total to a networked, cross-platform-verified, moddable fighting-game framework: **6-9 months of primary work.** Anyone quoting 6-10 weeks is quoting the DSL and omitting the combat systems that do not exist (D7). Phases 0-2 are the ones that can kill the design; they cost ~6 weeks and must not be reordered.

---

### Phase 0 — Does the model fit? (1 week, **zero engine code**)

**Why first.** This is the biggest unknown, it is the cheapest to test, and it can invalidate D2, D7, and §5 simultaneously. `docs/00-research-program.md:110-118` names it as the paper's own biggest risk. Building the runtime first means discovering in month four that the vocabulary is wrong.

**Build.** Transcribe **three complete characters** from an existing fighting game into a first-draft JSON schema modelled on `json_spec.py:126-198`. Write a ~200-line C++ adapter to `comboprover::Character` and run `analyse` on all three.

**Proves it works.**
1. **Fit:** count moves needing a C++ escape hatch. **Gate: < 20%.** Above that, the vocabulary is wrong (fix it now, for free) or the approach is (find out now, for a week).
2. **Verdict usefulness:** how many of the three return `Status::Unknown`? Verified **[V]**: `analyse` defaults to `limit = 200000` (`comboprover.hpp:314`), sets `capped` at `:485`, returns `Unknown` at `:553`, and `Config` keys on an unbounded `ResourceVec` with **no ceiling anywhere in the C++ model**. MUGEN authors meter in units of 1 with `power >= 1000` thresholds. ~~**Gate: quantize meter to bars in the schema until all three resolve.**~~ **STRUCK — see the amendment below.** This is a *schema* decision and it must be made here.
3. **Certificate availability:** verified **[V]** that `spendOnly` is cleared (`:376-386`) if any reachable cancel has a positive resource effect, and the ranking certificate is gated on `spendOnly && resCount > 0` (`:563`). Every real character builds meter on hit. **Expect `Terminating` with `hasRanking == false` as the common case** and design the editor panel for it.
4. **Timing:** wall-clock `analyse` per character. Header claims "well under a millisecond" (`:9-13`); confirm on real data.

**Deliverable.** ~~A frozen v1 schema~~, three `.json` characters, and a measured answer to "does the declarative fragment fit."

---

> ### ✅ PHASE 0 IS DONE. Read [`ADR-001-fighting-core.md`](ADR-001-fighting-core.md).
>
> Ran 2026-08-12 on three MUGEN characters (59 moves, 247 cancels). Deliverables
> are in `Editor/src/Exported/Characters/` (moved there 2026-08-13 so the
> asset staging copies them next to the executables). Four amendments to what
> this section says:
>
> **1. The answer, against gate 1.** Moves needing a C++ effect or an expression
> language: **1 of 59 (1.7%)**. D7 and D2 stand. But **23 of 59 (39%)** needed a
> schema field v1 lacks — all of them *missing nouns, not missing verbs*, closable
> by nine named fields. **The v1 schema must NOT be frozen until those exist.**
> That is a blocking item on Phase 3, and it is why "a frozen v1 schema" is struck
> from the deliverable above.
>
> **2. Gate 2's prescribed fix is refuted and struck.** "Quantize meter to bars"
> is not merely unnecessary — it is *harmful*. Rounding up fabricates an infinite;
> rounding down deletes meter. Its stated mechanism is also wrong on its own
> terms: coarsening every resource by a common factor is a bijection on reachable
> states and does not shrink the search at all. **`Unknown` never occurred**, in 6
> runs, at any granularity including 100× finer than authored. Use **1 unit = 10
> MUGEN power**, exact by GCD.
>
> **3. Gate 3 confirmed, gate 4 confirmed.** `hasRanking` false in both
> terminating cases, as predicted. `analyse` in C++: **0.033 / 0.041 / 0.009 ms**
> — the header's claim holds on real characters.
>
> **4. A finding this section did not anticipate.** 26 of 247 cancels gate on the
> **defender** (`p2bodydist`, `p2movetype`, a juggle counter). Phase 5 below is
> scoped for a *self* namespace only. And one character selects its attack by a
> random roll evaluated every tick — a **policy, not a rule**, which D7's escape
> hatch does not cover and which does not belong in move data at all. See the
> Phase 5 note.

---

### Phase 1 — Prove determinism before there is anything to be non-deterministic (2 weeks)

**Build.**
- `cse_fp_strict` INTERFACE target + the `fast-math` `FATAL_ERROR` guard.
- `.github/workflows/ci.yml`: **the grep gate first** (fails on `/fp:fast`, `-ffast-math`, `-Ofast`, `-ffp-contract=fast` in any generated `build.ninja` under `out/build`, including `vcpkg_installed`; asserts expected `/arch:` and `JPH_USE_*`), then build+test on windows-latest/MSVC and ubuntu-latest/GCC-13. **No ARM leg. No macOS leg.**
- `tools/det_trace`: a headless target linking Engine with no window or GL, replaying a checked-in `TickInput` trace and emitting one 8-byte hash per tick. 3600 ticks = 28.8 KB per scenario per platform.
- The canonical hash: never over raw struct memory. `-0.0f → +0.0f`, canonical NaN, explicit field order.

**Proves it works.** Two toolchains, 3600 ticks, byte-identical trace files. Failure output names the **first differing tick index** and scene, not "hashes differ". The grep gate turns red on the current tree (Jolt's 139 `/fp:fast` TUs) — allowlist `vcpkg_installed/**/joltphysics/**` explicitly, with a comment pointing at D3, so the exemption is visible rather than accidental.

**Effort note.** There is no `.github/` today **[V]**, therefore no vcpkg binary cache. A cold leg builds assimp (+draco, the known long-path offender), glfw, imgui, sol2, lua, yoga, pugixml, meshoptimizer and Jolt from source: 30-60 min. Wiring the binary cache is its own day and is part of this phase. Two legs, not five — the ARM legs only matter under a design we rejected.

> ### ✅ PHASE 1'S CORE CLAIM IS PROVEN. 2026-08-13.
>
> **A golden state hash recorded from MSVC 19.44 / Windows was reproduced exactly
> by gcc 13 / Linux, over 3000 ticks.** `CrossPlatformDeterminism.TheScriptedMatchHashesToTheRecordedValue`
> passes on both CI legs, and both legs are required. Cross-platform bit-identity
> is now a checked property rather than an argument, which is what `NORTHSTAR` Q1
> (crossplay in scope) needs and what D2 predicted would come free from integer
> arithmetic. It did.
>
> **What was built** (`tests/test_determinism_crossplat.cpp`): 3000 scripted ticks
> exercising walking, jumps and their whole arc, stun, the stage clamps and the
> RNG stream, hashed at **every** tick — a final-state-only hash can be
> accidentally equal after a divergence that cancels, and localises nothing.
> Checkpoints every 1000 ticks name the first diverging one. `sizeof`, `alignof`
> and trivially-copyable are asserted **before** the hash, so a padding change
> reports itself as a padding change rather than as an arithmetic divergence.
>
> **Where the design above differs from what shipped, and why.**
> - No `tools/det_trace` and no checked-in trace files. A recorded constant in the
>   test source achieves the same comparison with no binary artifacts to keep in
>   sync, because the test binary is compiled by both toolchains and the constant
>   is the meeting point. Trace files become worth their weight when there are
>   many scenarios; there is one.
> - The hash IS over raw struct memory, which this section forbids. That
>   prohibition exists to defend against `-0.0f`, NaN and padding — and the kernel
>   has no float at all, with `Fighter::pad_` an explicit named byte. The
>   structural asserts are what keep that true. If a float ever enters the kernel
>   the determinism gate rejects it at the source (`scripts/check_determinism_flags.py`
>   scans `Kernel/` for `float`, `double`, `<cmath>`, `<random>`, `<chrono>`).
> - **The grep gate did not turn red and needs no Jolt allowlist.** Jolt compiles
>   its own TUs inside vcpkg's buildtrees, which never reach our compile lines.
>   Measured, not assumed — see the note in `check_determinism_flags.py`.
> - The vcpkg binary cache is wired and CI is four required jobs, so the effort
>   note above is settled.
>
> **Still owed from this phase:** the mirror-symmetry property (D2's
> `scaleBy` rounding asymmetry) has a sub-unit arithmetic test but not a full
> mirrored-input trace — there is not yet enough simulation to mirror.

---

### Phase 2 — The kernel skeleton and the rollback contract (3 weeks)

**Build.** `Engine/src/gameplay/`: includes neither Jolt nor `Components.h` and contains no `float`. `GameState`, the `SimId` slot allocator, `Simulate(GameState&, TickInput, Phase)`, the 128-slot ring, the input ring, the confirmed-event queue, `scaleBy`, and the reflection table. Movement, gravity, pushboxes, one attack. `InputMap` becomes a producer.

**Proves it works.** Four tests in the existing 50-file suite **[V]**:
1. **In-process rollback:** run to tick 400; snapshot at 200; run to 400 again; restore; re-run to 400; assert hashes match. **The scenario must spawn and despawn entities**, or it proves nothing about `SimId` stability.
2. **Mirror symmetry:** the same input sequence mirrored produces exactly mirrored state, bit for bit, for 3600 ticks. This is what catches `scaleBy` rounding asymmetry.
3. **Ring exhaustion:** a correction older than the ring stalls and never drops a tick.
4. **Cross-platform:** the Phase-1 trace test now covers the kernel.

**Measure.** Per-tick cost with 2 characters + 32 projectile slots. **Gate: full rollback event (1 restore + 8 resim) under 1 ms.** Expectation ~300-500 µs.

---

### Phase 3 — Combat systems (8-10 weeks — the honest number)

**Build.** Frame-indexed animation with per-frame events; per-frame hit/hurt/push/throw box authoring and storage (4×`int16` offsets = 8 B/box, exactly MUGEN's CLSN); hit detection with priority and trade resolution; hitstop; pushback; juggle and proration; the state machine executing Phase-0 schema data. **No trigger expression language yet** (CHOICE B). The hitbox editor is historically the most time-consuming tool in a fighting-game project — budget it here, not as a footnote.

**Proves it works.** The three Phase-0 characters are playable locally against each other, and a scripted 20-hit combo replays bit-identically from a recorded input trace on both toolchains.

---

### Phase 4 — Netcode (4 weeks with GekkoNet, 12+ without)

**Build.** `ISession` wrapping GekkoNet; the connect handshake (schema-version hash, character-content hash over the **loaded POD arrays**, not canonicalized text; `JPH_VERSION_ID` for the cosmetic layer); checksum exchange every 8 ticks; desync → abort + named frame (CHOICE C); the reconciler (`const GameState&` — enforce the one-way flow with the type system).

**Proves it works.** Two processes on one machine, then two machines across OSes, 10-minute match, zero checksum mismatches. A deliberately corrupted peer produces "desync at tick N, field F" within 8 ticks.

---

### Phase 5 — The trigger language (4 weeks)

Port `triggers.py`'s grammar (`:407-519`): ~10 atom kinds, 6 comparison operators, `&&`/`||`/`!`, inclusive/exclusive ranges, an integer variable bank. Keep the three-valued evaluation **and the reason tracking** (`triggers.py:56-64`). Must express `[Statedef -1]`-style global transitions — real characters put nearly all transitions there (`mugen_cns.py:11-31`), so "from any attack that has already connected" must be one authored row.

**Proves it works.** The escape hatches counted in Phase 0 drop by ≥80%. Trigger evaluation stays under the Phase-2 gate.

> **SCOPE GROWN by [ADR-001](ADR-001-fighting-core.md). The grammar above is half of what real characters use.**
>
> `triggers.py:407-519` is the **self** namespace. Phase 0 found 26 of 247 cancels
> gating on the **defender**, in two of three characters independently:
> `p2bodydist x <= 30 && p2movetype != H` on every AOF2 cancel, and Kung Fu Girl's
> own juggle counter — which it runs *after disabling MUGEN's* (`AssertSpecial
> nojugglecheck`). **Phase 5 needs an opponent namespace: `p2statetype`,
> `p2movetype`, `p2bodydist`.** Budget accordingly; 4 weeks assumed the self half.
>
> **And one thing that is not a trigger-language problem at all.** AOF2 chooses
> its attack with `random < 50` evaluated every tick. That is a *policy*, not a
> rule: a deterministic rollback sim cannot use MUGEN's random, and the prover's
> model has no probability. D7's escape hatch is "a named C++ effect" and does not
> cover "the transition itself is a policy". **AI selection belongs in a separate
> authored artifact**, not inside the move/cancel data, where it would put a
> distribution somewhere the prover expects an edge.

---

### Phase 6 — The editor panel and the research integration (2 weeks)

See §5.

---

### Phase 7 — Moddability and late-join (3 weeks)

Mod folder loading through `PathIsContained` (`ScriptWorld.cpp:246`); run `analyse` on a mod at load and warn the player before the match; `entt::continuous_loader` for spectate/join.

---

# 4. THE DETERMINISM CONTRACT

> **This contract now lives in [`docs/DETERMINISM.md`](DETERMINISM.md)**, which carries every rule below plus a column naming what actually enforces it — and corrects the four this document asserts that the tree does not implement (`cse_fp_strict`, the `workerThreads` startup assert, the `JPH_VERSION_ID` `static_assert`, and `Simulate`'s `Phase` parameter). Read the rules there. What follows is the record of what was decided, kept until this document is rewritten.

## 4.1 What the simulation is

The simulation is `Simulate(GameState&, TickInput, Phase)` and everything it transitively calls, in `Engine/src/gameplay/`. Everything else is presentation.

## 4.2 Rules for `GameState`

- [ ] `static_assert(std::is_trivially_copyable_v<GameState>)` and `static_assert(std::has_unique_object_representations_v<GameState>)` — the second forbids padding, which is what makes byte-wise hashing valid.
- [ ] No pointers, references, virtuals, `std::string`, `std::vector`, `std::map`, `std::unordered_map`, `std::function`, or any type carrying an address.
- [ ] Every array is fixed-capacity with an explicit count. No unbounded growth, ever.
- [ ] Every integer field is explicitly sized (`int32_t`, not `int`; `int16_t`, not `short`).
- [ ] **No `float` or `double` anywhere in `GameState` or in any function `Simulate` calls.** Enforced by a CI grep over `Engine/src/gameplay/`.
- [ ] Every field added to `GameState` is added to the reflection table in the same commit.

## 4.3 Rules for `Simulate`

- [ ] Takes all input as parameters. Reads no global, no singleton, no clock, no environment variable, no file.
- [ ] Never calls `InputMap`, `AudioWorld`, `UIWorld`, `Renderer`, `CameraDirector`, or any `IScriptHost` method.
- [ ] Never calls `<cmath>`, `<random>`, `rand()`, `time()`, `std::chrono`, or `libm`. Integer `sqrt` and table trig only.
- [ ] Never allocates. No `new`, no `malloc`, no container growth.
- [ ] Never iterates an associative container. **Dense arrays indexed by slot, always.**
- [ ] Never branches on a pointer value, an address, `sizeof`, or an EnTT handle.
- [ ] Never reads wall-clock or frame time. Game time is `tick`, which is in `GameState`.
- [ ] Every side effect that leaves the simulation is appended to the per-tick event queue inside `GameState` and consulted `Phase`. Nothing plays a sound, prints, spawns a particle, or shakes the camera directly.
- [ ] Multiplication of two non-count quantities goes through `scaleBy` only. No `*` between two scaled values, no `>>` for division, no `/` without a documented rounding rule.
- [ ] Signed overflow is impossible by range analysis, or the operation goes through a checked helper. Positions are bounded by stage limits; velocities are clamped.

## 4.4 Rules for identity

- [ ] `entt::entity` never appears in `Engine/src/gameplay/`. `SimId` only.
- [ ] `SimId` slot allocation is a linear scan for the lowest free slot. No free list, no counter outside `GameState`.
- [ ] Generation counters live in `GameState` and therefore roll back.
- [ ] Presentation ordering ties break on `SimId`, never on `entt::to_entity` or `entt::to_integral`. Audit sites: `CameraDirector.cpp:23`, `UIWorld.cpp:189`.

## 4.5 Rules for input

- [ ] Input is a value parameter. The simulation never queries hardware.
- [ ] Directions are quantized to 9-way at the sampling boundary, before the network or the sim sees them.
- [ ] The input ring is immutable once written. Re-simulation reads the same bytes.
- [ ] Producer-side sticky state (the between-tick press accumulator) is never read during re-simulation and is never networked.
- [ ] UI focus never suppresses input while a session is live.

## 4.6 Rules for the tick loop

- [ ] The session clock decides how many ticks run. **Never drop a tick.** Zero ticks this frame is legal; a dropped backlog is not.
- [ ] `FixedTimestep::advance` is not on the sim path (`FixedTimestep.h:33-35` zeroes the accumulator at the cap).
- [ ] `paused_`, `timeScale_` and scene swap are inert while a session is live.
- [ ] Every tick emits a checksum. Every 8th is exchanged.
- [ ] Ring exhaustion stalls; it never drops or truncates.

## 4.7 Rules for the build

- [ ] No `/fp:fast`, `-ffast-math`, `-Ofast`, or `-ffp-contract=fast` in any generated `build.ninja`, including under `vcpkg_installed`. Enforced by the CI grep gate. Exemptions are allowlisted with a comment naming the reason.
- [ ] Simulation targets link `cse_fp_strict`.
- [ ] No LTO / IPO on simulation targets — it can legally reassociate across TU boundaries.
- [ ] `PhysicsSettings::workerThreads == 0` is asserted at startup (`PhysicsTypes.h:143`).
- [ ] `JPH_VERSION_ID` is `static_assert`ed and logged. It does **not** encode instruction set or FP mode (`Core.h:68,71`) — do not rely on it for those.

## 4.8 Rules for authored data

- [ ] Float→integer quantization at load uses one documented rule, applied identically on every peer (D8).
- [ ] The connect handshake hashes the **loaded POD arrays**, not the source text. Text canonicalization is where float-repr and key-order bugs live.
- [ ] A content-hash mismatch is a lobby error with a named reason, never a gameplay bug.

## 4.9 Known hazards already in the tree (fix or fence, do not ignore)

- [ ] `ScriptWorld.cpp:262` builds `order_` from `reg.view<ScriptComponent>().each()` — EnTT packed order, permuted by `swap_and_pop`. **Script execution order is currently a function of storage internals.** Sort by entity index in `Build()`. Note that `order_` is documented as "stable Build() order for the UI" (`ScriptWorld.h:141`), so this also changes Inspector ordering.
- [ ] `PhysicsWorld.cpp:186,210` iterate `std::unordered_map<entt::entity, BodyId>` **[V]**, and the readback loop at `:238` computes `glm::inverse(ResolveWorldMatrix(parent))` **while the same loop is writing parents' TRS at `:243`**. A dynamic body parented to a dynamic body therefore reads a parent that has or has not been updated this tick depending on bucket order. Not latent-benign; latent-broken. Fence it: assert no dynamic body has a dynamic parent, or sort the iteration.
- [ ] `SimplePhysicsBackend.cpp:70,82,97-104` — contact events depend on `unordered_map` iteration order via a strict-`>` tie-break, so its contact events are STL-dependent, which quietly undermines the `PhysicsConformance` suite it exists to serve.
- [ ] `IScriptHost::timeSeconds()` and `wasActionPressed()` (`ScriptWorld.cpp:169-176,392`) are unusable under re-simulation. Neither may be reachable from `Engine/src/gameplay/` — enforced structurally by that directory not including `IScriptHost.h`.

---

# 5. HOW THE RESEARCH LANDS

## 5.1 The plug point

The engine's character file **is** the analysis input. One JSON document, one loader, two consumers: the kernel executes it, `ComboProverAdapter` projects it into `comboprover::Character`. `nlohmann-json` is already a dependency (`vcpkg.json:9`).

## 5.2 What the prover reads, and what is lost

The projection is **lossy and one-directional**, and every loss needs a written soundness argument. Verified against both files **[V]**:

| Engine schema field | `comboprover::Character` | Handling |
|---|---|---|
| `startup, active, recovery, hitstun, damage, effect, guard` | `Move` (`:121-132`) | Direct |
| `from, to, delay, onHit, effect, guard` | `Cancel` (`:139-146`) | Direct; `on` is a *string* in `json_spec.py:104` and a `bool` in the header — collapse `ON_BLOCK`/`ON_WHIFF` to `onHit=false`, **document that this over-approximates** |
| resource `ceiling` (`json_spec.py:158-164`) | **absent** — no ceiling exists anywhere in the C++ model | ~~Must be enforced by quantizing meter to bars so the ceiling is unreachable within the search bound,~~ **or** by pre-clamping `effect` rows. ~~Otherwise the in-engine verdict is unsound.~~ **AMENDED TWICE. (1) [ADR-001](ADR-001-fighting-core.md): take the second option — the adapter clamps in its own loop; quantize-until-unreachable is unsound in the DANGEROUS direction, because it can make a reachable meter state look unreachable and turn an infinite into a `Terminating`. (2) 2026-08-13, on a proof rather than an argument: "unsound" is the wrong word for the ceiling's ABSENCE. Let `d = unclamped − clamped` along the same run. `d` starts at 0 and only grows, because clamping subtracts and never adds; guards are `≥` only. So every clamped step is legal unclamped, and every clamped infinite IS an unclamped infinite. Running without ceilings can only ADD infinites — it OVER-APPROXIMATES, which is the safe direction. The clamp is still worth having (it removes false alarms and is implemented in `ProverAdapter.cpp`, which also replays a reported witness through its own clamped loop and reports whether the loop survives), but its absence cannot HIDE an infinite and this row should not have implied it could.** |
| resource **order** | positional — `ResourceVec` is indexed, not keyed | **NEW, from [ADR-001](ADR-001-fighting-core.md): the order of the resource list is a build-wide contract.** Nothing in either implementation names a resource; index 0 in the engine must be index 0 in every character file and in the prover. Reordering one file silently compares meter against juggle points. Assert it at load. |
| `stance, reach, pushback, walk_speed, gap_actions, stage` | absent | Dropped. `walk_speed` absence is the corner-only scope (`:15-24`); the panel must say so. |
| trigger expressions | not representable (`meetsGuard` is componentwise ≥; `applyEffect` is componentwise +) | Three-valued: any edge whose trigger is not a resource comparison is marked `DOUBTFUL` with a reason, exactly as `triggers.py:56-64` does. |

## 5.3 What the editor shows a designer

A dockable panel, re-running `analyse` **asynchronously with a budget** on every edit — not synchronously, because §5.4 says `Unknown` is a real outcome. **Three states, not two:**

1. **INFINITE** — the loop printed as a move sequence (`Result::prefix` + `Result::loop`), clickable to jump to each move.
2. **TERMINATING** — `maxHits`, `maxFrames`, `maxDamage`. Show the ranking certificate (`rankingOrder`) **when available**, and say plainly when it is not: verified **[V]**, `spendOnly` is cleared by any reachable cancel with a positive resource effect (`:376-386`) and the certificate is gated on it (`:563`), so **any character that builds meter on hit gets `Terminating` with `hasRanking == false`.** That is the common case, not the exception. Do not build the panel around the certificate.
3. **UNRESOLVED** — `capped` (`:485`, `:553`), with the config count and a "raise the budget" action. Not an error; a budget statement.

Always shown, whatever the verdict, and free: `deadCancels` (cancels the designer authored that can never connect), `unreachableMoves`, `settlingIndex`, `usableCancels`. **These are the features a designer will actually use daily**, and they land before anyone cares about the theorem.

> **AMENDED by [ADR-001](ADR-001-fighting-core.md). The panel must name its stage position, and one verdict must be marked conditional.**
>
> Phase 0 ran every character twice, corner and midscreen, and **the verdict
> differs between them** — Kung Fu Man is `TERMINATING` midscreen and `INFINITE`
> in the corner. A panel that shows "the" verdict without saying where the
> characters are standing is showing a coin flip.
>
> Worse for the midscreen half: **pushback is not derivable from MUGEN at all.**
> No source file contains it, so every midscreen run rests on an estimated
> constant, and a ±20% error in that estimate flips one of the three verdicts. The
> winning loop's space budget closes by a single unit. **Mark midscreen verdicts
> as conditional on the estimate; corner verdicts do not depend on it and are the
> ones to trust.** The single highest-value follow-up measurement is deriving
> pushback from an instrumented Ikemen GO build — about a day's work, and it
> converts every midscreen verdict from conditional to derived.
>
> Also: distinguish the **two different reasons** a ranking certificate can be
> missing (`spendOnly` cleared vs `resCount == 0`), and show the **pre-decay**
> dead-cancel list — post-decay it can report real cancels as dead. Corner runs
> are fast enough for C++ synchronously (0.033-0.041 ms); midscreen is 147-226 ms
> in Python and needs the async budget and a cancel-and-supersede path.

Always shown as a caveat: *"Corner-pinned defender. Distance and walk-forward are not modelled. Away from the wall this verdict is an under-approximation."* (`comboprover.hpp:15-24`.) `README.md:120-128` shows that distinction flipping `corner_only.json` (safe) to `microwalk.json` (infinite) on the same jab — the panel must not show a green tick it cannot back.

## 5.4 Determinism of the verdict itself

`Move::damage` (`:127`), `Character::scaling` (`:156`) and `Decay::ratio`/`table` (`:71,:73`) are float, and `Decay::Multiplicative` does repeated float multiply then truncate (`:82-88`) **[V]**. So the verdict itself can differ between x86 and ARM. Harmless in the editor. **Not harmless** for the Phase-7 "warn the player about a modded infinite" feature — if both peers run the check they can disagree about admitting the mod. Fix: compute the verdict on one side and hash it into the connect handshake alongside the content hash.

## 5.5 What the paper harvests

Contribution #9 is "an integration of the analysis into a working engine's editor," and this design produces evidence no offline tool can:

1. **Fit measurement.** N characters authored in the schema, X% of moves requiring a C++ escape hatch (Phase 0, then again in Phase 5 after the trigger language). This is the honest answer to "does the formal model describe real fighting games," and it is a number, not an opinion.
2. **Latency distribution.** `analyse` wall-clock per keystroke over a real editing session, with the `Unknown` rate. The header claims sub-millisecond (`:9-13`); a distribution over real authoring is a stronger claim.
3. **Found-bugs evidence.** Dead cancels and unreachable moves discovered in genuinely authored content — the tool finding real defects a designer did not know about.
4. **Ground-truth validation, which only an engine can supply.** Take a character the prover calls `Infinite`, hand the printed loop to the engine as a scripted input trace, and *execute it*. Either the combo works — the analysis is validated end to end on a running game — or it does not, and the gap between the model and the game is itself a publishable finding. **No offline analysis can produce this.**

   > **DELIVERED, 2026-08-13** — `tests/test_ground_truth.cpp`, and it returned *both* outcomes at once rather than one of them.
   >
   > **The loop works.** `fighter_a_infinite`'s printed witness executes as written: 26 hits in 160 ticks, one every 6, defender out of hitstun on **0** ticks, bit-identical on replay, unaffected by a mashing defender. The input trace is derived from `ProverResult::loop` — nothing hardcodes a button — so the claim is that *the engine* can read the verdict, not that a human can.
   >
   > **The gap is real, and it is on the SAFE character.** `fighter_a` is `Terminating` with a ranking certificate whose content is "juggle runs down". The kernel has no juggle — no resources at all — so `air_mp`'s self-cancel, which the model permits **4** times, ran **18** times in 200 ticks with the defender unable to act. The analysis is correct about the file and the projection to the running game is what fails, which is precisely the D8 hazard §5.2's loss table exists to name. It is now a measured number rather than a caveat.
   >
   > The two findings are complementary, not contradictory: the prover is sound about the graph it is given, and *the graph is not yet the game* wherever the kernel lacks a mechanism the file uses. Closing it is Phase 3's resource work, and the test will say so the day it lands.
5. **A soundness note on the projection.** §5.2's loss table, with the argument for each drop, is a contribution in its own right: it is the first written account of what an executable fighting game contains that a decidable model does not.

---

# 6. THE FIRST WEEK

In order. Items 1-4 are hours and are correct regardless of everything above. Item 5 is the week.

**Day 1, morning — three fixes and a doc correction.**

1. **Sort `order_` by entity index** in `ScriptWorld::Build` (`ScriptWorld.cpp:262`). One line. Closes the EnTT-pool-order divergence in script execution — the single most dangerous ordering hazard in the tree, because script order *is* the gameplay. Update the `ScriptWorld.h:141` comment; Inspector ordering changes with it.
2. **Fix `SimplePhysicsBackend`'s hash-order tie-break** (`:70,:82,:97-104`). Iterate a sorted vector of body ids; break the `topY` tie on the lower id. Its contact events are currently STL-dependent, which undermines the `PhysicsConformance` suite it exists to serve.
3. **Fix `SceneHierarchyPanel.cpp:84-85`** — change the *second* argument only to `(unsigned)entt::to_entity(e)`. The first `(uint32_t)e` is the ImGui ID and correctly wants full uniqueness. This is why ids jump to `1048577+` after a reload.
4. **Correct `docs/NORTHSTAR.md:117-139`.** It reports a measured physics desync; what was measured was `PhysicsWorld::Rebuild`, which is `Clear()+Build()` and could not possibly round-trip (`RigidBody` has no velocity field, `PhysicsComponents.h:21-30`). Jolt's `SaveState` path has never been tested in this repo. Leaving that sentence uncorrected will cause someone to re-derive D3 from a false premise in a year.

**Day 1, afternoon — the grep gate.**

5. Create `.github/workflows/ci.yml` with **only** the build-flag grep gate (§4.7) and a Windows build. No test matrix yet, no binary cache yet. It should go red on the current tree because of Jolt's 139 `/fp:fast` TUs; allowlist that path with a comment pointing at D3. This is the highest value-per-hour item in the entire investigation and it takes two hours.

**Days 2-5 — Phase 0.**

6. Draft the schema JSON, modelled field-for-field on `json_spec.py:126-198`, with the three deltas from §5.2 written into the file as comments.
7. Transcribe three complete characters. Real ones, from frame data you can look up. This is typing, and it is the most valuable typing in the project.
8. Write `ComboProverAdapter` (~200 lines) and a throwaway `tools/prove_character.cpp` that loads a JSON and prints `describe(analyse(c))`.
9. Run it. Record: escape-hatch rate, `Unknown` rate, `hasRanking` rate, wall-clock. **Quantize meter to bars until all three resolve.**
10. Write the answer down in `docs/ADR-001-fighting-core.md` next to this document. If the escape-hatch rate is above 20%, **stop and redesign D7 before writing any engine code.** That is what this week is for.

**What is deliberately NOT in the first week:** the vcpkg overlay port, any Jolt work, any `GameState` code, any DSL parser, any netcode. All of them are cheaper after Phase 0 answers, and two of them are cancelled outright if it answers badly.

---

## APPENDIX — Adjudications, for the record

| # | Dispute | Ruling | Basis |
|---|---|---|---|
| 1 | RelWithDebInfo Jolt `std::abort()` | **Critic.** No bug exists. | Verified **[V]**: the define is present in `out/build/x64-RelWithDebInfo/build.ninja`; vcpkg maps the imported config. |
| 2 | "Every combat quantity is already an integer" | **Critic.** False. | `comboprover.hpp:71,73,82-95,127,156` **[V]**. Argument survives on two other legs. |
| 3 | "The prover reads the shipping game with no importer" | **Critic.** False. | `meetsGuard`/`applyEffect` are componentwise ≥ and + (`:249-262`) **[V]**; `DOUBTFUL` exists only because the abstraction loses information. |
| 4 | Lua rejected on snapshot cost | **Critic**, twice: cost is arguable *and* the real cost is `Rebuild` recompiling. Both moot. | Reject on `ltable.c:159-185` pointer hashing, which is unenforceable. |
| 5 | `PhysicsWorld` loops "order-independent" | **Critic.** They are not. | `PhysicsWorld.cpp:238` reads a parent the same loop writes at `:243` **[V]**. |
| 6 | Worst ordering hazard | **Critic.** `ScriptWorld.cpp:262`, not the physics maps. | Script order is the gameplay. |
| 7 | Trigger eval 200 µs vs ~30 µs | **Critic.** Only the active state's triggers evaluate. | Kills the bytecode-VM case for now. |
| 8 | Ring of 8 vs 128 | **Critic.** Size to the protocol window. | 360 KB is nothing; a >8-frame-late packet is routine. |
| 9 | Jolt overlay port in week 2 | **Neither.** Off the critical path entirely. | Under D2 it buys bit-identical hit sparks for 1.5-2.5× physics cost + a permanent patch. |
| 10 | Q16.16 vs plain sub-units | **Critic.** No general fixed-point type. | Mirror asymmetry from mixed `>>`/`/` rounding; C++17 shift semantics. |
| 11 | Periodic authoritative resync | **Rejected** for 2P P2P. Detection kept, correction dropped. | No authority exists; a resync hitch costs a round. |
| 12 | Write vs adopt the session layer | **Critic.** Adopt. | 6-12 weeks of the riskiest, least differentiating code in the project. |
| 13 | "6-10 weeks to a usable authoring loop" | **Critic.** 4-6 months. | `IScriptHost.h:27-78` contains none of the combat systems **[V]**. |
| 14 | The input latch as "sloppiness" | **Neither.** It solves a real frame-vs-tick problem; the fix is structural. | `InputMap.h:88-93` documents the problem it solves. |
| 15 | `UIWorld.cpp:189` → `to_entity` | **Rejected.** Order on `SimId`. | Stripping version bits removes the only thing making a recycled index monotone. |