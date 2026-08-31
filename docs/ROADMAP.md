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
| **Every verdict is demonstrable as a replay** | `tests/test_ground_truth.cpp` executes the prover's own printed witness; since M1.3e the demonstration presses directions, establishes stances and performs its turns across jumps; since M1.3g one `WitnessCursor` performs every witness in the repository | Only one character's witness is executed. |
| **The game is the file** | `MatchBuilder`'s loss ledger, checked move-by-move in `tests/test_match_bridge.cpp`; stance and guard height carried since M1.3e (`EveryAuthoredNormalIsReachableThroughItsButtonAndStance`); juggle since M1.1f, hitstop since M1.3i | Priority, chip and scaling are still authored and dropped. |

**The headline, measured three times (third: 2026-08-30, M1.3e).**

`tests/test_gap_extent.cpp` drives all **121** usable cycles of `fighter_a`
through the real kernel. **97** ran forever while an aerial was startable from
the ground (the bridge dropped stance, so `stand_hk` shadowed `crouch_hk` and
12 of 18 normals could not be performed at all); **77** with the stance wire
alone; **ZERO** with the genre's movement rules enforced — commitment, the
ballistic jump, posture-following-the-move, stance on both start routes. Every
cycle passes through `air_mp`, entering an aerial costs a real jump, and the
landing hands the defender their turn: the prover says TERMINATING, and the
executed game agrees on all 121 — **for the game's own reason.** The model
charges juggle; the kernel runs out of air (the arc holds exactly the 4
repetitions the budget permits — `GroundTruthGap.TheArcEndsEveryStringAtThe
CountTheModelChargesToJuggle`) -- and since M1.1f the budget is wired too,
so the arc and the juggle gate refuse the fifth aerial IN AGREEMENT: the
kernel now terminates the string for the model's own reason as well as its
own.

**And since M1.4 the pair of numbers is measured by execution** (ADR-013's
`ComboSearch`, a bounded search over macro-actions on the real kernel):
`fighter_a`'s **model worst case is 21 hits; the executed worst case is 7**
(`air_lp air_mp air_lp air_mp stand_lp stand_mp crouch_hk` — a jump-in air
chain that rides the arc down, lands inside the last hit's stun, gatlings and
ends in the sweep), so the sound half is loose by 14 hits on this character
and **the bound held** — `GapExtentSearch.TheExecutedWorstCaseIsInsideThe
Models` is the sentence as a test. The authored infinite is FOUND the same
way: `fighter_a_infinite` comes back INFINITE with a witness the test replays
(`tests/test_combo_search.cpp`). 2026-08-31.

**And one qualifier the write-up does not yet carry.** `counter_hit` appears zero
times in `schema.v2.json`. The model reads one `hitstun` per move, so a
TERMINATING verdict says nothing about the same string opened with a counter hit,
and `air_hitstun_ticks` — authored on every move and differing from ground
hitstun on all of them — has the same shape. Three ways out are in M1.3(c); none
is chosen, because it changes what the tool claims.

**The shortest credible path to the claim**, reordered 2026-08-21 under
[ADR-012](adr/ADR-012-the-tick-is-a-pipeline.md) after complexity itself became
the risk, and now WALKED END TO END: **M1.3f** (the tick is a pipeline of
pure stages — golden held), **M1.3e** (posture follows the move + the stance
wire — the third measurement above), **M1.3g** (ONE `WitnessCursor`, five
copies deleted) and **M1.4a + M1.4** (`ComboSearch` runs the real kernel,
section 3's parallel model is deleted, and the paper's pair — model 21 /
executed 7 — is printed by CI). Next: **M1.6**, the showcase. Everything else
is done or serves it.

## Now

| In flight | Owner | Since |
|---|---|---|
| M1.6 — the showcase; slices 1–5 landed, rest blocked on walk macros + M1.3 | Claude | 2026-08-31 |

M1.6 stays open, blocked on a stated design step (walk/wait macros amend
ADR-013) and on M1.3's mechanics. M1.1e landed inside its slice 5 — the
buffer pair is a catalogue row.

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

