# The determinism contract

Verified: 2026-08-18 @ 5cb3256

Every rule the simulation, the build and the authored data must obey, and — for
each one — what stops it being broken. This is the only home for these rules;
[MAINTENANCE.md](MAINTENANCE.md) and `docs/manual/` link here rather than
restating, and code comments cite a rule by its id (`DETERMINISM.md K3`).

The property all of it protects: **the same inputs against the same match data
produce byte-identical `GameState` on every machine, every toolchain and every
re-simulation.** Rollback needs it within one process; netplay needs it between
two; the combo-termination paper needs it because a verdict about a simulation
nobody can reproduce is not evidence.

## How to read the table

| Enforced by | Means |
|---|---|
| **configure** | CMake fails before anything compiles |
| **CI** | `scripts/check_determinism_flags.py`, in the `determinism-flags` job and again after configuring |
| **static_assert** | the compiler refuses |
| **test** | a named test, run by `ctest -LE "perf\|gl"` in CI |
| **structural** | the code physically cannot express it (the type has no such field; the target cannot reach the header) |
| **review** | nothing mechanical. A human reading a diff is all there is |
| **not yet** | the rule is agreed and the thing it governs is not built. The WP that builds it owns the enforcement |

A **review** row is a liability, not a decision. §3 lists the ones that could be
mechanical and names the work package that would do it.

---

## 1. Where the simulation is

`Simulate(GameState&, const InputPair&, const MatchData&)` in
`Games/UntitledFighter/Kernel/` and everything it transitively calls. That is the
whole of it. `Games/UntitledFighter/Game/` is not the simulation, but it is held
to the arithmetic rules anyway (K3, K6) because it computes the bits handed to
`Simulate`, encodes them into replay files, and judges the result — a float there
leaves the simulation bit-identical and makes the *verdict* drift, which is
worse, because the run really is reproducible and only the sentence shown to a
human is not.

Everything else — `Engine/`, `Editor/`, `Player/`, the modes, the renderer — is
presentation, and its only determinism obligation is §7.

## 2. The rules

### State (`GameState`)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| S1 | `GameState` is trivially copyable — the snapshot is a `memcpy` | static_assert | `tests/test_kernel.cpp` (M1.1(d) moves it into `GameState.h`, where a change to the struct meets it) |
| S2 | No pointer, reference, virtual, `std::string`, `std::vector`, `std::map`, `std::function`, or any type carrying an address | static_assert (partial) + review | S1 rejects every one of those except a **raw pointer**, which is trivially copyable and would pass. Raw pointers are review-only |
| S3 | No padding: byte-wise hashing is only valid over a struct with a unique object representation | **not yet** | nobody asserts it today. `static_assert(std::has_unique_object_representations_v<GameState>)` is ROADMAP M1.1(d) |
| S4 | Every array is fixed-capacity with an explicit count; no unbounded growth | structural + test | `kMaxFighters`, `kMaxTeams` in `Games/UntitledFighter/Kernel/include/cse/kernel/GameState.h`; `kMaxMovesPerFighter`, `kMaxCancelsPerFighter`, `kMaxInvulnWindows` in `Games/UntitledFighter/Kernel/include/cse/kernel/Combat.h`; `KernelLayout.StateIsSmallEnoughToSnapshotEveryTick` |
| S5 | Capacities and the code that indexes them move together | static_assert | `kMaxFighters == 8` is tied to `Fighter::alreadyHitBits`' eight bits in `GameState.h`; `sizeof(MoveDef) == 128` and `sizeof(CancelEdge) == 16` in `Combat.h` |
| S6 | Every integer field is explicitly sized (`std::int32_t`, never `int`) | review | — |
| S7 | No `float` or `double` in `GameState` or anything `Simulate` calls | CI | `scripts/check_determinism_flags.py`'s `KERNEL_FORBIDDEN`, over `KERNEL_GLOBS` |
| S8 | Every field added to `GameState` is added to the reflection table in the same commit | **not yet** | there is no reflection table. ROADMAP E6 / M2.3 builds it and owns this rule |
| S9 | `GameState` is a wire contract: changes are batched into one planned expansion, re-goldened once, reviewed once | review | `tests/test_determinism_crossplat.cpp` holds the golden hash; the process is [ADR-005](adr/ADR-005-playable-priority.md) §3 |

