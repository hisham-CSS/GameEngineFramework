# ADR-011 — Mechanics are fields, not rules

**Status.** Proposed 2026-08-17. Constrains every kernel, data and presentation
work package in [ROADMAP.md](../ROADMAP.md) M1 and M3. Changes no behaviour on its
own. Verified against `master` @ `99669cc`.

**Decides.** How a fighting-game mechanic enters the engine (as authored data on
a move, off by default — never as a rule the kernel applies to everyone); what
"movement" is (a move); how presentation relates to frame data (a pure
function, with always-cancelable tails); how the showcase demonstrates that
frame data alone changes the verdict (one fighter, many patches); and how the
two provers — the published graph prover and the kernel's own bounded search —
are labelled so neither overclaims.

**Constraint from the author, 2026-08-17.** *The showcase must load the same
fighter with different frame data to show the different infinites that frame
data and the cancel graph make possible — including microwalk, jump cancels and
the other odd interactions that are simply how Street Fighter-style games work.
The game underneath must treat every 2.5D mechanic as opt-in per move per
character, so frame data is authored in the most modular way and the visuals
are driven by it; intermediary frames outside the frame data (return-to-idle)
are always cancelable, for maximum animation smoothness.*

---

## 1. What the tree does today, and why it is not yet that

- **Two mechanics are global constants.** `Games/UntitledFighter/Kernel/src/Simulate.cpp`
  hard-codes `kWalkSpeed` (2 px/tick) and `kJumpImpulse` (5 px), while the
  character file already authors `walk_speed` (`CharacterData::walkSpeedSub`)
  and the kernel ignores it. Every character walks and jumps identically.
- **A cancel can only target a move, and contact is one bit.** `CancelEdge` in
  `Games/UntitledFighter/Kernel/include/cse/kernel/Combat.h` carries
  `from`, `to` (move ids), a frame window and `onHit`; its own comment records
  that Hit and Block were collapsed because the kernel had no blocking. It has
  blocking now (`Fighter::blockstun` is written since `41ea6e5`), so the
  collapse is a leftover, and "cancel this normal into a jump" cannot be
  written at all.
- **Presentation does not exist yet** beyond the box overlay
  (`Games/UntitledFighter/Modes/src/FightView.cpp`), which is exactly why the
  rule for it is cheap to state now and expensive to retrofit.
- **The showcase has one variant** — `fighter_a_infinite.json` is a second
  full copy of `fighter_a.json` with a few numbers changed; the diff that makes
  it infinite is invisible.