- `[x]` **M1.1c Attack selection is (button × stance), not a button per move.**
  *(S–M)* — (a)(b)(d) `a891f55` `fa46343`; the central claim proved with M1.3e.
  Six buttons bound once each, `Down` bound, HUD showing one row per stance
  variant. The claim — "every normal `fighter_a` authors is reachable" — was
  FALSE until the stance wire, and its first test proved the property at the
  one layer where it was not broken (a synthetic `FighterData`, never
  `BuildMatchData`). It is now proved at the right layer:
  `MatchBridgeMechanics.EveryAuthoredNormalIsReachableThroughItsButtonAndStance`
  drives all 18 normals end to end through `BuildMatchData` and the real
  kernel, each asked for the way a player asks.
  **Residue:** part (c)'s `TwoMovesOnOneButtonWithOverlappingStancesIsRefused`
  is still unwritten — the builder warns rather than refuses a genuinely
  shadowed binding. (The warning itself learned with M1.3e that stance
  disambiguates, and stays silent where Down or the takeoff tells a pair
  apart.)

- `[x]` **M1.1d Input edges and buffering — the second state expansion.** *(M)* —
  `8795a46`.
  A press is a rising edge; release starts a special that opted in
  (`MoveDef::negativeEdge`, off by default); holding is reserved for mechanics
  that do not exist yet. A buffered press **triggers and consumes** a cancel, so
  a two-frame link is performable by a human.
  **Done when:** the seven `P3Input` tests. All exist and pass.

- `[x]` **M1.1e The modern input buffer, authored.** *(S–M)* — `83555d6`.
  Asked for 2026-08-21 in the author's words: *"a 3 frame buffer
  that makes a tightly timed link much easier"* — adversarially reviewed then,
  landed now. `input_buffer_frames` is in `schema.v2.json` (character-global,
  the genre note says why; the window arithmetic N = N+1 ticks documented
  where the field is defined), loaded with a HARD 255 refusal naming the key
  (the kernel ages the buffer in a uint8; past 255 the age wraps and the
  buffer is eternal), carried whole by `MatchBuilder` with an Exact ledger
  row. The Done-when's link test is
  `P3Input.AOneFrameLinkNeedsTheWindowAHumanCannotHitAlone`: a press two
  ticks early and HELD — what a human does — never edges the one tick the
  link needs at window 0, and is consumed on exactly that tick at window 2.
  The catalogue got the feature as a row: the `one_frame_link` /
  `one_frame_link_buffered` variant pair differs by the single field and
  flips the search's verdict TERMINATING → INFINITE on the same one-tick
  link, both invisible to the model (no restart route, no buffer vocabulary
  — deliberate, `edgeUsable` already assumes the ideal player).
  **Checklist residue, named:** (e) proceeded under clear-on-any-start (a
  cancel consumes an unrelated buffered press) — the simple default, the
  author may prefer clear-only-when-used; (f) negative edge is NOT buffered —
  documented as the current rule, extension is its own decision; (g) the
  SHIPPED base does not author the field, so no hash moved — the variants
  carry it, and `test_gap_extent`'s post-build override is reconciled by its
  own comment (synthesised cycles, not a shipped file).
  **Done when (met):** a file authoring `input_buffer_frames` produces a
  `FighterData` carrying it; none produces zero; out of range is a load error
  naming the key (`InputBuffer.TheWindowIsCarriedAbsentIsZeroAndPast255Is
  RefusedByName`); and the one-frame link test passes with a 2-frame window
  and fails at zero.

