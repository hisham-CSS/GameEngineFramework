# ADR-015: What a verdict claims when hitstun stops being one number

Status: Proposed (2026-08-31). Blocks ROADMAP M1.3(c) and M1.3(d). This one
needs the human: every option changes what the published tool claims, which
CLAUDE.md lists under "publishing" rather than under "safe and reversible".

## Context

The prover reads **one `hitstun` per move**, and today that is exactly true of
the kernel — the agreement is what lets a TERMINATING verdict be demonstrated
by execution. Two authored mechanics are waiting to break it, and both are
already staged:

- **Counter-hit** (M1.3(c)): per-move `counter_hit {hitstun_bonus,
  damage_bonus}`. The MoveDef bytes are reserved (M1.3(b2)); the schema field
  and kernel reads are not. The moment a counter-hit opens a string with more
  stun than the model charged, a TERMINATING verdict is silent about a longer
  string the game contains.
- **Air hitstun** (M1.3(d)): `air_hitstun_ticks` is authored on every shipped
  move and **differs from ground hitstun on all of them**. It is loaded and
  uncarried; the launcher reactions (d) exist to make it reachable. Same
  shape: one model number, two game numbers.

ROADMAP's "Where this stands" qualifier has named this since the reorder:
"Three ways out are in M1.3(c); none is chosen, because it changes what the
tool claims."

## The three ways out

1. **Qualify the verdict.** The tool prints "TERMINATING *under neutral,
   grounded hit*" — the assumption becomes part of the claim, on screen, in
   the ledger, and in the paper's text. Soundness is preserved by narrowing
   the sentence, not the analysis. Cost: the headline claim is weaker, and
   every place the verdict is quoted must carry the qualifier or become a
   misquote.
2. **Worst-case over hit types.** The model charges every hit at
   `max(hitstun, counter, air)`. One verdict, sound for all openings — and
   looser: strings the game cannot perform become model-permitted, widening
   the model/executed gap the paper measures (fighter_a's pair is 21/7
   today; worst-casing moves the 21, not the 7).
3. **One verdict per hit type.** The tool answers per opening ("TERMINATING
   under neutral; TERMINATING under counter; …"). Sharpest and most honest;
   also the largest change to the tool's interface, the panel, the cooker's
   replay-per-verdict plan, and the paper's framing.

## Recommended default (not enacted)

**Option 1, qualification**, because it is the only one that changes no
computed number: the analysis stays byte-identical, the executed
demonstrations stay valid, and the qualifier is a true sentence about both.
Options 2 and 3 change measured results and the tool's surface, which is a
bigger paper edit than a stated assumption. If the human wants the stronger
claim later, 3 subsumes 1 without invalidating anything published under it.

None of this is enacted now. Per CLAUDE.md, a decision that changes published
claims is not proceed-under-default territory: M1.3(c) and (d) stay blocked
on this ADR's Status line, and the reserved MoveDef bytes stay zeroed and
unread.

## Consequences of deciding

- Accepting 1: (c) and (d) unblock immediately — mechanics land as fields
  with tests, and the qualifier lands in the panel, the ledger rows, ROADMAP's
  claim table and the paper text in the same commit.
- Accepting 2 or 3: the prover and its adapter change first; the mechanics
  land against the new vocabulary; the paper's measured pair is re-derived.
- Until then: the kernel's one-hitstun agreement with the model is exact and
  tested, and stays that way.
