# ADR-012 — The tick is a pipeline of pure functions

Status: Proposed (2026-08-21). Recommended default; safe and reversible, so work
proceeds under it per CLAUDE.md.

## Context

The kernel is already functional at its boundary: `Simulate` is
`(GameState, InputPair, MatchData) → GameState'`, the state is a value, rollback
is `memcpy`, and the configure gate keeps clocks, floats and allocation out.
That boundary is not the problem and this ADR does not touch it.

Inside the tick, the discipline decays. A week of measured failures, each one a
**write-order bug on a multi-writer field**:

- `crouching` (4 write sites) is computed *before* the landing clamp, so a
  crouching move cannot start on the tick of landing — a rule nobody wrote.
- The commitment rule froze `crouching` mid-move, which silently refused every
  cross-posture cancel (`stand_mp → crouch_hp`, an ordinary gatling) and
  collapsed 120 of the instrument's 121 cycles.
- `StanceAllows` reads `airborne` (5 write sites) raw, while `AttackKinds` reads
  `AirborneNow` — two adjacent functions answering "is this fighter airborne"
  differently, correctly, and confusingly.
- Five copies of the witness cursor (four test `Driver`s plus
  `BuildDemonstration`) each hold mutable state and drifted: the seam test
  caught one copy missing the waiting rule the original had.
- `tests/test_gap_extent.cpp` maintains a parallel *model* of the kernel
  (section 3's frame arithmetic) and every kernel change re-derives it by hand;
  its two-route account disagreed with the real kernel on 97 of 121 cycles the
  moment stance was carried.

## Decision

Four rules, applied inside the existing boundary. No new types of state, no
persistent data structures, no allocation — C++17 value discipline, not an FP
library.

1. **The tick is a fixed pipeline of pure stages.**
   `ReadIntent(state, input) → Intent` (walk wish, jump wish, selection posture
   — pure, no writes) · `StepPhysics(state, intent) → state'` ·
   `StepAttack(state', intent) → state''` · `Resolve(state'') → state'''`.
   A stage receives values and returns values; the only mutation is the
   assignment between stages. What each stage may write is its signature, not a
   convention.

2. **Every field has one writing stage.** A field written by two stages is a
   bug by definition. The audit that seeds this: `crouching`, `airborne`,
   `facing`, `velX` today have 4–6 write sites across two files.

3. **Derive, don't store, wherever the wire allows.** A fact computable from
   (state, input) is a function, not a field. *Selection posture* — "which
   stance variant does this press ask for" — is `f(input, airborne)`, never
   `f.crouching`. **Posture follows the move**: starting a crouching move makes
   the fighter crouching; `crouching` keeps exactly two writers — the free-tick
   input rule and the move-start rule — and commitment forbids only
   *input-driven* posture change. This is the semantics fix M1.3e's third
   attempt proved necessary.

4. **Predictions are made by execution, not by a parallel model.** The one
   legitimate model of the kernel is the kernel: M1.4's `ComboSearch` runs the
   real `Simulate` over macro-actions, and section 3's hand-derived account is
   deleted when the properties replace the counts, not maintained alongside.
   Likewise ONE witness cursor lives in `CseGame` as a pure step
   `(cursorState, observed) → (bits, cursorState')`; the five copies die.

## Consequences

- The pipeline refactor itself is **golden-locked**: stage extraction with
  byte-identical behaviour, proved by the cross-toolchain hash not moving. The
  posture-follows-move change is a separate, deliberate re-golden.
- Net lines go **down**: the cursor unification deletes four copies (~400
  lines); section 3's deletion in M1.4 removes ~600 more; the kernel's two
  files (1519 lines) should not grow.
- Extension cost drops where it hurt this week: a new mechanic is a new field
  read by one stage, not a new write racing three existing ones for order.
- What this does NOT do: change `GameState`'s layout (wire contract, ADR-005),
  introduce allocation or indirection into the kernel, or alter the
  determinism gates. `Fighter::crouching` stays in the state — it is posture
  *history* the next tick reads — but gains the two-writer rule above.

## Rejected

- **Full FP style** (immutable snapshots per stage, persistent structures):
  the state is 728 bytes and `memcpy` already is the immutable snapshot;
  copying per stage buys nothing the pipeline signatures don't.
- **Removing `crouching`/`airborne` from the state entirely**: `airborne` is
  position history (cleared by the floor), not derivable per tick; removing
  fields moves the wire format for no behavioural gain.
- **Keeping section 3 and teaching it jumps**: maintaining a second
  implementation of the kernel's arithmetic is the complexity this ADR exists
  to stop; the kernel can answer the question itself.
