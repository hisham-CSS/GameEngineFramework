# Architecture

Verified: 2026-08-17 @ 9b7d26c

Why the engine is shaped the way it is: nine decisions, the things deliberately
not built, and where the combo-termination research plugs in. Each decision says
what it trades away and what would reverse it — a decision with no reversal
condition is a preference.

This document is the decisions' **current shape**. The record of when and why
each was made is its ADR ([index](adr/README.md)); the rules those decisions
impose on the simulation, the build and the data are
[DETERMINISM.md](DETERMINISM.md); what is done, in flight and next is
[ROADMAP.md](ROADMAP.md). The original of this document, before Phase 0 amended
five of its sections, is [`docs/archive/ARCHITECTURE-2026-08-12.md`](archive/README.md).

## 0. In one paragraph

The authoritative fighting-game simulation is a **fixed-size POD struct of
integers** advanced by a **tick-driven integer kernel** that includes no float,
no Jolt, no EnTT, no Lua and no libm — a rule the build enforces at configure
time rather than a rule a document asks for. It is snapshotted by `memcpy`,
rolled back by `memcpy` back, and re-simulated. Cross-platform bit-identity is a
property of integer arithmetic, not of a build flag, which is why no Jolt overlay
port, no NEON patch and no ARM CI leg is on the critical path. Character
behaviour is authored data whose schema is a superset of `comboprover::Character`
(`ThirdParty/comboprover/comboprover.hpp`), so the published prover reads the
shipping game through a small adapter rather than a heuristic importer. EnTT
stays as the presentation registry; Jolt stays for cosmetics, props and the
framework's non-fighting games.

---

# 1. The decisions

## D1 — The simulation / presentation split

**Decision.** Authoritative simulation state lives in one `struct GameState`
outside the EnTT registry: POD, trivially copyable, fixed-capacity dense arrays,
no pointers, no `std::string`, no `std::vector`, no virtuals. Everything in the
registry — every renderer, camera, audio and UI object, and
`Transform::modelMatrix` — is presentation, derived from `GameState` each
rendered frame and never read back.

**Why.** Not performance — **correctness**. The registry structurally cannot be
snapshotted: `Name`, `ModelComponent` and `MaterialOverrides` are not trivially
copyable; `AABB` derives from the polymorphic `BoundingVolume` and therefore
carries a **vtable pointer**, a process address that must never enter a snapshot
or a wire; EnTT's packed order is permuted by `swap_and_pop` on every component
removal, so any loop over a view is order-unstable across a restore; and EnTT
recycles indices with bumped versions, which is why the editor hierarchy jumps
from `1..401` to `1048577..`.

The performance framing is explicitly **rejected**, including the version of it
this document reached for first. Full-registry snapshot is around 0.003% of a
frame and `entt::snapshot` re-measures at 12–40 µs rather than 30–80 µs. State
the correctness case alone; it is the one that holds.

**What it trades away.** The Inspector, the scene serializer and undo/redo all
work on EnTT components for free. `GameState` gets none of that until it has an
authoring path, and two notions of "an entity" now exist, joined by a
reconciler. The mitigation that inverts this cost is a reflection table over
`GameState` (`{name, offset, type, count}`), which yields the Inspector view, the
JSON serializer **and** the per-field desync log from one artifact — cheaper than
the desync log alone. It is ROADMAP E6.

**Revisit if.** A gameplay feature genuinely requires emergent rigid-body
interaction inside combat — throwable physics objects whose contacts decide hits.
Then D2 changes, and this follows.

## D2 — What the gameplay core is built out of

**Decision.** The authoritative core is **plain `int32` in fixed sub-units (1
pixel = 256 sub-units)**, with **no general fixed-point type and no general
fixed-point multiply**. Positions, velocities and hitboxes are integers. Every
genuine scaling operation goes through one helper with one documented rounding
rule — round half away from zero, so a mirrored character loses the same least
significant bit moving left as moving right. Jolt is not in the authoritative
path.

**Why.** Three independent arguments, in descending strength.

1. **Bit-identity is a property of the language, not of a vendor.** Integer add,
   shift, compare and AABB overlap are exact on x86-64, AArch64, MSVC, GCC and
   Clang. No flag, no patch and no CI matrix is needed to *believe* it. Contrast
   the float path: `JPH_CROSS_PLATFORM_DETERMINISTIC` disables FMA on x86 only,
   while around twenty `vmlaq_f32` sites in Jolt's `Mat44.inl`, `DMat44.inl`,
   `Vec3.inl` and `Vec4.inl` are unguarded — and `Mat44::operator*` is on the
   path of every body transform. The flag as shipped is **not sufficient for
   x86 ↔ ARM**.
