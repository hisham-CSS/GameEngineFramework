# Kickoff prompt — paste into a fresh session (Opus 5)

> Everything below the line is the prompt. `CLAUDE.md` in the repo root is
> loaded automatically and is binding; this prompt only adds the mission, what
> is already decided, and where to start. Update the "First task" line as
> milestones close.

---

You are continuing autonomous development of **GameEngineFramework** — Cat Splat
Engine and its first title, **Untitled Fighting Game** — in this repository.
`CLAUDE.md` is binding; read it first, then `docs/ROADMAP.md`, then
`docs/ADR-011-mechanics-are-fields.md` and `docs/ADR-010-one-roadmap-one-rule.md`,
then `docs/MAINTENANCE.md` and `docs/STYLE.md`. Until `docs/DETERMINISM.md`
exists, the determinism contract is `docs/ARCHITECTURE.md` §4.

**The mission, and why.** This engine exists to make a research result visible
and convincing: a combo-termination prover (`ThirdParty/comboprover/`) analyses
fighting-game frame data, and this engine runs the *published* prover on the
*shipped* character files inside a working editor. The engine's job is to prove,
in the running game, that every verdict the prover prints is true — and to show
it: the **same fighter loaded with different frame data must demonstrate
different infinites** (a link that becomes a loop from one extra frame of
hitstun, a microwalk loop the corner-only prover cannot see, a jump-cancel air
loop, a wall-bounce corner loop, a counter-hit-only link, a meter loop), each
performed frame-perfectly by a **tool-assisted player**, recorded as a replay,
verified bit-identical, and playable with an on-screen input display. Timing
that is impossible for a human is fine; the input source is scripted.

Three standing constraints from the author, already written into the ADRs and
ROADMAP — do not soften them:

1. **Provable and showcased before any art.** Placeholder Mixamo rigs come
   after the showcase works with boxes; SF6-tier art comes last, and only after
   the showcase already sells the paper without it.
2. **Every 2.5D mechanic is an opt-in field per move per character, never a
   rule in the kernel** (ADR-011). Visuals are a pure function of frame data;
   return-to-idle tails are always cancelable; a blend never moves a box or
   delays a move.
3. **Modern design principles that reduce duplicated code**: the build
   enforces, prose reminds; pure functions and POD state; tables over parallel
   hand-maintained lists; one implementation per concept; delete before you add.
   The concrete items are ROADMAP's E1–E8, attached to the WPs that need them.

**Already decided — do not re-open** (write a new ADR if you believe one is
wrong, and continue under the standing decision meanwhile): the not-scheduled
list in ADR-010 §3.4 (no trigger language, no `SimId`/own rings, no projectiles
until a character has one, no asset mounts before M2); the milestone order
M0 → M1 → M2 → M3 → M4 (ADR-010 §9.5); the defaults in ADR-010 §9 and ADR-011
§6; the frozen ADRs 001–009 as records of decisions made.

**How you work.** Exactly as `CLAUDE.md` says: one work package in flight; its
"Done when" test first; prove the fix by reverting it; four presets build; docs
fixed in the same commit; ROADMAP updated; the determinism gate (and, once it
exists, the docs gate) green; a commit message written for `git blame`. Work on
`roadmap/<milestone>` branches, push after each WP, watch CI to green before the
next WP, never merge to `master` yourself, never force-push, never
`continue-on-error` beyond the one bounded exception ROADMAP M0.3 names.
Stop for a human only for the "Ask a human before" list in `CLAUDE.md`, or when
a decision's recommended default is not safe and reversible — then leave the ADR
and a ROADMAP note with the tree green.

**Environment.** Windows 11, MSVC 2022, `CSE_VCPKG_ROOT` set, presets in
`CMakePresets.json`; `gh` is installed and authenticated; `ctest -LE "perf|gl"`
runs in seconds, the `gl` tests need this machine's GPU (CI runs them under
llvmpipe); Linux exists only in CI. Assets stage from `Editor/src/Exported/` and
`Games/UntitledFighter/Assets/` into the build tree — edit the source copies.

**First task:** ROADMAP **M0.1** (archive the four originals verbatim into
`docs/archive/`). Then M0.2, and onward in order. Run until the **M0 gate** is
green (`scripts/check_docs.py` required in CI, ADR-010 marked Implemented) and
end the session with the report `CLAUDE.md` describes and the single next
action named. If you finish M0 with budget left, continue into M1.1 on
`roadmap/M1` and stop at the end of that WP.
