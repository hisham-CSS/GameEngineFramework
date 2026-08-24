# ROADMAP — the one place status lives

Verified: 2026-08-21 @ db8dd1a

This is the **only** roadmap. `README.md` carries one paragraph and a link;
`docs/manual/` never lists gaps; ADRs record why, not what is next. If a fact
about status is anywhere else, that copy is wrong. How this file is maintained
is at the bottom (§ How to update this file); the decision behind it is
[ADR-010](adr/ADR-010-one-roadmap-one-rule.md).

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
([ADR-011](adr/ADR-011-mechanics-are-fields.md)). Visuals are a pure function of
frame data; return-to-idle tails are always cancelable. Everything is provable
and showcased **before** any real art is made; placeholder rigs (Mixamo) come
first, SF6-tier art last, and only after the showcase already sells the paper
without it. Details, tests and proofs of the four properties:
[NORTHSTAR.md](NORTHSTAR.md).

## Where this stands against the paper

The paper's claim has four load-bearing parts. This is what each rests on today.

| The claim needs | Evidence in CI today | What is missing |
|---|---|---|
| **A deterministic simulation** | `tests/test_kernel.cpp` T1/T2; `tests/test_determinism_crossplat.cpp` T3, re-checked by gcc 13 on the Linux leg | Nothing. This one is done and has been re-goldened three times for stated reasons. |
| **The prover reads the shipped files** | `tests/test_character_data.cpp` load assertions; the editor's Combo Prover panel | Nothing structural. `counter_hit` is absent from the schema entirely — see the qualifier below. |
| **Every verdict is demonstrable as a replay** | `tests/test_ground_truth.cpp` executes the prover's own printed witness | **The largest gap.** Only one character's witness is executed, the demonstration cannot press a direction, and see the two findings below. |
| **The game is the file** | `MatchBuilder`'s loss ledger, checked move-by-move in `tests/test_match_bridge.cpp` | **The bridge carries ~10 of `MoveDef`'s fields.** Stance, juggle, hitstop, priority, chip and scaling are authored and dropped. |

**Two findings decide how much of the claim survives, and both are measured.**

1. **The bridge drops stance, so 12 of `fighter_a`'s 18 normals cannot be
   performed at all** — `stand_hk` shadows `crouch_hk` on a shared button. Wiring
   it moves `tests/test_gap_extent.cpp`'s headline from **97 of 121** cycles
   "run forever" to **77**, and every one of the 41 cycles that changes contains
   an air move: they ran forever only because an aerial could be performed on the
   ground. **97 was earned by neither the model nor the kernel.** (M1.3e, M1.4a.)
2. **The combo graph gates on nothing about fighter state.** A jump is 38 ticks
   and `air_mp` is 22, so `air_mp > air_mp` — the one cycle the graph calls
   performable end-to-end through the cancel system — yields about **1.7
   repetitions before landing.** It is not an infinite. (M1.4a.)

**And one qualifier the write-up does not yet carry.** `counter_hit` appears zero
times in `schema.v2.json`. The model reads one `hitstun` per move, so a
TERMINATING verdict says nothing about the same string opened with a counter hit,
and `air_hitstun_ticks` — authored on every move and differing from ground
hitstun on all of them — has the same shape. Three ways out are in M1.3(c); none
is chosen, because it changes what the tool claims.

**The shortest credible path to the claim:** M1.3e + M1.4a together (the kernel
and the graph both learn state, and the number they agree on is the one to
publish), then M1.4 (`ComboSearch` on the real kernel), then M1.6 (a replay per
verdict). Everything else is either done or serves those three.

## Now

| In flight | Owner | Since |
|---|---|---|
| M1.2 — push boxes and the corner | Claude | 2026-08-20 |
| M1.3d — the bridge carries the mechanics the kernel has | Claude | 2026-08-20 |

Two are open because M1.3d's remaining half is blocked on M1.3e's decision; do
not start a third. The next unblocked WP is always the top `[ ]` in milestone
order.

## Legend

`[ ]` not started · `[~]` in flight · `[x]` done (commit) · `[-]` dropped (why) ·
**Size** S = hours–a day, M = days–a week, L = weeks. Calendar estimates are
absent on purpose: the six days after the first kernel commit (`a3cc8c7`)
delivered two thirds of a phase that was budgeted at 8–10 weeks.

