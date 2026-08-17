# ROADMAP — the one place status lives

Verified: 2026-08-17 @ 99669cc

This is the **only** roadmap. `README.md` carries one paragraph and a link;
`docs/manual/` never lists gaps; ADRs record why, not what is next. If a fact
about status is anywhere else, that copy is wrong. How this file is maintained
is at the bottom (§ How to update this file); the decision behind it is
[ADR-010](ADR-010-one-roadmap-one-rule.md).

## The goal, in one paragraph

An SF6-like fighting game — 3D characters on a 2D plane, two players, 60 Hz,
rollback netcode, characters authored as files — on a framework the author reuses
for the next game, which **proves the combo-termination research** by running
the *published* prover on the *shipped* files inside a working editor. The
engine's job is to make that proof **visible and convincing**: every verdict the
prover prints must be demonstrable in the running game, frame-perfectly, by a
tool-assisted player, as a replay anyone can watch — and the **same fighter,
loaded with different frame data, must show different infinites** (microwalk,
jump cancels, wall bounces, counter-hit links, meter loops), because every
mechanic is an opt-in field on a move, never a rule in the kernel
([ADR-011](ADR-011-mechanics-are-fields.md)). Visuals are a pure function of
frame data; return-to-idle tails are always cancelable. Everything is provable
and showcased **before** any real art is made; placeholder rigs (Mixamo) come
first, SF6-tier art last, and only after the showcase already sells the paper
without it. Details, tests and proofs of the four properties:
[NORTHSTAR.md](NORTHSTAR.md).

## Now

| In flight | Owner | Since |
|---|---|---|
| *(nothing — next is M0.2)* | | |

One work package in flight at a time. The next unblocked one is always the top
`[ ]` in milestone order below.

## Legend

`[ ]` not started · `[~]` in flight · `[x]` done (commit) · `[-]` dropped (why) ·
**Size** S = hours–a day, M = days–a week, L = weeks. Calendar estimates are
absent on purpose: the six days after the first kernel commit (`a3cc8c7`)
delivered two thirds of a phase that was budgeted at 8–10 weeks.

Every work package (WP) has a **Done when** that is a test name, a number or a
demo. A WP is done when that test exists, fails without the change, passes with
it, runs in CI, and the manual page it touched says the new truth.

---

## M0 — One roadmap, one rule *(size M)*

Consolidate the documentation and install the gate, first, because every later
WP will be read by someone (or something) that starts from these files. The
full mapping is [ADR-010 §5–§7](ADR-010-one-roadmap-one-rule.md); this is the
work list.

