# ADR-018: The jump move is opt-in, and the placeholder jump is its fallback

Status: Proposed (2026-09-01), with a recommended default the session proceeds
under: it is safe (no golden moves, no shipped character changes, every
existing test keeps its meaning) and reversible (one gate, one field, one
takeoff rule; `git revert` undoes it). Executes ADR-014 step (b3) with one
amendment recorded here because ADR-014 is frozen.

## Context

ADR-014 committed (b3) as "the hard-coded jump deleted, jump cancels
retargeted, the golden re-recorded" — and a four-seam map of the current tree
found that outright deletion detonates far more than the ADR's blast-radius
note recorded:

- **The pipeline jams the clean version.** StepPhysics runs before StepAttack,
  and the level-triggered jump fires on the same Up press, setting `airborne`
  before `StanceAllows` reads it — so a grounded jump MOVE can never start
  from a direct press while the level trigger lives; and a jump move bound
  with an air-startable stance is a free infinite double jump.
- **The data-less overload exists for the golden.** The crossplat script runs
  `Simulate(state, inputs)` over `kNoMoves` — a path in which no move can
  start, kept "exactly the pre-hitbox kernel" so the golden hashes stay
  meaningful. Deleting the built-in jump makes that script unable to jump at
  all: the coverage gate fails and no re-record can fix it. ADR-014's
  deletion and Simulate.h's stated purpose are in direct tension.
- **Takeoff direction has no data-only expression.** `MoveDef::button` is
  absolute screen bits while `MotionKey` X is facing-relative, so three
  directional chord moves mirror wrongly by construction; a single move needs
  a kernel rule for where its horizontal velocity comes from.
- **Every recorded artifact with an Up bit re-means at once** — the golden,
  the arc-count pins (38/78/86 ticks), the P2 ballistic/commitment suites,
  fighter_a's eight delay-5 jump-cancel edges and their prose census — if the
  flip is global.

Meanwhile the kernel already has a doctrine for exactly this shape: the walk
speed, the jump impulse and gravity are all PLACEHOLDER FALLBACKS that an
authored number displaces per character. And ADR-011's own rule — every
mechanic is a per-character field, off by default — was never going to be
satisfied by a global flip.

## Decision (recommended default)

**A character may author a JUMP MOVE; the built-in level-triggered jump is
demoted to the fallback for characters that author none.**

- `FighterData::jumpMoveSlot` (tail-appended; 0 = none) names the move whose
  `button` is exactly `kInputUp`. The bridge sets it and REFUSES two such
  moves — two jump moves would race the button scan.
- StepPhysics' free-branch jump is GATED on `jumpMoveSlot == 0`. For an
  opting-in character the Up press therefore reaches StepAttack still
  grounded, and the jump move starts on the press EDGE like every move since
  M1.1d: held Up does not restart it, landing under held Up does not re-jump,
  and a grounded stance makes the mid-air re-press refuse itself — the double
  jump is not a special case, it is `StanceAllows`.
- **Takeoff direction is chosen on the takeoff tick.** The jump move's
  launching key (velY > 0) authoring NO horizontal component takes
  `walkWish` — the direction held on the tick the fighter leaves the ground —
  and afterwards deliberately does not own X, so the arc is ballistic from
  takeoff exactly as the fallback jump's is. Scoped to the jump move's key:
  an uppercut's rising key keeps (b2)'s both-component ownership. Prejump is
  the move's own startup: authored frames on the ground before the key fires,
  which makes fighter_a's "delay 5 = four pre-jump ticks plus one of
  transition" a sentence the simulation can finally make literal.

**The amendment to ADR-014**: "deleted" becomes "demoted to the unauthored
fallback". What (b3) promised — the jump as a move, jump cancels as edges
into it, the cursor and search taught — all still lands, per character, where
an exhibit can show it. What changes is who pays: nobody who did not opt in.

## Consequences

- **No golden re-record.** The crossplat script's `kNoMoves` authors no jump
  move, so its Up bits keep their recorded meaning and the cross-toolchain
  evidence survives intact — the outcome ADR-014 warned re-recording would
  destroy. This is not a dodge: the golden pins the FALLBACK, which still
  exists and is still the path it exercises. A future opt-in by a scripted
  golden character would be a deliberate re-record, reviewed then.
- Base fighter_a does not opt in — the M1.1e/(b1) precedent: hash, arc
  counts, the 121-cycle census and every measured number stay put. The
  jump-cancel/kara showcase variant is where the jump move first bites, with
  cancels retargeted in the VARIANT's patch.
- The witness cursor's `kStanceAir -> hold Up` rule and the search's
  movement-move outcome stay correct for non-opting characters and are taught
  the jump move alongside the variant that needs them.
- Binding Up admits it to the input buffer's usable-bit union for opting
  characters: an Up tap in hitstun buffers a JUMP, which may replace a
  buffered attack (replace-on-write). For an opt-in character that is a real
  input, not the clobber bug the mask fixed; said here so the next reader
  does not re-diagnose it.

**What would reverse this.** A roster direction in which every character
jumps as a move — the fallback then dies of disuse and its deletion becomes
the cheap change this ADR made it (one gate, one branch, a golden re-record
at a moment of the humans' choosing). Or a fallback/authored divergence a
player can feel that data cannot close; that would force the global flip
ADR-014 first drafted, with its full blast radius.