2. **The decision procedure needs the discrete automaton to be the game.** The
   prover decides over `(move index, frame in move, integer resource vector, hits
   so far)`. A continuous rigid-body simulation is not analysable by it, and
   contribution #9 degrades from "the analysis decides this game" to "the
   analysis decides a model of this game" the moment the authoritative layer is
   continuous.
3. **The re-simulation budget stops being a measurement.** An integer kernel is
   tens of microseconds per tick; nobody has ever measured a Jolt step in this
   engine.

**One argument for this decision is false, and it is worth saying which.** "Every
combat-relevant quantity is already an integer" does not survive reading the
prover's header: `Move::damage` is `float`, `Character::scaling` is
`std::vector<float>`, `Decay::ratio` and `Decay::table` are float, and
multiplicative decay does repeated float multiplication then `static_cast<int>`.
The correct claim is narrower and still sufficient: *every quantity the
simulation integrates over time is an integer, and the handful that are not are
single multiplies at authoring-time boundaries, quantized once at load.* D8 is
the consequence.

**Q16.16 versus plain sub-units.** The simpler option wins. A 2D fighter almost
never multiplies two positions; it adds velocity to position and compares
rectangles. A general `Fixed` type buys an overflow surface, a
division-versus-shift rounding mismatch (`>>` rounds toward −∞ while `/`
truncates toward zero, which makes the simulation **not mirror-symmetric** and
accumulates), and a C++17 implementation-defined arithmetic-right-shift
question. Delete the type; keep one helper.

**What it trades away.** We own a simulation Jolt would have given us. Jolt's
decade of edge cases stops helping the moment gameplay wants a slope or a
wall-bounce with non-axis-aligned normals. Two collision systems live in the tree
with a standing temptation to wire a gameplay object into the wrong one. And the
two features that motivated the original dependency choices are retired *for this
game*: `PhysicsSystem::SaveState` and `entt/entity/snapshot.hpp` are both unused
by the rollback loop. Say that out loud rather than softening it — Jolt and EnTT
are not being replaced, but neither is doing the job it was chosen for here. Both
remain load-bearing for the framework's other games and for late-join (D5).

**Revisit if.** A shipped title needs solver-quality contact response inside
combat, or the character controller starts accreting a general collision solver
of its own. The second is the real failure mode; watch for it.

## D3 — Cross-platform determinism mechanics

**Decision.** Determinism rests on three mechanisms, in descending order of
load-bearing-ness: **the integer kernel** (D2), which does most of the work; **a
flag gate in CI** over both the build configuration we author and the generated
compile lines, including anything a dependency exports as an INTERFACE option;
and **a cross-toolchain hash test**, which is the only thing that converts either
of the others from an assertion into a fact. `JPH_CROSS_PLATFORM_DETERMINISTIC`
is **not** flipped on the critical path.

**Why the flag gate is second and not sixth.** It is the highest
value-per-hour item in the whole investigation, and it caught two things no
amount of reading `CMakeLists.txt` could reveal. The linked Jolt was compiled
with `/fp:fast` on all 139 of its translation units — appended at directory scope
and never written back to the cache, so `CMakeCache.txt` showed nothing. And
Windows Jolt and Linux Jolt are *different libraries*: Jolt's x86 block is gated
on `CMAKE_VS_PLATFORM_NAME`, which is empty under vcpkg's Ninja generator, so
Windows gets an SSE2 baseline and no `JPH_USE_*` defines while Linux takes the
`CMAKE_SYSTEM_PROCESSOR` branch and gets `-mavx2 -mfma`. One manifest line, two
numerically different libraries, and `JPH_VERSION_ID` cannot tell them apart.

**A fourth mechanism was specified and does not exist.** The original text asked
for `cse_fp_strict`, an INTERFACE target linked by every simulation-adjacent
target. It was never built and it *cannot* be: linking anything at all — even an
INTERFACE target carrying only compile options — trips the kernel's own
configure-time guard. The guard is the stronger rule and it is the one that
shipped; [DETERMINISM.md](DETERMINISM.md) B5 records what is true instead.