- `[x]` **M1.1f The juggle wiring, and the mirror that waits on it.** *(S)*
  — `79fd8d7`. Landed once M1.4's property rewrite unblocked it: both
  halves together — `FighterData::juggleMax` from the resource the file calls
  `juggle` (found BY NAME; the positional contract fixes order, not meaning)
  and `MoveDef::juggleCost` mirrored from each spending move's authored delta
  — so the budget gate that had never fired for a built character now REFUSES
  the overspending hit, with an Exact `resource.juggle (gate)` ledger row
  (zero for Kung Fu Girl, and the zero is right: her transcript disables
  MUGEN's juggle system and the resource is declared only for the positional
  contract).
  **The wire moved no measured number, and that is the finding:** on
  `fighter_a` the ballistic arc and the juggle budget both stop the aerial
  string at four, so ground_truth's count-agreement became an agreement of
  REASON — the kernel now terminates for the certificate's own mechanism as
  well as its own. The nine "juggle is still unwired" claims across docs and
  test prose were swept in the same commit.
  **Done when (met):** a built `fighter_a` spends juggle on the moves that
  author it, the budget refuses the overspending hit
  (`MatchBridgeMechanics.TheJuggleBudgetReachesTheKernelAndRefusesThe
  OverspendingHit`, failing-first), and the gap-extent file asserts the
  relationship — its census and headline speak the doubly-enforced bound —
  rather than the old count.

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
  **The remainder became M1.3i:** `hitstop` was loaded and deliberately not
  carried — it freezes BOTH fighters, so every frame-exact prediction in
  `tests/test_gap_extent.cpp` moved by the freeze duration (120 of 121 cycles
  fell short, six to nine timing mismatches each). The objection died when M1.4
  made the counts properties, and M1.3i carried the wire.

- `[x]` **M1.3e Stance reaches the kernel, and the drivers establish it.** *(M)*
  — `5db85a6` `47ba2b2`. Landed in two steps, on the fourth attempt, with the
  third attempt's map (its full text lives in this entry's git history; the
  four test-harness traps it named were each avoided and are pinned in code
  comments where they lived).
  **Step 1 — the semantics the third attempt died for**
  ([ADR-012](adr/ADR-012-the-tick-is-a-pipeline.md) rule 3):
  commitment froze input-driven posture, `StanceAllows` read the frozen
  posture, and every cross-posture gatling (`stand_mp → crouch_hp`, Down held)
  was refused — 120 of 121 cycles collapsed when stance first landed. Now
  SELECTION READS THE INPUT (is Down held now; `Intent::crouchWish`, and
  FindCancel derives the same bit) and POSTURE FOLLOWS THE MOVE (`adoptStance`,
  `crouching`'s second authorized writer: a crouching move crouches the body
  its frame data was authored against, a standing move stands it up, a move
  stating no posture imposes none). Proved test-first in `P2Stance.*`: the
  gatling was refused before, takes with Down held after, and the two pins —
  no Down means no crouching follow-up; an any-stance chain keeps its posture
  — never moved. Deliberately invisible to shipped files until step 2.
  **Step 2 — the wire, the drivers, and the third measurement.** `MatchBuilder`
  maps `stance` and `blockedAs` BY NAME (the two enum families order their
  values differently; a bare cast ships crouching normals as air moves), both
  ledger rows read `Exact`, and the shadowed-binding warning learned that
  stance disambiguates. The five witness cursors and both mash policies gained
  the two driver rules the third attempt proved — **the stance hold is pressed
  with the button and kept through releases** (the release is of the button,
  not the posture) **and a stall re-presses after two waiting ticks**, now
  also in `BuildDemonstration`, whose seam test kept all copies honest.
  `test_gap_extent` re-derived end to end: **97 → 77 → 0 of 121** (§ Where
  this stands), its own tripwire `static_assert` rewritten from
  `kEscapable < kUsableCycles` to `==` — the file demanding the route be named
  if a cycle ever stops leaking. Section 3's tick-exact predictions were
  DELETED rather than taught the jump (ADR-012 rule 4; M1.4 deletes the rest);
  `ground_truth` section 5 inverted into the count-agreement finding; the
  training-mode "certified-away cycle outruns maxHits" alarm inverted into
  earned quiet; the replay format's RLE-win claim demoted to the human-input
  case it was chosen for.
  **No re-golden:** the crossplat match runs no moves, so the predicted
  deliberate re-golden never happened — recorded like M1.1b's.
  **Done when (met):** `MatchBuilder` assigns `MoveDef::stance` and
  `blockedAs`; `MatchBridgeMechanics.ADirectionEstablishesTheStanceOnTheTickIt
  IsPressed`; M1.1c's claim re-tested **through `BuildMatchData`**
  (`EveryAuthoredNormalIsReachableThroughItsButtonAndStance`, all 18); the
  suite green with the counts re-derived.

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

