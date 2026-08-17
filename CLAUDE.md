# GameEngineFramework — how to work here

Cat Splat Engine: a C++17 / OpenGL 3.3 engine (EnTT ECS, 11-pass renderer,
in-game UI toolkit, three physics backends behind one seam) whose first title,
**Untitled Fighting Game** under `Games/UntitledFighter/`, is a deterministic
rollback fighter that doubles as the case study for a combo-termination paper.
The engine's job is to make that proof visible and convincing.

## Read these first, in this order

1. `docs/ROADMAP.md` — the ONLY roadmap. What is done, what is in flight, what is
   next. Start every session here; end every session by updating it.
2. `docs/DETERMINISM.md` — the rules the simulation, build and data must obey,
   each with how it is enforced. *(Created by ROADMAP M0.2; until then the
   contract is `docs/ARCHITECTURE.md` §4.)*
3. `docs/MAINTENANCE.md` — the change loop (failing test first, prove the fix by
   reverting it, build all four configurations) and the invariants that bite.
4. `docs/STYLE.md` — comments say WHY and name the bug they prevent; tests are
   named as properties; diagnostics list what exists.
5. `docs/ARCHITECTURE.md` and `docs/adr/` (or `docs/adr/ADR-*.md` before M0.4) — why
   things are the way they are. ADRs are frozen records. **ADR-011** (mechanics
   are fields, not rules; visuals are a pure function of frame data) constrains
   every kernel, data and presentation change.
6. `docs/manual/` — how to use a subsystem. `docs/archive/` — history; never
   cite it as current.

## The session loop

1. `git status` must be clean and on `master` (branch first if it is not).
   Read ROADMAP "Now". If a WP is in flight, continue it; else take the top `[ ]`
   in milestone order and mark it `[~]` with your name and the date.
2. For the WP: write the failing test its "Done when" names, make it pass,
   **prove it by reverting** (`grep -c REVERTED` must be 0 before commit),
   build the four presets, run the suite.
3. Same commit: fix any doc sentence the change made false (search for the OLD
   name or path, not the new one); update ROADMAP; the header comment of any
   contract you changed.
4. `python scripts/check_determinism_flags.py` and (once it exists)
   `python scripts/check_docs.py` are green.
5. Commit with a message written for `git blame` in two years: what was wrong,
   what it cost, what you chose and what you did not. Never amend published
   history, never force-push, never skip hooks, never `continue-on-error`.
6. A WP that reveals a decision: write `docs/adr/ADR-NNN-<slug>.md` as
   *Proposed* with a recommended default. If the default is safe and reversible,
   proceed under it and say so in ROADMAP; otherwise stop, leave the ADR and a
   ROADMAP note, and end the session with the tree green.

One WP in flight at a time. Finish before starting. Scope creep is a new WP,
written into ROADMAP, not folded in.

## Branches, CI, and ending a session

- Work on `roadmap/<milestone>` (e.g. `roadmap/M0`), never directly on `master`.
  Push after every WP; `gh run watch` (or `gh pr checks`) until the four
  required jobs are green before starting the next WP. Red CI is the next task,
  not a note.
- **Do not merge to `master`.** Open or update one PR per milestone; the human
  merges. (The human may lift this per session — treat that as a per-session
  grant, not a standing one.)
- A session ends when: the milestone gate is green; or the next WP needs a
  human (see below); or a decision's default is not safe — then the ADR is
  written, ROADMAP says so, and the tree is green. Never end mid-WP with a red
  tree; revert to the last green commit instead and note it in ROADMAP.
- End every session with a short report: WPs closed (sha, the test that proved
  each, and that it failed when reverted), docs touched, ROADMAP delta, CI
  state, and the single next action. The commit messages carry the *why*; the
  report carries the *what*.

## Never (each is enforced somewhere; do not route around it)

- Add a fast-math flag anywhere, or link the kernel (`CseKernel`) to anything.
- Put `float`, `<cmath>`, `<random>`, `<chrono>`, a wall clock, an
  `unordered_*` iteration, an allocation or an `entt::entity` in the simulation
  (`Games/UntitledFighter/Kernel/`, `Games/UntitledFighter/Game/`).
- Make `Engine/`, `Editor/`, `Player/` or `Cooker/` depend on a title. A title
  depends on the engine; never the reverse.
- Change `GameState` outside a planned expansion — it is a wire contract with a
  cross-toolchain golden hash; batch changes, re-golden once, review once.
- Use `FixedTimestep`, `paused_` or `timeScale_` to decide how many simulation
  ticks run while a session is live; the session decides, zero is legal,
  dropping is not.
- Annotate a living doc with AMENDED / STRUCK / strike-through — rewrite the
  sentence. Edit an accepted ADR beyond its Status line — write a new ADR.
- Add a status table, feature matrix or "not yet built" list anywhere but
  `docs/ROADMAP.md`. Restate a rule instead of linking `docs/DETERMINISM.md`.
- Commit an asset whose licence is not written down beside it.
- Publish a claim (README, website, paper text) that outruns a test in CI.
- Add a renderer feature that the showcase does not need (skinning is the one
  admitted).
- Hard-code a mechanic, speed, window or reaction as a kernel constant that a
  character file cannot set. Every mechanic is a per-move or per-character
  field, off by default, with ADR-011's five parts (schema field appended ·
  kernel slot · loss-ledger row · property test · showcase variant).
- Let presentation own state the sim did not produce, or delay a sim action
  for an animation. Pose is a pure function of sim state; tails are always
  cancelable; a blend never moves a box.

## Ask a human before

Spending money or creating accounts; publishing anything outward (website,
release, paper text, social); deleting user data or git history; changing a
licence; adding or upgrading a dependency in `vcpkg.json` or a submodule (write
the ADR first, then ask); anything you cannot undo with `git revert`.

## Commands

```
cmake --preset x64-relwithdebinfo-tests && cmake --build --preset x64-relwithdebinfo-tests
ctest --preset x64-relwithdebinfo-tests --output-on-failure        # everything (needs a GPU for -L gl)
ctest --preset x64-relwithdebinfo-tests -LE "perf|gl"              # what CI gates on
cmake --build --preset x64-debug && cmake --build --preset x64-release && cmake --build --preset x64-relwithdebinfo
python scripts/check_determinism_flags.py --self-test && python scripts/check_determinism_flags.py
```

`CSE_VCPKG_ROOT` must point at a vcpkg checkout (per machine). Linux:
`scripts/linux-build.sh`. Executables land in `out/build/<preset>/build/bin/<Config>/`;
runtime assets are staged there from `Editor/src/Exported/` and
`Games/UntitledFighter/Assets/` — edit the *source* copies, and check which copy
you are running before you debug anything.

## Definition of done for a work package

Its "Done when" test exists, fails without the change and passes with it, and
runs in CI (add the `gl` label if it needs a context); the four presets build;
the manual page it touched says the new truth; ROADMAP shows `[x] <sha>`; the
determinism and docs gates are green.

## Documentation, the five-line rule

1. One home per fact; link, do not restate.
2. Same commit: a change that makes a sentence false fixes the sentence.
3. Rewrite living docs; freeze ADRs (Status line only).
4. Cite docs by anchor, code by path; numbers only where a test asserts them
   or in ROADMAP with a date.
5. `Verified: YYYY-MM-DD @ sha` at the top of every living doc, bumped only
   after re-reading it against the code; `scripts/check_docs.py` in CI.