**No Jolt overlay port, no NEON patch, no ISA pin.** Under D2, Jolt computes hit
sparks, debris, ragdolls and level props — things that by construction cannot
desync a match. The proposal was to ship a locally patched physics engine, eat an
estimated 1.5–2.5× on physics throughput, and maintain a NEON macro patch across
every upstream upgrade for years, **to make hit sparks bit-identical across
architectures**. That is a bad trade. Same-binary determinism, which
`mDeterministicSimulation` plus zero worker threads already gives, is sufficient
for cosmetics that are re-derived from confirmed state anyway. The overlay port
remains worth about one engineer-day later as *framework* work, gated on a
measured `PhysicsSystem::Update` cost and on the existing `PhysicsConformance`
suite passing.

**What it trades away.** Ever running the fighters through Jolt without redoing
this analysis, and cross-platform determinism for the framework's *other* games.

**Revisit if.** D2 is revisited, or a second title needs networked rigid-body
physics. Then the overlay port, the NEON patch and the ARM CI legs all come back,
and the flag gate must be extended to assert `/arch:` parity — which
[DETERMINISM.md](DETERMINISM.md) B3 already wants for a different reason.

## D4 — The rollback snapshot design

**Decision.** The snapshot unit is the whole of `GameState`, one `memcpy`, small
enough to save and restore in well under a microsecond and to checksum every
tick. Re-simulation of eight ticks is a low single-digit percentage of a 16.67 ms
frame. On the wire a tick costs a few bytes of input, sent with the last several
inputs redundantly, plus a periodic checksum.

**Ring exhaustion is defined behaviour: STALL, never drop.** If a correction
targets a tick older than the ring, or the local simulation is further ahead than
the prediction window allows, the session runs **zero ticks this frame**. Running
zero ticks is a legitimate hitch; dropping a tick is an unrecoverable desync.
This is the single most common way a first rollback implementation fails in the
field, and it was absent from every proposal that reached this document.

**Adjudications that still stand.**

- **`entt::snapshot` is rejected for the rollback loop** and retained for the
  late-join and spectator handshake, where `continuous_loader`'s entity remapping
  is exactly right. It is a save-file API, not a 60 Hz API.
- **`PhysicsWorld::Rebuild` is not a restore and cannot be made into one.** It is
  `Clear()` plus `Build()`, so velocity, sleep state, warm-start impulses,
  contact manifolds and island assignment are all discarded, and it frees bodies
  in hash order so the next build assigns different ids on a different standard
  library. The desync once measured "through Jolt" measured `Rebuild`, which
  could not possibly have round-tripped, and therefore says nothing about Jolt's
  own `SaveState` path.
- **Jolt is not snapshotted at all.** If it ever is: `BodyManager::RestoreState`
  iterates the *stream*, not the world, so a body created after the snapshot
  survives the restore as a live ghost, and pooled bodies must be deactivated
  rather than removed, because removal takes them out of the broad phase and
  therefore out of the save filter.
- **Rollback side effects are suppressed by a phase parameter, never a global.**
  Audio, VFX, camera shake, contact events and logging are appended to a per-tick
  event buffer *inside* `GameState`, and presentation drains only confirmed
  ticks, de-duplicated by `(tick, slot, kind)`. Otherwise a seven-frame rollback
  plays the hit sound eight times. Free if designed in from line one; an audit of
  every call site if retrofitted. It is [DETERMINISM.md](DETERMINISM.md) K11 and
  ROADMAP M3.1.

**Two parts of the original sizing are not what shipped, deliberately.** The
snapshot ring and the input ring are GekkoNet's, not ours — writing our own was
rejected outright once GekkoNet was adopted (D9 A, [ADR-010](adr/ADR-010-one-roadmap-one-rule.md)
§3.4). And the projectile pool does not exist, because no shipped character has a
projectile; the fixed 32-slot design stands for the day one does. What the state
does carry is eight fighter slots, and that number is measured rather than
chosen: `Fighter::alreadyHitBits` is eight bits, so eight is the capacity the
multi-hit guard already had ([ADR-009](adr/ADR-009-how-many-fighters.md)).

**What it trades away.** Fixed capacity everywhere. An unbounded spawn list would
break the fixed-size-snapshot property, so it is forbidden rather than managed.

**Revisit if.** The snapshot exceeds roughly 64 KB — then reconsider delta
encoding.

## D5 — Entity identity

