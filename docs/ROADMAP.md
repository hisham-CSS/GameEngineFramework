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

**The shortest credible path to the claim**, reordered 2026-08-21 under
[ADR-012](adr/ADR-012-the-tick-is-a-pipeline.md) after complexity itself became
the risk: **M1.3f** (the tick becomes a pipeline of pure stages, golden-locked —
same bytes, fewer write sites), then **M1.3e** (posture follows the move + the
stance wire, one deliberate re-golden), then **M1.3g** (ONE witness cursor in
`CseGame`, five copies deleted), then **M1.4a + M1.4** (`ComboSearch` runs the
real kernel and section 3's parallel model is deleted — the new headline number
falls out of that), then M1.6. Everything else is done or serves these.

## Now

| In flight | Owner | Since |
|---|---|---|
| M1.3e — stance reaches the kernel, posture follows the move | Claude | 2026-08-30 |

One at a time. The next unblocked WP is always the top `[ ]` in milestone
order — which, under the 2026-08-21 reorder, is the sequence named above.

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

- `[ ]` **M1.1e The modern input buffer, authored.** *(S–M)* Asked for
  2026-08-21: *"holding inputs for a few frames before consumption so things
  like one frame links become easier ... a 3 frame buffer that makes a tightly
  timed link much easier"* — and adversarially reviewed the same day, six
  agents, every claim refuted-or-confirmed against the code. **The mechanism is
  in and mostly right:** reversal buffering out of hitstun works (fires on the
  first actionable tick, zero added delay), wake-up from knockdown works,
  rollback replays buffering identically by construction, overwrite is
  last-wins, and buffering is INVISIBLE to the prover — `edgeUsable` already
  assumes first-frame cancels, so a buffer only makes the kernel match the
  model's ideal player. What remains is authoring and the verified checklist:
  (a) `input_buffer_frames` in `schema.v2.json` (character-global matches the
  genre — SF6/GGST use a game-global 4–5f; the real exceptions are
  per-SITUATION, wakeup/jump/tech windows, which no per-move field expresses
  either — say so in the schema note), the load with an A-assertion, the
  `MatchBuilder` copy, a ledger row;
  (b) **window N accepts N+1 ticks** — the "3-frame feel" is
  `input_buffer_frames: 2`; document it where the field is defined;
  (c) **loader bound ≤ 255**: `bufferAge` is uint8 against an int32 window — a
  window past 255 wraps the age and the buffer becomes eternal;
  (d) fixed 2026-08-21: a direction tap no longer clobbers a buffered attack
  (`P3Input.ADirectionTapDoesNotClobberABufferedReversal`) — capture masks to
  the union of move buttons;
  (e) **a decision, the author's:** a cancel fired by a HELD button consumes an
  unrelated buffered press (`FindCancel` clears on any edge). Clear-on-any-start
  is simple and slightly lossy; clear-only-when-used keeps a buffered link
  alive through an unrelated cancel. Genre feel, not mechanics;
  (f) **negative edge is never buffered** — releases get 1 tick where presses
  get N+1, inverted relative to why negative edge exists; extend or document;
  (g) authoring the field **moves the replay/handshake content hash** for that
  file (by design — it is character data) and `test_ground_truth`'s counts
  re-derive; `test_gap_extent`'s post-build override then measures a number the
  shipped file does not use — reconcile, do not leave both.
  **Done when:** a file authoring `input_buffer_frames` produces a `FighterData`
  carrying it; none produces zero; out of range (or > 255) is a load error
  naming the key; and a one-frame link test passes with a 2-frame window and
  fails at zero.

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

