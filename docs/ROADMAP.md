# ROADMAP — the one place status lives

Verified: 2026-09-02 @ 0e2d423

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
loaded with different frame data, must show different verdicts** (the cooked
thirteen-row catalogue; NORTHSTAR names the exhibits), because every
mechanic is an opt-in field on a move, never a rule in the kernel
([ADR-011](adr/ADR-011-mechanics-are-fields.md)). Visuals are a pure function of
frame data; return-to-idle tails are always cancelable. Everything is provable
and showcased **before** any real art is made; placeholder rigs (generated
through Blender, [ADR-019](adr/ADR-019-placeholders-through-blender.md)) come
first — ahead of the netcode link, by the author's
[ADR-020](adr/ADR-020-the-bounded-lift.md) (accepted 2026-09-02), which also
lets one modeled body precede M4 — and SF6-tier art last, only after the
showcase already sells the paper without it. Details, tests and proofs of the four properties:
[NORTHSTAR.md](NORTHSTAR.md).

## Where this stands against the paper

The paper's claim has four load-bearing parts. This is what each rests on today.

| The claim needs | Evidence in CI today | What is missing |
|---|---|---|
| **A deterministic simulation** | `tests/test_kernel.cpp` T1/T2; `tests/test_determinism_crossplat.cpp` T3, re-checked by gcc 13 on the Linux leg | Nothing. This one is done and has been re-goldened three times for stated reasons. |
| **The prover reads the shipped files** | `tests/test_character_data.cpp` load assertions; the editor's Combo Prover panel | Nothing structural. `counter_hit` and the air numbers are authored, simulated and answered per opening since the openings wave (ADR-015); what remains is the cooker's executed pair, still singular neutral-corner until a bench can produce the other openings. |
| **Every verdict is demonstrable as a replay** | `tests/test_ground_truth.cpp` executes the prover's own printed witness; since M1.3e the demonstration presses directions, establishes stances and performs its turns across jumps; since M1.3g one `WitnessCursor` performs every witness in the repository | Only one character's witness is executed. |
| **The game is the file** | `MatchBuilder`'s loss ledger, checked move-by-move in `tests/test_match_bridge.cpp`; stance and guard height carried since M1.3e (`EveryAuthoredNormalIsReachableThroughItsButtonAndStance`); juggle since M1.1f, hitstop since M1.3i | Priority, chip and scaling are still authored and dropped. Blockstun was on that list without a ledger row to say so until M3.0b carried it (found by M3.4a's pose test, 2026-09-02, and closed the same day). |

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

**And the qualifier landed as a vocabulary.** Since the openings wave
([ADR-015](adr/ADR-015-what-a-verdict-claims-about-hitstun.md) Accepted,
option 3, enacted 2026-09-01) **the tool answers one verdict per opening** —
neutral, counter, air — with the legacy surface staying the neutral answer
verbatim. The re-derived pair, per the tests: the base pair stands at **model
21 / executed 7**; the **counter opening** charges the authored bonus (on the
`counter_hit` exhibit it revives one dead cancel, 88 → 89 usable, and holds
the 21 bound); the **air opening** reads each move's authored
`air_hitstun_ticks`, simulated since (d); and the **jump_cancel** exhibit's
executed worst case is **13** against the model's held 21 — the bound
stretched from the other side, through an edge the model prices dead.

**The shortest credible path to the claim**, reordered 2026-08-21 under
[ADR-012](adr/ADR-012-the-tick-is-a-pipeline.md) after complexity itself became
the risk, and now WALKED END TO END: **M1.3f** (the tick is a pipeline of
pure stages — golden held), **M1.3e** (posture follows the move + the stance
wire — the third measurement above), **M1.3g** (ONE `WitnessCursor`, five
copies deleted) and **M1.4a + M1.4** (`ComboSearch` runs the real kernel,
section 3's parallel model is deleted, and the paper's pair — model 21 /
executed 7 — is printed by CI). The showcase (**M1.6**) closed 2026-09-01 at
thirteen rows; the consolidation (**M1.9**) closed the wave. Next is **M3**, from
M3.4a, under [ADR-020](adr/ADR-020-the-bounded-lift.md) (accepted
2026-09-02); **M2** follows M3.5a, and M3.1, M3.5b and M3.6 follow M2.

## Now

| In flight | Owner | Since |
|---|---|---|
| M3.4e Overlay modes for validation over the mesh | Claude | 2026-09-03 |

The openings wave is landed end to end: per-opening prover surface → (c) →
(d) → (b3) — the golden re-record (b3) was expected to need DISSOLVED under
ADR-018's opt-in design, so no re-record review was ever pending — then the
four exhibits with the cursor/search jump teaching, then M1.9 (the
consolidation). M1.3, M1.6 and M1.9 are closed above; M1.1e landed inside
slice 5 — the buffer pair is a catalogue row. What remains in M1 is
human-gated only: the R6 second look at thirteen rows, and PR #7's merge.

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

- `[x]` **M1.3h The host delivers every press.** *(S–M)* — `2cb599a`. From the buffer
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