**Decision.** Identity inside the simulation is a **slot index into a dense
array**, allocated by linear scan for the lowest free slot, with everything the
allocation depends on living inside `GameState` so that it rolls back.
`entt::entity` never crosses into `Simulate`.

**Why.** The spawn-identity mechanism must be *inside the snapshot* or it cannot
replay: a rollback to tick N−3 restores the occupancy state to its N−3 value, so
re-simulation reissues exactly the same ids. Do not hash the spawn site; do not
use a process-global counter.

**The generation counter is designed and not built.** The original design packed
a 16-bit slot index with a 16-bit generation into a `SimId`. With fixed slots and
no runtime spawning, the slot index *is* the id, and the generation half buys
nothing; it comes back the day something spawns at runtime
([ADR-010](adr/ADR-010-one-roadmap-one-rule.md) §3.4).

**Presentation ordering must not leak EnTT handles.** Stripping the version bits
off a handle is not the fix people reach for it as: under D1 the reconciler
creates and destroys presentation entities per frame, and `registry.create()`
recycles indices from a free list whose history differs between a peer that
rolled back and one that did not. Ties break on the **entity index** where the
set of entities is stable, and on the **sim slot** where it is not — never on the
raw handle, whose version bits reset on load.
[DETERMINISM.md](DETERMINISM.md) I3 tracks the one site still doing it.

**What it trades away.** A hard cap on simultaneous simulation entities.

**Revisit if.** The cap becomes limiting. Widen the slot field; never remove the
cap.

## D6 — The input path

**Decision.** `Simulate` takes input **as a value parameter** and never queries
`InputMap`. Input is a small bitfield of digital buttons and directions,
quantized at the sampling boundary — before anything the network or the
simulation sees — so a float deadzone and per-pad variance stop being cross-peer
concerns. A fighting game has no analog input.

The input record for a tick is immutable once written, and re-simulation reads
the same bytes. Prediction repeats the last confirmed input; on arrival, a
mismatch makes that tick the rollback target.

**The largest omission the investigation found, and the fix.** `InputMap`
contains a **phase latch**: `consumePressed` serves a press exactly once per
phase, the phase begins once per rendered frame, unconsumed latches are dropped,
and the *first* caller to ask sees `true` while every later one sees `false`.
That is hidden, order-dependent, consuming mutable state living **outside the
snapshot, in the subsystem rollback depends on most.** Re-simulating a tick would
either double-consume a press or see nothing.

The latch is not sloppiness and "stop using it" is not the fix: `wasPressed` is
genuinely scoped to a *rendered frame*, while the fixed tick may run zero or
several times per frame. The structural fix is three parts, and all three are
built (ROADMAP M1.3h). The session clock decides how many ticks run, and each
tick consumes exactly one immutable input record sampled when the tick was
scheduled. Between ticks the producer OR-accumulates a sticky "pressed since
last tick" mask — `PressAccumulator` in `cse/game/InputSource.h` — so a tap
during a stall is not lost; that mask is producer-side, local-only, spent into
the input record *before* it is latched, never re-read during re-simulation,
and therefore never needs snapshotting. `InputMap` keeps its latch machinery
unchanged for the editor and menus, where it is correct, and is a *producer*
into the input stream and nothing more.

Two couplings go with it: while a session is live the producer ignores UI capture
entirely, and pausing becomes a session protocol message rather than a local
`paused_` or `timeScale_`, because a local pause changes how many ticks run and
the peer will not agree. Both are [DETERMINISM.md](DETERMINISM.md) N4–N5 and
T1–T3, and ROADMAP M2.4.

**What it trades away.** No analog movement, ever. No mouse in the authoritative
simulation.

**Revisit if.** A framework title needs analog gameplay input. Then quantize to a
fixed integer grid at the boundary and keep everything else.

## D7 — The behaviour-authoring language

**Decision.** Behaviour is **authored data**: a JSON character format whose schema
is a strict superset of `comboprover::Character`. **Lua is not on the simulation
path.** The escape hatch is named C++ effects registered by the engine and
invoked from data as opaque rows. A trigger expression language was part of this
decision and has since been dropped in favour of typed schema nouns
([ADR-006](adr/ADR-006-stance-and-guard.md), and §2 below).

