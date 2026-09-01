# The Fighting-Game Core

Verified: 2026-08-31 @ 6036b00

Cat Splat Engine is being built toward a deterministic, rollback-capable fighting game. That work does not live in `Engine/`. It is a **title** — `Games/UntitledFighter/` — and the engine does not depend on any of it. The link direction is a configure-time error, not a convention.

Six libraries, and the order of this table is the dependency order:

| Piece | Directory | Links | What it does |
|---|---|---|---|
| **`CseKernel`** | `Games/UntitledFighter/Kernel/` | **nothing at all** | The authoritative simulation. One function advances one tick. |
| **`CseData`** | `Games/UntitledFighter/Data/` | nlohmann_json, comboprover | Loads a character file; projects it into the combo prover; bridges it into the kernel. |
| **`CseGame`** | `Games/UntitledFighter/Game/` | `CseKernel`, `CseData` — and nothing else, by an exact whitelist | The headless game layer: the session, tick-indexed input sources, the replay format and its verifier, and the live combo judge. No GL, no window, so its claims are testable without a context. |
| **`CseNet`** | `Net/` | GekkoNet (`PRIVATE`) | The rollback session seam. General-purpose, so it sits outside the title. |
| **`UntitledFighterModes`** | `Games/UntitledFighter/Modes/` | `Engine`, `CseGame` | The game modes the Player and the editor's Game view both run: training mode, the box overlay, the HUD. |
| **`UntitledFighterEditor`** | `Games/UntitledFighter/Editor/` | `CseData`, ImGui | The Combo Prover panel — the decision procedure's verdict in front of a designer. |

This page explains how to use them. It is not the design rationale — that lives in [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) (decisions D1–D9) and in the [ADRs](../adr/README.md), and this page links to them rather than restating them.

It is also not the rules. What may not appear inside a tick, what the build refuses, what an authored file must promise and — for each — what actually stops you breaking it, is [DETERMINISM.md](../DETERMINISM.md). Rules are cited below by id (`K2`, `T1`) so you can read the enforcement rather than take this page's word for it.

---

## Before you build on it

**What is built, what is in flight and what is missing is [ROADMAP.md](../ROADMAP.md), and only there.** This page used to keep its own "Not there yet" table; two lists of gaps disagree within a week, and the one nobody edits is the one people read. If a mechanism you need is absent, the roadmap says which work package adds it and what test will prove it.

What this page can usefully tell you is the shape of the evidence, because it decides how much of the manual to trust:

- Cross-toolchain bit-identity is **proven**, not argued: a golden state hash recorded under MSVC on Windows is reproduced exactly by gcc on Linux (`tests/test_determinism_crossplat.cpp`), on every CI run.
- Two of this project's own characters ship in `Games/UntitledFighter/Assets/Characters/`, staged as `Exported/Characters` beside the executables — the source moved and the staged path did not. The three transcribed MUGEN characters are *evidence* rather than content: they live in `tests/fixtures/characters/` and are deliberately unstaged, because this project holds no licence to ship them. One of them drives 400 deterministic ticks with snapshot/restore round-trips (`MatchBridgeSimulation.ARealCharacterDrivesTheKernelDeterministically` and `.SnapshotRestoreAndResimulateReproducesTheStraightRun`, `tests/test_match_bridge.cpp`).
- **A printed loop has been executed.** The prover's witness for `fighter_a_infinite`, turned into an input trace by walking `ProverResult::loop` rather than by hand, performs 26 hits in 160 ticks with the defender out of hitstun on none of them (`tests/test_ground_truth.cpp`). It returned **both** of the outcomes that experiment could return — see [What a verdict promises](#what-a-verdict-promises-and-what-it-does-not), which is the most important section on this page.

The kernel is **not** wired into `Application`'s fixed tick, deliberately and permanently — [DETERMINISM.md](../DETERMINISM.md) T1 and T2, and the note at the top of `Games/UntitledFighter/Kernel/include/cse/kernel/GameState.h` that says why. You drive the kernel from a session, or from a loop you own.

---

## The gameplay kernel

### Why it links nothing

This is [DETERMINISM.md](../DETERMINISM.md) K2, and it is a configure-time build failure rather than a convention — read the rule and its enforcement there.

The practical consequence for you: **you cannot reach the physics world, the ECS, GLM, or the standard library's floating-point machinery from inside a tick.** If you try, the build stops at configure time with a message explaining what you are about to break. That is the intended experience. The plan itself names "just ask the physics world for the hitbox" as the standing temptation, and against a separate target it is an unresolved symbol rather than a bad idea somebody has to talk you out of.

The same discipline shows up inside the source: `Games/UntitledFighter/Kernel/src/Simulate.cpp` includes `<cstring>` and nothing else, and it does not use `std::clamp` or `std::max` — because reaching for either is how a `double` overload gets selected by accident (`Games/UntitledFighter/Kernel/src/Simulate.cpp:25-32`).

### Units and rates

| Constant | Value | Meaning |
|---|---|---|
| `kSubUnitsPerPixel` (`GameState.h:36`) | `256` | Every position, velocity and box coordinate is `int32` sub-units. 1 pixel = 256. |
| `kTicksPerSecond` (`GameState.h:43`) | `60` | Every duration is ticks. |

256 was chosen so halving a velocity is exact — friction and knockback are authored as halvings, and a rounding difference is a desync.

There is **no general fixed-point type** and no fixed-point multiply: a 2D fighter adds velocity to position and compares rectangles, and an operator-overloaded `Fixed` buys an overflow surface for nothing. What you scale by, you scale through the one rounding rule — [DETERMINISM.md](../DETERMINISM.md) K8, which names the rule, the tests that pin it, and why `>>` is not division here.

### The state

```c++
// Kernel/include/cse/kernel/GameState.h:48
struct Fighter {
    std::int32_t  posX, posY, velX, velY;   // sub-units; posY 0 is grounded
    std::int32_t  health, meter;
    std::uint16_t moveId;                   // index into FighterData::moves; 0 = idle
    std::uint16_t moveFrame;                // ticks since the move started; it starts on 0
    std::uint16_t hitstun, blockstun;
    std::uint8_t  facing;                   // 0 = facing +X, 1 = facing -X
    std::uint8_t  airborne;
    std::uint8_t  comboHits;
    std::uint8_t  alreadyHitBits;           // GameState.h:94
};

// Kernel/include/cse/kernel/GameState.h:98
struct GameState {
    std::uint32_t tick;
    std::uint32_t rng;      // the PRNG lives INSIDE the snapshot
    Fighter       p[2];
};
```

`facing` is a flag, not a sign multiplier. A sign would invite `pos * facing`, which is the mirror-asymmetry bug the whole design is arranged to avoid.

`rng` being a member is load-bearing: rolling the state back rolls the random stream back, which is the only way a re-simulated tick can reproduce its first run.

> **Gotcha — a fifth `uint8_t` in `Fighter` is a wire-format change.** The four `uint8` fields exactly fill a 4-byte group, so the struct has no indeterminate padding, which is what makes hashing the object representation sound. Add a fifth without adding three more explicit bytes and the compiler inserts tail padding nobody initialises — and the desync checksum starts comparing two machines' uninitialised memory. The obligation is written out at `GameState.h:86-93`.

### Input

```c++
// Kernel/include/cse/kernel/GameState.h:108
struct Input     { std::uint16_t bits; };
struct InputPair { Input p[2]; };          // GameState.h:122
```

Ten bits, declared at `GameState.h:111-120`: `kInputUp`, `kInputDown`, `kInputLeft`, `kInputRight`, `kInputLP`, `kInputMP`, `kInputHP`, `kInputLK`, `kInputMK`, `kInputHK`.

Input is a **value parameter**; `Simulate` never queries `InputMap` ([DETERMINISM.md](../DETERMINISM.md) N1–N2). D6 is the reason and the [gameplay page](gameplay-scripting.md)'s `consumePressed` latch is the hazard it is avoiding: that latch is hidden, order-dependent, consuming state living outside the snapshot.

### Running a tick

```c++
#include "cse/kernel/Simulate.h"

using namespace cse::kernel;

GameState s{};
ResetMatch(s, 0xC0FFEEu);            // Simulate.h:42 — opens the two fighters
                                     // 200 px apart with 1000 health each

InputPair in{};
in.p[0].bits = kInputRight | kInputLP;
in.p[1].bits = kInputLeft;

Simulate(s, in, match);              // Simulate.h:29 — `match` is a MatchData
const std::uint32_t h = Checksum(s); // Simulate.h:51 — FNV-1a over the bytes
```

There are two overloads:

| Call | When |
|---|---|
| `Simulate(state, inputs, data)` (`Simulate.h:29`) | The real one. `data` is the read-only `MatchData` both fighters are fought with. |
| `Simulate(state, inputs)` (`Simulate.h:36`) | No characters loaded. Delegates to `kNoMoves` (`Combat.h`), which has `moveCount 0` and a degenerate hurtbox, so nothing can start, no box can be live, and nothing can hit. This is **exactly** the pre-hitbox kernel, not approximately — the cross-toolchain golden hashes were recorded against it. |

`ResetMatch` is a free function rather than a constructor because `GameState` must stay an aggregate with no user-provided constructors; that is what keeps it trivially copyable, and trivially copyable is what makes the snapshot a `memcpy`.

> **Important — `Simulate` is a pure function of `(state, inputs, data)`.** It reads no clock, no filesystem, no globals, and no RNG other than the one inside `GameState`. If you add anything to the kernel that breaks that, rollback stops working — and it stops working *silently*, on the remote peer, several minutes into a match.

### What one tick actually does

The tick is a **pipeline of fixed stages** ([ADR-012](../adr/ADR-012-the-tick-is-a-pipeline.md)), each run for every fighter in fixed slot order — never over a container whose order can vary, the hash-ordering hazard that has already bitten `SimplePhysicsBackend` and `ScriptWorld` in this repository. From `Games/UntitledFighter/Kernel/src/Simulate.cpp`:

1. **`ReadIntent`** — pure. What this input *means*: press and release edges against the latched `prevButtons`, the walk/jump/crouch wish, the buffer-eligible bits, and whether hitstop froze this fighter. Computed once, so no two stages can disagree about what was pressed.
2. **`StepPhysics`** — the freeze (a frozen fighter only counts its `hitstop` down), the stun clocks, then movement: the walk wish lands in `velX`, up sets `velY` and `airborne` when grounded, gravity applies while airborne, position integrates, `posX` clamps the **body** to the stage, landing at `posY <= 0` zeroes `posY`, `velY` and `airborne`. Since M1.3(b2), a committed fighter whose move authors **motion keys** (`MoveDef::motion`) flies them instead: the active key owns both velocity components, an upward key leaves the ground, and gravity is skipped while a key owns the arc — the lunge, the hop kick and the divekick are authored fields, exactly as ADR-011 demands.
3. **`StepAttack`** (declared in `Combat.h`, implemented in `Games/UntitledFighter/Kernel/src/Combat.cpp`) ends a move that has run out and starts one the fighter is asking for. It runs *after* movement so a move started this tick sees the position the fighter reached.
4. **Resolve** — everything that needs more than one fighter, plus the input latch. `LatchInputs` (the one writer of `prevButtons` and the buffer, and it records **during hitstop** — see the buffer note below) · the invisible wall and the push boxes, read off top-of-tick positions · **facing** derived from relative position, after everyone moved so it cannot depend on step order · guard · **`ResolveHits`** · the round rule · the RNG advances every tick whether or not anything consumed it, so its position is a function of the tick count alone · `++state.tick`.