## M0 — One roadmap, one rule *(size M)* — **complete**

Six WPs, all landed, gate required in CI. The decision is
[ADR-010](adr/ADR-010-one-roadmap-one-rule.md).

| WP | What | Sha |
|---|---|---|
| M0.1 | Archive frozen, blob-hash checked | `5f756c6` |
| M0.2 | `DETERMINISM.md` — the rules, each with how it is enforced | `26b9b1e` |
| M0.3 | `check_docs.py` + `--self-test`, in CI | `9b7d26c` |
| M0.4 | Dead links to zero | `2daa884` |
| M0.5a/b/c | `Verified:` stamps across the living set | `e1f6194` `e2f08bd` `9f518c2` |
| M0.6 | Entry points, and the docs gate made **required** | `3a09677` |

## M1 — The graph is the game *(size L)* — the paper's central claim

- `[x]` **M1.0 UI document ordering.** *(S)* — `e042415`.
  **Done when:** `UIWorld.DocumentsAtOneSortOrderKeepTheirOrderAcrossAReload`.

- `[x]` **M1.1a The one state expansion, and nothing else.** *(M)* — `79f3369`.
  Layout only, no behaviour: `Fighter` gains `res[kMaxResources]` and loses the
  dead `meter`; it gains M1.3's `reaction`/`bounces`/`flags` and `GameState`
  gains M3.1's event ring, both reserved now so the format changes once.
  **Done when:** the `static_assert`s in `GameState.h`, and one re-golden.

- `[x]` **M1.1b The data path onto the fields M1.1a reserved.** *(M)* —
  `a177ac2` `9af12c0` `01bab86` `dbd07a9`.
  `ResourceDef` per slot; `effect[]` and `guard[]` on moves; `walk_speed` out of
  `Simulate.cpp`'s constants and into `FighterData`.
  **Done when:** `P3Resources.MeterGainsOnHitAndSpendsOnGuard`,
  `.AGuardedCancelRefusesBelowTheMinimum`, `.IndexOrderIsTheFilesOrder`,
  `P3Movement.WalkSpeedComesFromTheFile`. All four exist and pass.
  **Two carve-outs, both still true:** cancel-EDGE effects and guards were
  counted and deliberately not built — all 51 authored edge guards restate their
  target move's own guard, and `cancel.effect` is authored zero times, so
  `CancelEdge` stayed 16 bytes and a builder warning guards the one direction
  that could go wrong. And the re-golden this WP predicted never happened: walk
  speed did not move the golden, because the crossplat match authors none.

- `[ ]` **M1.1c Attack selection is (button × stance), not a button per move.**
  *(S–M)* — (a)(b)(d) landed on master (`a891f55`, `fa46343`); (c) did not.
  Six buttons bound once each, `Down` bound, HUD showing one row per stance
  variant.
  **THIS WP'S CENTRAL CLAIM IS CURRENTLY FALSE AND THAT IS WHY IT IS STILL
  OPEN.** It reads "every normal `fighter_a` authors is reachable". It is not:
  `MatchBuilder` never assigns `MoveDef::stance`, so `StepAttack` takes the first
  slot whose button matches and **12 of the 18 normals — every `crouch_*` and
  `air_*` — are unreachable.** A built `fighter_a` emits about twelve "can never
  start" build warnings that nothing reads.
  **And its Done-when test proves the property at the one layer where it is not
  broken.** `P3Attacks.OneButtonPicksTheMoveForTheStanceYouAreIn` assigns
  `stance` **by hand** on a synthetic `FighterData` and never calls
  `BuildMatchData`. The kernel honours stance; the bridge drops it; the test only
  ever asked the kernel. Any re-test must go through `BuildMatchData`.
  **Done when:** the above, through the builder; plus
  `P3Attacks.TwoMovesOnOneButtonWithOverlappingStancesIsRefused` for part (c),
  which is not written — the shipped
  `…OverlappingStancesShadowTheHigherSlot` asserts today's behaviour instead.
  **Blocked on M1.3e**, which is the same wire.