**Why Lua goes.** Four determinism failures in the vendored 5.4.7, one of which
has no fix. The string hash seed is randomised per process from `time(NULL)` and
three ASLR'd addresses — mitigable, but only through an overlay port. The whole
`math` library routes to libm, and `^` is `pow`. `math.random` auto-seeds from
the clock and a pointer. And, decisively, **non-string table keys hash by
address, and this cannot be fixed**: userdata, tables, closures and C functions
all hash their pointer, so the most natural line a modder can write —
`for e, box in pairs(activeHitboxes)` — enumerates in heap-address order. Windows
and Linux will never agree, there is no compile error, no runtime error, no way
for the host to detect it, and every mod is a fresh chance to reintroduce it.

Reject Lua on that failure, which is unenforceable, rather than on cost, which is
arguable. The snapshot-cost argument against it is wrong twice over and does not
matter. And the sunk cost of removing it was approximately zero: the entire Lua
content footprint of the repository was two example scripts.

**"The prover reads the shipping game with no importer at all" is false.** The
prover consumes exactly two guard and effect forms — a componentwise minimum on
an integer vector, and componentwise addition. A trigger condition like
`var(1) == 1` or `stateno != [3050,3100)`, both taken from real MUGEN characters,
is expressible as neither. The abstraction step is real and lossy, which is
precisely why three-valued evaluation exists in the reference implementation. The
honest claim is stronger anyway: *the importer becomes a small total function
over our own schema, whose every lossy step is documented with a soundness
argument, instead of a 1123-line heuristic over a foreign format that its own
docstring admits cannot evaluate helpers, opponent state or randomness.*

**What the script host does not have**, and why the schedule is months rather
than weeks: log, name, transform get and set, impulse, linear velocity, raycast,
three input queries and a clock. That is all of it. There is no animation system
with per-frame events, no hitbox authoring or storage, no hit detection with
priority and trade resolution, no hitstop, no pushback, no juggle or proration
bookkeeping and no state-machine runtime. The DSL was never the expensive part;
the combat systems it would drive did not exist.

**What survives from the existing seam.** `ScriptWorld`'s lifecycle, registry,
path containment and report-once error policy are worth keeping verbatim.
`IScriptBackend`'s five-hook, `dt`-carrying interface is not — a fighting-game
state machine wants a step indexed by frame with no `dt` at all. The seam makes
the swap **supported**, not **free**.

**What it trades away, deliberately and permanently.** No loops, no recursion, no
allocation, no strings, no closures, no unbounded collections in authored
content. Some behaviour that would be five lines of Lua becomes a C++ effect plus
a schema field. Every one of those constructs is a way for a mod to desync a
match, and their absence is what makes the prover's answer a decision rather than
a guess.

**Revisit if.** More than roughly 20% of moves in a real character need a C++
escape hatch. That means either the vocabulary is wrong (fixable) or the approach
is (not). Phase 0 measured 1.7%.

## D8 — Where the floats live, and the quantization rule

**Decision.** The authored schema stores integers. The three float quantities in
the research model — damage, damage scaling, and hitstun decay ratios and tables
— are quantized **at load, on both peers, by one specified rule**, into hundredths
and thousandths respectively.

**Multiplicative decay is forbidden in the schema.** The original plan was to
mitigate it: implement decay with the same rounding helper the kernel uses, and
assert that engine-computed and prover-computed hitstun agree. Phase 0 closed the
gap outright instead. MUGEN 1.0 has **no global hitstun decay** — every hit states
an absolute hit time and nothing reduces it as a combo runs — so `decay.kind:
"none"` is the truthful transcription, and like `"linear"` it is pure integer
arithmetic in both implementations. There is then no float multiplication for the
two to disagree about, and multiplicative decay is refused at load rather than
worked around.

**`decay.floor` must never exceed the smallest hitstun in the file, and this is
asserted at load.** Both implementations compute `max(floor, base − step·n)`, so
a floor *above* a move's base hitstun **raises** it. That is not hypothetical:
the first draft house rule (linear, step 2, floor 10) exceeded `stand_lp`'s
authored hit time of 9 and **fabricated an infinite combo** in this project's own
first draft. It was found by measurement, not review.

**Authoring a decay rule you did not derive from source is the most dangerous
thing this schema permits.** Also measured: a decay rule invented for a character
that has none *deletes the character*. Both implementations evaluate cancel edges
at the settled hitstun, so a two-frames-per-hit rule collapsed 128 of 134 real
cancels to dead.

**Why any of this matters.** For a decision procedure whose output hinges on
whether `hitstun >= startup - advantage`, a one-frame disagreement between the
engine and the prover is exactly the difference between `Terminating` and
`Infinite`. It is the single most likely way a paper's case study certifies a
game that is not the game.