### `Simulate` (K)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| K1 | Takes all input as parameters. Reads no global, no singleton, no clock, no environment variable, no file | test | `KernelPurity.SimulationReadsNothingOutsideTheState` interleaves two matches in one process and compares byte-for-byte |
| K2 | Cannot reach the renderer, audio, UI, physics, EnTT, Lua or the engine at all | configure | `Games/UntitledFighter/Kernel/CMakeLists.txt` fails if `CseKernel` acquires **any** link dependency, interface ones included. `Games/UntitledFighter/Game/CMakeLists.txt` holds `CseGame` to an exact whitelist (`CseKernel;CseData`) |
| K3 | No `<cmath>`, `<math.h>`, `<random>`, `<chrono>`, `rand()`, `float` or `double` | CI | `KERNEL_FORBIDDEN`; comments are stripped first, so a header may explain the rule without tripping it. `det-ok` on a line exempts it, visibly |
| K4 | Never allocates. No `new`, no `malloc`, no container growth | CI | an **include allowlist** over `Games/UntitledFighter/Kernel/`: the module may include `<cstdint>`, `<type_traits>` and `<cstring>` and nothing else, so `<memory>`, `<vector>` and friends cannot get in. Neither `new` nor container growth is greppable; both are unreachable |
| K5 | Never iterates an associative container. Dense arrays indexed by slot, always | CI | as K4 — `<map>` and `<unordered_map>` are off the allowlist |
| K6 | Game time is `GameState::tick`. No wall clock, no frame time, no delta time | CI + test | `<chrono>` in K3; K1's test would catch a static counter |
| K7 | Never branches on a pointer value, an address, `sizeof`, or an EnTT handle | structural + review | EnTT is unreachable (K2); the rest is review |
| K8 | One rounding rule for scaling, applied identically everywhere: round half **away from zero**, never `>>` for division | test + review | `SubUnitArithmetic.IntegerDivisionTruncatesTowardZeroForBothSigns`, `.RoundingTowardMinusInfinityWouldBreakTheMirror`, `.WalkingLeftAndRightAreExactMirrorsThroughSimulate`. The rule is written inline in `Games/UntitledFighter/Data/src/MatchBuilder.cpp`; ROADMAP M1.8 makes it one `constexpr scaleBy` helper |
| K9 | 1 pixel = 256 sub-units, 60 ticks per second; positions, velocities and boxes are `int32` sub-units | test | `SubUnitArithmetic.OnePixelIsExactlyTwoHundredAndFiftySixSubUnits`; `kSubUnitsPerPixel` and `kTicksPerSecond` in `GameState.h` |
| K10 | Signed overflow is impossible by range analysis, or goes through a checked helper | review | positions are clamped to stage limits; nothing asserts the bound |
| K11 | Nothing leaves the simulation from inside a tick — no sound, no particle, no print, no camera shake. Effects are events in the state, drained by phase | **not yet** | `Simulate` has no `Phase` parameter and `GameState` has no event ring today. ROADMAP M1.1(c) reserves the fields; M3.1 fills and drains them |
| K12 | No RNG outside the state. The only randomness is `GameState::rng`, which rolls back with everything else | CI | `<random>` and `rand(` in K3 |

### Identity (I)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| I1 | `entt::entity` never appears in the simulation | configure | K2 — EnTT is not linkable from `CseKernel` |
| I2 | A fighter's identity is its **slot index**, fixed for the match. No runtime spawn, no id allocator, no generation counter | structural | `GameState::p[kMaxFighters]` with an `active` flag. `SimId` and a snapshot ring are deliberately not built — [ADR-010](adr/ADR-010-one-roadmap-one-rule.md) §3.4 |
| I3 | Presentation ordering ties break on the **entity index**, never the raw handle: version bits sit above the index and reset on load, so raw-handle order flips across a save/reload | test + review | obeyed at `Engine/src/core/CameraDirector.cpp`, `Engine/src/script/ScriptWorld.cpp` and `Engine/src/ui/UIWorld.cpp`; the last is pinned by `UIWorld.DocumentsAtOneSortOrderKeepTheirOrderAcrossAReload` (`tests/test_ui_component.cpp`). New ordering sites are review-only |

### Input (N)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| N1 | Input is a value parameter. The simulation never queries hardware | structural | `Simulate`'s signature takes `const InputPair&` |
| N2 | No analog value reaches the simulation. Directions are quantized at the sampling boundary | structural | `Input` is ten digital bits (`GameState.h`). A stick has to be reduced to those bits before it can be expressed at all |
| N3 | Once written, a tick's input is immutable; re-simulation reads the same bytes | structural | GekkoNet owns the input ring ([ADR-003](adr/ADR-003-gekkonet-spike.md)); `CseNet` moves bytes and a length, never a `GameState` |
| N4 | Producer-side sticky state — the between-tick "pressed since last tick" accumulator — is never read during re-simulation and never networked | **not yet** | ROADMAP M2.4 |
| N5 | UI focus never suppresses gameplay input while a session is live | **not yet** | ROADMAP M2.4 |