- `[x]` **M1.1d Input edges and buffering — the second state expansion.** *(M)* —
  `8795a46`.
  A press is a rising edge; release starts a special that opted in
  (`MoveDef::negativeEdge`, off by default); holding is reserved for mechanics
  that do not exist yet. A buffered press **triggers and consumes** a cancel, so
  a two-frame link is performable by a human.
  **Done when:** the seven `P3Input` tests. All exist and pass.

- `[ ]` **M1.1e The buffer window as an authored character field.** *(S)*
  `FighterData::inputBufferFrames` exists and the kernel honours it; nothing sets
  it from a file, so every caller that wants buffering assigns it directly.
  Needs [ADR-011](adr/ADR-011-mechanics-are-fields.md)'s five parts, and it
  touches `schema.v2.json`, which the published prover reads.
  **Done when:** a file authoring `input_buffer_frames` produces a `FighterData`
  carrying it; a file authoring none produces zero; out of range is a load error
  naming the key.

- `[ ]` **M1.1f The juggle wiring, and the mirror that waits on it.** *(S)*
  `MatchBuilder` sets neither `FighterData::juggleMax` nor `MoveDef::juggleCost`,
  so the budget gate in `Combat.cpp` — which refuses a hit that would overspend,
  and whose comment says its absence "let 33 of them run forever" — has never
  fired for a built character. **Both fields or neither:** a budget with no cost
  never depletes, a cost with no budget refuses every hit. Then `Fighter::juggle`
  becomes the mirror of the slot the file calls juggle.
  **Needs the author:** `tests/test_gap_extent.cpp` exists partly to quantify the
  absence of this wire. Sequence it with M1.4, which rewrites that file as
  properties.
  **Done when:** a built `fighter_a` spends juggle on the moves that author it,
  the budget refuses the overspending hit, and the gap-extent file asserts the
  new relationship rather than the old count.

- `[x]` **M1.1g The stage constants are exported.** *(S)* — `595aa71`.
  `kStageHalfWidthSub` and `kMaxSeparationSub` are public; `Simulate` clamps
  against the first, `FightView` derives its framing from the second, and
  `test_determinism_crossplat` derives its expected half-width instead of writing
  480 down again. Widening the stage is now one line and a deliberate re-golden.
  **This WP also shipped a kernel RULE it was not scoped for, and that is a debt.**
  `kMaxSeparationSub` (374 px) is the invisible wall: two fighters may never be
  further apart, and the limit follows the chaser. It is a **hard-coded kernel
  constant no character or stage file can set**, which the "Never" list in
  `CLAUDE.md` forbids. It moved the cross-toolchain golden for behaviour
  (`0xF2001926` → `0xD6F0F687`). It belongs in stage data with M1.2's corner, and
  until it is there it is an exception this file is naming rather than hiding.
  **Done when (the debt):** the separation limit is authored, not constant.

- `[~]` **M1.2 Push boxes and the corner.** *(S–M)* Claude, 2026-08-20.
  Body separation between fighters and the stage edge as a wall; resolution order
  per NORTHSTAR Phase 2: pushbox separation → strikes.
  **Done when:** `P3Pushbox.FightersNeverOverlapAfterSeparation`,
  `.TheCornerIsAWallOnBothSides`, `.SeparationIsAnExactMirror`. **All three exist
  and pass**, plus `.AnAirborneFighterPassesOverAGroundedOne` and
  `.TheCornerStopsTheBodyRatherThanTheOrigin`.
  **Still open:** `engine.constants.default_pushbox_sub` is authored and unread —
  it is in MUGEN's **Y-DOWN** convention, which `fighter_a.json` warns about at
  length, and reading it without the flip buries a body sixty pixels underground.
  The separation limit above belongs here too.