## D9 — The four choices, and the answers

All four were named as genuinely open and all four are now decided
([ADR-002](adr/ADR-002-open-decisions.md)).

**A — Write the rollback session layer, or adopt one? Adopt GekkoNet, and vendor
it.** Input delay, prediction, confirm frames, frame-advantage adjustment, packet
loss, disconnect handling and desync detection are months of the riskiest code in
the project and none of it is differentiating. It is not in vcpkg, so adopting
means a pinned submodule ([ADR-003](adr/ADR-003-gekkonet-spike.md)), and the
two-day spike that decided it passed all three of its gates. It sits behind a
thin `ISession` that moves **bytes and a length** — `GameState`'s type never
enters the session layer, or game #2 forks the netcode.

**B — Trigger DSL, or data-only first? Data-only, and the DSL never arrived.**
Phase 0 measured the escape-hatch rate at 1.7%, and 39% of the real gap was
missing struct *fields* rather than missing expressions. The follow-up decision
([ADR-006](adr/ADR-006-stance-and-guard.md)) closes those with typed nouns —
stance, guard height, priority, invincibility windows — instead of a grammar. The
expression language is now on the deliberately-not-building list, with its own
come-back trigger.

**C — Resync on desync, or abort? Abort, and name the frame.** Periodic
authoritative resync would demote determinism from a correctness requirement to
an optimization. That is attractive for a server-authoritative game and wrong for
2-player peer-to-peer, where there is no authority, a resync hitch costs a round,
and an integer kernel makes determinism free anyway. Keep the detection half,
drop the correction half: exchange a checksum periodically, and on mismatch stop
the match and report the tick **and the field**. Silent correction in a fighting
game is worse than a stop.

**D — Keep Jolt in the fighting-game build? Yes, cosmetics only — and make the
boundary a link error.** Ragdolls on knockdown, debris and stage props are worth
having and cannot desync anything. The cost is one more subsystem in the process
and the standing temptation D2 names, which is why the gameplay kernel is its own
target that links nothing at all: a rule the build enforces beats a rule in a
document.

---

# 2. What we are not doing

Named so they are not re-proposed. Each with the reason and the condition under
which it comes back. Items whose come-back condition is a milestone rather than a
discovery live in [ROADMAP.md](ROADMAP.md)'s "Not scheduled, on purpose".