Each field of `Fighter` has **one writing stage** — the audit table is in `Simulate.h`, with the two named exceptions (imposed facts like `hitstun`, written by physics as a clock and by `ResolveHits` as a consequence; and ADR-012's own two-writer rule for `crouching`).

Most movement numbers are **placeholders**, not character data. `Simulate.cpp`'s tuning block declares gravity 0.25 px/tick², jump impulse 5 px/tick and the exported `kStageHalfWidthSub` of 480 px, with a comment saying so. Walk speed is the exception: `FighterData::walkSpeedSub` is authored and the kernel reads it, falling back to 2 px/tick when a file says nothing. `engine.constants` authors `gravity_sub` and `jump_vel_sub` too, and the loader does not read them yet — [ROADMAP.md](../ROADMAP.md) M1.3d.

### How a fighter moves

The genre's movement rules, each landed 2026-08-20/30 with the test that owns it.

- **Selection reads the input; posture follows the move** ([ADR-012](../adr/ADR-012-the-tick-is-a-pipeline.md) rule 3, ROADMAP M1.3e). Which stance variant a press asks for is decided by what is **held now** — Down for a crouching normal, the takeoff Up provides for an aerial — never by the posture commitment froze, so an ordinary gatling like `stand_mp → crouch_hp` works mid-move. Starting a crouching move makes the fighter crouching and a standing move stands them up (the frame data was authored against that body); a move that states no posture imposes none. `P2Stance.*`, and end-to-end on the shipped file `MatchBridgeMechanics.ADirectionEstablishesTheStanceOnTheTickItIsPressed` and `.EveryAuthoredNormalIsReachableThroughItsButtonAndStance`.

- **Commitment.** A fighter whose move was already running at the top of the tick does not walk, does not jump and does not change posture — the frames were given up on the press. The one-tick edge matters: `Up`+button on a single tick still takes off and attacks, because `StepPhysics` runs before `StepAttack`, so no aerial needs a scripted jump. Per-move exceptions (a lunge, a hop kick) are authored motion, ROADMAP M1.3(b), never a kernel rule. `P2Commitment.*` in `tests/test_p2_mechanics.cpp`.
- **The jump is ballistic.** Horizontal velocity is decided at takeoff from the direction held on the jump tick, and nothing in the air recomputes it — not an air normal (it rides the arc), not a held direction (no air steering). Being hit still zeroes it, until M1.3(d)'s launch vector replaces that properly. The arc is ~38 ticks against `air_mp`'s 22, which is why an air self-cancel yields repetitions per jump rather than an infinite. `P2Ballistic.*`.
- **The invisible wall.** Two fighters are never further apart than `kMaxSeparationSub` (374 px), anchored to where the opponent stood at the top of the tick — so retreat resumes for exactly as long as the chaser advances, and stops when they stop. Simulation, not camera: it decides whether a move reaches. It is currently a kernel constant, named as a debt in ROADMAP M1.1g. `P2Stage.*`.
- **Push boxes.** Fighters cannot occupy the same ground: the overlap splits equally, rounds up (an odd overlap must resolve), and is an exact mirror. Airborne fighters pass over — that is what a cross-up is. The wall clamps the **body**, not the origin, so nobody stands half inside a corner; `wallLimitFor` is shared by the walk clamp and the separation pass so neither can undo the other. `P3Pushbox.*`.
- **Knockdown.** A downed fighter cannot act, cannot be hit (`InvulnerableTo` answers before the move lookup, because a downed fighter has no move), and **is lying down** — the kernel's own `Hurtbox` returns the standing box tipped over, floor edge at zero, so the state reads in silhouette and not just in the overlay's colour. OTG will arrive as an authored per-move field, not a loosening. `P2Knockdown.*`.
- **Crouching.** The body is the authored `crouch_height_px` (34 px against `fighter_a`'s 60) at the standing width; a move's own `hurtboxOverride` outranks the posture, because the move is the more specific statement; a character authoring nothing keeps one body. One ordering fact worth knowing: a crouching move cannot start on the exact tick of landing, because `crouching` is computed before the landing clamp. `P2Crouch.*`, `P2Movement.ACrouchingMoveCannotStartOnTheTickOfLanding`.

The training keys: punches on **U/I/O**, kicks on **J/K/L** (the arcade rows read off a keyboard), **V** toggles corner/midscreen, **R** resets, and the floor's checkerboard is a ruler — 20 px squares, a heavier line every reach unit (100 px), so an authored `reach: 0.42` is four squares and a bit, counted off the floor.

### Boxes

Boxes are integer rectangles in sub-units, authored **relative to the fighter's origin and as if the fighter faced +X**, and **half-open on both axes**: `x ∈ [x0, x1)`, `y ∈ [y0, y1)`.

Half-open removes the off-by-one that inclusive bounds smuggle into every width calculation, and it makes two boxes that merely touch *not* overlap — so a hit at exactly maximum range is a clean miss rather than a value that depends on which end you measured.

| Function | Header | What it does |
|---|---|---|
| `BoxIsValid` | `Combat.h` | Well-formed, non-empty, within bounds. For the **data loader** — the simulation is total and never needs to ask. |
| `MirrorBox` | `Combat.h` | Reflect across x = 0: `{x0,y0,x1,y1}` → `{-x1, y0, -x0, y1}`. Negate **and swap**. |
| `PlaceBox` | `Combat.h` | Mirror by facing, then translate by the origin. Inputs clamped symmetrically about zero. |
| `BoxesOverlap` | `Combat.h` | Half-open overlap; shared edges do not overlap. |
| `ActiveHitbox` | `Combat.h` | The fighter's live hitbox in stage coordinates, or `false`. |
| `Hurtbox` | `Combat.h` | The fighter's body in stage coordinates. Always exists. |

> **Important — mirroring is negate-and-swap, never a multiply.** Negating each edge *without* the swap is also exact and also wrong: it produces `x0 > x1`, an inside-out rectangle that every overlap test reports as empty, so a mirrored character would simply never hit anything. And any construction that *scales* (reflecting about a pivot, multiplying by a signed facing, halving a width to find a centre) goes through a division, where `>>` and `/` round differently — costing a left-facing character one sub-unit of reach that its right-facing twin keeps. One sub-unit is 1/256th of a pixel, and it decides whether a combo connects. `MatchBridgeReach.ReachIsExactlyMirroredForALeftFacingFighter` pins this to the sub-unit on a real character.

### Hit resolution

`ResolveHits` (`Games/UntitledFighter/Kernel/src/Combat.cpp`) is **three loops**, and the shape matters:

1. Decide every overlap from the same pre-hit state.
2. Apply damage and hitstun.
3. Interrupt the defenders.

If p0's hit were applied before p1's overlap were tested, a trade would stop being a trade — p1 would already be in hitstun, and whether it landed its own blow would depend on which slot was checked first. That is worse than a rounding bug, because it is *stable*: it never looks like nondeterminism locally, it just makes player 1 lose trades.

The rule today is the **symmetric** one: both fighters land. Counter-hit is an opt-in per-move field, not a kernel rule -- [ROADMAP.md](../ROADMAP.md) M1.3.

Three behaviours to design against:

- **The multi-hit guard.** `Fighter::alreadyHitBits` records which slots this active window has already connected on. Without it a 3-frame jab deals its damage three times and every string in the game is an infinite for a reason that has nothing to do with the character. It is cleared when a move starts and when it ends (`StepAttack`, in `Games/UntitledFighter/Kernel/src/Combat.cpp`) — the bug on the other side of that line is a jab that connects once per match. `tests/test_combat.cpp:349` and `:378` are the pair.
- **Hitstun is set, not added.** A fresh hit refreshes stun rather than stacking it. Stacking is how a two-hit string becomes inescapable.
- **Being hit interrupts the defender's move** (`moveId = 0`). Hitstun gates *starting* a move and nothing else, so without this a fighter would go on swinging while being hit.

**Moves start on the PRESS, and a press is an edge.** `StepAttack` (`Games/UntitledFighter/Kernel/src/Combat.cpp`) scans move slots in ascending order and takes the first whose `button` mask is entirely down *and* newly down this tick — `bits & ~prevButtons`. `Fighter::prevButtons` is where last tick's buttons live, and it is in `GameState` rather than in the input producer because a rollback hands `Simulate` only the current tick's bits, so an edge computed anywhere else replays a press as a hold ([DETERMINISM.md](../DETERMINISM.md) D6). Holding a button therefore starts a move **once**, and holding it is reserved for mechanics that do not exist yet.

Two routes reach a move besides that press, both opt-in per character or per move:

- **A buffered press.** `FighterData::inputBufferFrames` — zero, and so off, unless a file asks for it — remembers a press made while the fighter could not act, and **consumes** it the tick they can. It feeds the button scan *and* the cancel scan: a buffered press takes a cancel the tick its window opens, which is what makes a two-frame link something a human can hit. Consumed rather than aged, or one press would walk a fighter down a chain. Only bits some move can use are recorded, so a direction tap cannot clobber a buffered reversal. **Hitstop does not eat it**: recording runs during the freeze, aging is suspended (frozen ticks are not time), and the `prevButtons` latch is withheld so a release inside the freeze still fires a negative edge on thaw — the three `P3Input` freeze tests in `tests/test_p2_mechanics.cpp` pin all of this.
- **A release.** `MoveDef::negativeEdge`, off by default, lets a move fire on `~bits & prevButtons` — the SF-lineage hold-motion-release special. Nothing distinguishes a "normal" from a "special" in the schema, so the rule *no normal fires on release* holds by construction: a normal that opts in is an authoring error, not a kernel one.

> **Gotcha — a move whose mask is a superset of an earlier slot's can never start.** Slot order still decides, so `{Down, LP}` in a later slot is unreachable if `{LP}` sits in an earlier one; see the binding warning below. A test that wants a single hit now simply presses, but a test that wants the *same* move twice must release between the two presses — two presses on consecutive ticks is one hold.

### Snapshot, restore, checksum

There is no serializer. `GameState` is trivially copyable, so a snapshot is an assignment or a `memcpy`, and a restore is the same thing backwards:

```c++
const GameState snapshot = live;   // save
// ... predict forward ...
live = snapshot;                   // restore
for (std::size_t t = from; t < to; ++t)
    Simulate(live, inputs[t], match);   // re-simulate
```

`Checksum` (`Simulate.h:51`) is FNV-1a over the raw bytes — ADR-002 CHOICE C's desync checksum, exchanged every 8 ticks. Hashing the object representation is only sound because the struct has no padding holes and no pointers; `tests/test_kernel.cpp:53` asserts `sizeof`, `alignof` and trivial-copyability **before** any hash, so a padding change reports itself as a padding change rather than as an arithmetic divergence.

Properties the suite pins down, so you know what you may rely on:

| Property | Test |
|---|---|
| Same inputs → byte-identical state | `tests/test_kernel.cpp:92` |
| Re-simulating from a snapshot reproduces the straight run | `tests/test_kernel.cpp:126` |
| Every rewind depth 1–8 is exact | `tests/test_kernel.cpp:150`; with real character data, `MatchBridgeSimulation.EveryRewindDepthUpToTheBudgetIsExact` |
| The checksum catches a single flipped bit | `tests/test_kernel.cpp:167` |
| Walking left and right are exact mirrors through `Simulate` | `tests/test_determinism_crossplat.cpp:923` |
| A recorded hash reproduces across MSVC and GCC | `tests/test_determinism_crossplat.cpp:667` |

---

## Character data

### Where the files live

`Games/UntitledFighter/Assets/Characters/` — inside the title's asset root, so it is staged
next to every executable — holds:

| File | What it is |
|---|---|
| `fighter_a.json` | **This project's own character**, authored against schema v2 with the engine's own numbers in front of the author. 18 moves, 73 cancels, TERMINATING in the corner, and the first character here to carry a ranking certificate |
| `fighter_a_infinite.json` | A second character of the same family carrying **one deliberate bug**: `cancels[0]` is `stand_lp` cancelling into itself after 2 ticks. INFINITE, and the printed loop executes. (It is not `fighter_a` plus an edge — the move lists differ — so read it as its own character) |
| `schema.v2.json` | The current schema, including its `x-load-assertions` block |

`cmake/stage_runtime_assets.cmake` copies every subdirectory of `Editor/src/Exported` next to each executable, so a path like `Characters/fighter_a.json` resolves against `Exported` with no configuration.

> **Gotcha — the three MUGEN characters are not here and are not staged.** `kung_fu_girl.json`, `kung_fu_man.json`, `aof2_strength_training.json` and `schema.v1.json` live in `tests/fixtures/characters/`: they are Phase-0 *evidence*, they are somebody else's characters, and staging them would put them in front of a designer as though they were content. Tests reach them by fixture path. Anything else — the Combo Prover panel included — will not find them, and that is the intent.

### What is in a character

`Games/UntitledFighter/Data/include/cse/data/CharacterData.h` is the whole in-memory model. It is a plain vector-of-struct: no hash containers anywhere, because this repository has twice had iteration order over a hash container leak into a simulation.

| Type | Header | Carries |
|---|---|---|
| `CharacterData` | `:267` | id, name, `stage`, walk speed, scaling table, decay, resources, gap actions, moves, cancels, starters, plus prebuilt `cancelsFrom` and `moveIndexById` |
| `Move` | `:155` | The prover-read subset (`startup`, `active`, `recovery`, `hitstun`, damage, `reachSub`, `pushbackSub`, stance, `effect`, `guard`) plus engine-only extras |
| `Cancel` | `:208` | `from`, `to`, `delay`, `on`, effects, guards, and `certain` |
| `GapAction` | `:236` | Frames, max uses, effects, guards, and the `__space` displacement only gap actions may author |
| `Decay` | `:258` | `kind` (None / Linear / Table), `step`, `floor`, permille table |
| `ResourceDef` | `:95` | Name, initial, floor, optional ceiling |
| `HitRecord` | `:123` | One HitDef of a move that registers several |
| `MotionKey` | `:145` | One keyframe of the attacker's own displacement |

**Every field is an integer.** The authored files carry damage, reach, pushback, walk speed and the scaling table as JSON floats; the loader converts once, at load. Distances are sub-units (1 px = 256), durations are ticks, damage is **hundredths** of a point, meter is in units of 10 MUGEN power, and scaling/decay tables are permille (`CharacterData.h:35-41`). Where a file ships a pre-quantized integer beside the float — `engine.quantized_sources` — the **integer wins**, because it is the number the author actually derived.

Two things are deliberately **not** loaded (`CharacterData.h:301-316`): the structured predicate form of `hit_condition` / `engine.condition` (every file in the tree authors prose instead, and evaluating one needs an opponent namespace that the trigger expression language would have owned -- and that language is deliberately not being built; see [ARCHITECTURE.md](../ARCHITECTURE.md) section 2), and nine `engine.*` sub-objects with no data behind them yet — including `engine.constants`, which is why you have to supply a body to the bridge yourself.

### Loading one

```c++
#include "cse/data/CharacterData.h"

using namespace cse::data;

LoadOptions options;                                    // CharacterData.h:320
options.expectedResources = { "meter", "juggle" };      // the BUILD's order
// options.maxFileBytes defaults to 64 MiB

CharacterData character;
LoadReport    report;                                   // CharacterData.h:338

if (!LoadCharacterFile("Exported/Characters", "fighter_a.json",  // as staged, next to the exe
                       options, character, report)) {   // CharacterData.h:349
    // report.error is non-empty; report.rule is "A01".."A08" when a load
    // assertion is what refused it.
    log(report.error);
    return;
}
for (const std::string& w : report.warnings) log(w);    // non-fatal
```

`LoadCharacterJson` (`CharacterData.h:359`) takes JSON already in memory. It exists so a test can construct a character that violates a load assertion without writing a hostile file to disk — an assertion nobody has watched fail is not an assertion.

**A rejected file is a normal outcome.** There is no `throw`, no `abort` and no `exit` anywhere in the loader: authored content is untrusted, and a hostile or merely broken character must not be able to take a match-start path down.

Three untrusted-content rules apply to every load:

- `relPath` goes through `MyCoreEngine::PathIsContained` **before** the file is opened (`Games/UntitledFighter/Data/src/CharacterData.cpp:1050`). Absolute paths, drive/UNC roots and any `..` component are refused lexically, before any filesystem access.
- The file is refused above `LoadOptions::maxFileBytes` before it is read (`Games/UntitledFighter/Data/src/CharacterData.cpp:1063`). A 4 GB "character" costs nothing to author.
- The JSON is parsed with exceptions off, and every field is type-checked before it is read.

### The load assertions, and what each one prevents

These are ADR-001's assertions, and they run at load because load time is where they were always meant to run. Each one refuses the file and sets `LoadReport::rule`.

| Rule | Site | Refuses | Because |
|---|---|---|---|
| **A01** | `CharacterData.cpp:812` | `decay.floor` greater than the smallest **nonzero** hitstun in the file | Both implementations compute `max(floor, base − step·n)`, so a floor *above* a move's authored hitstun **raises** it and invents frame advantage. This is not hypothetical: the project's own draft house rule (linear, step 2, floor 10) exceeded Kung Fu Girl's `stand_lp` hitstun of 9 and **fabricated an infinite combo**. Minimised over moves that actually deal hitstun, because a dash or a taunt would otherwise drag the minimum to zero and fail every file with a nonzero floor. |
| **A02** | `CharacterData.cpp:600` | `decay.kind: "multiplicative"` | Both reference implementations compute it as a chain of float multiplies then a truncating cast, and an integer kernel reproduces neither. For a decision that turns on `hitstun >= startup − advantage`, a one-frame disagreement is the whole difference between TERMINATING and INFINITE. Banning the kind removes a divergence class instead of bounding it. Use `linear`, or `table` with integer permille multipliers. |
| **A03** | `CharacterData.cpp:576` | A resource order that disagrees with `LoadOptions::expectedResources` | The prover keys its resource vector **positionally**, so a character whose index 0 is `juggle` compares juggle against meter and says nothing about it. This is a cross-file rule no single file can check, so the caller supplies the build's order. Leaving `expectedResources` empty **skips** the check and records a warning — it never passes silently. |
| **A04** | `CharacterData.cpp:267` | `__space` in a move's or a cancel's effect/guard map | `__space` is the prover's spatial resource. On a gap action it is the only way to express displacement and every character in the tree authors it, so gap actions are exempt. |
| **A05** | `CharacterData.cpp:372` | `hits[0]` disagreeing with the move's own `startup` / `hitstun` scalars | A cancel delay is measured from first contact, and `startup` is what that arithmetic subtracts. The scalars must **be** the first hit, not an average of the hits. |
| **A06** | `CharacterData.cpp:393` | A multi-hit move whose last *unconditional* hit changes the hitstun, with no `engine.hits_projection_caveat` naming the direction of the error | Only unconditional records count as sequels. Kung Fu Girl's chop registers its second HitDef only when the first **whiffed**, so the two can never both land — a draft of this check without the exclusion fired on it. |
| **A07** | `CharacterData.cpp:351` | `engine.hits[]` ticks that do not strictly increase | Out-of-order hits make "the first hit" ambiguous. |
| **A08** | `CharacterData.cpp:431` | `engine.motion[]` ticks that do not strictly increase, or a non-integer velocity | Two keyframes on one tick means the result depends on which the loader applied last; a float velocity is a float in the simulation. |

The corresponding tests are in `tests/test_character_data.cpp` — `:320` for the A01 case that fabricated an infinite, `:393` for A02, `:273` and `:300` for A03's two halves.

### Talking about moves

```c++
const MoveIndex i = character.FindMove("stand_lp");   // CharacterData.h:294
if (i == kInvalidMove) { /* CharacterData.h:62 — 0xFFFF, NOT 0 */ }
```

`FindMove` is a binary search over a sorted vector. The sentinel is `0xFFFF` rather than `0` because 0 is a perfectly good move index here — a sentinel that collides with a real value is how "not found" becomes "the first move in the file".

If you assemble a `CharacterData` by hand rather than loading one, call `RebuildIndices()` (`CharacterData.h:298`) afterwards. It rebuilds `moveIndexById` and `cancelsFrom`; without it `FindMove` finds nothing.

---

## MatchBuilder: two characters into one `MatchData`

`Games/UntitledFighter/Data/include/cse/data/MatchBuilder.h` is the bridge. On one side is a loaded `CharacterData` with `std::string` and `std::vector` in it; on the other are the bytes a tick reads.

The dependency runs **one way only**. `CseData` includes `cse/kernel/Combat.h` for the struct definitions and calls no function declared in it — not even `BoxIsValid`, which would be the natural thing to reuse, because calling it would put `CseKernel` into `CseData`'s link line and `Games/UntitledFighter/Data/CMakeLists.txt` asserts that never happens. The one bound check the bridge needs is written out again against the kernel's own constant.

### Building

```c++
#include "cse/data/MatchBuilder.h"
#include "cse/kernel/Simulate.h"

using namespace cse::data;

BuildOptions options;                                   // MatchBuilder.h
options.body.halfWidthSub = 13 * cse::kernel::kSubUnitsPerPixel;
options.body.heightSub    = 60 * cse::kernel::kSubUnitsPerPixel;
options.bindings = {                                    // MatchBuilder.h
    { "stand_lp", cse::kernel::kInputLP },
    { "stand_mk", cse::kernel::kInputMK },
};

MatchBuild build;                                       // MatchBuilder.h
if (!BuildMatchData(p0Character, options,
                    p1Character, options, build)) {     // MatchBuilder.h
    log(build.report[0].error);                         // both sides are reported
    log(build.report[1].error);
    return;
}

cse::kernel::Simulate(state, inputs, build.data);
```

`BuildFighterData` (`MatchBuilder.h`) builds one side, if that is all you need.

### Two things you must supply, because the schema has neither

**The body.** `CharacterData` carries no hurtbox at all — the files put it in `engine.constants`, which the loader deliberately does not read. So `BodySpec` (`MatchBuilder.h`) is the caller's. Leave it at zero and you get `kDefaultBodyHalfWidthSub` / `kDefaultBodyHeightSub` (13 px and 60 px, `MatchBuilder.h`) **plus a warning naming them** — so a mystery number never looks like the character's own. The box is symmetric about the origin and stands on the floor: `x ∈ [-halfWidth, +halfWidth)`, `y ∈ [0, height)`.

**The button bindings.** The schema has no input notation — not a command list, not a motion, not a button name. Move ids like `stand_lp` carry it only in the English of their spelling, and reading a button out of a string suffix is exactly the heuristic import D7 rejects. So bindings come from the caller, where they are visible. A move nobody binds gets `button = 0`, which the kernel reads as "not startable from a button".

Three binding diagnostics, all warnings rather than errors:

| Situation | What you get | Why it is not fatal |
|---|---|---|
| A binding names a move this character lacks | Warning naming the id (`MatchBridgeOptions.ABindingForAMoveThisCharacterLacksIsAWarningNotAFailure`) | One binding table shared by two characters with different movesets is a reasonable thing to write |
| The same move bound twice | Warning; **first wins** (`MatchBridgeOptions.ADuplicateBindingKeepsTheFirstAndSaysSo`) | "The last one in the list" is a rule nobody reading the call site can see |
| A binding that can never start | Warning saying `can never start` (`MatchBridgeOptions.ABindingThatCanNeverStartIsReportedAndReallyNeverStarts`) | See below |

> **Gotcha — a crouching normal bound to Down+LP silently never fires.**
> *(Being fixed: [ROADMAP.md](../ROADMAP.md) M1.1c makes a binding
> `button → strength` and lets the move's `stance` disambiguate, so a
> crouching normal is LP-while-crouching rather than a chord.)* `StepAttack` takes the first move in slot order whose buttons are *all* down on the tick they are pressed. `stand_lp` is slot 1 with `{LP}` and `crouch_lp` is slot 12 with `{Down, LP}`, so holding Down+LP can only ever produce `stand_lp`. That is the natural way somebody binds crouching normals, and it does not work. The builder detects the shadowing and warns; `MatchBridgeOptions.ABindingThatCanNeverStartIsReportedAndReallyNeverStarts` checks the warning *against the kernel* rather than merely believing it.

### Capacity: a refusal, not a truncation

`FighterData` has 32 move slots (`kMaxMovesPerFighter`, `Combat.h`), slot 0 reserved for idle, so 31 are usable — `kMaxBuildableMoves` (`MatchBuilder.h`) is derived from the kernel's constant rather than written as 31.

A character with more moves is **refused**. `BuildFighterData` returns `false`, the message names the character, the count and the cap, and nothing is left behind — no half-built `FighterData`, no half-built `MatchData` (`MatchBridgeCapacity.OneMoveOverTheCapIsRefusedRatherThanTruncated` and `MatchBridgeCapacity.AFailedSideLeavesNoHalfBuiltMatch`).

Truncating would be worse than it sounds. A truncated build is not a smaller character, it is a *different* one: the moves that fell off the end are the ones a designer added last, every cancel into them becomes a cancel into nothing, and — fatally — the verdict the prover computed was computed over the whole file. A truncating bridge would let the editor certify a character that terminates and then ship a game playing a character nobody analysed.

### `MoveIndexMap`: naming moves after the build

`MoveIndexMap` (`MatchBuilder.h`) is the mapping between a character's move ids and the kernel's slots. **It is one addition**: character move `i` becomes kernel slot `i + 1`. File order is preserved rather than sorted or compacted, which is what lets a live match and a prover verdict name the same move.

```c++
const std::uint16_t slot = build.moves[0].Find("stand_lp");   // 0 if absent
const std::string_view id = build.moves[0].IdOf(slot);        // "" for idle
MoveIndexMap::CharacterMoveOf(slot);                          // back to MoveIndex
MoveIndexMap::KernelMoveIdOf(moveIndex);                      // and forward again
```

Note the sentinel asymmetry with `CharacterData::FindMove`: here `0` **is** safe, because slot 0 is reserved and can never name a move, so "0", "idle" and "no such move" are the same statement — a caller who forgets to check gets a fighter that stays idle rather than one that performs the first move in the file.

### Reading `BuildReport` — the part that matters most

```c++
// MatchBuilder.h
struct BuildReport {
    std::string              error;            // empty iff the build succeeded
    std::vector<std::string> warnings;
    std::vector<BuildLoss>   losses;
    std::int32_t             lossesThatBite  = 0;   // :224
    bool                     playsAsAnalysed = false; // :239
};
```

A `BuildLoss` (`MatchBuilder.h`) is one named thing the projection could not carry, with a **count** scoped to this character and a **direction** (`MatchBuilder.h`):

| Direction | Meaning |
|---|---|
| `Exact` | The kernel reproduces what the file says. |
| `KernelPermits` | The kernel lets the character do something the file does not — a move with no stance restriction, an attack that costs meter the fighter never earned. **More** is possible in the game than in the file. |
| `KernelOmits` | The kernel does not do something the file authors — a cancel, a pushback, a scaling curve. **Less** is possible in the game than in the file. |

Neither direction is "the safe one". The prover's verdict was computed over the file; either direction means the verdict is about a character nobody is playing.

**A loss with count 0 is still listed.** That is deliberate: knowing a check ran and found nothing is what tells "this character has no decay" apart from "nobody looked". Kung Fu Girl's `decay` entry has count 0 with a note containing the word `inert`, and `MatchBridgeLosses.TheDecayEntryRecordsThatItCheckedRatherThanThatItSkipped` asserts exactly that.

Here is the full table for Kung Fu Girl; `MatchBridgeLosses.EveryDropIsCountedAgainstKungFuGirlsActualFile` (`tests/test_match_bridge.cpp`) counts every row of it out of her actual file, and asserts the row *count* as well, so an entry that appears or disappears has to be recorded here:

| Field | Count | Direction |
|---|---:|---|
| `cancels (dropped)` | 0 | KernelOmits |
| `cancels (link, not cancel)` | 0 | KernelPermits |
| `cancel.contact_frame` | 132 | KernelPermits |
| `cancel.on` | 4 | Exact |
| `cancel.certain` | 103 | KernelPermits |
| `cancel.guard` | 41 | KernelPermits |
| `cancel.effect` | 0 | KernelOmits |
| `move.cancel_window (absent)` | 8 | KernelPermits |
| `character.walk_speed` | 1 | Exact |
| `character.movement` | 0 | Exact |
| `resource.juggle (gate)` | 0 | Exact |
| `character.input_buffer_frames` | 0 | Exact |
| `move.pushback` | 24 | Exact |
| `move.corner_push` | 0 | Exact |
| `move.counter_hit` | 0 | Exact |
| `move.air_hitstun` | 0 | Exact |
| `move.launch` | 0 | Exact |
| `move.on_hit` | 0 | Exact |
| `move.hitstop` | 0 | Exact |
| `move.stance` | 25 | Exact |
| `move.blocked_as` | 0 | Exact |
| `move.guard` | 2 | Exact |
| `move.effect` | 24 | Exact |
| `resources` | 2 | Exact |
| `move.hit_condition` | 17 | KernelPermits |
| `move.escape_hatch` | 15 | KernelPermits |
| `scaling` | 6 | KernelOmits |
| `decay` | 0 | KernelOmits |
| `gap_actions` | 1 | KernelOmits |
| `starters` | 21 | KernelOmits |
| `move.engine.hits` | 0 | KernelOmits |
| `move.engine.motion` | 0 | Exact |
| `move.engine.motion (pos_add)` | 0 | KernelOmits |
| `move.reach (absent)` | 0 | KernelOmits |
| `move.reach (provenance)` | 25 | KernelPermits |
| `move.hitbox.y` | 25 | KernelPermits |
| `hurtbox` | 1 | KernelPermits |

Nineteen entries bite, so `lossesThatBite == 19` — nonzero `Exact` rows count too, because a nonzero row of any direction is a place somebody has to have looked. Her seven zero-count `Exact` rows (juggle gate, input buffer, hitstop, blocked_as, movement, motion keys, corner push) are the why-zero exhibits: her converted file authors none of those mechanics, and the row is the proof a check ran.

The first eight rows replaced what used to be a single `cancels` entry with a count of 134. **All 134 of her edges are now carried**; what remains listed is the part of each edge the kernel cannot yet honour, and every row still short errs `KernelPermits` — the game chains in situations the file does not allow. A combo system uniformly more permissive than the analysed one is exactly how a `TERMINATING` verdict becomes a game with an infinite in it, which is the next section. `cancel.on` left that list at M1.3 slice (a): the contact mask carries the file's `on` whole — `on: hit` no longer fires off a blocked contact, `on: block` no longer off a clean hit, and `on: whiff` (the kara) is expressible at all — so its row reads `Exact`, with the count recording how many edges the old one-bit collapse used to move.

### `playsAsAnalysed`

> **This is the field to read, and today it is `false` for every character the schema can express.**

`playsAsAnalysed` is true only when `lossesThatBite == 0` — when the kernel plays the character the file describes, and therefore the character the prover analysed. It is **computed, not hardcoded**, and it has already been through the event it was written for: the kernel grew cancels, the `cancels` row with 134 against it disappeared, and the flag did not move, because plenty else still bites. A remembered flag would have flipped and been wrong.

Two entries are nonzero for structural reasons no file can avoid — the schema authors no vertical extent for an attack (`move.hitbox.y`) and no body at all (`hurtbox`) — so this is a goal-post rather than a discriminator today. What it buys you right now is that **a green build cannot be mistaken for a faithful one**. If you are building tooling on top of this, surface `lossesThatBite` next to any successful build, and never let a UI imply that a verdict describes the running game while this flag is false.

The next section is what happens when somebody does.

### What the bridge converts, and what it does not

Exactly one conversion happens here, because D8 says a quantization happens once, at load, by one documented rule:

| Quantity | Conversion |
|---|---|
| Frame data (startup / active / recovery / hitstun) | **Identity.** Schema v2 authors ticks at 60 Hz already. |
| Distances | **Identity.** The loader already produced sub-units. |
| Damage | **hundredths → points**, rounded half away from zero, matching D2's `scaleBy`. Exact for every character in the tree. |

The hitbox is built so that `Move::reachSub` — documented as "maximum gap at which the move connects" — is literally true: the box starts at the front of the body and extends the authored reach, plus one sub-unit converting the file's inclusive maximum into the kernel's half-open bound. `MatchBridgeReach.TheAuthoredReachIsTheExactMaximumGap` tests that boundary to the sub-unit, and `MatchBridgeReach.ReachIsExactlyMirroredForALeftFacingFighter` tests it again mirrored.

A move whose file authors `reach: null` (Kung Fu Man's two projectiles) gets a **zero-width box** rather than a guess. Its frame data still survives; it just cannot connect (`MatchBridgeReach.AMoveWhoseFileDeclinesToStateAReachConnectsWithNothing`).

---

## The rollback session seam

`Net/include/cse/net/ISession.h` is the interface. GekkoNet is the only implementation, and `Net/src/GekkoSession.cpp` is the only translation unit in the project that includes `gekkonet.h` (`:14`) — `Net/CMakeLists.txt` links GekkoNet `PRIVATE` to make that structural rather than aspirational.

### It is an event pump, not a "call me to roll back" API

**The session drives the loop.** It decides when to advance, when to save and when to load, and it tells you. This shape was found in GekkoNet during the spike and named as a constraint the plan had missed; it lives in the seam rather than in one implementation of it, so a replacement must emit the same stream.

```c++
// ISession.h:40
enum class SessionEventType { Advance, Save, Load };
```

The pump, in full:

```c++
#include "cse/net/ISession.h"
#include "cse/kernel/Simulate.h"

using namespace cse::net;

// The wire input type is deliberately NOT cse::kernel::Input: input size is a
// network concern and the kernel's input type is a simulation concern.
struct WireInput { std::uint16_t bits; };

SessionConfig cfg;                                    // ISession.h:95
cfg.playerCount         = 2;
cfg.inputBytesPerPlayer = sizeof(WireInput);
cfg.stateBytes          = sizeof(cse::kernel::GameState);
cfg.predictionWindow    = 8;                          // D4 budgets 8
cfg.desyncDetection     = true;
cfg.desyncCheckInterval = 8;                          // ADR-002 CHOICE C

ISession* session = CreateGekkoLocalSession(cfg);     // ISession.h:141 — null on failure

cse::kernel::GameState live{};
cse::kernel::ResetMatch(live, 0xC0FFEEu);

// once per frame:
WireInput a{ localBits0 }, b{ localBits1 };
session->AddLocalInput(0, &a);                        // ISession.h:116
session->AddLocalInput(1, &b);

int n = 0;
const SessionEvent* ev = session->Update(&n);         // ISession.h:121
for (int i = 0; i < n; ++i) {
    switch (ev[i].type) {
    case SessionEventType::Save:
        std::memcpy(ev[i].saveBuffer, &live, sizeof(live));
        *ev[i].saveLength   = sizeof(live);
        *ev[i].saveChecksum = cse::kernel::Checksum(live);
        break;
    case SessionEventType::Load:
        std::memcpy(&live, ev[i].loadBuffer, sizeof(live));
        break;
    case SessionEventType::Advance: {
        const auto* w = reinterpret_cast<const WireInput*>(ev[i].inputs);
        cse::kernel::InputPair in{};
        in.p[0].bits = w[0].bits;
        in.p[1].bits = w[1].bits;
        cse::kernel::Simulate(live, in, match);
        break;
    }
    }
}

DestroySession(session);                              // ISession.h:148
```

> **Important — handle the events IN ORDER.** A `Load` followed by `Advance`s *is* the rollback. Reordering them silently corrupts the state. The event array is owned by the session and valid only until the next `Update`.

> **Important — suppress player-visible effects while `rollingBack` or `runningAhead` is set** (`ISession.h:64`, `:67`). Sound, particles, screen shake, haptics. Getting this wrong is the classic rollback bug where a 7-frame correction plays the same hit sound eight times.

### Three rules the seam enforces

1. **No `GameState`, anywhere.** Not in a parameter, not in a template argument, not behind a typedef. The session moves **bytes and a length** ([DETERMINISM.md](../DETERMINISM.md) N3). The snapshot is a `memcpy` of a POD, so bytes is all it needs — and letting the state's *type* into the session layer is how game #2 ends up forking the netcode.
2. **No float crosses the boundary.** `FramesAhead()` (`ISession.h:127`) returns an `int`. GekkoNet computes a frame-advantage average in `f32`; the adapter rounds it with `std::lround` (a cast would truncate toward zero, so a client that is 0.6 frames behind would be told it is level). Positive means you are ahead and should slow down slightly. Ignoring it still works; you just drift into deeper rollbacks.
3. **Desyncs are reported, never corrected** ([DETERMINISM.md](../DETERMINISM.md) T6). `PollDesync` (`ISession.h:131`) fills a `DesyncReport` (`ISession.h:88`) with the frame, both checksums and the remote player. Once it returns true the match is over. In 2-player P2P there is no authority to resync from, and a silently corrected position is worse than a stop because the player cannot tell it from a lost interaction.

### The two factories, and what has not happened yet

| Factory | Header | What it is for |
|---|---|---|
| `CreateGekkoLocalSession` | `ISession.h:141` | An ordinary local session. |
| `CreateGekkoStressSession` | `ISession.h:146` | Rolls back **continuously** to hunt state divergence. This is how you test a simulation's determinism with no network at all. |

> **No transport has ever sent a packet.** Both factories add only `GekkoLocalPlayer` actors, and no network adapter is configured anywhere. `CreateGekkoStressSession` is the useful one today: `Session.SurvivesHundredsOfRealRollbacks` in `tests/test_session.cpp` drives hundreds of real rollbacks through it and compares byte-for-byte against a straight run, and `Session.LocalSessionMatchesTheKernelRunningAlone` asserts a local session reproduces the kernel running alone. The connect handshake, the remote actor and everything that follows are [ROADMAP.md](../ROADMAP.md) M2.

When that handshake is built it must hash the **loaded POD arrays** — the `MatchData`, not the canonicalized text — and a content mismatch has to surface as a lobby error, never as "desync at tick 3" ([DETERMINISM.md](../DETERMINISM.md) A4–A5). The reason it is the `MatchData` and not the state: a `GameState` alone no longer describes a match. It is meaningless without the data it was simulated against, and proving both peers hold the same one is the handshake's job.

---

## The game layer: `CseGame`

Between the kernel (which advances one tick) and a host (which owns a window)
sits `Games/UntitledFighter/Game/` — a **headless** library that links `CseKernel`
and `CseData` and nothing else. That whitelist is a configure-time error, and it
is what makes this layer's claims cheap to prove: every test over it runs with no
GL context, no window and no engine.

### `FightSession` — the thing that owns a match

`FightSession` (`Games/UntitledFighter/Game/include/cse/game/FightSession.h`)
holds the `GameState`, the `MatchData` it was built against, and the tick count.
It owns **no clock**: `Tick()` advances exactly one tick, and deciding how many
ticks to run is the caller's job, which is the whole of
[DETERMINISM.md](../DETERMINISM.md) T1.

- `Begin(const FightSetup&, std::string& error)` — validate and start. `ValidateSetup` is separately callable, so a host can reject a bad setup before it has committed to anything.
- `Tick()` pulls each player's input from its **input source**; `Tick(const InputPair&)` takes the inputs directly, which is what a network session does.
- `State()`, `Data()`, `CurrentTick()`, `Checksum()` for reading; `Snapshot(GameState&)` and `Restore(const GameState&)` for rolling back. `HighWaterTick()` is the furthest tick ever reached, which is how a rolled-back session knows it has been here before.
- `SetInputSource(int player, const IInputSource*)` — the seam that makes a human, a script and a replay interchangeable.
- `AddObserver(ITickObserver*)` — up to `kMaxTickObservers` (8). An observer sees every tick after it happens. The recorder, the verifier and the combo judge are all observers, which is why none of them can influence a tick.

### Input sources — "what did the player press on tick T" is a pure function of T

`InputSource.h` has three implementations and the difference between them is the
whole design:

| Source | What it is | Why it exists |
|---|---|---|
| `ScriptedInputSource` | a tick-indexed, immutable list of `InputSample` | a tool-assisted player. Frame-perfect timing is free, because nothing about it is live |
| `LatchedInputSource` | the local pad, **made pure by writing it down** — `Latch(tick, input)` records what was pressed, and every later read of that tick returns the same bytes | re-simulation must see the same input it saw the first time. A source that asked the pad again would answer differently |
| `FallbackInputSource` | a primary with a secondary behind it | how a scripted demonstration hands control back to the human when it runs out |

`kMaxMatchTicks` is one hour at 60 Hz, and a source that would run past it is
truncated rather than trusted.

### Replay — `CSRP`, and a verifier that refuses to be optimistic

`Replay.h` is a file format and three classes.

- `ReplayRecorder` is an observer. It records the input stream as runs, plus a **state checksum every `kDefaultCheckpointInterval` (60) ticks**, and the hash of the `MatchData` the match was built from (`HashMatchData`) so a replay cannot be played back against different frame data by accident.
- `ReplayInputSource` turns a recorded replay back into an input source. Playing a replay is therefore *the same code path* as playing the match, not a second implementation.
- `ReplayVerifier` is an observer that re-runs a replay and compares every checkpoint. It reports a `ReplayDivergence` naming the **first** tick that disagreed — not the last, and not "the end states differ", which is the failure mode a final-state hash has.

A replay that cannot be read is refused **by name**: `ReplayRefusal` distinguishes a wrong magic number from a wrong version from a content-hash mismatch, and `ReplayRefusalName` turns it into a sentence. A silently-empty replay is the bug this exists to prevent.

### `ComboWatcher` — the live judge

An observer that watches the state and reports what actually happened: which
cancel edges were *performed*, how long the combo ran, and whether the sequence
repeated. `ComboReport` carries the performed edges up to `kMaxComboSequence`
(256), and `Describe()` renders it for a HUD. Its arithmetic never reaches
`GameState` — which is exactly why the determinism gate holds this module to the
same integer rules as the kernel ([DETERMINISM.md](../DETERMINISM.md) §1): a float
here would leave the simulation bit-identical and make the *verdict* drift.

### `BuildDemonstration` — the tool-assisted attacker, headless

`BuildDemonstration(const DemonstrationRequest&, Demonstration&)` takes a move
sequence — typically the prover's own printed loop — and rehearses it against the
real kernel until it finds the frame-perfect input trace that performs it, or
reports that it cannot. It runs with no window and no host. This is the mechanism
the showcase catalogue is built on ([ROADMAP.md](../ROADMAP.md) M1.6): a verdict
becomes a replay anyone can watch, and the replay is verified bit-identical
before it is written.

---

## The modes: training, frame step, HUD

`Games/UntitledFighter/Modes/` is the title's presentation, and it is a
`MyCoreEngine::IGameMode` — so the **shipped Player and the editor's Game view
enter the same object**, which is the "Play == Player" property in one sentence.
`RegisterTitleGameModes` is the seam: the engine never names a title, the title
pushes itself in.

**Training mode** (`UntitledFighterMode`) loads a character, starts a
`FightSession`, binds the local pad through a `LatchedInputSource`, and draws the
box overlay (`FightView`) and the frame-data HUD (`FightHud`).

**Pause, slow motion and frame step cost about six lines between them**, and the
reason is the session's design rather than cleverness: the mode decides how many
times to call `Tick()`. Paused is "do not call it", slow motion is "call it every
Nth step", frame step is "call it exactly once". Nothing about time is stored
inside the simulation, so there is nothing to keep consistent.

**Demonstrate** runs the prover's verdict. The mode asks for the analysis, hands
the printed loop to `BuildDemonstration`, and swaps the attacker's input source
for the resulting script through a `FallbackInputSource` — so when the
demonstration ends, the pad takes over mid-match with no seam.

**Hot reload** is the authoring loop ([ADR-016](../adr/ADR-016-a-reload-restarts-the-match.md)):
the mode polls the loaded character file's (mtime, size) stamp every 0.25 s and
a change **restarts the match with the freshly built data** — health, position
and combo history do not survive, because after a frame-data edit they describe
a match that no longer exists. A broken save (the normal state while typing)
keeps the last good match running and puts the loader's own error on the HUD's
`hot reload:` line; the save that fixes it lands like any other edit. Pause and
slow motion survive a reload — the person saving the file is usually
frame-stepping the move they are editing. Two things to know when it seems not
to work: the watch reads the **staged** copy under the content root beside the
executable, so an edit to the source under `Games/UntitledFighter/Assets/`
lands when a build restages it (or copy it by hand); and `C` still swaps
characters while `R` still restarts without re-reading — hot reload replaces
neither. The property test is `tests/test_character_hotreload.cpp`; the mode's
poll is its mirror.

---

## What a verdict promises, and what it does not

**A `TERMINATING` verdict is a statement about the file.** The decision procedure reads the moves, cancels and resources of a `CharacterData` and decides a question about that graph. It is right about that graph by construction, and the [projection-loss table](#the-projection-loss-table-and-the-soundness-alarm) tells you where the *graph* might not describe your character.

Neither of those is the question a designer is actually asking. That question is **"is the game I am shipping free of infinites"**, and between the file and the game sits `BuildMatchData`. The kernel does not play the file; it plays the projection. **Wherever the file's termination argument uses a mechanism the projection drops, the verdict is about a character nobody is playing.**

| The build reports | In the running game | A `TERMINATING` verdict |
|---|---|---|
| `KernelOmits` | **less** is possible than the file describes | is **not** safe: if the file terminates *because of* the omitted mechanism, nothing in the game reproduces that |
| `KernelPermits` | **more** is possible than the file describes | is **not** safe: a chain the file forbids is available at a real controller |
| `Exact` | the two agree on this field | is unaffected by this field |

`BuildReport::losses` is that list per character with a count; `playsAsAnalysed` is the one-bit summary, and it is **false for every character the schema can currently express**. So the honest reading of any verdict on this page today is: *proved about the file, unproved about the game except where somebody has executed it.*

### The gap that has been measured rather than suspected: resources

> **The kernel simulates resources, and since M1.1f the bound binds.** Since ROADMAP M1.1b, `FighterData::resources` carries each declared slot's initial, floor and ceiling in file order; `Fighter::res` is primed on the match's first tick; `MoveDef::effect` is applied on contact and clamped; and both routes into a move refuse one whose `MoveDef::guard` minimum is unmet. `super_beam` is no longer startable on an empty bar. **And the juggle BUDGET is wired** (M1.1f): `MatchBuilder` sets `FighterData::juggleMax` from the resource the file calls `juggle` and each spending move's `MoveDef::juggleCost` from its authored delta, so the gate in `Games/UntitledFighter/Kernel/src/Combat.cpp` REFUSES the overspending hit — the one thing the clamped effect path never could. `Fighter::juggle` is the mirror of the authored slot: same numbers, gating where `ApplyEffects` only clamps. On `fighter_a` the wire moved no measured number — the arc and the budget both stop the aerial string at four — it aligned the reason, which is what a certificate needs.

That is not a prediction. It was executed on 2026-08-13, on this project's own character, and it is the second half of the ground-truth validation [ARCHITECTURE.md](../ARCHITECTURE.md)'s research section asks for (ADR-001 section 6.1 records both halves).

#### The worked example: `fighter_a`

**What the file says**, all of it checkable in `Games/UntitledFighter/Assets/Characters/fighter_a.json`:

| | |
|---|---|
| `air_mp` | 6 startup / 4 active / 12 recovery, 22 hitstun, `stance: air`, and `effect: { "juggle": -1 }` |
| its cancel into **itself** | `delay: 5`, `on: hit`, `certain: true` |
| so the edge is comfortably live on frames | advantage `22 − 5 = 17` against a startup of `6`. Frame data is **not** what stops it |
| `juggle` | `initial: 4`, `floor: 0` — and nothing anywhere in the file returns a point |
| therefore | the model permits that self-cancel exactly **4 times**, and `nonNegative` refuses the fifth |

The verdict is `TERMINATING` over 68 usable cancels of 73 (5 are printed dead), with a worst case of 21 hits, and it carries a **ranking certificate** — the first in this repository. The certificate is the whole termination argument: a resource strictly runs down.

It names both resources, in order, `meter, juggle`. Only juggle can actually run down inside a cycle: the other two spenders are `air_hk` (2 juggle) and `super_beam` (100 meter behind a `guard` of 100), and **neither has a single outgoing cancel**, so no cycle passes through either. Juggle is what ends them.

> `fighter_a`'s verdict is *already* qualified once before any of this: `soundnessAlarm` is true, raised by one `Conservative` loss — `projectile contact frame, outgoing ×1`. That alarm is about the **model** possibly being wrong about the file. What follows is a different axis entirely: the model being right about the file, and the **game** being a different character. Two qualifications, neither implying the other.

**What the game does with it.** `air_mp` → `air_mp` crosses into the kernel with a window it can match, and then:

| Mechanism | Where it went |
|---|---|
| `resources` ×2 | `Exact` since M1.1b — initial, floor and ceiling all carried, in file order |
| `move.effect` ×3 | `Exact` since M1.1b — the delta is applied on contact and clamped |
| `resource.juggle (gate)` ×6 | `Exact` since M1.1f — the budget refuses the overspending hit, the certificate's own mechanism executed |
| `move.guard` ×1 | `Exact` since M1.1b — the minimum is checked on both start routes, so `super_beam` is no longer startable on an empty bar |
| `move.stance` ×22 | `Exact` since M1.3e — mapped by name into `MoveDef::stance` and enforced by `StanceAllows` on both start routes. Selection reads the held **input** (is Down held now) and the posture then follows the started move, so a cross-posture gatling works and an air move needs the takeoff Up provides |
| `move.blocked_as` ×9 | `Exact` since M1.3e — a low goes through a standing block and an overhead through a crouching one, on the shipped file |
| `move.hitstop` ×22 | `Exact` since M1.3i — the authored freeze reaches `MoveDef::hitstop` and ResolveHits imposes it on BOTH fighters, so every clock stops together: no frame-data relationship moves, only wall-clock periods stretch (the air loop's hit period is now 23 = the 11-tick cancel plus 12 of freeze) |

`tests/test_ground_truth.cpp` then hands the kernel a trace derived from the prover's own witness — nothing hardcodes a button; the trace is built by walking `ProverResult`, so the claim is that *the engine* can read the verdict, not that a human can. Before M1.3e the kernel performed `air_mp` into itself 18 times in 200 ticks — a cancel the model permits four of. **Since M1.3e the loop must jump, and the arc ends every string at exactly the model's count: four hits per jump at a fixed period, then a landing on which the defender is genuinely free.** The count agrees, and since M1.1f the reason does too: the ballistic arc runs out of air at four, and the wired juggle budget — `FighterData::juggleMax` mirroring the resource the certificate ranks — would refuse the fifth aerial in the same breath. `GroundTruthGap.TheArcEndsEveryStringAtTheCountTheModelChargesToJuggle` is that sentence as a test. `BuildReport::playsAsAnalysed` is still `false` (priority, chip and scaling are still dropped, and the carried cancel conditions are honoured only in part), and the tests are what the flag costs.

The model agrees, once you ask it the right question. Hand the *same* character to `AnalyseCharacter` with every move and cancel `effect` and `guard` emptied — the resource declarations left in place, because the pool existing and nothing moving it is exactly the kernel's state — and the verdict is **`INFINITE COMBO`, with the loop `air_mp`**. That is the panel's third question, and it names the very loop whose infinite the movement rules then close on the stage.

The same test file also carries the *positive* result, which is why the pair is worth reading together: `fighter_a_infinite` carries **one deliberate bug** — `cancels[0]` is `stand_lp` cancelling into itself after 2 ticks — the prover calls it `INFINITE`, and its printed loop executes as written: 26 hits in 160 ticks, one every 6, first on tick 4, 30 damage a turn from 1000 down to 220, defender out of hitstun on **0** ticks, bit-identical on replay. Run the *same* derived trace against `fighter_a` and the defender gets out: 12 hits, one every 14, free on 22 ticks and starting a move on 11 of them. The analysis is validated end to end **and** the gap is a number, from one test file.

#### What this does not say

The claim is precise, and overstating it would cost the page its usefulness:

- **The prover is not wrong.** It is sound about the file it was given. What fails is the projection from that file to the running game — the *conservative* direction in `ProverAdapter.h`'s vocabulary, moved one layer down.
- **It does not mean every model cycle is performable in the game.** The kernel differs in both directions at once, and the model's loop is one question while the kernel performing it is another. What is settled is that the *bound* is gone.
- **A loop being inert in the kernel is not a safety property.** An edge whose authored delay outlives its source resolves to an empty window, so the kernel can never take it *as a cancel* — but the file's requirement was **contact**, and the ordinary button start permits the follow-up whether or not the source connected. `MatchBuilder` records that case as `cancels (link, not cancel)`, direction **`KernelPermits`**. What closed the gap was never that inertness: it was the genre's own movement rules landing one by one — the press edge (M1.1d) killed the hold-repeat chain, and M1.3e's stance wire plus commitment, the ballistic jump and posture-following-the-move killed the ground route. The execution gap went **97 → 77 → 0 of 121**: `GapExtentKernel.ZeroOfThe121RunForever` measures that every cycle must now take a real jump each turn and the landing hands the defender their turn. The standing account lives in [ROADMAP.md](../ROADMAP.md) § Where this stands; the attempt-by-attempt history lives in git, not here.
- **And the question is now asked of the game by searching the game.** `ComboSearch` ([ADR-013](../adr/ADR-013-verdicts-by-execution.md), `Games/UntitledFighter/Game/include/cse/game/ComboSearch.h`) performs macro-actions on the real kernel — each "ask for move M" pressed the way a player presses it — and returns INFINITE with a replayable witness, TERMINATING with the executed worst case, or UNRESOLVED when the budget ran out (a budget is never a verdict). On the shipped pair: `fighter_a_infinite` comes back **INFINITE** and the test replays the witness; `fighter_a` comes back **TERMINATING with an executed worst case of 7 hits** against the model's stated 21 — the sound half is loose by 14 on this character, and the bound held. `tests/test_combo_search.cpp` and `GapExtentSearch.TheExecutedWorstCaseIsInsideTheModels`.
- **Frame arithmetic survives the projection far better than resources do**, and for a structural reason: the kernel counts `hitstun` down every tick, so a follow-up that arrives after the defender is free meets a defender who can act. Nothing in the kernel counts a resource down. Termination that rests on frames is carried by the simulation; termination that rests on a resource is carried by nothing.

#### What to do about it today

1. **Read the block the Combo Prover panel draws under the verdict.** It computes this check for the character on screen — see [The resource warning under the verdict](#the-resource-warning-under-the-verdict).
2. **Read `lossesThatBite` and `playsAsAnalysed` on any build you make yourself**, and never let a UI you write imply that a verdict describes the running game while that flag is false.
3. **Prefer a termination argument the kernel can carry.** If the only thing standing between your character and a loop is a resource, you have a design that is proved safe and ships unsafe. Make the frame arithmetic do the work, or accept the loop knowingly and write it in the file's `notes`.
4. **Re-run the day resources land.** The panel's warning and the bridge's loss table are both computed rather than remembered, so they turn themselves off when the kernel grows the mechanism — nobody has to remember to delete this paragraph.

#### Where this is measured

| Test | What it establishes |
|---|---|
| `tests/test_ground_truth.cpp` | The printed loop executes; and the safe character's certified bound does not exist in the kernel. Both numbers above come from here |
| `tests/test_gap_extent.cpp` | How **wide** the gap is: how many of `fighter_a`'s cycles are ended by a resource the kernel does not implement, and how many of those the kernel will execute forever |
| `tests/test_one_frame.cpp` | How **close** an authoring mistake is to the edge of it: one integer moved on one cancel delay, and what the prover and the kernel each do across the sweep |
| `tests/test_match_bridge.cpp` | The loss table itself, counted out of a real file, row by row |

---

## The Combo Prover panel

**Window → Combo Prover** in the editor. It is **off by default** (`Editor/src/EditorApplication.h:341`): it is a fighting-game authoring tool, and an editor session that is not authoring a character should not have it in the way. The menu item is at `Editor/src/EditorApplication.cpp:1634`.

The panel owns the character it is inspecting — a path field and a Load button — rather than following the scene selection, because nothing in the ECS represents a character yet.

### The verdict answers the CORNER, whatever the file says

The banner at the top of the panel is not collapsible, and this is why:

**The corner and midscreen are two different questions, and the same character gets two different answers.** The in-engine decision procedure scopes itself to a defender pinned against the wall: no distance between the players, therefore no walking forward to stay in range. Phase 0 ran Kung Fu Man both ways and measured **TERMINATING midscreen and INFINITE in the corner** — both correct.

The three MUGEN fixtures declare `"stage": "midscreen"` in their files while the in-engine answer is the corner one (`fighter_a` declares `corner` and agrees), so `ProverResult` carries **both** the stage it answered (`stage`, `ProverAdapter.h:124`) and the stage the file claims (`fileStage`, `:125`). A panel that prints "the" verdict without saying which question it answered is showing a coin flip.

How to read the corner answer: it is the attacker's best case. **A combo that dies here dies everywhere, and one that loops here need not loop midscreen.** Away from the wall the verdict is an under-approximation. Midscreen is not available in-engine — that model lives in Python, and its pushback constant is *estimated* rather than derived (MUGEN records a velocity and a friction and never a displacement), so a 20% error in the estimate flips one of the three Phase-0 verdicts.

### The three verdicts

`ProverStatus` (`ProverAdapter.h:48`) has three members, and the panel gives them equal weight (`Games/UntitledFighter/Editor/src/ComboProverPanel.cpp`):

| Verdict | What it means | What to do |
|---|---|---|
| **INFINITE COMBO** | A loop exists in the corner. `result.prefix` then `result.loop` is the sequence, and `loop` ends on the move it returns to. Every move is a clickable button. | Break one link in the loop. |
| **TERMINATING** | No loop **in the file**. `maxHits`, `maxFrames` and `maxDamageHundredths` bound the worst case. | Read the two qualifications drawn with it: the soundness alarm, which changes the verdict's colour rather than adding small print, and [the resource warning](#the-resource-warning-under-the-verdict) directly beneath it. Absent both, nothing. |
| **UNRESOLVED** | The search hit `ProverOptions::limit` (`ProverAdapter.h:211`, default 200 000). | Raise the budget with the control the panel provides. An unfinished search dressed up as a clean bill of health is worse than no answer at all. |

**And since ADR-015 (accepted 2026-09-01, option 3), every analysis answers PER OPENING.** `ProverOpening` (`ProverAdapter.h`) names the three ways a string can start — **neutral** (the grounded hit every verdict above describes), **counter**, and **air** — and `ProverResult::openings` carries a complete result for each; the top-level fields stay the *neutral* answer verbatim, so nothing that reads the single-verdict surface changed meaning. Which numbers differ: **counter charges each move's authored `counter_hit` bonus** (M1.3(c), authored as `engine.reaction.counter_hit { hitstun_bonus, damage_bonus }` and simulated by `ResolveHits` when the defender is caught mid-startup — startup only, a trade is a trade; the *model* charges the bonus on every hit of that opening's search while the game grants it on the opening hit only, the Permissive direction, named in the opening's own loss row; a character authoring no bonus restates neutral exactly, and the tests pin that identity); **air substitutes each move's authored `air_hitstun_ticks`** — simulated since M1.3(d): `ResolveHits` charges the air number as the base stun against an airborne defender (falling back to ground where unauthored), a launcher (`engine.reaction.launch { vel_x_sub, vel_y_sub }`, +Y up, X pointed away from the attacker) is what puts one there, and a launched body keeps its arc through stun (`Fighter::reaction` marks it; an un-launched air hit still drops straight — recorded golden behaviour). The air opening's own loss rows name the charge rule: air for the whole string in the model, only while airborne in the game, split Permissive/Conservative by which number is larger — a move whose air stun is *shorter* than ground raises the alarm on that opening. `AnalyseCharacter` runs the model once per opening — three runs cost well under a millisecond.

Two caveats the panel surfaces for INFINITE:

- **`loopEntryKnown`** (`ProverAdapter.h:139`). When the loop is not entered on the very first move, the prover omits the opening move from both lists.
- **`loopHoldsUnderCeilings`** / **`ceilingReplayRan`** (`ProverAdapter.h:155-156`). The prover carries no resource ceilings, so it searches a state space in which meter grows past three bars forever. That over-approximates the attacker — the *safe* direction — but an INFINITE verdict may rest on meter the game would never have handed out. The adapter replays the reported loop through its own clamped loop; if the replay fails, the verdict stands (you cannot conclude TERMINATING from a failed replay) but the loop was not reproducible under the character's declared ceilings.

For reference, the corner verdicts: `fighter_a` TERMINATING **with a ranking certificate over `meter, juggle`** and the soundness alarm up, `fighter_a_infinite` INFINITE (the loop is `stand_lp` into itself) — `tests/test_ground_truth.cpp`. On the three MUGEN fixtures: `kung_fu_girl` INFINITE (again `stand_lp` into itself), `kung_fu_man` INFINITE, `aof2_strength_training` TERMINATING with a worst case of exactly one hit — `tests/test_prover_adapter.cpp:230`, `:245`, `:265`.

### The part designers use daily

Drawn directly under the verdict with no collapsing header in front of it (`ComboProverPanel.cpp:967`), because these land before anyone cares about the theorem.

**Dead cancels** (`ProverAdapter.h:174`). A cancel you authored that can never connect: by the time the follow-up becomes dangerous, the defender is already free. Each row reads `from -> to  leaves N, needs M, short by K`, where `shortfall() == startup - advantage` (`ProverAdapter.h:115`). That number tells you exactly how many frames you need to find.

> **Important — the panel shows the PRE-DECAY list by default, and you need to know why.** Both implementations evaluate every edge at the **settled** hitstun, so a decay rule reports real cancels as dead. Under this project's own draft house rule, **128 of Kung Fu Girl's 134 cancels** collapsed to dead. `deadCancelsPreDecay` (`ProverAdapter.h:180`) is the list that blames you only for what you actually authored; `deadCancels` is the model's own. The checkbox switches between them (`ComboProverPanel.h:388`). If the two lists differ wildly, that is a statement about your decay curve, not about your character.

**Unreachable moves** (`ProverAdapter.h:181`). No combo can produce these.

> **Gotcha — an empty unreachable list means nothing unless the file declares starters.** The prover reads an empty `starters` list as "any move may open a combo", under which almost nothing can be unreachable. **`fighter_a` declares none** — so on the panel's own default character the list is empty for a reason that says nothing about the character, and the panel prints a warning rather than a green "none" (`ComboProverPanel.cpp:1011`). The other files do declare them (`fighter_a_infinite` 17, `kung_fu_girl` 21, `kung_fu_man` 23, `aof2_strength_training` 10), and there the answer means something. Author starters.

**Usable cancels** and the **settling index** sit above both lists. Every cancel is judged at the hitstun the decay curve has settled to by that hit, which is how the prover judges them too — so the two lists cannot drift apart.

### The missing certificate means four different things

`RankingAbsence` (`ProverAdapter.h:80`) exists because "no certificate" is bad news in one case and the best possible news in another:

| Value | Meaning |
|---|---|
| `Present` | `rankingOrder` is populated. |
| `NoResources` | The character declares no resources at all. |
| `CharacterGainsAResource` | Some reachable cancel **adds** a resource, clearing `spendOnly`. **This is the common case** — any character that builds meter on hit lands here. Do not build tooling around the certificate. |
| `NothingLoops` | No usable cancel between two reachable moves. **The strongest termination result the tool can produce**: nothing can loop at all. |
| `NoDescendingOrder` | Spend-only, edges exist, but no resource strictly runs down across all of them. |

`tests/test_prover_adapter.cpp:323` pins the distinction: `aof2_strength_training` is `NothingLoops` with 0 usable cancels, `kung_fu_man` is `CharacterGainsAResource` because `stand_lp` carries `effect.meter = +5`.

### The resource warning under the verdict

Drawn **inside** the verdict block, under the certificate, with nothing collapsible between them — because a green TERMINATING carrying a certificate is the most trustworthy-looking thing the page can print and today it is the least trustworthy thing the page can print, and those two sentences have to arrive together. It appears only for TERMINATING: an INFINITE verdict is not made worse by an engine more permissive than the model, and UNRESOLVED never proved a bound for a resource to be holding up.

It is **not** the soundness alarm, and the difference is worth holding on to. The alarm is about the *model* being wrong about the file (a `Conservative` projection loss). This is about the model being right about the file and the *game* being a different character. Both can qualify the same verdict, and neither implies the other.

Three questions, each answered from a different thing the panel can honestly reach — none of them from a memory of what the kernel implements:

| Question | Source |
|---|---|
| **What does this verdict rest on?** | `ProverResult::rankingOrder` — the certificate names resource indices, so the panel spells them with the character's own names and lists the moves that spend them as clickable buttons. With no certificate it falls back to the weaker true statement: *these resources were in play while the search decided, and how much of the bound they were carrying is not something this panel can tell you.* |
| **Does the running game carry it?** | `BuildFighterData`'s loss table, read for five entries by name: `resources`, `move.effect`, `move.guard`, `cancel.effect`, `cancel.guard`. That is `MatchBuilder`'s own account of its own projection, so **the day the kernel grows resources the warning turns itself off** rather than waiting for somebody to delete it. |
| **Does it reach *this* character?** | `AnalyseCharacter`, run a **second** time over a copy of the character with every move and cancel `effect` and `guard` emptied and the resource declarations left alone — which is exactly "the pool exists and nothing moves it", the kernel's actual state. Still TERMINATING and the panel says so quietly. INFINITE and it prints the loop the certificate was paying for. |

What it says instead of guessing:

- The second run is **the same decision procedure asked a second question, not a simulation of the kernel.** Whether the game can perform that particular loop is a separate question, and the game differs in both directions at once.
- The loss table counts **objects, not resources**, so the panel cannot claim "juggle specifically did not cross" — only that no resource delta crosses and juggle is one of them. It says the weaker sentence.
- If the bridge **refuses** the character (over 31 moves), that is reported in its own right: a verdict about a character that can never reach a tick is worth less than the refusal.
- If **none** of the five loss entries is found by name, the panel says the check is *broken*, not clear. Reading a renamed field as "nothing was dropped" would put a green light on the most dangerous claim on the page.

The cost is a second `AnalyseCharacter` plus a bridge build, and it runs only when the fingerprint moves. The footer reports it **beside** the run figures rather than inside them (`+ 0.0xx ms resource check`), because those four numbers are the `analyse` latency distribution [ARCHITECTURE.md](../ARCHITECTURE.md)'s research section asks the paper to harvest and have to stay comparable with every other measurement of that call.

### The projection-loss table and the soundness alarm

The panel's one collapsible section (`ComboProverPanel.cpp:1025`) is about the **tool** rather than about the character. Each `ProjectionLoss` (`ProverAdapter.h:100`) carries a direction (`ProverAdapter.h:70`):

| Direction | What it costs you |
|---|---|
| `Permissive` | The model can say INFINITE where the game is safe. You investigate a combo that turns out not to exist, are annoyed, and ship a correct character. **Safe.** |
| `Conservative` | The model can say TERMINATING where the game has a real infinite. You are told the character is fine and it is not. **This is a soundness bug.** |
| `Exact` | The projection loses nothing this question can observe. |

The asymmetry is not a preference: a tool whose failure mode is "you have work you did not know about" costs an afternoon; one whose failure mode is "you are finished" costs the shipped game.

Any `Conservative` loss with a nonzero count raises **`soundnessAlarm`** (`ProverAdapter.h:195`), which is drawn *outside* the collapsing header so that collapsing the section cannot hide it, and which changes the colour of a TERMINATING verdict rather than qualifying it in small print. On all three Phase-0 characters it is false (`tests/test_prover_adapter.cpp:370`).

Losses with count 0 are hidden behind a checkbox but kept, because a check that ran and found nothing is not the same as a check that does not exist.

### It re-runs on data changes, not on frames

The panel takes no change notification from the editor and does not want one. Every draw it folds the character into a 64-bit fingerprint — one pass over moves, cancels, resources and gap actions, integer mixing, no allocation — and re-runs only when the fingerprint moves (`ComboProverPanel.cpp:302`). The options are folded in too, which is what makes "raise the budget" work through the ordinary path with no second code path to rot.

The road not taken is a dirty flag set by whoever edits the character. It is cheaper per frame and it is wrong the first time somebody adds a second edit path and forgets to set it — and the failure is *silent*, a stale verdict that looks live.

The footer (`ComboProverPanel.cpp:1078`) shows run count, last / worst / mean milliseconds, and a **Copy verdict** button that produces `DescribeVerdict` text (`ProverAdapter.h:253`) — the same text the tests assert on, so a bug report and a test can never describe the character differently.

**And every real run is recorded** ([ADR-017](../adr/ADR-017-one-line-per-prover-run.md)): one JSON line — wall time, the file read, character, nonce-free content hash, changed-since-last (`false` for a Re-run on unchanged bytes), move/cancel counts, resource ranges, `explored`, run ms with the resource-check ms as its own field, verdict — appended to `telemetry/prover_runs.jsonl` beside the content root, through the sandboxed writer/reader pair in `cse::data::AuthoringTelemetry.h`. The footer says `recorded:` or shows the writer's error; a failed append never blocks the analysis. The reader skips-and-counts torn lines, and `tests/test_prover_telemetry.cpp` pins the round trip.

### Two wiring caveats

The editor calls `comboProver_.Draw(nullptr, &panels_.comboProver)` (`Editor/src/EditorApplication.cpp:281`) and does not call `SetContentRoot` or `SetExpectedResources`. Two consequences today:

- **The default content root is `"Exported"`** (`ComboProverPanel.h:269`), resolved relative to the process working directory, and the default path is `Characters/fighter_a.json` (`:282`) — this project's own character, chosen because a default path is a promise that something is there and the MUGEN corpus stopped being staged. The characters live in `Games/UntitledFighter/Assets/Characters`, which the asset staging copies next to the executable as `Exported/Characters`, so the default finds them with no configuration. A `..` in the path field is refused lexically before anything opens a file, so you cannot climb out. A wrong root is visible rather than silent — the panel prints the loader's own error, which names the file.
- **A03 is skipped**, not passed. With no expected resource order supplied, the loader records a warning and the panel shows it in the same colour as everything else that could make the verdict meaningless. Call `SetExpectedResources({"meter", "juggle"})` (`ComboProverPanel.h:211`) if you wire this up yourself.

### Running the analysis outside the editor

The panel is a view. `AnalyseCharacter` (`ProverAdapter.h:240`) is the API, and `comboprover.hpp` appears nowhere in its header — not in a member, not in a signature, not in a forward declaration:

```c++
#include "cse/data/ProverAdapter.h"

ProverOptions options;                              // ProverAdapter.h:198
options.expectedResources = { "meter", "juggle" };
// options.limit                 = 200000
// options.ceilingReplayRounds   = 64
// options.ceilingReplayFrontier = 64

ProverResult result;
ProverReport report;
if (!AnalyseCharacter(character, options, result, report)) {
    // The character could not be PROJECTED at all — an empty move list, a
    // resource index that does not exist, a resource order contradicting the
    // build. report.rule is "A03" when a load assertion is what refused it.
    return;
}
// A projected character the search could not decide returns TRUE with
// status == ProverStatus::Unknown: "I ran and could not tell you" is an
// answer; "I could not run" is not.
```

`ProverStage` (`ProverAdapter.h:53`) has exactly one member, `Corner`, and that is deliberate — the day the midscreen model is ported, every `switch` over it stops compiling until it is handled. That is also the moment to add an async worker, and not before: corner runs measure 0.033–0.041 ms, and an async path with nothing slow behind it is a race condition with no benefit.

---

## Where to read more

| Document | What it decides |
|---|---|
| [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) | D1–D9, the determinism contract, the phased build order, and the table of rejected ideas with the condition under which each comes back |
| [`docs/adr/ADR-001-fighting-core.md`](../adr/ADR-001-fighting-core.md) | Phase 0's measured result: three transcribed characters, the nine missing schema fields, and the two instructions ARCHITECTURE.md originally gave that would have fabricated an infinite combo |
| [`docs/adr/ADR-002-open-decisions.md`](../adr/ADR-002-open-decisions.md) | CHOICES A–D: adopt GekkoNet, data-only first, abort-and-name-the-frame on desync, and make the D2 boundary a link error |
| [`docs/adr/ADR-003-gekkonet-spike.md`](../adr/ADR-003-gekkonet-spike.md) | The building spike, all three gates, and the event-pump shape the plan had missed |
| [`docs/adr/ADR-004-choronos-considered.md`](../adr/ADR-004-choronos-considered.md) | The alternative weighed against GekkoNet, and not taken |
| [`docs/NORTHSTAR.md`](../NORTHSTAR.md) | What the game is for and what crossplay has to mean |
| [`docs/MAINTENANCE.md`](../MAINTENANCE.md) | How this repository audits its own documentation for drift — including the drift a page of stale line numbers is |