- `[~]` **M1.3h The host delivers every press.** *(S–M)* Claude, 2026-08-31. From the buffer
  review's host strand: the kernel buffer cannot fix what never arrives, and
  it did not always arrive — `readPad_` level-read the pad only on the fixed
  steps a tick actually ran, so a tap made and released between run ticks
  (slow motion at divisor 8 sampled the pad at 7.5 Hz; pause sampled it never;
  above 60 fps most render frames run zero ticks) vanished before the kernel
  saw it, and at the engine `glfwGetKey`'s cached state missed any
  press+release inside one `glfwPollEvents`.
  **Built, in three composable parts.** `GLFW_STICKY_KEYS` is set at the one
  window-creation site (`Window.h`), so a sub-poll tap reads `GLFW_PRESS`
  once — its two residual limits (first-poll-wins when one key feeds several
  polls; two taps in one poll interval collapse to one) are recorded at the
  call, and neither is reachable by a headless test. `InputMap`'s existing
  press latch already survives zero-tick frames (the `awaitingTick` rule).
  And `PressAccumulator` (CseGame, beside `LatchedInputSource` — the pure
  half with the hardware removed) Notes the `consumePressed` edges on EVERY
  fixed step and Spends them into the next run tick's level bits **before**
  the latch, so replay, rollback and the checksum read the same bytes the
  simulation did. Pending presses deliberately do not age: between run ticks
  game time is not passing (pause is the divisor made infinite), so a tap
  made while paused comes out on the frame-step that follows — which is what
  stepping is for. The mode's `notePadPresses_`/`Spend` glue mirrors the rule
  pinned in `tests/test_press_delivery.cpp`, the one test that links Engine
  AND the game libraries so the claim is measured from `InputMap` through the
  latch into the kernel, not modelled; the mode itself still cannot be
  constructed headlessly (Application owns a real window), which that file
  records. ARCHITECTURE's input section had already specified this producer
  design; DETERMINISM N4 is now structural + tested, its "never networked"
  half vacuous until M2.4's CseNet exists.
  **Done when (met):** a one-tick synthetic tap reaches the kernel at any
  render rate the harness can simulate
  (`ATapInsideZeroTickFramesIsDeliveredWhenTheNextTickRuns`), slow motion
  latches taps across non-run ticks
  (`ATapBetweenSlowMotionRunTicksIsDeliveredOnTheNextRunTick`, plus the
  paused/frame-step and replay-identity legs), and the sticky-keys gap is
  closed with its platform limits recorded at the `glfwSetInputMode` call.