| Rejected | Why | Comes back if |
|---|---|---|
| **A vcpkg overlay port flipping `CROSS_PLATFORM_DETERMINISTIC`, on the critical path** | Jolt is not in the authoritative simulation. Costs 1.5–2.5× physics throughput and a permanent patch set, to make cosmetics bit-identical | D2 is revisited, or a second title needs networked physics. Then it is one engineer-day of framework work |
| **Patching Jolt's unguarded NEON multiply-add sites** | A real upstream gap, correctly diagnosed. Shipping a locally patched physics engine that upstream has never validated for x86 ↔ ARM is a bet we do not need to make. The proposed `#undef` of the intrinsic is also unsafe, because the determinism define is exported PUBLIC and would redefine an ACLE intrinsic in the consumer | Same as above. If it does come back, patch the fused-multiply-add helper and rewrite its call sites — no macro |
| **Pinning Jolt's instruction set down to SSE2** | The mechanism is one-sided and broken: the `USE_AVX2`-style options are consumed by a block that never executes under Ninja on Windows, so they only bind on Linux. If it must be pinned, pin *up* and let Jolt derive its defines from compiler macros | Never, as written |
| **Vendoring Jolt as a submodule + `add_subdirectory`** | Its headline benefit is false — that does not fix the empty platform variable under Ninja — and Jolt's build mutates `CMAKE_CXX_FLAGS` at directory scope, a class of contamination this repository has already been burned by | Never |
| **`entt::snapshot` / `continuous_loader` in the rollback loop** | An archive-callback loop, allocating and linear in scene size, that cannot capture `Name`, `ModelComponent`, `MaterialOverrides` or `AABB`'s vtable | Never for the loop. It is the right tool for spectate and late-join |
| **`PhysicsWorld::Rebuild` as a rollback restore** | It is `Clear()` + `Build()`: a re-roll, not a restore. Discards velocity, sleep state, warm-start impulses, contact cache and island assignment, and reassigns body ids in hash order | Never |
| **Hardening Lua into determinism** | Weeks for a forked language no tooling works against, and the rule that actually matters — no `pairs` over pointer-keyed tables — is **unenforceable from the host**. Silent Windows-versus-Linux desync with no error | Never in the simulation. Lua may live *outside* it — editor tooling, asset pipelines, build scripts — where the seam remains an asset |
| **AngelScript / Wren / QuickJS** | All keep the structural problem: a GC'd heap, their own hash ordering and libm questions, none readable by the prover, moddability no better than Lua. They change which nondeterminism bugs you find and in what order, not the answer | Never |
| **WASM (wasm3 / WAMR)** | *Deferred, not rejected.* Genuinely good determinism properties — math compiles into the module, and the snapshot is linear memory plus globals. But "edit a text file" becomes "install a toolchain and compile", losing the non-programmer modder, and the prover cannot read a `.wasm` | A mod author needs logic the schema genuinely cannot express. It slots in as another effect implementation |
| **A custom bytecode VM for triggers** | Premature, and now moot: there is no trigger language to compile. A hand-rolled VM accretes opcodes until it is a general-purpose language with none of Lua's testing | Profiling on real characters says a tree walk exceeds a rollback budget. It would be an optimization behind an interface, not an architecture |
| **Writing the rollback session and transport layer from scratch** | D9 A | The GekkoNet adoption is reversed |
| **Periodic authoritative state resync** | D9 C | The framework builds a server-authoritative title |
| **Making the gameplay core a fourth physics backend** | Would force it through a float API and drag the physics world's entity map and a transform decompose round-trip into the rollback path | Never |
| **A general `Fixed` type with operator overloads** | D2. Buys an overflow surface, a mirror-asymmetry bug and a C++17 arithmetic-shift question, for a game that rarely multiplies two positions | Never for this game |
| **Extending the simple physics backend into the gameplay core** | Float `glm` throughout, iterates an `unordered_map`, Y-up 3D, and its own header declares "no dynamic-versus-dynamic collision" — the *primary* interaction in a fighting game | Never. Fix its hash-order tie-break regardless — [DETERMINISM.md](DETERMINISM.md) §4 |
| **`FixedTimestep` driving the simulation** | It caps at eight steps and then **zeroes the accumulator, dropping the backlog**, and a test asserts that behaviour. A dropped tick in lockstep is an unrecoverable desync | Never for the simulation. It stays for the editor and single-player |
| **Making Jolt or PhysX rollback-capable** | Correct work for the general engine, and not on this game's critical path; it would absorb weeks. Fighting-game motion is authored per-frame data and AABB overlap | A future title needs solver rollback. The simple backend's POD map round-trips exactly and is the natural default |
| **A general Lua VM snapshot** | Closures capture upvalues, userdata carries a raw host pointer, metatables and coroutines have no portable serialization — and the shipped example script puts its state in a chunk-level local, invisible even to a hypothetical environment-table walker | Never. The declarative route is days where this is months |
| **Extending `EntitySnapshot` into a rollback snapshot** | Not trivially copyable, polymorphic member, three orders of magnitude slower than a `memcpy`, and a closed list whose own comment promises silent destruction of anything you forget | Never. It is a good editor undo facility doing a job it does well twice per session |
| **Hosting character data in `UIDataSource`** | The UI stack is the obvious thing to reuse and the wrong shape: its value kinds have no object and no array, a record is a flat list of pairs, and a list cannot contain a list. A move is two nesting levels beyond what the model expresses | Never. Reuse the UI stack's **discipline** instead — strict allow-list, whole-file staged commit with last-good-on-failure, diagnostics quoting what the author typed |
| **MUGEN CNS as the authoring format** | A real character's cancels do not live in its attack states at all, but in `[Statedef -1]` as trigger expressions routed through user variables — 1123 lines of importer for partial coverage of a side-effecting interpreter | Never. CNS is the right **corpus** format and the wrong **authoring** format |
| **A general time-based animation system with blend trees** | Fighting animation is indexed by integer frame from the state machine, not by wall-clock time. A general animator is months, and you would then fight it to get frame-exactness back | Never. Build the frame-indexed sampler |
| **A 2D sprite pipeline** | `Renderer2D` is capable and stays for HUD, hitbox overlay, input display and damage numbers — but for an SF6-like the perspective forward renderer is the right tool | A 2D title is wanted. Then a sprite component and a world-space 2D pass are a real decision |
| **Cook / pack pipelines** | For a moddable fighting game, loose files are arguably correct — it is what the MUGEN ecosystem runs on | The loose-file assumption starts costing load time or invites tampering |
| **The `Scene` god-object split, as a prerequisite** | Real debt, and worth fixing eventually — but the fighting simulation routes around it entirely by owning its own flat state, so starting there would burn a month before the first determinism test exists | It blocks something. Not before |
| **Renderer features beyond skinning** | The renderer is the largest piece of already-reusable work in the repository for this goal, and it is done enough. Skinning is admitted because the showcase needs it | The showcase is watched and genuinely needs one more. Then it is an ADR line, not a drive-by |