### The tick loop (T)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| T1 | The session decides how many ticks run. Zero ticks this frame is legal; **dropping a tick is not** | **not yet** | today `UntitledFighterMode::FixedTick` runs one tick per `Application` fixed step. ROADMAP M2.4 |
| T2 | `FixedTimestep` never decides the simulation's tick count — it caps at 8 steps and then zeroes the accumulator, discarding the backlog | review | the reason is written at the top of `GameState.h`; nothing enforces it. ROADMAP M2.4 |
| T3 | While a session is live, pause, time scale and scene swap are inert | **not yet** | ROADMAP M2.4 |
| T4 | Every tick can produce a checksum; a session exchanges one periodically and stops the match on mismatch | test (half) | `Checksum()` and `KernelRollback.ChecksumDetectsASingleBitOfDivergence`; the exchange arrives with the transport, ROADMAP M2.1–M2.3 |
| T5 | Snapshot → restore → re-simulate is byte-identical at every rollback depth | test | `KernelRollback.ResimulatingFromASnapshotReproducesTheStraightRun`, `.EightTickRewindIsExactAtEveryDepth`, `Session.SurvivesHundredsOfRealRollbacks` |
| T6 | A desync is reported and the match stops. It is never silently corrected | review | `ISession::PollDesync`; there is no correction path to disable |
| T7 | A full input or snapshot ring **stalls**. It never drops a tick and never truncates | structural | GekkoNet owns both rings ([ADR-003](adr/ADR-003-gekkonet-spike.md)); we do not implement one, which is why we cannot get this wrong |

### The build (B)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| B1 | No `/fp:fast`, `-ffast-math`, `-Ofast`, `-ffp-contract=fast`, `-mfma` or their relatives — in what we author **or** in what a dependency exports into our compile lines | CI | `scripts/check_determinism_flags.py`, twice: over `CMakeLists.txt`/`cmake/`/`CMakePresets.json`/`scripts/linux-build.sh`, and again after configuring over `build.ninja` and `compile_commands.json`. `--self-test` runs first, because a gate nobody has watched fail is not a gate |
| B2 | The gate proves the **absence** of those flags, not the presence of `/fp:precise`. MSVC defaults to precise and emits no flag, so there is nothing to assert positively | — | stated by the gate's own output, so a green result is not read as more than it is |
| B3 | No instruction-set flag that enables FMA contraction (`/arch:`, `-march=`, `-mavx`) | CI | prefix-matched in `FORBIDDEN`. None of them *says* reassociate; each says "you may use FMA", which is the same rounding change `-ffp-contract=fast` buys. `GLM_FORCE_*` is still review-only: GLM is unreachable from the simulation (K2) |
| B4 | No LTO / IPO on simulation targets — it can legally reassociate across translation units | review | none is configured anywhere in the tree |
| B5 | The simulation links nothing, so there is no such thing as a simulation-wide compile-option target | configure | K2. ARCHITECTURE §4.7 asked for a `cse_fp_strict` INTERFACE target linked by simulation targets; that target does not exist and **cannot**, because linking it would trip the kernel's own guard. The link guard is the stronger rule and it is the one that shipped |
| B6 | Physics never runs on worker threads | review | `PhysicsSettings::workerThreads` defaults to `0` (`Engine/src/physics/PhysicsTypes.h`) and both backends honour it; nothing asserts it at startup. Physics is presentation-only ([ADR-002](adr/ADR-002-open-decisions.md) CHOICE D) so this cannot desync a match — it can desync a *replay's cosmetics* |
| B7 | Cross-toolchain agreement is checked, not assumed | test | `CrossPlatformDeterminism.TheScriptedMatchHashesToTheRecordedValue` — a golden hash recorded under MSVC and re-checked by gcc 13 on the Linux CI leg. A hash checked by one compiler proves only that the compiler agrees with itself |
| B8 | Jolt's version id is not trusted to describe Jolt's arithmetic | — | ARCHITECTURE §4.7 asked for `JPH_VERSION_ID` to be `static_assert`ed and logged; it is neither. Read at Jolt 5.1.0, the id encodes `JPH_DOUBLE_PRECISION`, `JPH_CROSS_PLATFORM_DETERMINISTIC` and nine other build switches — but **not** the instruction set (`JPH_USE_AVX2` and friends) and not the compiler's FP model, so it can confirm the switch and never the arithmetic. B5 is why that is survivable: physics cannot reach a tick |