The per-move vocabulary is otherwise already the right shape:
`Games/UntitledFighter/Data/include/cse/data/CharacterData.h`'s `Move` carries
timing, reach, pushback, stance, `blocked_as`, priority, invincibility windows,
resource effects and guards, per-hit records, an airborne-from tick, a hurtbox
override and a cancel window — all opt-in, exactly as ADR-006 argued ("typed
nouns, not a grammar"). This ADR makes that the rule rather than the habit.

---

## 2. Decision

1. **Every mechanic is data on a move or a character, off by default.** The
   kernel contains **no gameplay constant a character file cannot set**: walk
   speed, jump arc, gravity, pushback, hitstop, hitstun decay, juggle rules,
   meter gain — all `FighterData` / `MoveDef` fields, defaulted by the schema,
   never by a `constexpr` in `Simulate.cpp`. A character that authors none of
   the optional mechanics plays like `fighter_a` does today.
2. **Movement is a move.** Jump, super jump, dash, backdash and air dash are
   authored moves with a `movement` field (per-frame velocity and gravity,
   `airborne_from_tick`, optional landing recovery); the kernel keeps exactly
   two built-in actions — **idle** and **walk** (continuous input, per-character
   speed) — and crouch as a stance. Consequently a *jump cancel* is an ordinary
   cancel edge whose target is the jump move, and the kernel's special-case jump
   code is deleted rather than generalised.
3. **One cancel mechanism, typed by data.** A cancel edge has a `from`, a `to`
   (any move, including movement moves, or `idle` for an empty cancel), a frame
   window, a **contact mask** (`hit | block | whiff`, replacing the collapsed
   bit), a delay (links) and resource guards/effects. Chain, target combo, link,
   special, super, kara, whiff, jump, dash and empty cancels are the *same*
   mechanism with different data. No new cancel "kinds" in code.
4. **Reactions are per-hit fields.** `on_hit` (and `on_block`) on a hit record
   selects, opt-in: hitstun/blockstun, hard/soft knockdown, launch vector,
   wall bounce, wall splat, ground bounce, crumple, counter-hit bonus
   (extra stun/damage when the defender was in startup), punish-counter bonus.
   The kernel implements each reaction once; a hit that authors none behaves as
   today.
5. **A mechanic is complete only with its five parts:** a schema field
   (appended, never inserted — ADR-006's wire rule), a `MoveDef`/`FighterData`
   slot, a loss-ledger row in `MatchBuilder` saying truthfully what the prover
   sees of it, a kernel property test, and — if it can create or kill an
   infinite — a **showcase variant** that demonstrates it (§4). Kernel state
   it needs lands only inside a planned `GameState` expansion (ADR-005 §3).
6. **Presentation is a pure function of the state the sim already produces.**
   `pose = f(moveId, moveFrame, posX, posY, facing, stance/airborne, the stun
   fields, tick)`. Frame data authors the clip and first frame for every move
   (M3.3); the sim never waits for animation. **The tail** — return-to-idle,
   landing settle, anything after `startup + active + recovery` — is
   presentation only: it plays while the fighter stays idle and unmoving and is
   **interrupted instantly by any sim action**; a presentation-side blend from a
   tail into the next move's first frame is bounded (default ≤ 4 frames) and can
   never delay a move, shift a box, or hold a fighter in place. Idle, walk and
   air cycles are keyed by `(tick, posX)`, so they are stateless and
   deterministic across peers.
7. **Variants are patches.** A fighter file may be extended by JSON merge
   patches (RFC 7386, `nlohmann::json::merge_patch`, already in the dependency)
   under `variants/`; the loader, the cooker, the panel and the showcase treat
   `base + patch` as a fighter and can display the patch — **the diff is the
   exhibit**: "this one number turned a safe link into an infinite."
   `fighter_a_infinite.json` becomes `fighter_a` + `variants/infinite.json`.
8. **Two provers, honestly labelled.** The *graph prover* is the published
   `comboprover.hpp`, unmodified, corner-only, over the cancel graph and
   resources. The *kernel search* (`ComboSearch`, M1.4) is a bounded search
   over macro-actions — start a move, wait *k* frames, walk *k* frames, jump,
   dash — executed on the real kernel with state-hash de-duplication; it
   models distance, pushback, walk, corner and every opt-in mechanic. Agreement
   validates the model; disagreement is *named* by the projection-loss row that
   explains it (microwalk lives in `walk_speed`/`gap_actions`, which the C++
   header drops — [ARCHITECTURE.md](../ARCHITECTURE.md) §5.2); a search that hits
   its budget is **UNRESOLVED**, never a verdict, exactly as the prover's
   `capped` is.

---

## 3. The vocabulary — what "all 2.5D mechanics, opt-in" means concretely

Each row is one mechanic. *Today* is read from the tree at `99669cc`. *Variant*
names the showcase patch that demonstrates it (§4); "—" means it cannot create
or kill an infinite on its own.

| Mechanic | Authored where (per move unless said) | Kernel home | Today | Variant |
|---|---|---|---|---|
| startup / active / recovery | move | `MoveDef` | present | `hitstun+2` |
| hitstun, blockstun, chip | move / hit record | `MoveDef`, `Fighter` | present | `hitstun+2` |
| hitstun decay, floor | character (`decay`) | `FighterData` | present (A01 guards the floor) | `decay-off` |
| hitstop | move | `MoveDef::hitstop`, `Fighter::hitstop` | present | — |
| pushback (hit / block) | move | `MoveDef`, `Fighter::pushX` | present | `pushback-0` |
| push boxes, corner as a wall | character (`engine.boxes.pushbox`) | kernel | **absent** — fighters pass through each other | `microwalk` (needs it) |
| walk speed | character (`walk_speed`) | `FighterData` | authored, **ignored** (`kWalkSpeed`) | `microwalk` |
| jump / super jump / dash / backdash / air dash | **moves** with `movement` | `MoveDef::movement` | jump hard-coded (`kJumpImpulse`); no dash | `jump-cancel`, `dash-cancel` |
| stance (stand / crouch / air), `blocked_as` | move | `MoveDef` | present | — |
| priority, trades | move | `MoveDef::priority` | present | — |
| invincibility windows | move | `MoveDef::invuln[]` | present | — |
| hurtbox override, airborne-from | move | `MoveDef` | present | — |
| cancel window (chain / target combo) | move + cancel edge | `CancelEdge` window | present | `hitstun+2` |
| link (delay) | cancel edge `delay` | `CancelEdge` | present | `hitstun+2` |
| **contact mask** (hit / block / whiff) | cancel edge `on` | `CancelEdge` | **collapsed to 1 bit** | `kara`, `whiff-cancel` |
| kara / whiff cancel | cancel edge, early window, `on: whiff` | `CancelEdge` | expressible once the mask exists | `kara` |
| special / super cancel with meter | cancel edge `guard`/`effect` | `CancelEdge` + `res[]` | **schema-only** (meter dead in kernel) | `meter-loop` |
| **jump cancel / dash cancel** | cancel edge to a movement move | `CancelEdge` | **not expressible** | `jump-cancel` |
| empty cancel / feint | cancel edge to `idle` | `CancelEdge` | not expressible | — |
| gap actions / **microwalk** | idle walk between hits (schema `gap_actions`) | idle + `walk_speed` | walking exists; search must model it | `microwalk` |
| resources: meter, juggle, custom gauges | character `resources[]`, move `effect`/`guard` | `Fighter::res[]` (M1.1) | juggle present, meter **dead** | `meter-loop` |
| proration / scaling | character `scaling[]` | `Fighter::scaling` | present | — |
| knockdown hard / soft, wake-up | hit `on_hit` | `Fighter::knockdown` | present (one kind) | — |
| launch vector | hit `on_hit.launch` | `Fighter::velY/airborne` | partial (juggle exists) | `juggle-loop` |
| **wall bounce / wall splat / ground bounce** | hit `on_hit` | reaction fields (M1.1 reserves) | **absent** | `wallbounce` |
| crumple, reset | hit `on_hit` | reaction fields | absent | — |
| **counter-hit / punish-counter bonus** | move `counter_hit` | `MoveDef` | **absent** | `counter-hit` |
| throws (normal, command, tech) | move `throw` | kernel | absent | later |
| armor, parry, guard crush, unblockable | move | kernel | absent | later |
| projectiles | move `projectile` | pool | absent — not scheduled (ADR-010 §3.4) | later |
| cross-up direction | computed from position (ADR-006 §3.4) | kernel | rule, by design | — |
| rounds, timer, teams / tag | character / match | `GameState` | present | — |

Rows marked *later* are in the vocabulary so nobody designs them out; they land
when a character authors them, through the same five parts, never as a special
case.

---

## 4. The showcase: one fighter, many patches

The catalogue (ROADMAP M1.6) is built from **one base fighter** and a family of
**frame-data patches**, each changing as little as possible — ideally one number
or one field — so the exhibit is the diff. For every entry the cooker records
the graph prover's verdict, the kernel search's verdict, the reason if they
differ, the replay(s), the on-screen input trace, and a `graph.dot` of the
cancel graph with the loop highlighted.

| Variant | The patch | What it shows | Needs |
|---|---|---|---|
| `base` | — | TERMINATING; the ranking certificate | M1.1 |
| `hitstun+2` | one move's `hitstun` +2 | a link becomes a loop: **frame data alone** flips the verdict | — |
| `dead-cancel` | one cancel's window closes 1 frame early | an authored cancel that can never connect, and the tool naming it | — |
| `pushback-0` | one move's pushback → 0 | the corner stops mattering; midscreen and corner agree | M1.2 |
| `microwalk` | `walk_speed` +1 px/tick | graph prover (corner) says TERMINATING; kernel search finds walk-2-frames-then-jab forever midscreen — the **model/game gap, named** (`gap_actions` dropped by the header) | M1.2, M1.4 |
| `jump-cancel` | one normal gains a cancel to `jump` | an air loop: jump-cancel → air normal → land → normal → … | M1.3 |
| `kara` | a special gains `on: whiff` at frames 0–2 from a normal | range extension makes a whiffing link connect | M1.3 |
| `counter-hit` | one move gains `counter_hit.hitstun_bonus` | an infinite that exists **only** on counter-hit — the prover cannot see it; the loss table says why | M1.3 |
| `wallbounce` | one move's `on_hit` → wall bounce | a corner-only loop; the stage becomes part of the graph | M1.3 |
| `meter-loop` | a super cancel gains a `guard` on meter; hits gain `effect` | INFINITE only if gain per cycle ≥ spend — the resource ranking story, executed | M1.1 |
| `decay-off` | `decay.kind` → `none` | how decay ends a loop, and what happens without it | — |

Every variant is a `.json` merge patch under
`Games/UntitledFighter/Assets/Characters/fighter_a/variants/` with a one-line
`description`, and every replay is verified bit-identical by `ReplayVerifier`
before it is written. Tight timing is not a constraint on any of this — the
input source is scripted; it is only a constraint on humans, which is the point.

---

## 5. What this changes in the plan

- **ROADMAP M1.1** — the one state expansion also moves walk/jump/gravity out
  of `Simulate.cpp` constants into `FighterData`, and reserves the reaction
  fields the pass-1 mechanics need.
- **ROADMAP M1.3 (new)** — the mechanics pass 1: contact mask, movement moves
  and jump/dash cancels, counter-hit, wall bounce/splat, launch vector; each with
  its five parts.
- **ROADMAP M1.4** — `ComboSearch` searches macro-actions including wait, walk
  and jump, so microwalk and jump loops are *found*, not assumed.
- **ROADMAP M1.6** — the catalogue is base + patches (this ADR §4) with
  `graph.dot` and both verdicts per entry.
- **ROADMAP M3** — the presentation rule (decision 6) is the acceptance test
  for M3.2–M3.4.
- **`CLAUDE.md`** — two new *nevers*: no kernel constant a file cannot set; no
  presentation state the sim did not produce, no sim action delayed by a tail.
- The transport spike becomes ADR-012.

---

## 6. Not decided here

1. **Is walk a move too?** Default no — continuous input is a different shape
   from a discrete move, and the prover's `walk_speed` is a character number.
   Revisit if a character needs walk-speed-per-state (e.g. crouch-walk).
2. **The blend cap.** Default 4 frames of presentation-only blend on tail
   interruption. Any value is legal because it can never touch the sim.
3. **The Python midscreen prover in the cooker.** When `C:/rw` (or its
   successor) is on the machine, the cooker may also record the reference
   implementation's midscreen verdict per variant; when it is not, that column
   is "not run", never inferred. Default: optional, detected, never required for
   CI.
4. **How many patches ship in v1 of the catalogue.** Default: the eleven in §4,
   in the order their *Needs* column becomes true.