- `[x]` **M1.3g One witness cursor.** *(S–M)* — `bcd47fb`. ADR-012
  rule 4, pulled forward from M1.6 because the fifth copy drifted this week and
  the seam test caught it. Built: `WitnessCursor` in `CseGame` — an immutable
  table (slot/button/stance-hold per entry, read off the BUILT `MoveDef`) plus
  a value `State` and a pure `Step(state, observed) → {state', advanced,
  wrapped}` — with a thin stateful `WitnessDriver` over it. All five copies
  deleted; `BuildDemonstration` and the four test drivers sit on the same
  step; the ordering promoted is gap_extent's start-before-release (the one
  buffering requires), which is trace-identical for the unbuffered users. The
  masher's stance-hold lookups consolidated onto `WitnessCursor::StanceHold`.
  **Done when:** the seam test compares `BuildDemonstration` against the SAME
  function it uses ✓; every M1.3e driver trap has exactly one home
  (`WitnessCursor.h`'s header essay) ✓; and net lines are negative — measured
  **−185 net (−570 deleted, +271 for the one home)** against the entry's
  original estimate of −300, which had charged the five copies' deletion but
  not the shared home's documentation. The docs are a third of the new file
  and previously existed as five drifting paraphrases; the estimate is
  rewritten to the measured number rather than the mass trimmed to meet it.

- `[x]` **M1.4a Gate the combo graph on move state.** *(M)* — `db9c3c5`
  `c58bede`. Resolved BY EXECUTION with M1.4, not by the gating calculus this entry first sketched.
  [ADR-013](adr/ADR-013-verdicts-by-execution.md) rejects teaching the
  enumeration a stance-reachability predicate as a third implementation of
  kernel rules (the predicate's own draft here contradicted the commitment
  rule within one paragraph, which is the disease); whether a hop is reachable
  is answered by PERFORMING it — `ComboSearch` and the driven sweep, where the
  kernel is the only model of the kernel. The enumeration keeps answering what
  the AUTHORED graph contains, which is a fact about the file.
  **Done when (re-expressed and met):** the air self-loop is bounded by the
  arc rather than by juggle
  (`GroundTruthGap.TheArcEndsEveryStringAtTheCountTheModelChargesToJuggle`);
  no unreachable hop contributes a verdict — verdicts come from execution
  (`GapExtentKernel.ZeroOfThe121RunForever`, `GapExtentSearch.*`); and the
  graph's performable count equals what the kernel produces: zero unescapable,
  measured on both sides.

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

- `[x]` **M1.4 The kernel search, and the ground truth as the gate.** *(M)*
  — `db9c3c5` `c58bede`.
  **`ComboSearch` in `CseGame`** ([ADR-013](adr/ADR-013-verdicts-by-execution.md)):
  a bounded depth-first search over macro-actions performed the way a player
  performs them (a one-entry `WitnessCursor` each), executed on the real
  kernel, de-duplicated by a masked `Checksum()` (tick, the never-read RNG,
  healths and round fields out; everything else in, so a repeated key is an
  INDUCTION); a budget only ever reports **UNRESOLVED**, never a verdict. One
  implementation; the cooker, showcase and panel consume it when M1.6 wires
  them. The parallel model is gone: `test_gap_extent` section 3 is a build
  census only (which windows MatchBuilder resolved), its two-route timing
  account deleted rather than taught the jump, and its section 7 prints the
  paper's pair — model 21 / executed 7, bound held.
  **Done when:** `tests/test_combo_search.cpp`'s four verdicts (the authored
  infinite FOUND and its witness REPLAYED; the safe character exhausted;
  a small budget UNRESOLVED; the result bit-identical twice);
  `GapExtentSearch.TheExecutedWorstCaseIsInsideTheModels`; and no test asserts
  a frame the kernel did not produce.

- `[x]` **M1.3i Hitstop crosses the bridge.** *(S)* — `e843193`. The remainder M1.3d split
  out. `MatchBuilder` carries the authored `hitstop` into `MoveDef::hitstop`
  with a `move.hitstop` ledger row (Exact; 22 on fighter_a, 0 on Kung Fu Girl
  whose converted file authors no freeze), ResolveHits imposes it on BOTH
  fighters, and the buffer's freeze-tap behaviour (M1.3f's placement rule) is
  exercised with the real authored 8 rather than a synthetic value. Sequencing
  after M1.4 paid off as predicted: no gap-extent count was re-derived by
  hand — the frame-domain properties all held, and only wall-clock
  SCAFFOLDING moved (harness budgets gained a freeze term; the air loop's hit
  period is now 23 = the 11-tick cancel + 12 of freeze). Two real findings in
  the fallout, both fixed in the same change: `FightHud`'s
  `TicksUntilActionable` was EARLY during every freeze — the worse direction
  for a readout — and now adds the pause after its max-and-floor (the pause
  stalls every clock the terms read, and FrameAdvantage is unmoved because
  both fighters carry the same pending freeze); and the `one_frame_link`
  twins' fixed coin re-flipped — the freeze shifts a masher's re-press phase
  against a one-tick link — so both twins zero `hitstop_ticks` on the link
  move and their descriptions say why the freeze must sit out.
  **Done when:** a bridge test proves the authored freeze reaches both fighters
  and a tap inside it still buffers
  (`TheAuthoredFreezeReachesBothFightersAndATapInsideItBuffers`); no gap-extent
  count is re-derived by hand.