### Authored data (A)

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| A1 | Float → integer quantization happens **once, at load**, by one documented rule, identically on every peer | test | `Games/UntitledFighter/Data/src/CharacterData.cpp`; `OneFrameAnchor.ARoundTripThroughJsonWithNoMutationChangesNothing`, `OneFrameMutation.NothingBesidesThatOneIntegerMoved` |
| A2 | An unknown key in a character file is a load error, not a default | test | the load assertions A01–A20 in `Games/UntitledFighter/Data/src/CharacterData.cpp`; `tests/test_character_data.cpp` |
| A3 | A schema field is **appended**, never inserted or reordered | review | [ADR-006](adr/ADR-006-stance-and-guard.md)'s wire rule; the golden hash (B7) catches the consequence, not the cause |
| A4 | The handshake hashes the **loaded POD arrays**, never the source text — canonicalising text is where float-repr and key-order bugs live | **not yet** | `HashMatchData` exists in `Games/UntitledFighter/Game/include/cse/game/Replay.h`, written for exactly this. ROADMAP M2.2 wires it |
| A5 | A content mismatch is a lobby error naming the reason, never a gameplay bug | **not yet** | ROADMAP M2.2 |

## 3. Review-only rules that could be mechanical

Each is a hole; each has an owner. They are collected here so the list is short
enough to act on rather than spread through the tables.

| Rules | What would close them | WP |
|---|---|---|
| S3 | `static_assert(std::has_unique_object_representations_v<GameState>)` beside the struct | M1.1(d) |
| S6, K7, K10, A3 | Nothing cheap. They stay review-only and are named here so that is a decision rather than an oversight | — |

Four rows left this table in M1.0. K4 and K5 became the include allowlist, B3
became three more patterns in the flag gate, and I3 became a test — and I3 was
not a hypothetical: `UIWorld.cpp` really was sorting on the raw handle while its
own comment claimed stability across a save/reload.

## 4. Hazards outside the simulation

None of these can change a `GameState`, because none of them is reachable from
`Simulate` (K2). They are recorded because each makes something *else*
irreproducible — a replay's visuals, a physics-driven prop, a script's order —
and because the first three were found by grep rather than by a test.

- `Engine/src/physics/PhysicsWorld.cpp` iterates an `unordered_map` keyed by
  entity, and its readback loop resolves a parent's world matrix while the same
  loop is writing parents' transforms. A dynamic body parented to a dynamic body
  reads a parent that has or has not been updated this tick depending on bucket
  order. Fence it (assert no dynamic body has a dynamic parent) or sort the
  iteration.
- `Engine/src/physics/backends/SimplePhysicsBackend.cpp` orders contact events
  through an `unordered_map` with a strict-`>` tie-break, which makes its contact
  events STL-dependent — quietly undermining the conformance suite it exists to
  serve.
- `Engine/src/ui/UIWorld.cpp` — I3's open site.
- `IScriptHost::timeSeconds()` and `wasActionPressed()` are meaningless under
  re-simulation. Neither may become reachable from the simulation; today that is
  structural, because `CseKernel` links nothing.
- `Engine/src/script/ScriptWorld.cpp` **was** on this list — it built its
  execution order from EnTT packed order, permuted by `swap_and_pop`. It now
  sorts by entity index. Kept here as the worked example: the fix is four lines
  and the bug is invisible until two machines disagree.

## 5. The engine's side of the boundary (P)

Presentation may not feed the simulation and may not hold state the simulation
did not produce. Everything here is free today for one structural reason — the
kernel links nothing, so none of it is reachable from a tick — and each is one
well-meaning commit from stopping being free.

| Id | Rule | Enforced by | Where |
|---|---|---|---|
| P1 | Asset load order never reaches the simulation | structural + review | physics bodies are built from authored collider components (`Engine/src/physics/PhysicsWorld.cpp`), never from a loaded mesh's AABB, so what a model importer did last cannot change a body |
| P2 | The physics components carry no runtime handles | review | `Engine/src/physics/PhysicsComponents.h` — which is exactly the property a POD snapshot needs, and the reason to keep it |
| P3 | Worker threads never touch GL, the EnTT registry or ImGui; `onComplete` runs on the main thread | review | `Engine/src/core/JobSystem.h`, written up in [STYLE.md](STYLE.md#threading). It is a threading rule that also keeps rendering from feeding the simulation |
| P4 | Pose is a pure function of `(moveId, moveFrame, posX, posY, facing, stance, the stun fields, tick)`. A return-to-idle tail is presentation only, is interrupted the tick the simulation acts, and can never delay a move, move a box or hold a fighter in place | **not yet** | there is no pose yet — the box overlay is all that draws a fighter. [ADR-011](adr/ADR-011-mechanics-are-fields.md) decision 6 is the rule; ROADMAP M3.2–M3.4 build it and own the acceptance tests |

## 6. Changing a rule

A rule here is changed the way a decision is changed: write the ADR, then edit
this file in the same commit that makes the old sentence false. Adding a rule
means adding its **enforcement** in that commit too, or writing `not yet` with
the work package that owns it — a rule with no enforcement and no owner is a
sentence, and this file is not for sentences.

If a forbidden flag or pattern is genuinely wanted, put `det-ok` in a comment on
that line with the reason. The exemption then shows up in review instead of in a
desync six months later.
