# ADR-013 — ComboSearch: verdicts by execution

Status: Proposed (2026-08-31; decision 6, the movement macros, added later the
same day while still Proposed — the design step ROADMAP M1.6 named). Recommended
default; safe and reversible (additive code, no state or wire change), so work
proceeds under it per CLAUDE.md.

## Context

ADR-012 rule 4: the one legitimate model of the kernel is the kernel.
`tests/test_gap_extent.cpp` section 3 was the last parallel model — a
hand-derived two-route frame account re-derived on every kernel change — and
M1.3e retired its predictions the day every turn gained a jump it could not
describe. What remains is the question the paper actually asks: **what can
this character perform, and does any string run forever?** — answered today by
per-file test harnesses, each driving the kernel its own way.

The prover answers that question about the FILE, soundly and conservatively.
Nothing yet answers it about the GAME by searching the game, which is what
"the graph is the game" requires: the cooker wants a verdict per character,
the showcase wants a witness it can replay, the editor panel wants both, and
the tests want the number the paper quotes to be measured rather than derived.

## Decision

**`ComboSearch`, in `CseGame`: a bounded search over macro-actions executed on
the real kernel.** One implementation for tests, cooker, showcase and panel.

1. **A macro-action is "ask for move M next", performed the way a player
   performs it.** Press M's button with its stance-establishing hold, release
   and re-press on a stall — the `WitnessCursor` rules, reused not restated.
   The kernel decides whether that becomes a cancel, a buffered link or a
   restart; the search never re-implements a window.

2. **A node is a reached state; the key is its masked checksum.** The mask
   excludes `tick` (monotonic by construction), both healths and the round
   fields — exactly the exclusions `test_gap_extent`'s state-repetition rule
   proved out: an infinite repeats everything except the damage it deals.
   Visited keys are not re-expanded.

3. **A string lives while the defender is never actionable.** The direct
   reading — hitstun at the end of the previous tick, the same rule
   ComboWatcher and the gap sweep use. A macro-action during which the
   defender becomes actionable ends the string; the path is recorded and
   pruned. The defender is the silent training dummy, which is the recipe the
   ground-truth files themselves prescribe.

4. **Three verdicts, and the budget can only ever produce the third.**
   - **INFINITE** — a node key repeats along one path with every macro-action
     between the repeats connecting and the defender never actionable. The
     witness is the move sequence between the repeats, replayable by the same
     cursor that found it.
   - **TERMINATING** — the search exhausted every string within budget: all
     paths end with the defender free. The longest string found is the
     kernel's own worst case, the executed counterpart of the prover's
     `maxHits`.
   - **UNRESOLVED** — the tick or node budget ran out first. Never promoted to
     either verdict; a budget is not evidence.

5. **Deterministic by construction.** Fixed expansion order (move slots
   ascending), integer state, no clock, no randomness: the same character and
   budget produce the same verdict, witness and counts on every machine — so
   a verdict can be a golden the way a hash can.

6. **Movement macros, from a small fixed menu.** Two more macro-action kinds
   beside "ask for move M": *walk* (an absolute direction held for a fixed
   tick count) and *wait* (nothing held). They are witness entries like any
   move — encoded as reserved codes above the move-slot range
   (`WitnessCursor::kMacroWalkLeft/Right/Wait` + tick count), performed by the
   same cursor, replayable in the same demonstrations — because without them
   the search cannot approach at midscreen (measured: ±100 px opens to ZERO
   hits) and cannot express the microwalk the paper's vocabulary names.
   - **The menu is fixed and small** — walks of 1, 2, 4 and 8 ticks in each
     absolute direction, waits of 1, 2 and 4 — because every entry multiplies
     the branching factor, and D4's spirit applies to search spaces too.
     Directions are ABSOLUTE (left/right, not toward/away): the emitted bits
     are replay-stable, the useless direction dies by dedup (walking into a
     wall reproduces its own key), and the search stays free of a second
     facing rule.
   - **Only free ticks count toward a walk.** A committed or frozen fighter
     cannot walk, so the held direction rides silently through a move's tail
     or a hitstop freeze and "walk 8" always means eight ticks of walking —
     measured: without this, a walk issued right after a connect spent most
     of itself committed and the microwalk link was never performable.
   - **A movement macro scores no hit.** The witness records it; the hit
     count does not. `maxHits` keeps meaning hits.
   - **Two caps and one direction rule keep exhaustion affordable**, each
     measured against fighter_a's corner roster: the APPROACH (before the
     first hit) walks at most eight entries and only TOWARD the opponent (a
     retreat opening is a different question the corner/midscreen pair
     brackets); a live string walks at most two PER LINK — the genre's
     microwalk is one or two, and a per-STRING cap refused the very loop the
     vocabulary exists to find, since that loop walks on every repetition.
   - **Expansion order is a pure function of the node**: moves in slot
     order, then walks toward the opponent longest-first, then away, then
     waits. Longest-toward-first is what lets the depth-first dive BE the
     canonical walked loop — the walk that overshoots is clamped by the
     pushbox to one repeating state, while a walk that lands short starts a
     drifting near-miss chain; short-first ordering explored an exponential
     universe of those before ever trying the one that closes.
   - **The induction rule narrows.** A path-repeat proves an infinite only
     while the string is LIVE (defender never actionable, rule 3). Pre-string
     movement — the approach — happens with the defender free, so those nodes
     restart the path-key chain rather than extend it: a fighter walking in
     place at neutral repeats its state forever and means nothing. A
     MID-STRING repeat that includes walks is exactly the microwalk infinite
     and stays a verdict.
   - **A wait at neutral prunes itself**: the masked key ignores tick and
     rng, so the resulting state is its parent and dedup drops it. Waits earn
     their place mid-string, where delaying a press changes the link.

## Consequences

- `test_gap_extent` sections 3–4 and `test_ground_truth`'s hand-rolled drive
  loops become PROPERTY tests over ComboSearch results (M1.4): section 3 is
  deleted, not taught; counts become facts about the graph, verdicts become
  facts about execution.
- The headline becomes executable: prover `maxHits` (sound, conservative)
  against ComboSearch's longest string (measured), and INFINITE witnesses are
  demonstrable by construction — the search speaks WitnessCursor.
- M1.1f (juggle) and M1.3i (hitstop) become landable: their frame-exact
  objections die with the hand-derived counts.
- Cost: a search is ticks. The budget is the caller's, hitting it is
  UNRESOLVED, and the default budget must keep the shipped characters
  resolvable in test time.

## Rejected

- **Teaching the enumeration a stance-reachability calculus** (the original
  M1.4a shape): a third implementation of kernel rules, exactly the class
  ADR-012 rule 4 forbids. The enumeration keeps answering what the AUTHORED
  graph contains; reachability is answered by executing.
- **Verdicts from the prover's graph projected onto the kernel**: the bridge
  loss ledger exists because that projection loses; a verdict about the game
  must run the game.
- **Randomized/again-sampled search**: cheaper coverage, non-reproducible
  verdicts. A verdict that changes between runs cannot gate a cooker.