- `[~]` **M1.3d The bridge carries the mechanics the kernel already has.** *(M)*
  Claude, 2026-08-20.
  The kernel implements impact freeze, knockdown, knockback, chip, scaling, trade
  priority and guard height; the files author all of them; `MatchBuilder`
  populates ten `MoveDef` fields and leaves the rest at zero. Two layers: some
  fields are loaded and not copied, and `engine.reaction` / `engine.constants`
  are authored blocks the loader largely ignores.
  **Landed:** `pushbackHit` (a hit visibly knocks the dummy back);
  `engine.reaction` read into `CharacterData`; `knockdownTicks`; a crouching body
  (`FighterData::crouchHurtbox`, `fighter_a` authors 34 px against a 60 px
  stand); and a knocked-down fighter is invulnerable until they get up.
  **Held back on measurement, not caution:** `hitstop` is loaded and not carried.
  It freezes BOTH fighters, so every frame-exact prediction in
  `tests/test_gap_extent.cpp` moves by the freeze duration — 120 of 121 cycles
  fell short, six to nine timing mismatches each. Section 3's account must learn
  hitstop first.
  **Done when:** its four named `P3Reactions` tests — none of which is written.
  What landed is proved by `MatchBridgeMechanics.*` and `P2Crouch.*` and
  `P2Knockdown.AFighterOnTheFloorCannotBeHit` instead. **Rewrite the Done-when to
  name the tests that exist, or write the ones it names; do not leave both.**

- `[ ]` **M1.3e Stance reaches the kernel, and the drivers establish it.** *(M)*
  The ten-line wire that fixes M1.1c, plus the two driver rules it needs. Both
  rules are proved and written down:
  **a stance lands on the same tick as the press** — `stepFighter` sets
  `airborne` on the jump and `crouching` from Down, and `StepAttack` runs after
  both in one `Simulate` call, so `Up+button` starts an aerial off the ground and
  no derived trace has to script a jump; and **the release is of the button, not
  the posture** — dropping the direction on a release tick drops the stance, so a
  buffered press consumed on a silent tick is refused and every loop repeats one
  tick late.
  With both, four of five broken tests go green. **The fifth is the headline:**
  `test_gap_extent` goes 97 → 77 (see § Where this stands), and section 3's
  timing account still cannot predict a cycle that leaves the ground.
  **THIRD ATTEMPT (2026-08-21), CARRIED TO THE MEASUREMENT AND REVERTED ON A
  DESIGN HOLE NOBODY HAD NAMED.** Everything below was built and run; the tree
  is green without it. What worked: the wire, the ledger row to `Exact`, the
  shipped-file bridge tests, `BuildDemonstration` establishing stance AND
  re-pressing after a stall (two ticks of patience, mirroring the drivers), the
  four driver `holds_` vectors, and the certificate-twin restatements — with the
  wire in, `test_game_core` (38/38), `test_ground_truth`, `test_one_frame` (8/8)
  and `test_training_mode` all pass, telling the CLOSED-GAP story: the air
  self-cancel runs ~4 reps per jump (hit ticks 6,17,28,39 / landing gap with the
  defender FREE / repeat), the demonstration performs its turns ACROSS jumps,
  and no single string beats `maxHits` any more.
  **What stopped it: COMMITMENT KILLED EVERY CROSS-POSTURE CANCEL, and 120 of
  121 cycles collapsed.** A gatling like `stand_mp → crouch_hp` is ordinary in
  the genre — hold Down, the cancel takes you into the crouch — but the
  commitment rule freezes `crouching` while a move runs, `StanceAllows` reads
  the frozen posture, and the cancel is refused. **The fix is a semantics
  change:** selection must read the INPUT (is Down held?), and the posture must
  FOLLOW THE MOVE — starting a crouching move makes you crouching, whatever you
  were. That keeps commitment (no walking, no jumping, no posture change from
  input alone) while letting cancels change posture the way every fighting game
  does. Then the sweep must be re-measured a third time, and section 3's
  account must learn the jump before its two-route predictions mean anything
  (97 of 121 disagreed).
  **A near-miss worth the line:** the first driver edit asserted AFTER mutating
  its string but BEFORE writing the file, so `holds_` was declared and read but
  never filled — an empty-vector index that crashed all seven gap-extent tests
  with an access violation. An edit script must write before it asserts
  anything about a later block.
  **Four traps, verified in `tests/test_one_frame.cpp` and all four latent:**
  line 1691 mashes a bare `bindings[0].button`, which makes
  `EXPECT_TRUE(mashed.defenderActedTicks.empty())` unfalsifiable; line 774 pulses
  the whole input so the posture drops on odd ticks; folding the direction into
  `buttons_` breaks `Usable()`'s zero-button check (line 628) and `Observe`'s
  release predicate (line 660); and `kP0X`'s "holds an attack button and no
  direction" becomes false.
  **Do with M1.4a**, and publish the number they agree on.
  **Done when:** `MatchBuilder` assigns `MoveDef::stance` and `blockedAs`;
  `MatchBridgeMechanics.ADirectionEstablishesTheStanceOnTheTickItIsPressed`;
  M1.1c's claim re-tested **through `BuildMatchData`**; and the suite green with
  the counts re-derived.