- `[x]` **M1.3 Mechanics, pass 1 — the ones the showcase needs.** *(M–L)* Claude,
  2026-08-31 → 2026-09-01, all six letters landed: (a) `b9b2978` · (b1)
  `975e529` · (b2) `a70ee00` · (b3) `2e50e1a` (per ADR-018) · (c) `c92901d`
  (with the (c-pre) surface `c7e007f`) · (d) `2f25e71` + `73ed46d`.
  Sliced (a) first: the contact mask is the smallest of the four,
  and (c) counter-hit is entered only behind its own ADR — the entry itself
  says the choice changes what the tool claims. Each
  with [ADR-011](adr/ADR-011-mechanics-are-fields.md)'s five parts:
  (a) **contact mask** — LANDED, `b9b2978`. `CancelEdge::contactMask` replaces the collapsed `onHit`:
  hit=1 / block=2 / whiff=4, 0 staying UNGATED (`on: always` and every
  hand-built bench), with `hit` keeping the collapse's byte so fighter_a's
  all-hit MatchData hash did not move. The attacker OBSERVES the outcome: a
  blocked mirror of `alreadyHitBits` in `Fighter::flags`' low byte (a
  reserved M1.3 field — layout untouched, crossplat golden untouched),
  written in ResolveHits' blocked arm, cleared at the four
  `alreadyHitBits` clear sites. `on: hit` no longer chains off a blocked
  contact (the free block-confirm the collapse permitted), `on: block` is
  the authorable block-confirm, and a kara is expressible: whiff edges
  anchor `delay` at frame 0 (nothing to count contact from; `always` keeps
  its shipped startup anchor — 89 corpus edges, zero of which want earlier
  frames). `cancel.on` reads Exact; the PROVER keeps its own collapse
  ({hit,always} usable), so a whiff edge the kernel honours is an edge the
  model's graph skips — the D8 gap the kara showcase variant will
  demonstrate (blocked on M1.6's variant slice; note the kara's CLOSE frame
  needs the source's `cancel_window_ticks`, which clamps all edges from
  that source — acceptable for a dedicated exhibit source). No schema
  change: `on` has authored all four values since v1; the kernel caught up
  to the file. `P3Cancels.*` (5) pin it, all four mask tests red against
  the collapsed gate first;
  (b) **movement is a move** — staged by
  [ADR-014](adr/ADR-014-movement-lands-in-three-steps.md) after a consumer map
  measured the blast radius of arriving at ADR-011's destination all at once
  (the level→edge flip re-means every Up bit in every baked trace and golden;
  the cursor's stance-hold and the search's air reachability need re-teaching;
  fighter_a's jump cancels are encoded in edge delays). **Step (b1) LANDED, `975e529`:** `engine.movement { jump_impulse_sub, gravity_sub }`
  — kernel semantics at the boundary (+Y up, positive, explicit zero refused
  as the kernel's unauthored sentinel), parsed, carried into the
  `FighterData` slots the kernel has consulted since M1.1b (no layout
  change), `character.movement` Exact ledger row. Base fighter_a does NOT
  author it (the M1.1e precedent: hash, 38-tick arc and every measured count
  stay put); the `floaty_jump` variant does — fighter_a's own
  MUGEN-provenance 11 px/tick, finally flown: 86 airborne ticks, and the
  aerial string's termination now rests on the wired juggle budget ALONE
  (`AFloatyJumpHandsTheStringToTheBudgetAlone`), the M1.1f agreement
  deliberately broken apart as an exhibit.
  **Step (b2) LANDED, `a70ee00`:** the one batched MoveDef
  growth (164 → 284 bytes, one re-hash per ADR-005 §3): `MoveDef::motion` —
  up to 8 resolved velocity keys, the InvincibilityWindow pattern — carried
  from the already-parsed `engine.motion` with the one MUGEN-Y-down flip at
  load, sorted, teleport `pos_add` components split into their own
  KernelOmits row; the SAME growth reserves (c) counter-hit and (d)
  launch/reaction bytes, zeroed and unread, so their semantics stay unchosen
  while the wire pays once. In the kernel the active key owns a committed
  fighter's velocity (facing by branch, never multiply), an upward key
  leaves the ground, gravity skips while a key owns the arc. `P3Movement`'s
  lunge / hop kick / divekick / silence quartet pins it (three red before
  the physics branch existed), and
  `TheAuthoredMotionKeysCrossWithTheirOneSignFlip` measures the flip on
  fighter_a's own uppercut. fighter_a's two motion-authoring specials now
  genuinely move — the file honoured — and nothing measured shifted (no
  test binds them); ledger 31 → 32 rows, `move.engine.motion` KernelOmits →
  Exact.
  **Step (b3) LANDED, per [ADR-018](adr/ADR-018-the-jump-move-is-opt-in.md)
  (the amendment to ADR-014's frozen plan): the jump move is OPT-IN, and the
  hard-coded jump is demoted — not deleted — to the unauthored fallback**,
  the walk-speed doctrine. `FighterData::jumpMoveSlot` (tail-appended, the
  binding bound to exactly Up; two refused by name) gates off the level jump,
  so the Up press reaches StepAttack grounded and the jump starts on its
  EDGE: held Up does not re-jump on landing, the double jump refuses itself
  through StanceAllows, prejump is the move's own startup frames, the
  launching key with no authored X takes the direction held on the takeoff
  tick, and a jump cancel is an ordinary on-hit edge into the jump move.
  **The golden re-record DISSOLVED**: the crossplat script's kNoMoves authors
  no jump move, so its Up bits keep their recorded meaning and the
  cross-toolchain evidence survives — the fallback it pins still exists and
  is still the path it exercises. Base fighter_a does not opt in (hash, arc
  counts, census all stand); the jump-cancel/kara variant is where the jump
  move first bites, with the cursor/search taught alongside it
  (`P3Movement.AJumpIsAMoveAndAJumpCancelIsAnEdge`,
  `TheUpBoundMoveBecomesTheJumpSlotAndTwoAreRefused`). Commitment is already
  the KERNEL DEFAULT (`P2Commitment.*`) and the jump already BALLISTIC
  (`P2Ballistic.*`);
  (c) **counter-hit** — per-move `counter_hit {hitstun_bonus, damage_bonus}`.
  **This is a soundness qualifier on every verdict, not just a mechanic** — see
  § Where this stands. **UNBLOCKED 2026-09-01:
  [ADR-015](adr/ADR-015-what-a-verdict-claims-about-hitstun.md) Accepted,
  option 3 — one verdict per opening** ("the most robust; each hit qualified
  appropriately rather than swallowed"). Enactment order is the ADR's, and
  **(c-pre) — the per-opening surface — is LANDED**: `ProverOpening`
  {neutral, counter, air}, a full `ProverResult` per opening with the
  top-level fields staying the neutral mirror verbatim (every legacy assert
  passes unchanged), counter identical-by-construction until (c) authors the
  bonus (`ACounterOpeningWithNothingAuthoredIsIdenticalToNeutral`), air
  reading the file's own `air_hitstun_ticks` today — provably divergent on a
  synthetic (ground-dead, air-alive self-cancel:
  `AnAirOpeningReadsTheAuthoredAirHitstunAndUnauthoredFallsBackToNeutral`) —
  and the panel, `DescribeVerdict` and the telemetry record all speaking the
  vocabulary. The cooker's pair stays singular (neutral-corner, named) until
  the executed side can produce the other openings.
  **(c) itself LANDED**: `engine.reaction.counter_hit { hitstun_bonus,
  damage_bonus }` (negative refused; damage through the one hundredths rule),
  carried whole into the (b2)-reserved MoveDef pair, ledger row
  `move.counter_hit` (34 rows now), and ResolveHits charges both when the
  defender is caught MID-STARTUP — startup only, a trade is a trade, a punish
  is its own reward; bonus on the base stun, decay applies to the sum; damage
  bonus added before scaling so it prorates with its hit. The COUNTER
  opening's model verdict now charges the authored bonus — on every hit of
  that opening's search, first hit in the game, the Permissive direction, in
  the opening's own loss row (`counter bonus charged per hit`). Off by
  default everywhere: unpatched characters hash as before, the crossplat
  golden untouched (`P3Reactions.CounterHitAddsTheAuthoredStun`,
  `ACounterOpeningChargesTheAuthoredBonusAndNamesItsChargeRule`,
  `TheAuthoredCounterBonusCrossesAndNegativeIsRefused`).
  Next: (d) reactions land and the air opening's executed half becomes real.
  The launch MoveDef bytes stay reserved (b2), zeroed and unread; the paper's
  measured pair is re-derived per opening.
  (d) **wall bounce / wall splat / launch vector** as per-hit `on_hit` reactions
  using the fields M1.1a reserved. **(d1) LANDED — the launcher and the air
  number:** `engine.reaction.launch {vel_x_sub, vel_y_sub}` (+Y up, X a
  magnitude the kernel points away from the attacker; non-positive Y refused —
  a launch that does not rise is a knockdown, already authorable) takes the
  defender off the ground, `Fighter::reaction` marks the launched body so the
  airborne-stun rule keeps ITS arc while an un-launched air hit still drops
  straight — the first draft kept both and the crossplat golden caught it,
  ticks 1000..2000, exactly its job — and `air_hitstun_ticks`, loaded-and-
  thrown-away since the reaction block landed, is carried into the SECOND
  batched MoveDef growth (284 → 288, tail-appended: (b2)'s reservation missed
  it) and charged by ResolveHits as the base stun against an airborne
  defender. Ledger 34 → 36 (`move.air_hitstun` ×22 on fighter_a,
  `move.launch` ×0); the air opening's model rows split its
  whole-string charge by direction, alarming where air < ground
  (`ALauncherPutsTheDefenderInTheAirAndAirHitstunTakesOver`,
  `TheAuthoredAirHitstunCrossesOnEveryMoveThatAuthorsIt`).
  **(d2) LANDED — the wall gives the body back:**
  `engine.reaction.on_hit: "wall_bounce"` arms the defender
  (`Fighter::reaction`, armed-implies-launched so the arc survives stun) and
  StepPhysics' wall clamp fires and SPENDS it — velocity reversed whole, the
  spend recorded in `Fighter::bounces` (cleared on landing), the return arc an
  ordinary launch, a second wall inert without a fresh arming hit. The loop
  bound is the juggle budget's, not a bounce constant a file cannot set.
  `wall_splat` is enumerated in the schema and REFUSED at load until it is
  simulated — a key that loads and does nothing is the coin-flip trap. Ledger
  36 → 37 (`move.on_hit` ×0 everywhere shipped; the wallbounce showcase
  variant is where it first bites)
  (`AWallBounceReturnsTheDefenderIntoRange`,
  `WallBounceCrossesAndWallSplatIsRefusedByName`). **(d) is CLOSED**; M1.3's
  remaining letter is (b3).
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

- `[x]` `68f0747` **M1.5 Character hot reload.** *(S–M)* Claude, 2026-08-31. A frame-data
  edit lands in a running match; NORTHSTAR property (c)'s last clause. The
  rule is DETERMINISM.md T8 (never a swap under a live session), the authoring
  loop is the manual's hot-reload section, and ADR-016 is the decision record.
  **Done when:** a test proves an edited character file is noticed and lands in
  a running `FightSession` while a broken edit keeps the last good data
  (`AFrameDataEditLandsInARunningMatchAndABrokenEditKeepsTheLastGoodData`,
  tests/test_character_hotreload.cpp).

- `[x]` **M1.6 The showcase: one fighter, many patches, a replay per verdict.**
  *(M)* Claude, 2026-08-31 → 2026-09-01; slices 1–8 through `60de3b5`, the
  openings-wave machinery `a386c9c`, the four exhibits `9137599` — thirteen
  rows, all cooked, every caption pinned. Variants under
  `Games/UntitledFighter/Assets/Characters/fighter_a/variants/`; the rows,
  captions and bindings are `variants/catalogue.json` (ADR-011 §4 was the
  original plan). (The cursor-copies finding this
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
  authored to exhibit nothing.
  **Slice 6, `03c99bf` — the movement macros, ADR-013 decision 6.** From the realistic ±100 px opening the search used to measure ZERO
  hits — nothing reached and no macro walked. Now it walks: `WitnessCursor`
  carries walk/wait entries (absolute directions, counted ticks, replayable
  in the same demonstrations — `AWalkedWitnessReplaysAndItsFirstHitLands`),
  the search's menu adds them with two phase caps (approach 8, live-string
  2 — the microwalk IS one or two) and the approach-closes-distance rule
  that keeps a corner search from paying for midscreen's question too.
  Measured after: midscreen opens to a real walked string; the corner
  roster still EXHAUSTS (10.5M of the raised 20M-tick default) and
  fighter_a's executed worst case is **still 7 under walked links** — the
  arc and the juggle budget bind through a microwalk, which is its own
  finding; and the one_frame coin held against the wait vocabulary,
  measured, not assumed.
  **Slice 7, `42ec284` — the `microwalk` catalogue row, and the corner-push
  wire it needed.** A mid-stage walking infinite provably cannot
  state-repeat (pushback's halving sum is always odd against even walk
  quanta, and the defender drifts wallward every hit), so the genre's
  pressure-release valve had to exist first: `corner_push_vel_sub` — already
  authored at 0 on all 22 fighter_a moves, never parsed — now crosses whole
  (loader → `MoveDef::cornerPushHit` in two (b2) pad bytes, no layout
  change → ResolveHits recoils the ATTACKER when the wall already stops the
  defender, `WallLimitFor` promoted to one home for its two askers;
  `move.corner_push` Exact row, tables 32 → 33;
  `P3Movement.ACornerPushRecoilsTheAttackerOnlyAtTheWall`). On top of it,
  `microwalk.json`: recoil past a deliberately narrowed reach, the wall
  pinning the defender and the pushbox absorbing the walk's overshoot — the
  two quantizers that make the loop's state return EXACTLY once the combo
  counter saturates at 255 (probed rep by rep; the key goes constant at rep
  255). Three search rules were earned red-first getting there: walks count
  only FREE ticks (a walk issued after a connect spent itself committed);
  the per-string movement cap became per-LINK (the loop walks every rep,
  and a string cap refused it); and expansion order became a pure function
  of the node — toward-longest-first — because the dive must BE the
  canonical clamped loop or it drowns in drifting near-miss chains (both
  measured, in the slice commit). Exhibited with the link's own button
  bound alone, the full-roster drowning stated in the caption:
  `TheMicrowalkInfiniteNeedsTheWalkAndTheSearchWalksIt` — INFINITE, the
  loop CONTAINS the walks, the prover blind three ways.
  **Slice 8, `c5ca43b` — the cooker recording, and the `base` row with it:
  "a replay per verdict" is now files.** The manifest the
  catalogue never had (`variants/catalogue.json`: nine rows — base first —
  each carrying the one thing a patch cannot say, its bindings: meter_loop's
  super chord, microwalk's solo button, previously C++ literals inside
  tests). `CookCatalogue` (CseGame) cooks every row end to end: load →
  prover verdict → manifest bindings → search verdict from the derived
  corner bench → the demonstrated sequence rehearsed by BuildDemonstration
  (an INFINITE demos its LOOP — rotated until a rotation performs from
  neutral, since meter_loop opens on a cancel-only super, and excerpted to
  24 entries when the provable cycle is a drift super-cycle thousands long,
  as the microwalk's is) → recorded by ReplayRecorder → **verified
  bit-identical under ReplayVerifier before it is written** (ADR-011 §4's
  own rule) → `<name>.csrp` + `<name>.dot` (`WriteCancelGraphDot`, CseData —
  the emitter that did not exist: dead edges dashed, the loop red) +
  `catalogue.txt`. The tool is `UntitledFighterCatalogue`, title-owned
  because the never-list forbids Cooker/ touching a title — its CMakeLists
  says so in writing. All nine rows cook, exit 0, ~8 s; the pairs read
  exactly as the exhibits claim (three disagreements: hitstun_plus_7,
  one_frame_link_buffered, microwalk — each's description says why).
  `test_catalogue` cooks a two-row subset in CI and audits the artifacts
  FROM THE FILES: decode, hash-match against a rebuilt character,
  re-simulate bit-identical. Two findings fixed en route: the search's
  path-repeat scan now runs BACKWARD (the first match was the EARLIEST
  ancestor, so a wandering dive printed a mostly-approach "loop";
  backward gives the shortest enclosing cycle), and BuildDemonstration
  learned movement macros (its entry validation predated them — and its
  stall DIAGNOSTICS dereferenced a macro as a move, a crash the first full
  cook found).
  **The four openings-wave exhibits LANDED — the catalogue is 13 rows, all
  cooked, exit 0:** `counter_hit` (the per-opening verdict at work: neutral
  pair stands at base, the COUNTER opening revives one dead cancel — 88
  usable → 89 — and carries the damage bonus while the 21-hit bound holds);
  `wallbounce` (the measured NULL: the corner grants for free what the
  bounce exists to return, so the pair stands at 7/7 and the caption states
  the mechanic's midscreen scope instead of growing bench numbers);
  `jump_cancel` (the wave's headline: the model prices the hk→jump edge
  dead — jump's hitstun is 0 — and the GAME walks through it, executed
  worst case 7 → 13, the bound held from the other side at nearly twice
  the distance); `kara` (whiff edge honoured by the kernel, honestly
  invisible to BOTH instruments — the model drops whiff by doctrine, the
  search asks one connect at a time — and the caption says the shared gap
  out loud). Exhibit tests pin every number
  (`VariantExhibits.ACounterOpening…`/`AJumpCancel…`/`AWallBounce…`/`AKara…`).

- `[x]` `e3a1d45` **M1.7 Authoring telemetry.** *(S)* Claude, 2026-08-31. What the author
  actually needed to know, recorded while authoring rather than reconstructed
  after — one JSON line per real prover run. The line contract and sink live
  in the manual's Combo Prover section; ADR-017 (which restored the Done-when
  the `9c4dbd1` rewrite dropped) is the decision record. **Done when:** the
  log grows by one line per append and a test parses it back
  (`TheLogGrowsByOneAppendAndRoundTripsItsFields`,
  tests/test_prover_telemetry.cpp); the panel's run site is the mirror.

- `[x]` **M1.8 Housekeeping.** *(S)* Claude, 2026-08-31. The deferred small
  things; add to it rather than folding them into unrelated WPs. Items done:
  - `[x]` `68bdf12` `PathIsContained` refused a MISSING file under a relative
    base on MSVC (`weakly_canonical` returns a relative path for a
    nonexistent target), misreporting a typo'd filename as a containment
    refusal, differently per toolchain. Fixed in the sandbox itself
    (`AMissingFileIsReportedAsUnopenableNotRefused`); the local workarounds
    in `CharacterFileWatch` and `AuthoringTelemetry` shrank to pointers.
  - `[x]` `962df4f` Variant MOVE patches merged unknown keys silently (the
    one_frame_link coin-flip: `hitstop_ticks`, then `reaction`, both merged
    and changed nothing, costing two wrong diagnoses). The move level is a
    CLOSED 17-key set, unlike the annotation-tolerant `engine` namespace, so
    an unknown move-patch key is now a load error naming the key and listing
    what exists (`AMovePatchKeyTheLoaderDoesNotReadIsRefusedByName`).

- `[x]` `1868fa3` **M1.9 One home per current rule.** *(S–M)* Asked for by the human,
  2026-09-01: "consolidate documentation so it isn't so spread out over
  different ADRs." The ADRs stay frozen — they are why-records and the house
  rules forbid rewriting them — so the consolidation is the five-line rule
  enforced in the other direction: every rule that is CURRENTLY in force must
  have its one statement in a living doc (fighting-core.md for the title's
  mechanics and tools, DETERMINISM.md for the invariants, ARCHITECTURE.md for
  the seams), with the ADR cited only as the decision's history. Runs LAST in
  the openings wave, so the consolidated pages state the post-wave truth once
  rather than twice. **Done when:** a sweep of ADR-011..018's operative rules
  finds each stated in exactly one living doc (link, not restatement,
  everywhere else), and `check_docs.py` stays green.

## M2 — Two people, one match *(size L)* — ARCHITECTURE Phase 4 — after M3.5a ([ADR-020](adr/ADR-020-the-bounded-lift.md))

- `[ ]` **M2.1 Transport — spike, then an ADR.**
- `[ ]` **M2.2 Handshake** — hash the loaded POD arrays, never canonicalised text.
- `[ ]` **M2.3 Desync = abort + artifact**, naming the first divergent tick **and
  field** (needs the reflection table).
- `[ ]` **M2.4 The session owns the tick count.**
- `[ ]` **M2.5 VERSUS, and one presentation for three modes.**
- `[ ]` **M2.6 Play == Player, as a hash test.**

## M3 — Skinned fighters, frame-indexed *(size L)* — placeholders through Blender

The decisions are [ADR-019](adr/ADR-019-placeholders-through-blender.md) (how:
one glTF pipeline, stepped clips at exactly `startup + active + recovery`, a
manifest-pinned Rigify deform skeleton, T0 — no presentation state — an
orthographic fight camera as the one admitted camera field, Blender and its
MCP server as dev-machine tools never in CI) and
[ADR-020](adr/ADR-020-the-bounded-lift.md) (when: accepted 2026-09-02 —
M3.2–M3.5a before M2.1, one modeled body before M4). The
placeholder is a bpy-generated mannequin; the modeled shoto the human asked
for is that mannequin's mesh swap on the same skeleton (M3.3e). The list is
in recommended execution order — headless engine work first against
stdlib-generated fixtures, the training room as the first visible artifact,
the first swing at M3.4c — and every Done-when reads committed exported bytes,
never a `.blend`. M3.1, M3.5b and M3.6 sit after M2 in either ordering.

ADR-019 D10 — the licence for project-authored assets — is **deferred** by the
human (2026-09-02): only code-adjacent test fixtures under the repository's
MIT may be committed until it is answered, so M3.5a's room and M3.3b's
mannequin wait on that one-line answer; every WP before them does not.

- `[x]` `97ec6c2` **M3.0 Two ADRs and the M3 rewrite, on paper.** *(S)*
  **Done when:** `python scripts/check_docs.py` is green; ADR-019 and ADR-020
  exist with a `Status:` line in their first ten lines and a stated reversal
  condition each, and are rows in `docs/adr/README.md`; every M3 WP below
  carries a Done-when that names a test under `tests/` or a script under
  `scripts/`.

- `[x]` `1d0beb4` **M3.4a `PoseSelect`, and the presentation library spike.** *(M)*
  First because it is headless, Claude-paced and needs no Blender. `SelectPose(
  const MatchData&, const GameState&, slot)` in `CseGame` returns kinds and
  integers only — `{kind, moveSlot, frame, remaining, tick, posXSub, posYSub,
  mirror, visible}`; precedence: inactive → none; knockdown → stun (block by
  `guard`, hit by air) → move at `moveFrame` exactly → air by the sign of
  `velY` → crouch → walk by the sign of `velX` against facing → idle; `Ko`/`Win`
  only for an IDLE fighter after round end, because the training host keeps
  simulating past `kRoundOver`. Floor-div/floor-mod helpers for cycles. The
  GL-free presentation library (`FighterClips`, `FightPresentation` homes)
  is created and linked into an empty headless test in all three ctest jobs
  before anything rides on it. **Done when:**
  `PoseSelect.RestoreAndResimulateReproduceEveryPose`,
  `PoseSelect.WhileAMoveRunsTheFrameIsTheMoveFrame`,
  `PoseSelect.HitstopFreezesThePose`,
  `PoseSelect.KnockdownOutranksStunOutranksMoveOutranksFree`,
  `PoseSelect.AReleasedGuardMidBlockstunFallsBackToTheStandingBlock`,
  `PoseSelect.AFighterWhoActsAfterRoundOverIsPosedByItsAction`,
  `PoseSelect.NeverTouchesTheChecksum`, and
  `CycleFrame.FloorModKeepsANegativePositionOnTheCycle`.

- `[x]` `c09e67e` **M3.0b The bridge carries blockstun, with its ledger row.** *(S)* Found
  by M3.4a's blockstun test (2026-09-02): `fighter_a.json` authors
  `engine.reaction.blockstun_ticks` on all 22 moves, `Combat.cpp` applies
  `MoveDef::blockstun`, the loader read the key only inside the `engine.hits[]`
  records the kernel omits, and `MatchBuilder` carried neither the value nor a
  loss-ledger row — so every block in the shipped game gave zero blockstun and
  nothing said so. A loader read plus a bridge carry into an existing kernel
  slot, not a mechanic: the read and the carry in one commit, the census row,
  and the prover's projection unmoved by construction (it never reads
  blockstun, and a blocked hit is no link). It changes `MatchData` bytes, so it
  lands alone. **Done when:**
  `test_match_bridge.cpp` gains `BlockstunIsCarriedAndCountedInTheLedger` and
  the census pins the new row; `PoseSelect.AReleasedGuardMidBlockstunFallsBackToTheStandingBlock`
  is unchanged and still green — in particular its frozen-tick half, which
  drives `Simulate` with the carried blockstun and asserts `BlockstunCrouch` on
  every hitstop tick of a crouch-blocked hit; that is the one place a
  crouch → stand → crouch flicker would show, and the carry makes it reachable.

- `[x]` `e4ef6c6` **M3.2a glTF enters the static model path; stdlib fixtures.** *(S)*
  `AssetIndex::classify` learns `.gltf`/`.glb`; `collectMeshes` accumulates
  node transforms for unskinned meshes (identity stays bit-identical; bones
  stay in bind space); the two formats' UV conventions are PINNED to agree
  rather than "fixed" — Assimp's glTF2 importer flips V and `aiProcess_FlipUVs`
  flips again, landing exactly where OBJ lands, so the flag stays;
  `MaterialData` carries the authored material name; `.gitattributes` marks
  `.gltf` text and `.glb`/`.bin`/`.png` binary;
  `tests/fixtures/models/make_fixtures.py` — stdlib-only Python, data-URI
  buffers — writes this WP's committed fixtures (the child-offset quad, the
  glTF/OBJ UV pair and its 2×2 PNG); M3.2b–c extend the same writer with the
  skinned and animated ones, so no engine WP waits on Blender. **Done when:**
  `ModelDecode.AChildNodesTransformLandsItsVerticesInWorldSpace`,
  `AssetIndex.ClassifiesGltfAndGlbAsModels`,
  `ModelDecode.AGltfAndAnObjOfTheSameQuadSampleTheSameTexel`; fixtures
  committed with a README naming the generator.

- `[x]` `3d86070` **M3.3a Blender tooling of record, the pinned export, the paddle,
  `check_clips.py`.** *(S)* `Games/UntitledFighter/tools/blender/{README.md,
  common.py, export_gltf.py, make_paddle.py}`: one export function pins the
  contract (60 fps, Actions mode with every action stashed, Always Sample step
  1, Optimize Animation Size OFF, frame 0 = `moveFrame` 0 with
  `action.frame_range` explicit, deform bones only, rest-position armature, 4
  influences, glTF Separate, Draco off) and writes `<stem>.clips.json` from the
  same actions. The paddle has TWO actions (`paddle_swing` 14, `paddle_hold`
  5) so multi-action export is proven by the committed fixture.
  `scripts/check_clips.py` (stdlib) derives frame counts from accessor count
  and time span, fails if they disagree, and shares the `MoveDuration` clamp
  fixture with the C++; wired into the determinism-flags job.
  `docs/manual/art-pipeline.md` carries the install, the user-scope MCP
  registration with telemetry off, save-before-exec, the headless
  scripts-of-record rule, the banned integrations, the no-`.blend` rule and
  the per-session checklist (add-on install on 5.x, Start MCP Server, confirm
  exporter option names). Human pre-steps: install `uv`, register the server,
  answer ADR-019 D10. **Done when:** `python scripts/check_clips.py
  --self-test` is green in CI on the committed paddle, reporting exactly
  `{"paddle_swing": 14, "paddle_hold": 5}` and agreeing with its sidecar;
  `ci.yml` installs no Blender.

- `[ ]` **M3.5a The training room, generated in kernel units.** *(M)* The
  first visible artifact. `make_training_room.py` reads a committed
  `stage_dims.json` that a test pins to `kStageHalfWidthSub / 256`, and builds
  the SF-style room: floor top at `y = 0`, 20-unit cells, a heavy line every
  100 (R0c's ruler), a red centre line, blue plane squares on `z = 0`, side
  walls at exactly ±480, flat toon materials, no textures; exported to
  `Assets/UntitledFighter/Stage/` with a `CREDITS.md`; spawnable from the
  Assets panel at once. Same WP: `Assets.EveryModelHasALicenceBesideIt` over
  `.obj`, `.gltf` and `.glb`, and the unlicensed sample backpack removed or
  licensed so the test can pass. **Done when:**
  `StageAsset.TheFloorSpansExactlyTheKernelsStage`,
  `StageAsset.HeavyLinesFallOnReachUnits` (keyed on the material name M3.2a
  carries), `Assets.EveryModelHasALicenceBesideIt`,
  `AssetValidator.WarnsOnAModelDirectoryWithoutCredits`.

- `[x]` `17bb075` **M3.2b Skeleton and skin weights decode (CPU, GL-free).** *(M)*
  `Skeleton` from the `aiNode` hierarchy, `aiMesh::mBones` for offsets and
  weights only (one list per mesh); a parallel `SkinData` stream so the static
  `Vertex` stays byte-identical; `aiProcess_LimitBoneWeights` lands here, with
  its consumer; more than 128 joints refused by name. **Done when:**
  `ModelDecode.TwoBoneStripYieldsTwoNamedJointsWithNormalisedWeights`,
  `ModelDecode.TwoMeshesSharingOneSkinShareOneSkeleton`,
  `ModelDecode.AnObjDecodeCarriesNoSkeletonAndItsVertexBytesAreUnchanged`,
  `AssetValidator.RefusesARigOverThePaletteCap`.

- `[x]` `190805a` **M3.2c Clip decode on the 60 Hz grid, integer frames only.** *(M)*
  Every key time × 60/1000 within 1e-3 of an integer; every channel with more
  than one key carries exactly `N`, a single-key channel is constant (Assimp
  synthesises one for un-animated components); sample `k` is key `k`; the
  `Clip` type has no floating-point time member. **Done when:**
  `ModelDecode.AClipAuthoredAtFourteenFramesDecodesToFourteenFrames`,
  `ModelDecode.ARotationOnlyJointIsConstantNotRefused`,
  `ModelDecode.AClipWhoseKeysAreOffTheSixtyHertzGridIsRefusedNamingTheClipAndTheKey`,
  and the compile-time property that `Clip` carries no float.

- `[x]` `89e0f08` **M3.2d The frame-indexed sampler and pose bounds.** *(S)*
  `SamplePalette(skeleton, clip, uint32 frame, out)` — pure, clamped, no
  clock, no `dt`, no overload taking a floating-point frame; `PoseBounds` at
  decode from per-joint rest bounds skinned by their eight corners per frame
  (not every vertex — Debug decode on hot reload must stay fast). **Done
  when:** `ClipSampler.TheSameFrameYieldsTheSamePaletteInAnyOrder`,
  `ClipSampler.AFramePastTheEndClampsToTheLastSample`,
  `ClipSampler.TheRestPoseYieldsAnIdentityPalette`,
  `ModelDecode.TheSkinnedBoundsContainEveryVertexOfEveryClipFrame`, and the
  compile-time property that no `SamplePalette` overload accepts a double.

- `[x]` `80f9026` **M3.2e The skinned GPU path.** *(L)* A skin VBO at attributes 5 and
  6; a std140 UBO `uBones { mat4 bones[128]; }`; `#ifdef SKINNED` blocks in
  `vertex.glsl` and `shadow_depth_vert.glsl` that keep `invariant gl_Position`
  and transform normals and tangents by the skin matrix (toon bands read the
  normal); skinned variants for the forward colour and prepass programs, the
  CSM depth program and the transparent pass; nothing beyond skinning.
  **Done when:** `gl`-labelled
  `SkinnedDraw.ThePaletteMovesTheVerticesTheJointOwns`,
  `SkinnedDraw.PrepassOnAndOffDrawTheSamePixelsForASkinnedMesh`,
  `SkinnedDraw.ALitSkinnedQuadShadesByItsPosedNormal`; the existing
  `test_render_passes` probe assertions still pass.

- `[x] b18ded0` **M3.2f Submission, bounds and shadows for skinned items; the
  `SkinnedPose` component.** *(M)* A derived, never-serialized `SkinnedPose`;
  a pose identity in the `DrawItem` sort and run key so two fighters sharing
  one model never share one pose; palette upload in every geometry pass;
  culling on `PoseBounds`; `SkinnedPose` entities are dynamic casters every
  frame; a read-only Inspector section (joints, clip → frames) with a
  debug-only clip scrub in edit mode. **Done when:** `gl`-labelled
  `SkinnedDraw.TwoFightersSharingOneMeshNeverShareOnePose`,
  `SkinnedDraw.APosedLimbOutsideTheRestBoxIsStillDrawn`,
  `SkinnedDraw.AFighterAnimatingInPlaceRedrawsItsShadowCascade`; headless
  `SceneSerializer.SkinnedPoseIsDerivedAndNeverSaved`.

- `[x] 471c8c2` **M3.2g Orthographic camera mode.** *(M)* The one camera-side
  addition ADR-019 D4 admits: `CameraComponent` gains an appended projection
  mode and orthographic half-height; `Renderer`'s projection branches; CSM
  slice fitting and the culling frustum handle the orthographic view; the
  serializer appends the field; `CameraDirector` blends sanely between modes.
  **Done when:** `gl`-labelled
  `OrthoCamera.AUnitQuadCoversThePredictedPixels`; headless
  `OrthoCamera.TheProjectionMapsHalfHeightToTheViewportEdge`; existing scene
  files load unchanged.

- `[ ]` **M3.3b The mannequin and its rig.** *(M)* `make_mannequin.py`:
  Rigify human metarig → `rigify.generate` → deform bones kept, others
  removed; a proportioned primitive-built body, 60 units tall with feet at
  `y = 0`, automatic weights, influences capped at 4 before export; semantic
  names in `rig_bones.json`; `rig_manifest.json` written and enforced by the
  exporter; `Assets/Characters/fighter_a/model/fighter_a.gltf` with a 2-frame
  `idle`. Human at the viewport: review the deform-hierarchy diff and the
  weights. **Done when:**
  `PlaceholderRig.FitsThePaletteAndTheFourInfluenceCap`,
  `PlaceholderRig.StandsSixtyPixelsTallAtRest`,
  `PlaceholderRig.MatchesItsRigManifestBoneForBone`.

- `[ ]` **M3.3c Every clip generated from the frame data; four made
  legible.** *(M)* `make_move_clips.py` reads `fighter_a.json` and `poses.json`
  and keys one STEPPED pose per frame for every attack (anticipation, the
  contact pose held over the active window, recovery converging on idle) and
  every reserved cycle at the contract's lengths; `capture_pose.py` writes
  human-refined poses back; a bpy helper draws each move's authored boxes as
  wire boxes at the contact frame so a screenshot shows fit, not opinion. Day
  one is complete with three generic poses per attack; then `idle`,
  `walk_fwd`, `stand_lp`, `hitstun_stand` are made legible. **Done when:**
  `ShippedClipsMatchTheFrameData` (every move's clip == `MoveDuration`;
  knockdown == the largest `knockdownTicks`; every cycle present; root never
  translates), `ShippedClips.TheShippedModelAgreesWithItsSidecarClipForClip`,
  `ShippedClips.MutatingStandLpRecoveryMakesTheClipLengthCheckFire`, and
  `check_clips.py` on `fighter_a.json` green in CI.

- `[x] 94a8d4b` **M3.4b `engine.anim3d` lands with its consumer.** *(M)* APPENDED,
  presentation-only, off by default: character-level `engine.anim3d.model`
  (`PathIsContained`-gated) and per-move `engine.anim3d.clip`; `CseData` reads
  the sidecar with nlohmann and asserts **A21**/**A22** beside A01–A20;
  unknown `anim3d` keys refused by name; `GameModeContext` gains an appended
  `AssetManager*`; `FighterClips` builds the `(kind, moveSlot) → clip` table,
  rebuilds it in `adoptPrepared_`, polls the model and sidecar `(mtime, size)`
  in the hot-reload path, refuses a mismatch with keep-last-good and a HUD
  line. `CharacterData.h`'s not-loaded note says the new truth. **Done when:**
  `CharacterData.AClipOneFrameShortOfItsMoveIsALoadErrorNamingMoveClipExpectedAndActual`,
  `CharacterData.AnAnim3dKeyOutsideTheContractIsALoadErrorNamingTheKey`,
  `CharacterData.PresentationModelIsOptionalAndSandboxed`,
  `CharacterData.AMissingReservedCycleIsALoadErrorNamingIt`,
  `FighterClips.AReloadThatReordersMovesRebindsEveryClipByName`.

- `[x] 4ea73a2` **M3.4c The reconciler — the first swing.** *(L)* When the character
  authors a model, `adoptPrepared_` creates presentation entities (two
  fighters with `ModelComponent` + `SkinnedPose` + `Transform`, the room, an
  orthographic camera driven by `FightCamera` at priority 100) and applies a
  committed `fight_look.json` (sun, outline on, IBL, shadow distance ≥ camera
  distance + room depth, near/far); `teardownMatch_` removes them.
  `FightPresentation` composes every rendered frame: `SelectPose` →
  `FighterClips` → frame → palette; `Transform = translate(WorldPx) × yaw(180°
  when facing == 1)`; per-slot toon materials with a P1/P2 tint and a per-slot
  `z` tie-break. Draw drops the backdrop quad and 2D floor when a model
  loaded and keeps the kernel's `Hurtbox` and `ActiveHitbox` on top;
  `FightView::PhaseOf` reads `SelectPose` for the knockdown-over-stun ordering
  so that decision has one home (M3.4a left it in two). The R8 table is written
  into this file here. **Done when:** headless
  `FightPresentation.FacingLeftIsAYawWithPositiveDeterminantNeverANegativeScale`,
  `FightPresentation.TheModelMatrixTranslationIsPosOver256ForBothFacings`,
  `FightPresentation.TheSceneCameraAndTheFightCameraProjectTheFighterOriginToTheSamePixel`,
  `FightPresentation.TheFightCameraOutranksEveryCameraInTheHostScene`,
  `FightPresentation.TheBackWallIsInsideTheShadowRange`,
  `FightPresentation.EachSlotHasItsOwnToonMaterialAndZPlane`,
  `FightPresentation.ThePaletteBytesEqualSamplePaletteAtTheSelectedFrame`,
  `FightPresentation.ReconcilingAFrameLeavesTheGameStateBytesUntouched`.

- `[x] 7ea1ba6` **M3.4d P4 acceptance.** *(S)* The three tests that flip DETERMINISM
  P4 from "not yet" to enforced under T0. **Done when:**
  `Presentation.HoldsNothingARestoreCannotRebuild`,
  `Presentation.MoveStartIsNeverDelayed`,
  `Presentation.ABoxNeverMovesWithThePose`; P4's row cites all three.

- `[~]` **M3.4e Overlay modes for validation over the mesh.** *(S)* Boxes over
  mesh (default) · boxes over translucent mesh (a `MaterialOverrides` blend, an
  existing feature) · mesh only; the 2D ruler draws only with boxes on, because
  the room's grid is the ruler. **Done when:**
  `FightPresentation.OverlayModeCyclesThreeStatesAndStartsWithBoxes`.

- `[ ]` **M3.3d The remaining 33 clips made legible; the arcs pinned; poses
  fit their boxes.** *(L)* Opens with the kernel test that pins motion-key
  behaviour, committed whichever way it comes out; then batches of 4–6 clips
  per session from `poses.json`, the human refining at the viewport — the
  honest bottleneck. "Legible" is a bar: contact pose inside the live hitbox
  within 2 px; silhouette distinct at 200 px half-width. **Done when:**
  `Kernel.AMotionKeyOwnsVelocityAndSuspendsGravityUntilTheNextKeyOrTheMoveEnds`
  (or its negation),
  `ShippedClips.AContactPoseFitsItsMovesAuthoredHurtboxHeight` (`crouch_mk`
  26 px, `crouch_hk` 20 px, `crouch_hp` 36 px, +2 px),
  `ShippedClips.StunAndKnockdownClipsCoverTheLongestAuthoredCounters`,
  `ShippedClips.AWalkCycleAdvancesItsStrideInWholeTicks` (`N × walkSpeedPx ==
  stride px`); `ShippedClipsMatchTheFrameData` green after every batch.

- `[ ]` **M3.3e The modeled shoto body.** *(L — human hours)* Gated by
  [ADR-020](adr/ADR-020-the-bounded-lift.md) clause (ii) and started after
  R8 on the mannequin. A Blender-modeled body replaces the mannequin's mesh on
  the manifest-pinned skeleton: Claude drives blockout, mirroring, modifier
  stacks, UV seams, material slots and the export through bpy; the human
  sculpts, paints weights and judges the face; toon materials, textures beside
  the model, `CREDITS.md` per ADR-019 D10. Every clip survives unchanged.
  **Done when:** `PlaceholderRig.MatchesItsRigManifestBoneForBone` and
  `ShippedClipsMatchTheFrameData` pass on the new mesh with no clip edit,
  `ShippedClips.AContactPoseFitsItsMovesAuthoredHurtboxHeight` still holds,
  and R8's second look records what changed.

*After M2, in either ordering:*

- `[ ]` **M3.1 The event queue, before the first sound.** *(M)* `Simulate`
  gains the Phase parameter (DETERMINISM K11) and writes hit, block, land,
  knockdown and round-over events into the ring M1.1a reserved (layout
  unchanged); `FightSession` drains confirmed ticks, de-duplicated by
  `(tick, slot, kind)`. Because `kernel::Checksum` folds the ring, the
  crossplat golden is re-recorded ONCE in this commit on both toolchains with
  nothing else batched. **Done when:**
  `Kernel.AnEventIsWrittenTheTickItHappensAndRollsBackWithTheState`,
  `Session.OnlyConfirmedTicksAreDrainedAndARollbackNeverReplaysAnEvent`, and
  the goldens re-recorded once with the reason in the golden block.

- `[ ]` **M3.5b Feel, from drained events only; the perspective toggle.**
  *(S)* Hitstop shake as a pure function of `(tick, hitstop)`; a placeholder
  hit spark and a sound stub driven only from the confirmed-tick drain; a
  perspective toggle with the box overlay projected through the same
  view-projection. Also here: the depth-outline pass linearises depth with
  the perspective formula and is wrong under the orthographic camera M3.2g
  added (found there, not fixed there); the toggle is the WP that has both
  cameras in hand. R8 is looked at again here. **Done when:**
  `Presentation.EffectsComeOnlyFromDrainedEventsAndAResimulatedTickEmitsNone`,
  `FightPresentation.ThePerspectiveOverlayProjectsTheHurtboxCornersThroughTheSameMatrixAsTheMesh`.

- `[ ]` **M3.6 Roster and select.**

## M4 — Showcase and publish; then art *(size M + content)*

- `[ ]` **M4.1 The reel.**
- `[ ]` **M4.2 Paper artefacts.**
- `[ ]` **M4.3 Publish the claims** — nothing outruns a test in CI.
- `[ ]` **M4.4 Real art**, last, through a pipeline that already exists —
  [ADR-020](adr/ADR-020-the-bounded-lift.md) clause (ii) lets ONE body (M3.3e)
  precede it.

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

### R8 — After M3.4c: the first swing, and it looks like a fighting game

This point needs a character that authors `engine.anim3d.model` — the mannequin
(M3.3b) and its clips (M3.3c), which wait on ADR-019 D10. Until then the 3D
pass is off by default and every row below is about the code path, not a
picture; the moment a model is authored, run it.

| | |
|---|---|
| **Run** | `Editor.exe`, Game view, or `Player.exe`, with a character whose `engine.anim3d.model` points at a skinned glTF with a `<stem>.clips.json` beside it. |
| **Do** | Watch the two bodies stand, walk, jump and attack. Toggle the box overlay. Walk both into a corner; jump over the dummy; sweep it. Pause and frame-step through `stand_lp`. Resize the window to a tall and a wide shape. Hit the dummy from both sides. |
| **Should** | Two toon-shaded bodies in two tints, on two depth planes, facing each other; when one crosses over, it turns — the tint side and the silhouette flip as one, nothing looks inside-out. The Hurtbox outline sits around the body and the fist is inside the red box on exactly the active frames. The camera holds still until a fighter nears the edge, then scrolls the minimum, and stops at the walls; the box overlay and the mesh never drift apart at any window shape. Frame-stepping shows one clip frame per tick and the contact pose held across the active window. The floor shadow lands under the body. |
| **Wrong if** | A body looks mirrored rather than turned (a negative scale leaked in — `FightPresentation.FacingLeftIsAYawWithPositiveDeterminantNeverANegativeScale`). The mesh and its box outline separate as the camera scrolls or the window changes shape (the two cameras disagree — `TheSceneCameraAndTheFightCameraProjectTheFighterOriginToTheSamePixel`). A pose lags a frame behind the boxes or holds after a hit is rolled back (presentation state — ADR-019 D3). The back of the room or a fighter near the camera has no shadow (the look's ranges — `TheBackWallIsInsideTheShadowRange`). Both bodies read as one tint, or one flickers through the other where they overlap (`EachSlotHasItsOwnToonMaterialAndZPlane`). Leaving the mode leaves fighters, or a fight-lit sky, in the host scene. |

Looked at again after M3.3d (every clip legible) and M3.3e (the modeled body).

### R1–R9

R1 after M1.1b (the file beats a constant) · R2 after M1.2 (the corner is real) ·
R3 after M1.3 (every mechanic is a field) · R4 after M1.4 (the two provers,
honestly labelled) · R5 after M1.5 (the authoring loop) · **R6 after M1.6 — the
showcase, the one to judge the project on** · R7 after M2.5 (two people, one
match) · R8 after M3.4c (the first swing — it looks like a fighting game), looked
at again after M3.3d and M3.3e · R9 after M4.1 (the reel).

**R5 and R6 reviewed 2026-09-01: "behaving as expected."** The authoring loop
(edit lands mid-match, broken save keeps the last good match, pause survives)
and the cooked showcase (then nine rows, the three deliberate verdict
disagreements) both passed the human's play session with nothing reported
wrong. R6's second look is now due: the catalogue stands at thirteen rows
since `9137599`, with the four openings-wave exhibits cooked and captioned.

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