- `[ ]` **M1.5 Character hot reload.** *(S–M)* A frame-data edit lands in a
  running match; NORTHSTAR property (c)'s last clause.

- `[~]` **M1.6 The showcase: one fighter, many patches, a replay per verdict.**
  *(M)* Claude, 2026-08-31. Variants under
  `Games/UntitledFighter/Assets/Characters/fighter_a/variants/`; the eleven
  patches and what each shows are ADR-011 §4. (The cursor-copies finding this
  entry used to carry landed early as M1.3g.)
  **Slice 1 landed — the mechanism and the first two exhibits.**
  `LoadCharacterVariant` (CseData): a required one-line `description`, RFC 7386
  at the top level, and `patch.moves` as an OBJECT keyed by move id — merge
  patch treats arrays as atomic, so a standard patch touching one move would
  restate all of them and the exhibit would stop being the diff; an id the
  base does not author is refused rather than silently inert. Two exhibits,
  each a verdict PAIR in `tests/test_variants.cpp`:
  `hitstun_plus_7` — one number on one move hands the GAME an infinite the
  MODEL cannot see (the restart route is not in the prover's graph; the
  ledger's `starters` row names the blindness; ComboSearch finds the loop by
  performing it); `dead_air_window` — an authored cancel that can never
  connect, NAMED by MatchBuilder's window resolution (the prover has no window
  model — its dead list must not move, and the test pins that too).
  **Slice 2:** `decay_linear` — ADR-011's `decay-off` row realised as its
  inverse (the base already authors none), and the measurement flipped the
  exhibit's caption twice: the base file's own engine note predicted the
  certificate's LOSS (measured on the 73-edge two-aerial file) and the
  six-aerial remeasurement says it SURVIVES while the model's worst case
  collapses to 5 — BELOW the 7 the untouched kernel performs. One model-only
  field breaks the soundness bound without touching the game; the stale asset
  note was corrected against the measurement in the same commit.
  **Slice 3:** `meter_loop` — the resource ranking story executed on BOTH
  sides, and the loader grew `patch.cancels: {append: [...]}` for its return
  edge (append is the one array operation the showcase needs; anything else is
  refused with the atomic-array reason). Three diffs — stand_hp's hit builds a
  bar, the super gains the return cancel, and the super gains `reach` because
  the base authors none and the kernel builds a reachless move NO hitbox (the
  `move.reach (absent)` ledger row, discovered when the first probe watched
  the beam whiff). Gain equals spend, so the prover prints INFINITE and
  ComboSearch performs the same loop forever — the meter guard exercised every
  turn (meter oscillating 300↔200, measured), the super reached mid-string
  through its cancel under a chord binding the press scan correctly shadows.
  **Slice 4 measured two rows out of the catalogue (2026-08-31):**
  `pushback-0` has nothing to show on this data — the executed worst case is
  7 from the corner AND 7 from an in-reach midscreen opening (the pushbacks
  are ~5 px against 30–68 px reaches, too small to end a string early), so
  the row waits for a character whose pushback matters rather than being
  authored to exhibit nothing. And from the REALISTIC ±100 px opening the
  search measures 0 hits — nothing reaches and no macro walks — which is the
  measured form of ADR-011 §5's requirement: `microwalk` and every midscreen
  claim need walk/wait macro-actions in ComboSearch first (an ADR-013
  amendment, not a patch).
  **Still open:** walk/wait macros, then `microwalk`; `base` as a catalogue
  row; `jump-cancel`, `kara`, `counter-hit`, `wallbounce` are blocked on
  M1.3; and the cooker recording — both verdicts, replays, `graph.dot` per
  entry — is the "replay per verdict" half.

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
| **Wrong if** | It slides in the corner (the clamp is not holding). A light knocks it as far as a heavy (pushback is not per-move). Crouching changes nothing. Or **Down+HK does not knock it down and turn the box blue** — `crouch_hk` is selectable since M1.3e (Down decides the variant), so a sweep that does nothing means the stance wire regressed. |

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
