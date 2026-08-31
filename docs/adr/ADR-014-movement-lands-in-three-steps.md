# ADR-014: Movement lands in three steps, and the wire grows once

Status: Proposed (2026-08-31). Written mid-M1.3(b), before any of it landed.

## Context

ADR-011 decision 2 fixes the destination: jump, super jump, dash, backdash and
air dash are authored **moves** with a `movement` field; the kernel keeps idle
and walk built-in; a jump cancel is an ordinary cancel edge targeting the jump
move; the hard-coded jump (`kJumpImpulse`, `Simulate.cpp`) is deleted. That
destination is not in question here — this ADR is about the **order of
arrival**, because a consumer map (M1.3(b), 2026-08-31) measured the blast
radius of arriving all at once:

- The hard-coded jump is a **level** response (`jumpWish` = Up held); a move
  starts on a **press edge**. The flip changes the meaning of every Up bit in
  every baked trace: the cross-toolchain golden (`0xAD470388`, whose script
  jumps on modular cadences), `Demonstration::inputs`, and every recorded
  replay. A golden re-record on both platforms is mandatory, not incidental.
- `WitnessCursor`'s stance-hold rule (`kStanceAir → hold Up`) becomes actively
  wrong — a held Up would buffer jump-move presses the witness never asked
  for — and `ComboSearch` has no jump macro, so air reachability silently
  dies unless the search is taught in the same change.
- fighter_a's cancel graph encodes the jump **inside edge delays**
  (`jump_cancel` at delay 5 = four pre-jump ticks + one transition, targeting
  the aerials directly); under jump-as-move those edges must retarget the jump
  move, a re-authoring of the shipped file.
- Per-move motion (lunge, hop kick physics, divekick) needs `MoveDef` to
  **grow**, and `MoveDef` is hashed wire (the connect handshake and every
  replay's `matchDataHash`). ADR-005 §3 and CLAUDE.md require such contract
  changes to be **batched and re-goldened once** — and M1.3(c) counter-hit and
  M1.3(d) reactions also want MoveDef bytes, with (c)'s semantics deliberately
  not chosen yet (it changes what the tool claims).

Meanwhile two facts make a first step nearly free: `FighterData::gravitySub`
and `jumpImpulseSub` have existed since M1.1b as **kernel-consulted, never
authorable** slots (`velY = jumpImpulseSub != 0 ? … : kJumpImpulse`), and the
per-move `engine.motion` keyframes already **parse** into `CharacterData` and
are dropped with the `move.engine.motion` KernelOmits ledger row.

## Decision (recommended default; each step reversible by revert)

**(b1) The authored numbers reach the physics — now.** A new appended
`engine.movement { jump_impulse_sub, gravity_sub }` character block, kernel
semantics (+Y up, positive impulse, positive gravity magnitude), parsed into
`CharacterData` and carried into the existing `FighterData` slots with Exact
ledger rows. Zero is the unauthorable sentinel (the kernel's `!= 0` fallback),
so an explicit 0 is refused by name rather than silently meaning "placeholder".
**Base fighter_a does not author the block** — the M1.1e buffer precedent: its
`MatchData` hash, its 38-tick arc and every measured count stay put; the
MUGEN-provenance numbers stay recorded in `engine.constants` (Y-down, cited,
unread). A showcase **variant** authors it, which is ADR-011's whole thesis:
the same fighter with different movement physics shows a different string.

**(b2) One MoveDef growth, batched.** The per-move motion block (fixed-bound
keyframes, the `InvincibilityWindow` array+count pattern, bridged from the
already-parsed `Move::motion` with the documented Y-sign flip) lands in the
same layout change that **reserves** the (c) counter-hit and (d) reaction
bytes — reserving bytes chooses no semantics, exactly like M1.1a's reserved
`Fighter` fields, so (c)'s verdict question stays open while the wire pays its
re-hash once. The lunge and the divekick become authorable here; the hop kick
already half-exists (`airborneFromTick` classifies; motion supplies the
physics).

**(b3) Jump-as-move, last.** The level→edge flip, the cancel-graph
re-authoring, the cursor/search teaching and the golden re-record travel
together in one reviewed change, after (b2) has proven per-move motion on
moves that are *not* load-bearing for every witness in the repository.

## Consequences

- (b1) makes movement physics authorable this week at zero measured churn, and
  its variant is the first exhibit where the string's terminator visibly
  changes hands (the arc stops ending it; the wired juggle budget does).
- The full ADR-011 destination is deferred, not diluted: (b3) remains the
  committed target, and this ADR records why it goes last.
- Until (b3), a jump cancel stays encoded in edge delays and the prover's
  model keeps walking it; the D8 gap for movement stays where ADR-011 §2.8
  put it — in the loss ledger, named.
- The (c) decision is untouched by (b2)'s reservation and still blocks (c)'s
  semantics on its own ADR.