---

# 3. How the research lands

**The plug point.** The engine's character file **is** the analysis input: one
JSON document, one loader, two consumers. The kernel executes it; the prover
adapter projects it into `comboprover::Character`. There is no export step and no
second format, which is the whole of contribution #9's claim to be an
*integration* rather than a tool that happens to sit nearby.

**The projection is lossy and one-directional, and every loss needs a written
soundness argument.** Timing, damage, resource effects and guards map directly.
Cancels map directly except that contact is one bit in the prover's model, so
block-only and whiff-only edges over-approximate. Stance, reach, pushback, walk
speed and gap actions are **dropped entirely** — which is what makes the verdict
corner-only, and is why a midscreen microwalk loop is invisible to it. The
loss ledger in `Games/UntitledFighter/Data/src/MatchBuilder.cpp` names each
dropped field and the direction of the resulting error, and the editor panel
shows it beside the verdict.

**Resource ceilings are absent from the prover's model, and that is the safe
direction.** The C++ model has no ceiling anywhere, so it searches a state space
in which meter grows forever. Let `d` be the difference between an unclamped and
a clamped run: `d` starts at zero and only grows, because clamping subtracts and
never adds, and guards are lower bounds only. So every clamped step is legal
unclamped, every clamped infinite is an unclamped infinite, and running without
ceilings can only **add** infinites. The adapter clamps anyway — it removes false
alarms, and it replays a reported witness through its own clamped loop — but the
ceiling's absence cannot hide an infinite. Resource **order**, by contrast, is a
build-wide contract: the vector is positional, nothing in either implementation
names a resource, and reordering one file silently compares meter against juggle
points. That one is asserted at load.

**What the panel must say, and what it must not.** Three states, not two:
`INFINITE` with the loop printed as a move sequence; `TERMINATING` with maximum
hits, frames and damage, showing the ranking certificate **when available** and
saying plainly when it is not — any character that builds meter on hit gets
`TERMINATING` with no certificate, which is the common case, not the exception;
and `UNRESOLVED`, which is a budget statement rather than an error. The panel must
also name where the fighters are standing: Phase 0 ran every character twice and
**the verdict differs** between corner and midscreen. Worse for the midscreen
half, pushback is not derivable from MUGEN at all, so every midscreen run rests
on an estimated constant and a ±20% error in it flips one of three verdicts.
Corner verdicts do not depend on the estimate and are the ones to trust. The
things a designer uses daily — dead cancels, unreachable moves, the settling
index — are free, always shown, and land before anyone cares about the theorem.

**What the paper harvests, and the one item no offline tool can produce.** A fit
measurement (what fraction of a real character's moves need an escape hatch: 1.7%
in Phase 0). A latency distribution over real authoring. Found-bug evidence —
dead cancels and unreachable moves in genuinely authored content. The soundness
note on the projection, which is the first written account of what an executable
fighting game contains that a decidable model does not. And **ground-truth
validation**: take a character the prover calls `Infinite`, hand its *printed*
loop to the engine as a scripted input trace, and execute it. That has been done,
and it returned both of the outcomes it could. The loop works — the witness
executes as written, bit-identical on replay, with the input trace derived from
the prover's own output rather than hand-written. And the gap is real, on the
*safe* character: the analysis is correct about the file, and the projection to
the running game is what fails wherever the kernel lacks a mechanism the file
uses. The two findings are complementary. Closing the gap is what
[ROADMAP.md](ROADMAP.md) M1 is for, and the test says so the day it lands.

**A last hazard, for later.** The verdict itself is computed with floats, so two
machines can disagree about it. Harmless in the editor. Not harmless the day the
game warns a player about a modded infinite, because both peers would run the
check and could disagree about admitting the mod. Compute the verdict on one side
and hash it into the connect handshake alongside the content hash.