- `[x]` **M1.2 Push boxes and the corner.** *(S–M)* — `9bbd0db` `fd99827`.
  Body separation between fighters and the stage edge as a wall; resolution order
  per NORTHSTAR Phase 2: pushbox separation → strikes.
  **Done when (met):** `P3Pushbox.FightersNeverOverlapAfterSeparation`,
  `.TheCornerIsAWallOnBothSides`, `.SeparationIsAnExactMirror`, plus
  `.AnAirborneFighterPassesOverAGroundedOne` and
  `.TheCornerStopsTheBodyRatherThanTheOrigin` — all five in CI.
  **Debt, joined to M1.1g's:** `engine.constants.default_pushbox_sub` is authored
  and unread — it is in MUGEN's **Y-DOWN** convention, which `fighter_a.json`
  warns about at length, and reading it without the flip buries a body sixty
  pixels underground. It is settled by the same "authored, not constant" line as
  the separation limit (M1.1g's debt), not by a wire of its own.

- `[x]` **M1.3d The bridge carries the mechanics the kernel already has.** *(M)*
  — `06c77be` `7e0b642` `984ffbe` `ec022d2` `fd99827`.
  The kernel implements impact freeze, knockdown, knockback, chip, scaling, trade
  priority and guard height; the files author all of them; `MatchBuilder`
  populates ten `MoveDef` fields and leaves the rest at zero.
  **Done when (rewritten to the tests that exist, then met):**
  `MatchBridgeMechanics.*` (pushback and knockdown end-to-end through
  `BuildMatchData`), `P2Crouch.*` (the 34 px crouching body against the 60 px
  stand), and `P2Knockdown.AFighterOnTheFloorCannotBeHit` — all in CI. The four
  `P3Reactions` names the first draft promised were never written; this Done-when
  replaces them rather than leaving both.
  **The remainder is M1.3i:** `hitstop` is loaded and deliberately not carried —
  it freezes BOTH fighters, so every frame-exact prediction in
  `tests/test_gap_extent.cpp` moves by the freeze duration (120 of 121 cycles
  fell short, six to nine timing mismatches each). That objection dies when M1.4
  deletes section 3's parallel account, so the wire waits for it.

- `[~]` **M1.3e Stance reaches the kernel, and the drivers establish it.** *(M)*
  Claude, 2026-08-30.
  The ten-line wire that fixes M1.1c, plus the two driver rules it needs. Both
  rules are proved and written down:
  **a stance lands on the same tick as the press** — `StepPhysics` sets
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

- `[x]` **M1.3f The tick becomes a pipeline, byte for byte.** *(M)* — `019b7a8`. [ADR-012](adr/ADR-012-the-tick-is-a-pipeline.md) rules 1–2, built:
  `ReadIntent` (pure) → `StepPhysics` → `StepAttack` → `Resolve`; the fields the
  audit existed for (`crouching` 4 sites, `airborne` 5, `facing` 6 — the week's
  three bugs were write-order bugs on them) now have ONE writing stage each, and
  the audit table lives in `Simulate.h`.
  **GOLDEN-LOCKED, and it held:** `test_determinism_crossplat` passes without a
  re-golden, and the frame-exact suite (`test_gap_extent`'s 121 cycles,
  `test_ground_truth`, `test_one_frame`, `test_game_core`) is untouched — the
  one honest proof a pure restructure has.
  **The one sanctioned behaviour change is the freeze-tap fix** from the buffer
  review: hitstop's early-return ate a press-and-release made entirely inside
  the freeze (the modern tap-confirm) while doing two right things by accident
  (buffer age paused; a held press read as a fresh edge on thaw). Recording
  landed in **`LatchInputs`, the head of Resolve**, not in ReadIntent as first
  planned — capture must run after StepAttack, because "the buffer was spent"
  is derived from `moveFrame == 0` rather than written by StepAttack (which
  would be a second writer), and an interrupt would otherwise resurrect a spent
  press; ReadIntent stays pure. On frozen ticks it records with aging suspended
  and the `prevButtons` latch withheld. The golden cannot police any of this
  (no moves, no hitstop), so three `P3Input` freeze tests pin it: the
  tap-confirm buffers (failed before the change), a buffer outlives a freeze
  longer than its window, and a release inside the freeze still fires the
  negative edge.
  **Done when (met):** stages with fixed signatures; the `Simulate.h` audit —
  one stage per field, imposed-fact exceptions listed; crossplat green without
  re-golden; the freeze tests; kernel files did not grow (739 and 800 lines,
  from 739 and 800).

- `[ ]` **M1.3h The host delivers every press.** *(S–M)* From the buffer
  review's host strand, all verified with lines: the kernel buffer cannot fix
  what never arrives, and today it does not always arrive.
  `readPad_` level-reads a mask refreshed once per RENDER frame, so above 60 fps
  a tap released before the next tick-running frame **vanishes** — no edge ever
  reaches the kernel; below 60 fps one frame's sample is replicated across every
  catch-up tick (two taps in a stalled frame collapse into one hold); and under
  slow motion at divisor 8 the pad is effectively sampled at **7.5 Hz**, in the
  training tool whose whole point is practising links slowly. Deeper, at the
  engine: `glfwGetKey` cached state misses any press+release inside one
  `glfwPollEvents`, and `GLFW_STICKY_KEYS` is set nowhere.
  **The fix shape is already in the engine:** `Action::latched` +
  `consumePressed` accumulate-and-serve-once. The mode accumulates
  `pendingPressBits_` from `consumePressed` per fixed tick and ORs them into the
  next latched session input — determinism intact, because the bits flow through
  `LatchedInputSource` like every other input. The GLFW blindness needs the
  engine-side sticky/callback fix and is out of the mode's reach.
  **Done when:** a one-tick synthetic tap reaches the kernel at any render rate
  the harness can simulate, slow motion latches taps across non-run ticks, and
  the sticky-keys gap is closed or explicitly recorded as a platform limit.

- `[ ]` **M1.3g One witness cursor.** *(S–M)* ADR-012 rule 4, pulled forward
  from M1.6 because the fifth copy drifted this week and the seam test caught
  it: a pure step `(cursorState, observed) → (bits, cursorState')` in
  `CseGame`, used by `BuildDemonstration` and all four test drivers; the copies
  are deleted, and the two driver rules (the posture rides through a release;
  re-press after two waiting ticks) live in exactly one place.
  **Done when:** the seam test compares `BuildDemonstration` against the SAME
  function it uses, net lines are negative by at least 300, and every M1.3e
  driver trap has exactly one home.

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

- `[ ]` **M1.3i Hitstop crosses the bridge.** *(S)* The remainder M1.3d split
  out. `MatchBuilder` copies the authored `hitstop` (it is loaded and held back
  today, with the reason in code), the freeze is visible end-to-end, and the
  buffer's freeze-tap behaviour (M1.3f's placement rule) is exercised with a
  real authored value rather than a synthetic one. **Sequenced after M1.4**
  because carrying it earlier shifts every frame-exact count in
  `tests/test_gap_extent.cpp`; once those counts are properties, the objection
  is gone.
  **Done when:** a bridge test proves the authored freeze reaches both fighters
  and a tap inside it still buffers; no gap-extent count is re-derived by hand.

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