- `[ ]` **M1.4a Gate the combo graph on move state.** *(M)*
  `usableEdges` filters cancel edges by `Contact::Block`/`Whiff` and the prover's
  dead-cancel list, and by nothing else. Neither the enumeration nor section 3
  ever asks what state the fighter is in.
  **Prerequisite from M1.3e's third attempt: posture follows the move.**
  Selection reads the input; starting a move sets the fighter's posture to the
  move's stance; commitment forbids input-driven posture change only. Without
  this, every cross-posture cancel is refused and the graph has nothing to gate.
  **The gating predicate:** B's stance must be reachable from A's END state.
  Ground → air is free, even mid-move. Air → ground needs a **landing**, which no
  cancel window covers — `Fighter::airborne` is cleared by POSITION alone, so an
  aerial that ends in the air leaves the fighter airborne. Air → air is free
  while the arc lasts and impossible after it.
  **Three corrections the predicate must respect, each verified:**
  `StanceAllows` reads `f.airborne` RAW, not `AirborneNow` — so a move's
  `airborne_from_tick` makes it count as an aerial *attack* and does not let an
  air move start out of it; **a fighter is COMMITTED while a move runs** — no
  walking, no jumping, no posture change (`P2Commitment.*`, 2026-08-21), so a
  launcher cancel into an aerial needs the jump BEFORE the source move or an
  authored motion (M1.3(b)), not a mid-move takeoff; and a crouching move
  **cannot start on the tick you land**, because `crouching` is computed before
  the landing clamp.
  **Done when:** the enumeration refuses a hop whose stance is unreachable from
  its source's end state; the air self-loop is bounded by the arc rather than by
  juggle; and the graph's count equals what the kernel produces with stance
  wired.

