# ADR-020: The bounded lift — skinned placeholders before netcode, and one modeled body before M4

Status: Proposed (2026-09-02), with a recommended default that the session
does NOT proceed under. This is the human's decision alone: the order it lifts
is the author's stated order, written in three living places, and only the
author can lift it. Until it is answered, nothing in ROADMAP M3 below M3.0
starts; M2.1 remains the standing next WP.

## Context

The roadmap's order is stated three times and agrees with itself: ROADMAP's
goal paragraph ("everything is provable and showcased before any real art is
made"), NORTHSTAR's "The order, and why it is not negotiable", and ADR-010 §3
("provable before pretty … the plan does not bend it"). M2 (two people, one
match) precedes M3 (skinned fighters), and real art is M4.4, "last, through a
pipeline that already exists", after the reel and the paper artefacts already
sell the claim without it.

The same ADR-010 leaves the door on the latch. §9 item 5: "if the author would
rather see skinned placeholders sooner, swap M2 and M3 — nothing in either
depends on the other except that M3.1's event ring is reserved in M1.1 either
way. Default: M2 first." The coupling it names is real and is the ONLY one:
M3.1 writes the reserved ring, and because `kernel::Checksum` folds the whole
`GameState` the crossplat golden is re-recorded when it lands. Everything else
in M3 reads `GameState` and writes a picture.

The human asked (2026-09-01) for the art pipeline now — "at least one shoto
style character fully modeled and working so we can validate this with actual
animations rather than just hitboxes." [ADR-019](ADR-019-placeholders-through-blender.md)
decides HOW that pipeline is built so that the answer here changes only WHEN.
It also makes the modeled body a mesh swap on a manifest-pinned skeleton, so
one modeled character costs the roadmap no second pipeline.

Where M1 stands: the openings wave is closed; what remains is human-gated (the
R6 second look at thirteen rows, the PR #7 merge). A new milestone branch
starts from `master` once PR #7 is merged.

## Decision (recommended default), in two clauses

**(i) The bounded lift.** M3.2a–g, M3.3a–d, M3.4a–e and M3.5a run before
M2.1. M3.1 (the event ring write — the one M3 item that re-goldens the
crossplat hash and the one that benefits from M2.2/M2.3's reflection table),
M3.5b (which needs M3.1's drain) and M3.6 stay after M2.6, in either ordering.
*Recommended: accept.* The validation the human named — clip length equals
frame data, one pose per tick under frame-step, the contact pose inside the
live hitbox, a hot reload that rebinds or refuses — needs nothing from M2, and
M2.5's "one presentation for three modes" becomes cheaper when a GL-free
reconciler already exists.

**(ii) One modeled body before M4.** A Blender-modeled shoto body replaces the
mannequin's mesh on the pinned skeleton as ROADMAP M3.3e, before M4.1. This
amends ADR-010 §3.4's "real art … after M4.1–M4.3" for exactly one character,
and only its body — no second character, no stage art, no effects. *Recommended:
accept in principle now, and START it only after R8 on the mannequin has proven
the pipeline*, so that no sculpt hour is spent before the frame-data validation
it serves is already visible. The sculpt is the human's viewport time; ADR-019
D7 records the honest anecdotal budget.

**Reversal condition, written in.** If M3.4c — the first swing in the mode — is
not green within 20 sessions of M3.2a starting, M3 parks at its last green WP,
every parked WP keeps its `[ ]` and its Done-when, and M2.1 resumes. If M3.3e
has not landed within 25 human viewport hours of starting, it parks likewise
and M4.1 is not delayed by it.

## What accepting costs, honestly

- R7 (two people, one match) slips by roughly 15–20 sessions — 3–6 weeks at the
  recent cadence — and with it the netcode proof, the desync artifact and
  Play == Player as a hash (M2.6).
- P4's rollback pop cannot be watched over a real link until M2, so P4's
  evidence is the headless Restore tests only.
- M2.6 becomes a bigger test, because a 3D presentation exists when it lands;
  kept cheap because every reconciler matrix and palette is a headless
  property under ADR-019 D9.
- ADR-010's argument that "a networked replay of the same catalogue is a
  stronger showcase than a prettier local one" is deferred, not refuted.

## What accepting does not cost

- The `anim3d` fields never enter `MatchData` or `MoveDef`, so the handshake
  hash M2.2 computes is untouched (ADR-019 D11).
- Nothing in M2.1–M2.4 touches `PoseSelect`, `FightPresentation` or the
  skinning path.
- The crossplat golden does not move: M3.1 is the only M3 WP that moves it and
  it stays after M2.

## What the human does, in order, if accepting

1. Merge PR #7 so M1 closes and `roadmap/M3-art` rebases onto `master`.
2. Flip this ADR's Status line to Accepted, naming which clauses.
3. The same commit rewrites — never annotates — ROADMAP's goal paragraph and
   milestone order and NORTHSTAR's "The order" section to say the new truth,
   and adds one clause to ADR-010's and ADR-005's Status lines. Living docs are
   rewritten; frozen ADRs move only their Status line.
4. Answer ADR-019 D10 (the content licence) before the first committed asset.
5. Install `uv`, register the MCP server at user scope with telemetry off, and
   install the add-on (ADR-019 D8).

No money and no account anywhere in the default path.

## If declined

M3.0 alone lands now: both ADRs stay Proposed as the plan of record, ROADMAP
carries every M3 WP with its Done-when, and M2.1 is the next WP. M3 then runs
after M2.6 exactly as written, starting at M3.4a. The ADRs, the `uv` install
and the mannequin generator are useful under either answer, and nothing
written for this decision has to be unwritten.

## What would reverse this ADR

The reversal condition above firing; or M2 turning out to need something M3
built (it does not, by the coupling analysis) — then the order reverts to
ADR-010's default and this ADR's Status line says so.