- `[x]` **M0.1 Archive the originals.** *(S)* Copy `NORTHSTAR.md`,
  `ARCHITECTURE.md`, `AUDIT_FINDINGS.md`, `ENGINE_AUDIT_2026-07.md` verbatim to
  `docs/archive/<NAME>-<date>.md`; add `docs/archive/README.md` ("history;
  nothing here is current; frozen ADRs' line citations resolve here").
  **Done when:** the four files exist under `docs/archive/`, byte-identical to
  their originals at `99669cc`.
  **Deviation:** the README does not claim frozen ADRs' line citations *resolve*
  — two spot checks show they never did — only that they are read against the
  archived copy, which stops the gap growing. Byte-identity becomes a
  `check_docs.py` check in M0.3.
- `[ ]` **M0.2 `docs/DETERMINISM.md`.** *(S–M)* One table: rule · enforced by ·
  where. Sources: `ARCHITECTURE.md` §4 (the contract), `NORTHSTAR.md` appendix,
  `MAINTENANCE.md` "Never add a fast-math flag", the rules restated in
  `docs/manual/fighting-core.md`. Every rule names its enforcement — CI script,
  `static_assert`, configure guard, a test by name, or "review only". A rule that
  is "review only" but *could* be mechanical becomes a WP here (M1.1(d) is one).
  **Done when:** every rule in ADR-010's inventory has one row, and
  `docs/manual/fighting-core.md` links instead of restating.
- `[ ]` **M0.3 `scripts/check_docs.py` + CI step, advisory.** *(S)* Checks:
  relative links resolve; backticked repo paths exist (`:line` stripped;
  `docs-ok` escape); living docs carry `Verified: <date> @ <sha>` in the first ten
  lines; ADRs carry a `Status` line; no `AMENDED`/`STRUCK`/`~~`/`Correction (`
  outside `docs/adr/` and `docs/archive/`; `--self-test`. Add as a step in the
  `determinism-flags` job in `.github/workflows/ci.yml` with
  `continue-on-error: true` **only until M0.6**. Its first run's failure list is
  the checklist for M0.4–M0.6.
  **Done when:** `python scripts/check_docs.py --self-test` passes and the step
  is in CI.
- `[ ]` **M0.4 Rewrite the top level.** *(M)* `NORTHSTAR.md` → one screen (goal,
  four properties, done-tests, proofs — ADR-010 §2). `ARCHITECTURE.md` → D1–D9
  with every amendment folded into the prose, D9 as the four answers, §2
  rejection table plus `NORTHSTAR.md` §6's rows, the research plug-point in five
  paragraphs; **remove** the build order, the contract, the first week and the
  appendix. Fix `Engine/src/gameplay/` → `Games/UntitledFighter/Kernel/`.
  `git mv` `ADR-001`…`ADR-010` to `docs/adr/`; add/normalise one Status line
  each (005 Implemented P0–P2, 006 Implemented, 007 "trigger 3 fired — ADR-008",
  008 Implemented @ `1aaa2d1`, 009 Implemented @ `41ea6e5`, 002/003 name the
  standing verdict); `docs/adr/README.md` index. Repoint every inbound link.
  **Done when:** `check_docs.py` reports no dead links or paths outside the
  manual.
- `[ ]` **M0.5 The manual.** *(M)* Fix ~30 paths in `docs/manual/fighting-core.md`
  and delete its "Not there yet" (this file is that list now) and its restated
  rules (link `DETERMINISM.md`); add the Game layer (`FightSession`, input
  sources, replay, combo watcher) and the Modes (training, frame step, HUD).
  `docs/manual/editor.md`: Build Settings, Combo Prover, modes in the Game view.
  De-duplicate the four repeated blocks to one canonical page each (isolation
  note + frame order → `architecture.md`; staging rule + packaging + scene JSON →
  `scenes-and-shipping.md`); merge `lua-scripting.md` into
  `gameplay-scripting.md` as a "presentation and tooling only (D7)" section;
  fold `BUILDING_LINUX.md` into `getting-started.md`; `performance.md` gains the
  laptop section from the archived audit. Stamp every page after a read-through.
  **Done when:** `check_docs.py` is fully green.
- `[ ]` **M0.6 Entry points, and make it required.** *(S)* `README.md`: pitch,
  one roadmap paragraph + link, docs table for the new tree, build/run/ship;
  delete the feature matrix, "Not Yet Built" and the scale line; fix Project
  Structure. `MAINTENANCE.md`: "Keeping the documentation true" → the five-line
  rule (ADR-010 §8.1) + the adversarial audit as the periodic check; determinism
  invariants → pointer to `DETERMINISM.md`. Delete `docs/api-index.md`,
  `docs/CMakeLists.txt`, `docs/Doxyfile.in` and the root
  `add_subdirectory(docs)` (ADR-010 §9.1 default). Remove
  `continue-on-error`. Set ADR-010 Status → Implemented.
  **Done when:** CI is green with the docs step required; `docs/` is six living
  files + `adr/` + `manual/` + `archive/`.

---

## M1 — The graph is the game *(size L)* — the paper's central claim

Close the measured gap between what the prover proves and what the kernel does,
make every mechanic an opt-in field per move ([ADR-011](ADR-011-mechanics-are-fields.md)),
make the authoring loop live, and build the tool-assisted showcase that turns
every verdict — for one fighter under many frame-data patches — into a
watchable, verified replay. **`GameState` is a wire contract** (ADR-005 §3):
all of M1's state changes land as **one** expansion with one re-golden
(`tests/test_determinism_crossplat.cpp`), reviewed once — including the fields
M1.3 and M3.1 will need (M1.1 reserves them).

- `[ ]` **M1.1 Resources, movement parameters, and the one state expansion.** *(M)*
  Today `Fighter::meter` exists and no file in `Games/UntitledFighter/Kernel/src/`
  writes it; juggle has bespoke rules; walk speed and jump impulse are
  `constexpr` in `Simulate.cpp` while `walk_speed` is authored and ignored.
  (a) **Positional resources**: `int32 res[kMaxResources]` in `Fighter` (default
  `kMaxResources = 4`), `ResourceDef {initial, floor, ceiling, refill}` per slot
  in `FighterData`, so *index i in the file = index i in the kernel = index i in
  the prover* — the A03 contract, true by construction. Moves and cancel edges
  carry `effect[]` (applied per authored contact) and `guard[]` (a minimum,
  checked before the move starts); the resource-guard rows in
  `Games/UntitledFighter/Data/src/MatchBuilder.cpp`'s loss ledger become
  `Exact`. Juggle keeps its refill-on-ground rule as `ResourceDef::refill`.
  (b) **Character constants become fields**: `walk_speed`, jump arc, gravity,
  default pushback and hitstop move from `Simulate.cpp` into `FighterData`,
  defaulted by the schema (ADR-011 decision 1). (c) **Reserve** in the same
  expansion: M3.1's event ring (`Event ev[kMaxEventsPerTick]`, `uint8 evCount`)
  and the reaction fields M1.3 needs (`uint8 reaction`, `uint8 bounces`,
  `uint16 flags`), so the wire format changes once. (d) Move the type assertions
  from `tests/test_kernel.cpp` into `GameState.h`, adding
  `static_assert(std::has_unique_object_representations_v<GameState>)` (no one
  asserts padding today; hashing raw bytes depends on it).
  **Done when:** `P3Resources.MeterGainsOnHitAndSpendsOnGuard`,
  `.AGuardedCancelRefusesBelowTheMinimum`, `.IndexOrderIsTheFilesOrder`,
  `P3Movement.WalkSpeedComesFromTheFile`; the golden re-recorded once;
  `tests/test_gap_extent.cpp`'s `EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter`
  rewritten to assert the opposite. **Traps:** D8 quantisation once at load;
  `decay.floor` ≤ min hitstun (A01); ADR-009's `alreadyHitBits` width assert;
  explicit `pad_` bytes; the crossplat test scripts jumps by input bits — keep
  that working through the character's jump move.
- `[ ]` **M1.2 Push boxes and the corner.** *(S–M)* Body separation between
  fighters and the stage edge as a wall; resolution order per NORTHSTAR Phase 2:
  pushbox separation → strikes (throws when they exist). Authored `pushbox`
  under `engine.boxes` (schema v3, appended field per ADR-006's rule); default
  from `MatchBuilder.h`'s `BodySpec`. Same integer box math as
  `Games/UntitledFighter/Kernel/include/cse/kernel/Combat.h`; separation splits
  the overlap with `scaleBy`'s rounding so it is mirror-symmetric.
  **Done when:** `P3Pushbox.FightersNeverOverlapAfterSeparation`,
  `.TheCornerIsAWallOnBothSides`, `.SeparationIsAnExactMirror`.
- `[ ]` **M1.3 Mechanics, pass 1 — the ones the showcase needs.** *(M–L)* Each
  with ADR-011's five parts (schema field appended · `MoveDef`/`FighterData`
  slot · loss-ledger row · kernel property test · showcase variant):
  (a) **contact mask** on `CancelEdge` — `hit | block | whiff` bits replacing
  the collapsed `onHit`, so kara and whiff cancels are expressible;
  (b) **movement is a move** — jump, super jump, dash, backdash as authored
  moves with a `movement` field (per-frame velocity, gravity,
  `airborne_from_tick`, optional landing recovery); the kernel's hard-coded jump
  is deleted; a **jump cancel** and a **dash cancel** are ordinary cancel edges
  whose target is a movement move; `to: idle` is a legal empty cancel;
  (c) **counter-hit** — per-move `counter_hit {hitstun_bonus, damage_bonus}`
  applied when the defender was in startup; (d) **wall bounce / wall splat /
  launch vector** as per-hit `on_hit` reactions using the fields M1.1 reserved.
  Everything defaults off; `fighter_a` unpatched must hash exactly as before
  this WP except for the re-golden M1.1 already did.
  **Done when:** `P3Cancels.AKaraCancelFiresOnWhiffInsideItsWindow`,
  `P3Movement.AJumpIsAMoveAndAJumpCancelIsAnEdge`,
  `P3Reactions.CounterHitAddsTheAuthoredStun`,
  `P3Reactions.AWallBounceReturnsTheDefenderIntoRange`; the loss ledger has a
  row for each; the schema bumps to v3 with the fields appended.
- `[ ]` **M1.4 The kernel search, and the ground truth as the gate.** *(M)*
  Promote the cancel-graph walk out of `tests/test_gap_extent.cpp` into
  `CseGame` as **`ComboSearch`**: a bounded search over macro-actions — start
  move *m*, wait *k* frames, walk *k* frames, jump, dash — executed on the real
  kernel, de-duplicated by `Checksum()` of the state, with a budget; a search
  that hits its budget reports **UNRESOLVED**, never a verdict (ADR-011 decision
  8). One implementation for tests, cooker, showcase and panel. Rewrite
  `tests/test_ground_truth.cpp` and `tests/test_gap_extent.cpp` to assert
  properties: (a) every graph-prover `Infinite` witness reproduces — defender
  never actionable across N loops; (b) for every `Terminating` character no
  performable combo exceeds `maxHits` (search to `maxHits + k`), **or** the
  kernel search's counter-example is explained by a named loss-ledger row
  (microwalk → `walk_speed`/`gap_actions`); (c) every reported dead cancel never
  connects. Delete `NinetySevenOfThe121RunForever` when it is false.
  **Done when:** those tests pass on every shipped character and patch and on
  the three MUGEN fixtures in `tests/fixtures/characters/`; the search's cost
  per macro-action is measured and recorded here with a date.
- `[ ]` **M1.5 Character hot reload.** *(S)* Extract the mtime+size 0.25 s
  poller from `Engine/src/ui/UIAssetDocument.cpp` into `Engine/src/core/FileWatch.{h,cpp}`
  with an injectable clock; `UIAssetDocument` uses it (no second copy). Training
  mode watches the character file **and its patches**, rebuilds `MatchData`
  **between ticks**, keeps last-good on failure and shows the load report
  (naming the key), and the Combo Prover panel re-analyses on the same event.
  **Done when:** `TrainingMode.AnEditedCharacterFileTakesEffectWithinAQuarterSecond`
  (clock injected, no sleep) and `UIHotReload.*` still pass unchanged.
- `[ ]` **M1.6 The showcase: one fighter, many patches, a replay per verdict.** *(M)*
  Variants are JSON merge patches (RFC 7386, `nlohmann::json::merge_patch`)
  under `Games/UntitledFighter/Assets/Characters/fighter_a/variants/`;
  `fighter_a_infinite.json` becomes `base + variants/infinite.json`. The eleven
  patches and what each shows are ADR-011 §4; ship them in the order their
  *Needs* column comes true. `BuildDemonstration`
  (`Games/UntitledFighter/Game/include/cse/game/FightSession.h`) already
  rehearses a frame-perfect attacker headlessly; build **`Showcase`** in
  `CseGame` on it and `ComboSearch`: for base + every patch record the graph
  prover's verdict, the kernel search's verdict, the named reason when they
  differ, `.csrp` replays (the loop for N cycles, the max-hits combo, each dead
  cancel attempted, corner and mid-screen), the input trace, and a `graph.dot`
  of the cancel graph with the loop highlighted — every replay verified
  bit-identical by `ReplayVerifier` before it is written. Wire **REPLAY** as a
  mode (`Games/UntitledFighter/Modes/`, named in `UntitledFighterModes.cpp`)
  playing a `.csrp` through the shared presentation with an **on-screen input
  display** and the patch's one-line description on screen. Add
  `--replay <file>` and `--mode <name>` to the Player (`Player/src/PlayerMain.cpp`
  reads argv via `Engine/src/core/Main.h`). Ship the generator as
  `AssetCooker showcase <root>` (same fail-closed protocol as `validate`).
  **Done when:** `Showcase.EveryCatalogueEntryReplaysBitIdentically`;
  `Showcase.EveryPatchChangesTheVerdictItClaimsTo`;
  `Player --replay Exported/Showcase/fighter_a/microwalk.csrp` plays the walk
  steps with the input display; the catalogue is generated in CI and its
  verification is a test.
- `[ ]` **M1.7 Authoring telemetry.** *(S)* One JSON line per prover run from
  the panel and the cooker (content hash, move/cancel counts, resource ranges,
  `explored`, wall-clock ms, verdict, changed-since-last). NORTHSTAR §5 says it
  is worthless retroactively. **Done when:** the file grows by one line per run
  and a test parses it.
- `[ ]` **M1.8 Housekeeping.** *(S)* `constexpr scaleBy` as one helper in the
  kernel with `static_assert(scaleBy(-3, 1, 2) == -2)` beside it (the rule is
  inline in `MatchBuilder.cpp` today); delete dead code after confirming zero
  callers — `Engine/src/core/Mesh.h` (commented-out skinning),
  `Engine/src/core/EventBus.h` + `Event.h` (only `Engine/include/Engine.h`
  includes them), `Scene::RenderShadowDepth`; drop `.fchar` from any doc.
  **Done when:** builds clean in four configurations; nothing links against the
  removed symbols.

**M1 gate (demo + CI):** in training mode, edit `fighter_a.json` (or a patch)
and watch the change land; press Demonstrate and the prover's INFINITE plays
itself; the `microwalk` and `jump-cancel` patches play their loops with the
input display on; **CI asserts every verdict — graph prover and kernel search —
against the running kernel for base + every patch** and generates a verified
replay catalogue.

---

## M2 — Two people, one match *(size L)* — ARCHITECTURE Phase 4

Everything here wires things that already exist. Nothing here touches the
kernel.

- `[ ]` **M2.1 Transport — spike, then ADR-011.** *(M)* GekkoNet is built with
  `GEKKONET_NO_ASIO` and takes a `GekkoNetAdapter` (`send_data` /
  `receive_data` function pointers). Default: a plain UDP adapter in `Net/src/`
  behind a `CreateGekkoRemoteSession(config, localPort, remoteEndpoint)`
  factory — Winsock/BSD sockets behind one `#ifdef`, no new dependency. Spend at
  most one day comparing with vendoring asio; write the answer as
  `docs/adr/ADR-012-transport.md`. Two configure-time guards in
  `Net/CMakeLists.txt` stay: no interface leak, no `gekkonet.h` reachable.
  **Done when:** `SessionUdp.TwoSessionsOnLoopbackAgreeForATenMinuteMatch`
  (3,600+ ticks) with injected 100 ms / 5 % loss.
- `[ ]` **M2.2 Handshake.** *(S)* Before the first tick, exchange: schema
  version, `HashMatchData` (`Games/UntitledFighter/Game/include/cse/game/Replay.h`
  — written for exactly this), and a build id (git sha baked at configure via
  `configure_file`; none exists today). `Net/` sees opaque bytes; `Game/`
  computes them. Mismatch is a lobby error naming the reason, never a gameplay
  bug. **Done when:** `SessionHandshake.AContentMismatchIsRefusedByName`.
- `[ ]` **M2.3 Desync = abort + artifact.** *(S–M)* On `PollDesync`, stop the
  match and write both `GameState` blobs plus the input log since the last
  confirmed tick to `Builds/desync-<tick>.bin`; name the frame **and the
  field** — which needs the reflection table ARCHITECTURE D1 asked for: a
  `constexpr` array of `{name, offset, size}` over `GameState` in the kernel,
  which also feeds the editor's sim inspector and the field-level diff.
  **Done when:** `Desync.ACorruptedPeerIsNamedByFieldWithinEightTicks`.
- `[ ]` **M2.4 The session owns the tick count.** *(S–M)* Today
  `UntitledFighterMode::FixedTick` calls `session_.Tick()` once per
  `Application` fixed step, i.e. `FixedTimestep` (8-step cap, backlog dropped)
  decides how many ticks run. Under a network session **the session decides**:
  the mode pumps `ISession::Update` and runs exactly the ticks it is told, zero
  is legal, dropping is not. While a session is live: pause, time scale and
  scene swap are inert; UI focus never suppresses gameplay input; the producer
  keeps a sticky "pressed since last tick" mask outside the snapshot.
  **Done when:** `Session.ZeroTicksThisFrameIsLegalAndNoTickIsEverDropped`.
- `[ ]` **M2.5 VERSUS, and one presentation for three modes.** *(M)* Extract
  the fight presentation (`FightView`, `FightHud`, camera, box overlay) out of
  `UntitledFighterMode` into a `FightPresenter` that Training, Replay (M1.6)
  and Versus share — three modes, one drawing. Versus: local two-controller
  first (`SetInputSource` twice), then two processes on loopback, then two
  machines. Menu entry per `UntitledFighterModes.cpp`.
  **Done when:** two Players on one machine finish a match with zero checksum
  mismatches; the Windows ↔ Linux run is recorded here with a date.
- `[ ]` **M2.6 Play == Player, as a test.** *(S–M)* NORTHSTAR Q6 / ADR-002:
  the editor's Game view and the shipped Player must run the same code path.
  Extract the duplicated host setup (~90 lines each in `Player/src/PlayerMain.cpp`
  and `Editor/src/EditorApplication.cpp` as of 2026-08-12 — re-measure) into
  one shared helper, and add the CI test: same input log through both hosts,
  identical hash. **Done when:** `Hosts.TheEditorAndThePlayerHashTheSameMatch`.

**M2 gate:** a ten-minute match Windows ↔ Linux with zero checksum mismatches,
and a deliberately corrupted peer reports "desync at tick N, field F" within
eight ticks.

---

## M3 — Skinned fighters, frame-indexed *(size L)* — placeholders, not art

Mixamo rigs and clips are **placeholders** that make the showcase look like a
fighting game; they are not the art. Every rule of the frame-indexed design
(ADR-005 §4, made precise by [ADR-011](ADR-011-mechanics-are-fields.md)
decision 6) holds and is the acceptance test for M3.2–M3.4: **pose is a pure
function of the state the sim already produces** — `f(moveId, moveFrame, posX,
posY, facing, stance/airborne, stun fields, tick)`; the authoritative window is
exactly `startup + active + recovery` frames and its clip and first frame are
authored in the frame data; **the tail** (return-to-idle, landing settle) is
presentation only, plays while the fighter stays idle and unmoving, and is
**interrupted instantly by any sim action** — a presentation-side blend of at
most 4 frames may smooth that interruption and can never delay a move, shift a
box or hold a fighter in place; idle/walk/air cycles are keyed by `(tick,
posX)` so they are stateless. Nothing about animation ever influences a tick.
Skinning is the one renderer feature NORTHSTAR §6's freeze admits, because the
showcase needs it.

- `[ ]` **M3.1 The event queue, before the first sound.** *(S–M)* Fill the
  ring M1.1 reserved: `Simulate` appends `{slot, kind, a, b}` events; a
  `Phase { Predicted, Confirmed }` parameter; `FightSession` drains events for
  confirmed ticks only, de-duplicated by `(tick, slot, kind)`; presentation
  never plays a sound or spawns a spark from inside a tick.
  **Done when:** `Events.ARollbackReplaysTicksButFiresEachEventOnce`.
- `[ ]` **M3.2 Frame-indexed clip player + skinning (engine).** *(L)*
  `Engine/src/anim/` (new): `Skeleton`, `AnimationClip` (poses **sampled per
  60 Hz frame at import**, so runtime sampling is an integer index — no dt, no
  interpolation on the authoritative window), `SkinnedMeshComponent`
  (serialised, inspectable, in the component registry). `Model.h`'s `Vertex`
  gains `ivec4 boneIds` + `vec4 weights`; Assimp import reads bones and
  `aiAnimation`; a bone-palette UBO and a skinning branch in the forward shader
  **and the CSM depth pass** (a skinned mesh must cast a skinned shadow).
  **Done when:** `Skinning.APosedMeshMatchesTheCpuReference` (CPU-skinned
  vertices vs the GPU path, `gl` label) and a golden-frame render test of one
  posed Mixamo character.
- `[ ]` **M3.3 The Mixamo pipeline.** *(M)* Import FBX via Assimp into the
  engine's model + clips under `Games/UntitledFighter/Assets/Characters/<name>/`;
  the character file's `engine.anim` (today sprite-shaped in `schema.v2.json`)
  gains, **appended**, `clip` and `firstFrame` per move; load assertion:
  authored clip length ≥ `startup + active + recovery` and the authoritative
  window is the first N frames (ADR-005 §4.1). **Record the licence** for every
  imported asset in a `LICENSE.md` beside it, the way
  `tests/fixtures/characters/README.md` does; never commit an asset whose
  licence is not written down. All characters share Mixamo's rig, so no
  retargeting.
  **Done when:** `fighter_a` loads with clips and every move passes the length
  assertion.
- `[ ]` **M3.4 The presentation reconciler.** *(M)* `FightPresenter` writes
  `const GameState&` into `GameModeContext::scene`'s registry each frame —
  fighter transforms from `posX/posY/facing`, animation state from
  `moveId/moveFrame` (frozen during hitstop), camera from the pair — as a pure
  function of state; ties by slot index; the 2D overlay's `Renderer2D::BeginWorld`
  and the 3D camera agree on one projection so boxes sit on the mesh.
  **Done when:** `Presenter.TheSameStateProducesTheSameSceneTwice`,
  `Presenter.ATailIsInterruptedTheTickTheSimActs` (the blend never moves a box
  or delays a move), and the boxes visibly track the skinned mesh in the
  showcase replays.
- `[ ]` **M3.5 Feel and stage.** *(M)* Hit sparks and hitstop shake from the
  event queue, SFX through the existing `AudioWorld` (low-latency device
  setting), a stage scene using the engine's IBL/CSM/post stack, camera rules.
  **Done when:** the M1.6 catalogue re-recorded with skinned characters reads
  as a fighting game to someone who has not seen the repo.
- `[ ]` **M3.6 Roster and select.** *(M)* Second and third characters through
  the same pipeline; character select needs per-player nav scopes in `UIWorld`
  (one nav focus per document today) — an engine change worth its own short ADR.
  **Done when:** two players pick different characters and the handshake hashes
  both.

**M3 gate:** the showcase catalogue, re-recorded, looks like a fighting game;
CI exercises skinning under llvmpipe.

---

## M4 — Showcase and publish; then art *(size M + content)*

- `[ ]` **M4.1 The reel.** *(S–M)* `Player --replay <file> --dump-frames <dir>`
  writes a PNG sequence (encode offline with ffmpeg; do not vendor an encoder);
  overlay toggles; one script regenerates every reel from the catalogue.
- `[ ]` **M4.2 Paper artefacts.** *(S)* `AssetCooker combos <root> --json` (one
  record per character), the telemetry log, the ground-truth results, the
  cross-platform hash logs — written to `Builds/paper/` by one script, never
  into `docs/`.
- `[ ]` **M4.3 Publish the claims.** *(S)* README and website updated with
  numbers taken from CI output. NORTHSTAR Q7 forbade rollback claims until it
  ran; after M2 it runs.
- `[ ]` **M4.4 Real art.** *(content)* SF6-tier models and animation through the
  M3 pipeline; an art ADR when a modeller exists. This is content, not
  engineering, and it starts only after M4.1–M4.3 already sell the paper.

---

## Engine maintenance — done inside the milestones, not as a phase

Standing rule: **no new subsystem without deleting or unifying something**, and
every "MUST match X" comment becomes a call to X. These are the concrete items;
each is attached to the WP that first needs it.

| Item | What | Attached to |
|---|---|---|
| E1 | **Component registry table** for non-sim ECS components — `{name, toJson, fromJson, capture, apply, equal, drawInspector}` driving `SceneSerializer`, `UndoHistory` and the Inspector from one table, with a test that walks the registry and round-trips every type. Retires MAINTENANCE.md's "closed lists" invariant | M3.2 (the first new component) |
| E2 | **Shared host setup** for Player and Editor + the Play == Player hash test | M2.6 |
| E3 | **`FightPresenter`** — one presentation for Training / Replay / Versus | M2.5 |
| E4 | **`ComboSearch`** promoted from `tests/` into `CseGame` | M1.4 |
| E5 | **`FileWatch`** extracted from `UIAssetDocument` | M1.5 |
| E6 | **Reflection table over `GameState`** — inspector, desync field diff, serialiser from one artifact | M2.3 |
| E7 | **Dead code deleted** (`Mesh.h`, `EventBus.h`/`Event.h`, `RenderShadowDepth`) | M1.8 |
| E8 | **`Games/` and `Net/` targets to C++20** (`std::span`, designated initialisers, `<bit>`); engine stays C++17 | any WP that first wants one of those; record in `ARCHITECTURE.md` |

---

## Not scheduled, on purpose

Reasons and come-back triggers are in [ADR-010 §3.4](ADR-010-one-roadmap-one-rule.md);
this is the list, so nobody re-proposes them by accident.

- The trigger expression language (Phase 5) — typed schema nouns instead.
- `SimId`, our own snapshot ring, our own input ring — GekkoNet owns them.
- Projectile pool — until a shipped character has one.
- Asset mounts (ADR-007) — after M2, on its triggers.
- Engine install/export (G4) — after M2.
- Cook/pack pipeline, Lua hardening, Jolt determinism, new renderer features
  beyond skinning — ARCHITECTURE §2 conditions.
- Two open rows carried from the archived 2026-07 ledger: `EventBus` (deleted in
  M1.8) and gamepad verification on physical hardware (do it during M2.5).

---

## How to update this file

- Change a WP's box and, for `[x]`, append the commit sha; for `[-]`, the reason.
- Move the "Now" row when a WP starts; there is never more than one.
- A WP that grows a decision gets an ADR (Proposed, with a recommended default)
  and a one-line pointer here; a WP that splits stays under its number
  (`M1.5a`, `M1.5b`).
- Do not add prose about *why* here — that is the ADR's job. Do not add status
  anywhere else — that is this file's job.
- Bump `Verified:` at the top when you have re-read the whole file against the
  tree, not when you edit one line.