- `[ ]` **M1.3 Mechanics, pass 1 — the ones the showcase needs.** *(M–L)* Each
  with [ADR-011](adr/ADR-011-mechanics-are-fields.md)'s five parts:
  (a) **contact mask** on `CancelEdge` — `hit | block | whiff` replacing the
  collapsed `onHit`, so kara and whiff cancels are expressible;
  (b) **movement is a move** — jump, dash, backdash as authored moves with a
  `movement` field; the kernel's hard-coded jump is deleted; a jump cancel is an
  ordinary cancel edge. Commitment is now the KERNEL DEFAULT (`P2Commitment.*`)
  and the jump is BALLISTIC (`P2Ballistic.*`) — the arc is decided at takeoff
  and neither an attack nor a held direction recomputes it. This is where the
  authored exceptions arrive: a lunge that carries the fighter, a hop kick that
  leaves the ground mid-move, a divekick that changes trajectory mid-arc;
  (c) **counter-hit** — per-move `counter_hit {hitstun_bonus, damage_bonus}`.
  **This is a soundness qualifier on every verdict, not just a mechanic** — see
  § Where this stands. Three ways out: qualify the verdict ("TERMINATING under
  neutral hit"), take the worst case over hit types, or one verdict per hit type.
  **Not chosen; it changes what the tool claims.**
  (d) **wall bounce / wall splat / launch vector** as per-hit `on_hit` reactions
  using the fields M1.1a reserved. `air_hitstun_ticks` is already loaded and
  waiting for a launcher to put someone in the air.
  Everything defaults off; `fighter_a` unpatched must hash as before.
  **Done when:** `P3Cancels.AKaraCancelFiresOnWhiffInsideItsWindow`,
  `P3Movement.AJumpIsAMoveAndAJumpCancelIsAnEdge`,
  `P3Reactions.CounterHitAddsTheAuthoredStun`,
  `P3Reactions.AWallBounceReturnsTheDefenderIntoRange`; a ledger row each; schema
  v3 with the fields appended.

- `[ ]` **M1.4 The kernel search, and the ground truth as the gate.** *(M)*
  Promote the cancel-graph walk out of `tests/test_gap_extent.cpp` into `CseGame`
  as **`ComboSearch`**: a bounded search over macro-actions executed on the real
  kernel, de-duplicated by `Checksum()`, with a budget; hitting the budget
  reports **UNRESOLVED**, never a verdict. One implementation for tests, cooker,
  showcase and panel. Rewrite `test_ground_truth.cpp` and `test_gap_extent.cpp`
  as properties rather than counts — which is also what makes M1.1f and the
  hitstop half of M1.3d landable.

- `[ ]` **M1.5 Character hot reload.** *(S–M)* A frame-data edit lands in a
  running match; NORTHSTAR property (c)'s last clause.

- `[ ]` **M1.6 The showcase: one fighter, many patches, a replay per verdict.**
  *(M)* Variants as JSON merge patches under
  `Games/UntitledFighter/Assets/Characters/fighter_a/variants/`; <!-- docs-ok: this WP creates it -->
  the eleven
  patches and what each shows are ADR-011 §4.
  **Carry in one finding:** the witness-driving cursor exists in **five** copies —
  `BuildDemonstration` plus a `Driver` in each of `test_ground_truth`,
  `test_game_core`, `test_gap_extent` and `test_one_frame`. **Promote
  `test_gap_extent`'s**, and not for style: it is the only copy that checks
  whether the move has started BEFORE spending its release tick. The others are
  safe only because they release one tick after an advance, when nothing can be
  buffered.

- `[ ]` **M1.7 Authoring telemetry.** *(S)* What the author actually needed to
  know, recorded while authoring rather than reconstructed after.

- `[ ]` **M1.8 Housekeeping.** *(S)* The deferred small things; add to it rather
  than folding them into unrelated WPs.

## M2 — Two people, one match *(size L)* — ARCHITECTURE Phase 4

- `[ ]` **M2.1 Transport — spike, then an ADR.**
- `[ ]` **M2.2 Handshake** — hash the loaded POD arrays, never canonicalised text.
- `[ ]` **M2.3 Desync = abort + artifact**, naming the first divergent tick **and
  field** (needs the reflection table).
- `[ ]` **M2.4 The session owns the tick count.**
- `[ ]` **M2.5 VERSUS, and one presentation for three modes.**
- `[ ]` **M2.6 Play == Player, as a hash test.**

## M3 — Skinned fighters, frame-indexed *(size L)* — placeholders, not art

- `[ ]` **M3.1 The event queue, before the first sound** — uses the ring M1.1a
  reserved.
- `[ ]` **M3.2 Frame-indexed clip player + skinning (engine).**
- `[ ]` **M3.3 The Mixamo pipeline.**
- `[ ]` **M3.4 The presentation reconciler** — pose is a pure function of sim
  state; tails always cancelable.
- `[ ]` **M3.5 Feel and stage.**
- `[ ]` **M3.6 Roster and select.**

## M4 — Showcase and publish; then art *(size M + content)*

- `[ ]` **M4.1 The reel.**
- `[ ]` **M4.2 Paper artefacts.**
- `[ ]` **M4.3 Publish the claims** — nothing outruns a test in CI.
- `[ ]` **M4.4 Real art**, last, through a pipeline that already exists.

## Engine maintenance — done inside the milestones, not as a phase

E1–E8 are recorded in [ADR-010 §3.3](adr/ADR-010-one-roadmap-one-rule.md). The
live one is **E4**: promote a thing into `CseGame` the third time it is copied.
M1.6's five witness cursors are the outstanding instance.

## Review points — what the author checks, with their own eyes

A test answers *"is it correct"*. These answer *"is it right"*, which only a
person can. Each says what to run, what should happen, and — the part that
matters — **what would mean it is wrong**. A review point is not a gate.

Executables land in `out/build/<preset>/build/bin/<Config>/`. The editor's Game
view and the shipped Player enter the *same* mode. Assets stage from
`Games/UntitledFighter/Assets/` — edit the source copy.

### R0 — Available now

| | |
|---|---|
| **Run** | `Editor.exe`, Game view, or `Player.exe`. |
| **Do** | Move and attack. Toggle the box overlay. Pause, frame-step through a hit. Open the **Combo Prover** panel, load `fighter_a.json`, press **Demonstrate**. |
| **Should** | Boxes track the fighters; frame step advances exactly one tick; the panel prints a verdict; Demonstrate plays the printed loop frame-perfectly. |
| **Wrong if** | Frame step advances more than one tick, or the demonstration drops a link — either means the mode is deciding tick counts rather than the session ([DETERMINISM.md](DETERMINISM.md) T1). |

**R0 has earned itself three times.** It found that attacks were labelled wrong
(M1.1c), that holding a button rapid-fired it (M1.1d), and that the training
dummy does not react to being hit (M1.3d). No test caught any of them.

### R0b — After M1.1d: a press is a press

| | |
|---|---|
| **Do** | Hold one attack button for several seconds. Then mash it. Then press during another move's recovery, slightly early. |
| **Should** | Holding gives **one** attack. Mashing gives one per press. |
| **Wrong if** | Holding still repeats — the mode is feeding edges instead of levels. Or a **normal** comes out when you release: release is for specials that opt in, and no shipped normal does. |

The early-press half needs an authored buffer window (M1.1e); until then a press
during recovery is correctly forgotten.

### R0c — After M1.3d: the dummy reacts, and the floor is a ruler

| | |
|---|---|
| **Do** | Press **V** for midscreen. Hit the dummy and watch it slide; count the squares. Press **V** back to the corner and hit it there. Hold **Down** and look at its body. Walk into it. Walk it into a corner. Jump over it. Sweep it with `crouch_hk`. |
| **Should** | Midscreen: every hit carries it back, further on heavies. Squares are 20 px and every fifth line is one **reach unit** — so a move authored `reach: 0.42` reaches four squares and a bit. Corner: it does not move, and the HUD says the verdict on screen is about *this* position. Crouching: the body is visibly shorter, 34 px against 60. Walking into it: blocked, and neither of you inside the other. Its **body** stops at the wall, not its middle. |
| **Wrong if** | It slides in the corner (the clamp is not holding). A light knocks it as far as a heavy (pushback is not per-move). Crouching changes nothing. Or **the sweep does not knock it down and turn the box blue** — which is the known M1.3e bug: `crouch_hk` cannot be selected at all, so nothing authored on it can happen. |

### R1–R9

R1 after M1.1b (the file beats a constant) · R2 after M1.2 (the corner is real) ·
R3 after M1.3 (every mechanic is a field) · R4 after M1.4 (the two provers,
honestly labelled) · R5 after M1.5 (the authoring loop) · **R6 after M1.6 — the
showcase, the one to judge the project on** · R7 after M2.5 (two people, one
match) · R8 after M3.4/M3.5 (it looks like a fighting game) · R9 after M4.1 (the
reel).

## Not scheduled, on purpose

Reasons and come-back triggers are in
[ADR-010 §3.4](adr/ADR-010-one-roadmap-one-rule.md); this is the list, so nobody
re-proposes them by accident.

- The trigger expression language (Phase 5) — typed schema nouns instead.
- `SimId`, our own snapshot ring, our own input ring — GekkoNet owns them.
- Projectile pool — until a shipped character has one.
- Asset mounts (ADR-007) — after M2, on its triggers.
- Engine install/export (G4) — after M2.
- Cook/pack pipeline, Lua hardening, Jolt determinism, renderer features beyond
  skinning — ARCHITECTURE §2 conditions.
- **A required CI job that HANGS rather than fails.** The Linux `apt-get` step
  once sat for 2h05m. Fixed at the source with a step timeout, retries and
  `DEBIAN_FRONTEND=noninteractive`. The *class* is not closed — only one step is
  bounded — so it comes back if a second step ever hangs, and then every step
  gets a deadline.

## How to update this file

- Change a WP's box and, for `[x]`, append the commit sha; for `[-]`, the reason.
- Move the "Now" row when a WP starts; there is never more than one, unless a
  second is blocked on a decision and that is stated.
- A WP that grows a decision gets an ADR (Proposed, with a recommended default)
  and a one-line pointer here; a WP that splits stays under its number.
- **Do not add prose about *why* here — that is the ADR's and the commit
  message's job.** This file grew to 1698 lines by keeping a post-mortem of every
  attempt, all of which git already held. A WP entry is: what, its Done-when, and
  any trap that would cost someone a day. Findings that outlive a WP belong in
  the manual page they are about.
- Do not add status anywhere else — that is this file's job.
- Bump `Verified:` at the top when you have re-read the whole file against the
  tree, not when you edit one line.
