# ROADMAP — the one place status lives

Verified: 2026-08-19 @ c4539be

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
loaded with different frame data, must show different infinites** (microwalk,
jump cancels, wall bounces, counter-hit links, meter loops), because every
mechanic is an opt-in field on a move, never a rule in the kernel
([ADR-011](adr/ADR-011-mechanics-are-fields.md)). Visuals are a pure function of
frame data; return-to-idle tails are always cancelable. Everything is provable
and showcased **before** any real art is made; placeholder rigs (Mixamo) come
first, SF6-tier art last, and only after the showcase already sells the paper
without it. Details, tests and proofs of the four properties:
[NORTHSTAR.md](NORTHSTAR.md).

## Now

| In flight | Owner | Since |
|---|---|---|
| M1.1b — the data path onto M1.1a's fields | Claude | 2026-08-19 |

One work package in flight at a time. The next unblocked one is always the top
`[ ]` in milestone order below.

## Legend

`[ ]` not started · `[~]` in flight · `[x]` done (commit) · `[-]` dropped (why) ·
**Size** S = hours–a day, M = days–a week, L = weeks. Calendar estimates are
absent on purpose: the six days after the first kernel commit (`a3cc8c7`)
delivered two thirds of a phase that was budgeted at 8–10 weeks.

Every work package (WP) has a **Done when** that is a test name, a number or a
demo. A WP is done when that test exists, fails without the change, passes with
it, runs in CI, and the manual page it touched says the new truth.

---

## M0 — One roadmap, one rule *(size M)*

Consolidate the documentation and install the gate, first, because every later
WP will be read by someone (or something) that starts from these files. The
full mapping is [ADR-010 §5–§7](adr/ADR-010-one-roadmap-one-rule.md); this is the
work list.

- `[x] 5f756c6` **M0.1 Archive the originals.** *(S)* Copy `NORTHSTAR.md`,
  `ARCHITECTURE.md`, the audit findings and the July 2026 engine audit verbatim to
  `docs/archive/<NAME>-<date>.md`; add `docs/archive/README.md` ("history;
  nothing here is current; frozen ADRs' line citations resolve here").
  **Done when:** the four files exist under `docs/archive/`, byte-identical to
  their originals at `99669cc`.
  **Deviation:** the README does not claim frozen ADRs' line citations *resolve*
  — two spot checks show they never did — only that they are read against the
  archived copy, which stops the gap growing. Byte-identity becomes a
  `check_docs.py` check in M0.3.
- `[x] 26b9b1e` **M0.2 `docs/DETERMINISM.md`.** *(S–M)* One table: rule · enforced by ·
  where. Sources: `ARCHITECTURE.md` §4 (the contract), `NORTHSTAR.md` appendix,
  `MAINTENANCE.md` "Never add a fast-math flag", the rules restated in
  `docs/manual/fighting-core.md`. Every rule names its enforcement — CI script,
  `static_assert`, configure guard, a test by name, or "review only". A rule that
  is "review only" but *could* be mechanical becomes a WP here (M1.1(d) is one).
  **Done when:** every rule in ADR-010's inventory has one row, and
  `docs/manual/fighting-core.md` links instead of restating.
  **Deviations, all from reading the tree rather than the contract:**
  ARCHITECTURE §4.7's `cse_fp_strict` target does not exist and cannot — linking
  it would trip the kernel's own "links nothing" guard (B5); §4.7's claimed
  `workerThreads == 0` startup assert and `JPH_VERSION_ID` `static_assert` do not
  exist (B6); §4.1's `Phase` parameter and §4.2's reflection table do not exist
  (K11, S8). Four review-only rules that could be mechanical became **M1.0**.
- `[x] 9b7d26c` **M0.3 `scripts/check_docs.py` + CI step, advisory.** *(S)* Checks:
  relative links resolve; backticked repo paths exist (`:line` stripped;
  `docs-ok` escape); living docs carry `Verified: <date> @ <sha>` in the first ten
  lines; ADRs carry a `Status` line; no `AMENDED`/`STRUCK`/`~~`/`Correction (`
  outside `docs/adr/` and `docs/archive/`; `--self-test`. Add as a step in the
  `determinism-flags` job in `.github/workflows/ci.yml` with
  `continue-on-error: true` **only until M0.6**. Its first run's failure list is
  the checklist for M0.4–M0.6.
  **Done when:** `python scripts/check_docs.py --self-test` passes and the step
  is in CI.
  **First run: 78 findings** — 41 cited paths that do not resolve, 25 living docs
  with no stamp, 12 annotation markers, 0 dead links. Re-read it any time with
  `python scripts/check_docs.py`; the leaders are
  `Editor/src/Exported/Characters/` ×9 (moved) and `Engine/src/gameplay/` ×8 <!-- docs-ok: named here as paths that do NOT resolve -->
  (never existed). **Additions:** a sixth check verifies `docs/archive/` still
  holds its frozen bytes, by git blob hash recomputed in Python — CI checks out
  with no history, so `git rev-parse 99669cc:…` is not available there. The
  `--self-test` step is **required** while the scan is advisory: a detector that
  has stopped detecting reports a clean tree. The job's display name is
  deliberately unchanged — it is what branch protection was configured against,
  so renaming it is a repository-settings change for the human, not a workflow
  edit (M0.6).
- `[x] 2daa884` **M0.4 Rewrite the top level.** *(M)* `NORTHSTAR.md` → one screen (goal,
  four properties, done-tests, proofs — ADR-010 §2). `ARCHITECTURE.md` → D1–D9
  with every amendment folded into the prose, D9 as the four answers, §2
  rejection table plus `NORTHSTAR.md` §6's rows, the research plug-point in five
  paragraphs; **remove** the build order, the contract, the first week and the
  appendix. Fix `Engine/src/gameplay/` → `Games/UntitledFighter/Kernel/`. <!-- docs-ok: names the wrong path in order to replace it -->
  `git mv` `ADR-001`…`ADR-010` to `docs/adr/`; add/normalise one Status line
  each (005 Implemented P0–P2, 006 Implemented, 007 "trigger 3 fired — ADR-008",
  008 Implemented @ `1aaa2d1`, 009 Implemented @ `41ea6e5`, 002/003 name the
  standing verdict); `docs/adr/README.md` index. Repoint every inbound link.
  **Done when:** `check_docs.py` reports no dead links or paths outside the
  manual.
  **Met:** 0 dead links, 0 paths outside `docs/manual/`. The gate went 78 → 32
  findings; what is left is 23 missing stamps, 4 markers and 5 manual paths,
  which is exactly M0.5 and M0.6. **Additions:** ADR-011 moved too (ADR-010 §5
  lists `adr/ADR-001 … ADR-011`; M0.4's own text stops at 010). Moving the ADRs
  broke nine of their *outbound* links, repointed mechanically — no ADR's text
  changed, only its Status line. **A seventh gate rule:** `check_docs.py` no
  longer path-checks inside `docs/adr/`. An ADR describes the tree as it was, so
  demanding its paths resolve forces either rewriting frozen records or never
  going green.
M0.5 split in two, at the seam between *what the manual says* and *whether
anyone has re-read it*. The stamp is a claim — "I read this page against the
code" — and batching it behind the prose work is how a stamp becomes decoration.

- `[x] e1f6194` **M0.5a The shipped-but-undocumented, and the stalest page.** *(M)*
  Fix the paths in `docs/manual/fighting-core.md`, delete its "Not there yet"
  (this file is that list now) and its restated rules (link `DETERMINISM.md`);
  add the Game layer (`FightSession`, input sources, replay, combo watcher) and
  the Modes (training, frame step, HUD). `docs/manual/editor.md`: Build Settings,
  Combo Prover, modes in the Game view.
  **Done when:** `check_docs.py` reports no path finding anywhere, and the ~8,500
  shipped lines of `Games/UntitledFighter/Game/` and `Games/UntitledFighter/Modes/`
  have a manual page.
- `[x] e2f08bd` **M0.5b One home per fact, across the manual.** *(M)* De-duplicate the
  four repeated blocks to one canonical page each (isolation note + frame order →
  `architecture.md`; staging rule + packaging + scene JSON →
  `scenes-and-shipping.md`); merge `lua-scripting.md` into
  `gameplay-scripting.md` as a "presentation and tooling only (D7)" section;
  fold `BUILDING_LINUX.md` into `getting-started.md`; `performance.md` gains the
  laptop section from the archived audit.
  **Done when:** `docs/manual/` has no second copy of the four blocks, and
  `docs/` holds no page that another page now owns.
- `[x] 9f518c2` **M0.5c The read-through, then the stamps.** *(M)* Fourteen pages under
  `docs/manual/`, one at a time: read the page against the code it cites, fix what
  it gets wrong, and only then write `Verified: <date> @ <sha>`. **The stamp is
  the claim**, so it cannot be applied in the same pass that wrote the prose — a
  page stamped by its own author on the day it was written says nothing. M0.5a's
  four stale `ARCHITECTURE.md` citations are the shape of what this finds: a live
  link into a section that no longer means what it meant, which no gate can see.
  Start with the pages nothing else in M0 touched — `ui.md` (1,994 lines),
  `rendering.md`, `physics.md`, `assets.md`.
  **Done when:** `check_docs.py` reports no stamp finding under `docs/manual/`.
  **Met**, and the read found six things no gate could:
  (1) three pages told you to ship with `cpack -G ZIP`, which was **deliberately
  removed** — it skipped every validation the Build action performs and exited 0
  on a tree the editor had never saved in; (2) `entities-and-components.md`'s
  `uiDocument` row listed six serialized keys where the serializer writes nine,
  omitting the whole UI-scaling feature; (3) `editor.md` did not mention that Stop
  **reloads from file** rather than restoring the snapshot when the game swapped
  scenes mid-play; (4) `index.md` advertised a "generated API Reference" that is
  57 hand-written lines nothing generates; (5) `rendering.md` and `ui.md` kept
  four gap lists, which belong here and nowhere else; (6) `getting-started.md` and
  `performance.md` both called `test_perf_render`'s label `perf` when it is
  `perf;gl`, so `-LE perf` does not exclude it from a `gl` run.
  **Gate addition:** the two moved top-level directories joined `PATH_PREFIXES`. A prefix list
  of directories that *exist* is blind to the citation that went stale — fourteen
  lines still cited the kernel at its old top-level path, and the gate walked past every one.
- `[x]` **M0.6 Entry points, and make it required.** *(S)* `README.md`: pitch,
  one roadmap paragraph + link, docs table for the new tree, build/run/ship;
  delete the feature matrix, "Not Yet Built" and the scale line; fix Project
  Structure. `MAINTENANCE.md`: "Keeping the documentation true" → the five-line
  rule (ADR-010 §8.1) + the adversarial audit as the periodic check; determinism
  invariants → pointer to `DETERMINISM.md`. Delete `docs/api-index.md`, <!-- docs-ok: names the files this WP deletes -->
  `docs/CMakeLists.txt`, `docs/Doxyfile.in` and the root <!-- docs-ok: names the files this WP deletes -->
  `add_subdirectory(docs)` (ADR-010 §9.1 default). Remove
  `continue-on-error`. Set ADR-010 Status → Implemented.
  **Done when:** CI is green with the docs step required; `docs/` is six living
  files + `adr/` + `manual/` + `archive/`.
  **Met.** `docs/` is exactly `ARCHITECTURE` · `DETERMINISM` · `MAINTENANCE` ·
  `NORTHSTAR` · `ROADMAP` · `STYLE` + `adr/` + `manual/` + `archive/`; the gate
  reports **0 findings** over 39 files and is required. `AUDIT_FINDINGS.md` and
  `ENGINE_AUDIT_2026-07.md` were deleted too — their archived copies are the
  record, which is what M0.1 froze them for. **One thing for the human:** the
  `determinism-flags` job now carries the docs gate as well, and its display name
  still says *FP flag gate*. Renaming it means re-pointing branch protection,
  which is a repository-settings change, not a workflow edit.

---

## M1 — The graph is the game *(size L)* — the paper's central claim

Close the measured gap between what the prover proves and what the kernel does,
make every mechanic an opt-in field per move ([ADR-011](adr/ADR-011-mechanics-are-fields.md)),
make the authoring loop live, and build the tool-assisted showcase that turns
every verdict — for one fighter under many frame-data patches — into a
watchable, verified replay. **`GameState` is a wire contract** (ADR-005 §3):
all of M1's state changes land as **one** expansion with one re-golden
(`tests/test_determinism_crossplat.cpp`), reviewed once — including the fields
M1.3 and M3.1 will need (M1.1 reserves them).

- `[x] e042415` **M1.0 Close the determinism gate's own gaps.** *(S)* Written by M0.2's
  inventory, which found four rules that are review-only today and need not be
  ([DETERMINISM.md](DETERMINISM.md) §3). (a) An **include allowlist** for
  `Games/UntitledFighter/Kernel/` in `scripts/check_determinism_flags.py`: that
  module's entire include list is `<cstdint>`, `<type_traits>`, `<cstring>`, so a
  whitelist is exact and cheap, and it closes "never allocates" and "never
  iterates an associative container" (K4, K5) in one check. Scope it to
  `Games/UntitledFighter/Kernel/` — `Games/UntitledFighter/Game/` uses `<string>`
  and `<vector>` legitimately, which is why
  the existing purity globs cover both modules and this one must not.
  (b) `/arch:`, `-march=`, `-mavx` into `FORBIDDEN` (B3): none is present today
  and none would be caught; `/arch:AVX2` licenses FMA contraction, which is the
  same rounding change `-ffp-contract=fast` buys.  (c) `Engine/src/ui/UIWorld.cpp`'s
  tie-break sorts on `entt::to_integral` — the raw handle, version bits included
  — while its own comment claims stability "across a save/reload", which is
  exactly what version bits destroy (I3). The other two ordering sites already
  use `entt::to_entity`.
  **Done when:** `python scripts/check_determinism_flags.py --self-test` covers
  each new pattern *and* proves the allowlist fires on a `#include <vector>` laid
  down in `Games/UntitledFighter/Kernel/`; `UIWorld.DocumentsAtOneSortOrderKeepTheirOrderAcrossAReload`.
  **Met.** The self-test reports 14 flag patterns (was 11) and six
  include-allowlist probes; proved by reverting three ways — widening the
  allowlist, dropping a flag pattern, and adding `#include <vector>` to the real
  `GameState.h`, which the gate named by file, line and header. **(c) was a live
  bug, not a tidy-up:** the test failed before the fix with *"'first' painted
  last in the session that saved the scene and 'second' does after loading it"*.
  **Addition:** the failure message now fits the finding — an include hit no
  longer tells its author to "use sub-units: 1 pixel = 256", which is the same
  class of wrong signpost `module_of()` exists to prevent.
  [DETERMINISM.md](DETERMINISM.md) K4, K5, B3 and I3 move from *review* to *CI*
  and *test*.
M1.1 splits in two, at the seam between **layout** and **behaviour** — and *only*
there, because ADR-005 §3's requirement is one wire change, one re-golden,
reviewed once. M1.1b adds no field, so it needs no second re-golden.

**Measured 2026-08-18 before starting, because the numbers decide the split:**
`Fighter` is 52 bytes (7×`int32`, 8×16-bit, 8×`uint8`, no padding) and
`GameState` is a 20-byte header plus `Fighter p[8]`. `meter` and `juggle` are
named in **93 places across 27 files** — kernel, data, prover adapter, the HUD
and twelve test files. That is the cost of the layout change, and it is why it
is not sharing a commit with new behaviour.

**A decision the split forces, taken here so M1.1b cannot be tempted into a
second expansion:** `Fighter::juggle` is **kept**, and M1.1b makes it the
kernel's mirror of whichever resource slot the file declares as juggle rather
than deleting it. Deleting it would be tidier and would change the wire format a
second time. `meter`, which no file in `Games/UntitledFighter/Kernel/src/` ever
writes, is removed outright in M1.1a — it is dead, and `res[]` is what replaces
it. Safe and reversible, so proceeding under it (CLAUDE.md).

- `[x]` **M1.1a The one state expansion, and nothing else.** *(M)* Layout only,
  no behaviour change, so the golden is re-recorded exactly once. (a) `Fighter`
  gains `std::int32_t res[kMaxResources]` (`kMaxResources = 4`) and loses the
  dead `meter`; it gains M1.3's reaction fields — `std::uint8_t reaction`,
  `std::uint8_t bounces`, `std::uint16_t flags` — reserved now so M1.3 adds no
  field. (b) `GameState` gains M3.1's event ring: `Event ev[kMaxEventsPerTick]`
  where `Event` is `{uint8 slot, uint8 kind, int16 a, int16 b}`, plus
  `std::uint8_t evCount`, and whatever explicit `pad_` keeps both structs free of
  compiler padding. (c) The type assertions move from `tests/test_kernel.cpp`
  into `GameState.h`, where a change to the struct meets them, and gain
  `static_assert(std::has_unique_object_representations_v<GameState>)` — nobody
  asserts padding today and hashing raw bytes depends on it
  ([DETERMINISM.md](DETERMINISM.md) S3).
  **Done when:** `GameState.h` carries all five assertions and the two `sizeof`
  sums; `tests/test_determinism_crossplat.cpp`'s golden and its three checkpoints
  are re-recorded in **one** commit with the old values quoted in the message;
  the whole suite passes. **Traps:** `alreadyHitBits` is `uint8` and
  `kMaxFighters` is 8 — the assert tying them together must survive; the
  crossplat script drives jumps by input bits, so it must still reach the same
  ticks.
  **Met.** `Fighter` 52 → **68 bytes**, `GameState` 436 → **664**. Goldens
  re-recorded once: rolling `6D8A7334` → `F2001926`, checkpoints `F55B64EB` →
  `5904B505`, `B1CD00EA` → `A580ECA8`, `47E49F19` → `A49479EB`. 58/58 pass.
  **The padding assertion earned itself immediately** — deleting `pad2_[3]`
  makes the build fail with the sentence that names the fix, which is what the
  old arrangement (a `sizeof` sum in a test file) could only have told you after
  linking. `Fighter::meter` turned out to be five C++ sites; the other 88
  mentions were `juggle`, the data layer's resource *names*, or the prover's own
  record.
- `[~]` **M1.1b The data path onto the fields M1.1a reserved.** *(M)* Claude, 2026-08-19. No layout
  change. `ResourceDef {initial, floor, ceiling, refill}` per slot in
  `FighterData`; `effect[]` on moves and cancel edges (applied per authored
  contact) and `guard[]` (a minimum, checked before the move starts); index *i*
  in the file = index *i* in the kernel = index *i* in the prover, the A03
  contract true by construction. `walk_speed`, jump arc, gravity, default
  pushback and hitstop move out of `Simulate.cpp`'s `constexpr`s into
  `FighterData`, defaulted by the schema ([ADR-011](adr/ADR-011-mechanics-are-fields.md)
  decision 1). The resource-guard rows in
  `Games/UntitledFighter/Data/src/MatchBuilder.cpp`'s loss ledger become `Exact`.
  **Done when:** `P3Resources.MeterGainsOnHitAndSpendsOnGuard`,
  `.AGuardedCancelRefusesBelowTheMinimum`, `.IndexOrderIsTheFilesOrder`,
  `P3Movement.WalkSpeedComesFromTheFile`; `tests/test_gap_extent.cpp`'s
  `EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter` rewritten to assert the
  opposite. **Traps:** D8 quantisation once at load; `decay.floor` ≤ min hitstun
  (A01); the shipped `walk_speed` must equal today's `kWalkSpeed` or this is a
  behaviour change and the golden moves again — check before writing the field.
  **Checked 2026-08-18, and it does not. The trap fired.** `fighter_a.json`
  authors `walk_speed: 0.03` with `quantized_sources.walk_speed_px_per_tick: 3`;
  at the default 100 px per reach unit that quantizes to **768 sub-units,
  3 px/tick**. `Simulate.cpp`'s `kWalkSpeed` is **512 sub-units, 2 px/tick**. So
  honouring the file makes `fighter_a` walk **50% faster** and every position in
  the scripted match moves. **M1.1b therefore re-goldens, and that is correct
  rather than a defect** — the hash moves because the *game* changed, which is
  what a golden is for. It is a different category from M1.1a's re-golden, which
  moved bytes and no behaviour, and the commit message must say which it is.
  Two consequences to carry: the microwalk variant's premise (ADR-011 §4) is
  `walk_speed` +1 px/tick **from whatever the base is**, so measure from 3 and
  not from 2; and `quantized_sources` carries `walk_speed_px_per_tick` while the
  loader reads `walk_speed_sub_per_tick` — a key that reads as authoritative and
  is not consulted. Confirm which one wins before trusting either.
  **Attempted 2026-08-18 and reverted, with the blocker measured.** The vertical
  slice works: `FighterData::walkSpeedSub`, `MatchBuilder` carrying it, the
  kernel reading it with a zero-means-unauthored fallback, and
  `P3Movement.WalkSpeedComesFromTheFile` passing — and failing, with both
  distances named, when the constant is put back. **The golden does not move**:
  the crossplat scripted match builds a synthetic `MatchData` authoring no walk
  speed, so the fallback keeps it byte-identical. That is worth knowing before
  the next attempt, because it means M1.1b's re-golden comes from resources, not
  from walking.
  **The blocker is arithmetic, not effort.**
  `TrainingModeReadout.WalkingClosesTheGapAndOnlyTheIntervalRuleSurvivesContact`
  opens the fighters 34 px apart and requires the walk to land **exactly** on
  both the touch tick and the coincident tick: `interval / walkStep` and
  `dx / walkStep` must both come out whole. That needs `dx` and `dx − 26 px` —
  the two body half-widths — to both be multiples of the step, and at 3 px/tick
  it is **impossible**, because 26 is not a multiple of 3. The test's premise is
  tied to a 2 px/tick walk. Two ways out, and the choice should be argued rather
  than assumed: give that bench character an explicit 2 px/tick and say why (it
  is a HUD-arithmetic test, not a walk-speed test), or restate it in terms of
  *crossing* zero instead of landing on it — which loses "the one tick this test
  is really about". The other failure is mechanical: `character.walk_speed`
  becomes `Exact` in two expectation tables, and the training-mode test's
  gap-pinning assertion inverts, which its own message already asks for.
  **Decided 2026-08-19: the FIRST way, and the reason is that the premise was
  already there.** The bodies open 34 px apart and are 26 px wide together, so
  the step must divide both 8 and 34; two does and three divides neither. That
  test has always depended on a 2 px/tick walk — it just inherited it silently
  from `kWalkSpeed` and never said so. Authoring it on the built `FighterData`
  makes a hidden premise an owned one, which is what this WP is for, and it is
  reversible in one line. Restating the test in terms of crossing zero would
  weaken the assertion it exists to make, so it was not done.
  **Slice 1 of M1.1b landed: walk speed.** `FighterData::walkSpeedSub` (zero =
  unauthored, kernel keeps its placeholder), `MatchBuilder` copying it with the
  loss row now `Exact`, and `Simulate` reading it.
  `P3Movement.WalkSpeedComesFromTheFile` and
  `P3Movement.ACharacterThatAuthorsNoWalkSpeedKeepsThePlaceholder`; the first
  reports 5120 sub-units instead of 7680 when the kernel's read is reverted.
  **The golden did not move**, confirmed rather than assumed: the crossplat
  scripted match builds a synthetic `MatchData` that authors nothing and takes
  the fallback, so `test_determinism_crossplat` is untouched. The re-golden this
  WP still owes therefore comes from RESOURCES, and the commit that causes it
  must say so.
  **Still open in M1.1b:** `ResourceDef` per slot, `effect[]` on moves and cancel
  edges, `guard[]` minimums, and the remaining movement constants (gravity, jump
  impulse, pushback, hitstop) out of `Simulate.cpp`.
  **A batching decision the next slice forces, taken 2026-08-19 under a safe
  default.** Resources need fields on `FighterData`, `MoveDef` and `CancelEdge`;
  the movement constants need fields on `FighterData`. All three are hashed by
  the connect handshake, and `sizeof(MoveDef) == 128` is asserted with its
  growth history written into the message. [ADR-005](adr/ADR-005-playable-priority.md)
  §3's rule — batch changes, re-golden once, review once — therefore applies to
  `MatchData` exactly as it does to `GameState`, so growing these structs twice
  is the thing to avoid.
  **The complication is that the two halves are not equally ready.** Resources
  are already loaded: `CharacterData` carries `ResourceDef`, `ResourceAmount`
  and per-move `effect`/`guard`, and the schema declares them. Gravity and jump
  are NOT: `schema.v2.json` has no key for either, `CharacterData` has no field,
  and the only authored number anywhere is an undeclared
  `quantized_sources.gravity_sub_per_tick2` in `fighter_a_infinite.json` that
  nothing reads. Adding those keys is a change to the file the *published prover*
  reads, which is the same contract concern that split M1.1e out.
  **Default taken: expand the kernel structs ONCE, for resources and movement
  together, with movement zero-means-unauthored.** The kernel fields cost
  nothing until something sets them — a zero gravity falls back to
  `Simulate.cpp`'s placeholder exactly as `walkSpeedSub` does — so the struct
  moves once now and authoring gravity in a file later is a purely additive
  schema change that does not move it again. Safe (no behaviour change on any
  existing character) and reversible (delete the fields), so per CLAUDE.md this
  proceeds without stopping for a human, and is recorded here because it is a
  decision rather than a detail.
  **Slice 2 landed: the batched contract change, layout only.** `MoveDef` gains
  `effect[kMaxResources]`, `guard[kMaxResources]` and a `guardMask` (a mask and
  not a sentinel, because zero is a legal minimum for a resource with a negative
  floor); `FighterData` gains `ResourceDef resources[kMaxResources]`,
  `resourceCount`, `gravitySub` and `jumpImpulseSub`; a kernel-side
  `ResourceDef` with no name in it, because the loader resolved names to indices
  once and the index is the contract. `MoveDef` moves 128 → 164 and the assert is
  written as *old size plus the new members* so it still asks "did padding
  appear". `Simulate` reads gravity and jump with the same zero-means-unauthored
  fallback as walk speed. **No behaviour changed and no test moved**: 58/58 with
  the expansion in, and the crossplat golden untouched, because nothing authors
  any of these yet.
  **Next, and it is the behaviour half:** priming `Fighter::res[]` from
  `ResourceDef::initial`, applying `effect[]` on contact, checking `guard[]`
  before a move starts, and mirroring whichever slot the file calls juggle into
  `Fighter::juggle`. **The one open design point** is where priming happens.
  `fighter_a` authors `meter` with `initial: 300`, and meter PERSISTS across
  combos, so it cannot join the per-combo restore beside juggle — that block
  would wipe accumulated meter every time the defender left hitstun. It belongs
  at match start, but `ResetMatch` does not take the `MatchData` and has **71
  call sites across 16 files**. The intended shape is an optional
  `const MatchData*` parameter defaulting to null, where null means "this reset
  does not know the characters, so resources stay zero" — additive, no ripple,
  and the real paths (`FightSession`, `FightView`) pass it.
  **Slice 3 landed: resources are simulated — and it did NOT close the gap, which
  is the finding.** Priming happens on the match's first tick rather than in
  `ResetMatch` (the kernel already sited the juggle restore that way and says
  why); `MatchBuilder` scatters the authored sparse `(index, value)` lists into
  dense arrays in FILE ORDER; `ResolveHits` applies `effect[]` on contact, on
  block as well as on hit, clamped to each resource's floor and ceiling; and
  BOTH start routes — the button scan and the cancel scan — refuse a move whose
  `guard[]` minimum is unmet, falling through to the next slot rather than
  eating the press. `move.effect`, `move.guard` and `resources` are all `exact`
  in the loss ledger now. Proved by `P3Resources.MeterGainsOnHitAndSpendsOnGuard`,
  `.AGuardedCancelRefusesBelowTheMinimum` and `.IndexOrderIsTheFilesOrder`; all
  three fail with the kernel change reverted.
  **THE HEADLINE COUNT DID NOT MOVE, and the reason is a decision nobody has
  taken yet.** Three tests asserted their premise as *"the kernel does not
  simulate resources"* and went red on the loss row alone; their outcome
  assertions never moved. `GapExtentKernel.NinetySevenOfThe121RunForever` still
  counts 97. The gap is now narrower and exactly locatable: **`ApplyEffects`
  CLAMPS at the authored floor.** The model ends these cycles when juggle runs
  out; the kernel tracks the same juggle, arrives at the same zero, and carries
  on, because nothing refuses a move whose effect would breach a floor. A guard
  would refuse it — but no shipped move authors a guard on juggle, and juggle is
  spent through `effect`, not `guard`.
  **THE DECISION, AND IT IS THE AUTHOR'S BECAUSE IT MOVES THE PAPER'S NUMBER —
  but it is a WIRING decision, not a design one, and the first draft of this
  paragraph got that wrong.** The refuse-on-breach semantics are already
  designed, already documented and already implemented: `MoveDef::juggleCost`'s
  own comment says *"a hit that would take the defender's remaining budget below
  zero does not land"*, and `ResolveHits` enforces exactly that at the line
  above `target[a] = d`, whose comment names the consequence outright — *"its
  `nonNegative` condition is what ends all 41 of fighter_a's cycles in the model,
  and its absence here is what let 33 of them run forever."*
  **What is missing is the `MatchBuilder` wiring, and it is TWO fields rather
  than one.** `grep -c` in `MatchBuilder.cpp` is **0 for both `juggleCost` and
  `juggleMax`**, so the whole juggle path is unwired at the builder: the budget
  is never handed out and the cost is never charged, which is why the gate reads
  `juggleCost > 0` and never fires. Both authored data are present —
  `fighter_a.json` declares `juggle` as resource slot 1 with `initial: 4`,
  `floor: 0`, `ceiling: 4`, and seven of its moves author `effect: {juggle: -1}`
  or `-2` — so wiring means `juggleMax` from `resources[juggleSlot].initial` and
  `juggleCost` from the move's effect on that slot. A half-wiring is worse than
  none: a budget with no cost never depletes, and a cost with no budget refuses
  every hit.
  **So the question is not "should a floor breach refuse rather than clamp" —
  the kernel already says yes for juggle. The question is whether to connect it
  now, because doing so is what the `test_gap_extent` measurement is currently
  quantifying the ABSENCE of.** Wiring it does not just change 97-of-121; it
  changes what that file is for. Not taken here: an agent should not retire a
  research instrument by finding its missing wire.
  The generic form — should any resource's floor refuse rather than clamp —
  stays open behind it, and `ApplyEffects` clamps today.
  **Also still open:** `effect[]`/`guard[]` on CANCEL EDGES (only moves carry
  them; `cancel.effect` and `cancel.guard` are untouched in the ledger), and
  mirroring whichever slot the file calls juggle into `Fighter::juggle`.
  `GapExtentModel.EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter` was expected
  to invert here and did not need to: it reads the MODEL (`CharacterData`), not
  the kernel, so it says nothing about what the kernel now does.
- `[-]` **M1.1 Resources, movement parameters, and the one state expansion.** *(M)* — split into M1.1a and M1.1b above.
  Today `Fighter::meter` exists and no file in `Games/UntitledFighter/Kernel/src/`
  writes it; juggle has bespoke rules; walk speed and jump impulse are
  `constexpr` in `Simulate.cpp` while `walk_speed` is authored and ignored.
  (a) **Positional resources**: `int32 res[kMaxResources]` in `Fighter` (default
  `kMaxResources = 4`), `ResourceDef {initial, floor, ceiling, refill}` per slot
  in `FighterData`, so *index i in the file = index i in the kernel = index i in
  the prover* — the A03 contract, true by construction. Moves and cancel edges
  carry `effect[]` (applied per authored contact) and `guard[]` (a minimum,
  checked before the move starts); the resource-guard rows in
  `Games/UntitledFighter/Data/src/MatchBuilder.cpp`'s loss ledger become
  `Exact`. Juggle keeps its refill-on-ground rule as `ResourceDef::refill`.
  (b) **Character constants become fields**: `walk_speed`, jump arc, gravity,
  default pushback and hitstop move from `Simulate.cpp` into `FighterData`,
  defaulted by the schema (ADR-011 decision 1). (c) **Reserve** in the same
  expansion: M3.1's event ring (`Event ev[kMaxEventsPerTick]`, `uint8 evCount`)
  and the reaction fields M1.3 needs (`uint8 reaction`, `uint8 bounces`,
  `uint16 flags`), so the wire format changes once. (d) Move the type assertions
  from `tests/test_kernel.cpp` into `GameState.h`, adding
  `static_assert(std::has_unique_object_representations_v<GameState>)` (no one
  asserts padding today; hashing raw bytes depends on it).
  **Done when:** `P3Resources.MeterGainsOnHitAndSpendsOnGuard`,
  `.AGuardedCancelRefusesBelowTheMinimum`, `.IndexOrderIsTheFilesOrder`,
  `P3Movement.WalkSpeedComesFromTheFile`; the golden re-recorded once;
  `tests/test_gap_extent.cpp`'s `EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter`
  rewritten to assert the opposite. **Traps:** D8 quantisation once at load;
  `decay.floor` ≤ min hitstun (A01); ADR-009's `alreadyHitBits` width assert;
  explicit `pad_` bytes; the crossplat test scripts jumps by input bits — keep
  that working through the character's jump move.
- `[ ]` **M1.1c Attack selection is (button × stance), not a button per move.** *(S–M)*
  **Found at review point R0**, by playing training mode and reading the HUD:
  the labels are wrong because the bindings are. `air_mp` has its own dedicated
  key (`O` → `kInputHK`), and `stand_hk` is bound to `kInputMK`
  (`Games/UntitledFighter/Modes/src/UntitledFighterMode.cpp`). Six attack buttons
  exist and six *moves* are bound, one each, so the button you press has no
  relationship to the strength you get, and no move outside that list of six can
  ever start.
  **The kernel is already right; the binding table is the wrong layer.**
  `MoveDef::stance` exists ([ADR-006](adr/ADR-006-stance-and-guard.md)),
  `StanceAllows` is applied in both the button scan and the cancel scan
  (`Games/UntitledFighter/Kernel/src/Combat.cpp`), and `fighter_a.json` already
  authors `air_mp` with `"stance": "air"`. Nothing in the simulation has to
  change for MP-while-airborne to select `air_mp`; the mode has to stop
  allocating a button per move.
  (a) Bind the six attack buttons **once each** — LP, MP, HP, LK, MK, HK — and
  let the character file's `stance` decide which move each starts. A binding
  becomes `button → strength`, never `button → move`.
  (b) Stop encoding stance in the button MASK. The manual's own gotcha is that
  `crouch_lp` bound to `{Down, LP}` can never fire, because `stand_lp` sits at a
  lower slot and its `{LP}` is a subset of what is held. Crouching is a **state**
  the fighter is in, not a chord — which is exactly what `kStanceCrouching` says.
  (c) A load or build assertion, because (a) makes shadowing possible in a new
  way: **two moves sharing a button must have disjoint stances**, or the lower
  slot wins forever and the higher one is dead on arrival. `MatchBuilder`'s
  existing `can never start` warning becomes this check, stated positively.
  (d) The HUD reads whichever move the kernel actually started, so the label
  follows from (a) rather than being corrected separately.
  **Done when:** `P3Attacks.OneButtonPicksTheMoveForTheStanceYouAreIn` — the same
  MP bit starts `stand_mp` grounded and `air_mp` airborne;
  `P3Attacks.TwoMovesOnOneButtonWithOverlappingStancesIsRefused`; and training
  mode binds six buttons rather than six moves.
  **Traps:** `kInputDown` is deliberately unbound today because nothing read it —
  (b) is what gives a crouch state something to read, so bind it in the same
  change or `kStanceCrouching` stays unreachable. The scan takes the **first**
  matching slot, which is what makes (c) the difference between a rule and a
  lottery. And moves started on buttons **HELD** while this WP was written, which
  is a separate bug and easy to mistake for a selection one; M1.1d fixed it, so a
  re-test of (c) now has to press rather than hold.
  **Blocks M1.3**, which adds movement moves and cancel edges targeting them — a
  jump cancel means little while a button can only ever start one move.
  **(a), (b) and (d) landed; (c) did not, and the reason is worth keeping.** The
  binding table now maps six buttons to eighteen moves and `Down` is bound, so
  every normal `fighter_a` authors is reachable and the HUD names one row per
  stance variant. (c) — teaching the shadow check that two moves sharing a bit
  with *disjoint* stances are not shadows — was written, worked, and was
  **removed again because nothing proved it**: a shadow is a warning, no test
  counts warnings, and reverting the check to "every stance overlaps" left all 58
  tests green. The test that would prove it has to use a character whose variants
  are `standing` vs `crouching`; the first attempt used Kung Fu Girl, who authors
  `stance: ground` on everything — and `Ground` overlaps both, so her warning is
  **correct** and the assertion was false. Write it against `fighter_a` or a
  synthetic `FighterData`, then restore the check.
- `[x]` **M1.1d Input edges and buffering — the second state expansion, batched.** *(M)* — `8795a46`
  **Found at review point R0**: holding an attack button rapid-fires it. The
  kernel says so itself — `StepAttack`'s scan is *HELD, not pressed*, and the
  comment above it has been asking for this field since it was written: "honest
  edge detection needs a `prevButtons` field inside GameState … that is a real
  gap, named rather than papered over, and it is the next field this file will
  want." Rapid-fire-on-hold is not a fighting-game mechanic; it also makes any
  "combo" the showcase records suspect, because holding one key is not a link.
  **This costs a SECOND `GameState` expansion, and that is worth saying plainly.**
  M1.1a called itself "the one state expansion" and reserved M1.3's reaction
  fields and M3.1's event ring so the format would change once. It did not
  reserve anything for input, because nobody had noticed the gap yet — it was
  found by playing, not by planning. So the honest move is not to pretend, it is
  to make this expansion the last one: **batch all three input needs together**,
  and check before writing whether anything else in M1–M3 wants state.
  (a) **Positive edge.** `std::uint16_t prevButtons` per fighter. A move starts
  on a rising edge — `bits & ~prev` — not on a level. This is the whole of the
  reported bug.
  (b) **Buffer.** A press that arrives while the fighter cannot act is remembered
  for a few frames and consumed the tick they become actionable, which is what
  makes a link feel like timing rather than a coin flip. `std::uint16_t
  bufferedBits` and `std::uint8_t bufferAge` per fighter, with the window a
  **per-character field** (`input_buffer_frames`) defaulted by the schema and not
  by a `constexpr` — [ADR-011](adr/ADR-011-mechanics-are-fields.md) decision 1.
  **(a) and (c) are the fix the author asked for; (b) is the one they said comes
  later.** Stated 2026-08-19: *"the way normal attacks behave is they only
  activate when you press the button — and negative edge helps activate special
  moves on button up (but no normal attack)"*. So the rule is **press starts a
  normal; release starts a special that asked for it**, and holding a button is
  reserved for mechanics that do not exist yet (charge, held specials).
  **Enforced by being opt-in, not by inspecting the move.** The schema has no
  move *kind* — a special is distinguished only by its id, and reading semantics
  out of an id string is the heuristic import
  [ARCHITECTURE.md](ARCHITECTURE.md) D7 rejects. `negativeEdge` defaults off, so
  "no normal fires on release" is true by construction unless a file opts a move
  in, and a normal that opts in is an authoring error rather than a kernel one.
  (c) **Negative edge.** A move may opt in to firing on button *release*
  (`~bits & prev`) — the SF-lineage mechanic where holding a button, inputting a
  motion and releasing performs the special. Opt-in **per move**, off by default,
  so a character that authors nothing behaves as (a) alone.
  `prevButtons` gives (a) and (c) from the same field, which is why they belong
  in one commit rather than three.
  **Done when:** `P3Input.HoldingAButtonStartsTheMoveOnceNotEveryRecovery`;
  `P3Input.APressDuringRecoveryFiresTheTickTheFighterCanAct`;
  `P3Input.ANegativeEdgeMoveFiresOnReleaseAndOnlyWhenAuthored`; the golden
  re-recorded **once**, for behaviour; and `tests/test_determinism_crossplat.cpp`'s
  scripted match still reaches the same coverage (it drives jumps by *held* bits,
  so it will need edges — that is the point, not an obstacle).
  **Traps:** `prevButtons` must live in `GameState`, not in the input producer —
  D6 puts the input ring outside the state on purpose, and a rollback hands
  `Simulate` only the current tick, so an edge computed anywhere else is wrong on
  every re-simulation. The buffer is state for the same reason. And a buffered
  press must be **consumed**, not merely aged out, or one press starts two moves.
  **Changes what the search searches:** M1.4's macro-actions gain "press" as
  distinct from "hold", and buffering widens the window in which a link is
  performable — so this lands *before* M1.4 measures anything.
  **Attempted 2026-08-18 and reverted; the whole thing works and the fallout is
  the finding.** All five tests exist and pass, the kernel change is small, and
  the reverted kernel makes the headline test report *"the move started 5 times
  while the button was merely HELD"*. `Fighter` grows to 76 bytes
  (`prevButtons`, `bufferedButtons`, `bufferAge`, `pad_[3]`),
  `FighterData::inputBufferFrames` is the authored window, and `negativeEdge`
  took `MoveDef`'s spare pad byte — so `sizeof(MoveDef)` stays 128 and no
  `MatchData` layout moved for it.
  **The blocker is seven test files, and the reason they fail is worth more than
  the slice.** Every test that drives the kernel by *holding* a bit across ticks
  now gets one move where it expected several — which is the bug being fixed,
  seen from the other side. Most are mechanical, but `test_ground_truth` and
  `test_gap_extent` are not: they turn the **prover's printed loop** into an
  input trace, and its `Driver::Bits()` holds `buttons_[cursor_]` until the move
  starts. Under edge detection a repeat of the *same* button never fires a second
  press — and `air_mp → air_mp`, the self-loop those tests exist to execute, is
  exactly that case.
  **Done, ahead of the rest of this WP:** the release frame landed on its own
  (`GameDemonstration.ASelfCancellingWitnessReleasesBetweenRepeats`), because
  M1.6 needs it whether or not edge detection ever ships — see below.
  **So a derived input trace must insert a release frame between repeats of the
  same button.** That is a change to how a verdict becomes a performance, it
  belongs in the trace builder rather than in each test, and it lands on
  `BuildDemonstration` too — which means it reaches **M1.6's showcase**, where
  every replay is a derived trace. Do that first, in one place, and the seven
  files follow.
  **A finding, and then its correction — both recorded, because the first went
  out overstated.** The first pass reported that edge detection collapses
  `GapExtentKernel.NinetySevenOfThe121RunForever` (cycles managing 1 turn instead
  of 3) and drops `GroundTruthControl` from 12 hits to 1, concluding that most of
  the measured model/game gap was the held-button repeat. **That conclusion was
  wrong, and the cause was in the test harness rather than the kernel.**
  Four *drivers* — one shipped, three in tests — turn a witness into inputs by
  holding `buttons_[cursor_]` until the expected move starts. When it does not
  start, they stall **holding**, and a held bit is one press: the driver simply
  stops feeding the kernel anything. "The cycle managed one turn" meant "the
  driver went quiet", not "the game refused". Teaching a waiting driver to
  **re-press** restores `GroundTruthControl` to its original **12 hits** and puts
  the cycles back to 3 turns, periodic, state repeating — and
  `GroundTruthPayoff` executes the printed witness in full throughout
  (**26 hits in 160 ticks**), because `fighter_a_infinite`'s self-cancel is a
  real cancel edge either way.
  **What survives of the finding** is smaller and still worth having: the
  *timing account* moves. `NinetySevenOfThe121RunForever` checks that each
  transition happens exactly `startFrame` ticks after the last, and a re-pressing
  driver lands its press up to a tick late, so a few transitions per cycle
  disagree. A buffered press should close that — it is consumed the exact tick
  the fighter becomes actionable — and closing it is the remaining work here.
  **The lesson worth more than either:** "the measurement collapsed" is a claim
  about the harness until the harness has been ruled out, and it was published
  before it had been.
  **Second attempt (2026-08-19) got 7 failing files down to 4, and found the
  design question that blocks the rest.** Landed in the stash: the four drivers
  re-press while waiting instead of stalling on a hold; `test_cancels`'s hold
  became a *buffered press*; `test_combat`'s "two cycles" became two presses;
  the defender's *mash* became repeated presses, which is what mashing is.
  Two real bugs found in the kernel half, both mine, both from reverting or
  instrumenting rather than reading: the buffer captured a press the fighter
  **could** act on, so the press that started a move started the next one too a
  window later; and the guard that fixed it (`!canAct`) was itself wrong, because
  **`canAct` means "not stunned", not "not busy"** — a fighter mid-move is
  actionable by that measure, so almost nothing was buffered. The condition is
  "no move started this tick" (`moveId != 0 && moveFrame == 0`), the same signal
  the demonstration cursor and every driver key on.
  **THE OPEN QUESTION, ANSWERED 2026-08-19 BY THE AUTHOR: yes.** *"buffer does
  trigger and consume a cancel and link so that links and cancels are easier to
  do."* `FindCancel` now accepts a buffered press alongside a held one, and the
  cancel clears `bufferedButtons`/`bufferAge` exactly as the button scan does.
  Triggering is what makes a link performable by a human — a player aiming at a
  two-frame window presses early far more often than late, so a cancel reading
  only the current tick punishes the common miss. Consuming is what stops one
  press walking a fighter several moves down a chain, because `StepAttack`'s
  cancel branch returns before the button scan and an unconsumed press would
  still be waiting for the next window.
  **Also learned, from reverting:** the first version of
  `HoldingAButtonStartsTheMoveOnceNotEveryRecovery` passed against the bug,
  because it counted `moveId` transitions and a move that restarts the instant it
  recovers never leaves its slot. It counts `moveFrame == 0` now. A test that
  cannot fail is worse than no test, and only reverting finds them.
  **Two `static_assert`s earned themselves during the attempt**, both at compile
  time: `has_unique_object_representations_v` caught a one-byte pad where three
  were needed, and `Replay.h`'s `FighterData` sum caught the new `int32`.
  **Third attempt (2026-08-19) closed it, and the last two bugs were both in the
  harness rather than the kernel.** With the cancel answer in, the sweep's timing
  mismatches went to **zero** — the diagnosis in the paragraph above was right —
  but 96 of 121 cycles then failed *periodicity* instead. The cause: `Observe`
  spent its release tick before checking whether the move had started, so a
  driver was **blind to exactly the transitions buffering creates**. A buffered
  press is consumed the tick the fighter can act, which is very often a tick the
  driver is deliberately silent on; the cursor never advanced, the driver went on
  asking for the move already running, and it restarted a duration later. That
  read as "the loop decayed". Checking the start *before* spending the release
  tick took `test_gap_extent` from 96 failures to green, **97 of 121 intact**.
  The second: `test_one_frame` and `test_training_mode`'s probes hold a button to
  ask "how soon can the attacker act", which under press-activation is one press
  for the whole fork. They pulse and buffer now, so the answer is the frame the
  kernel opens rather than the frame the driver happened to be pressing on.
  **A methodological note that cost an hour.** Three parameter sweeps returned
  byte-identical failure counts, which looked like a structural cause; the
  binary was stale. `cmd /c "call scripts\ci\msvc_env.cmd && cmake --build ..."`
  **hangs** under the Bash tool and silently produces no build. Build from
  PowerShell; run from either.
  **Done when — actually done:** the five `P3Input` tests above, plus
  `P3Input.ABufferedPressTakesTheCancelTheTickItsWindowOpens` and
  `P3Input.WithNoAuthoredWindowAnEarlyPressMissesTheCancelEntirely` for the
  cancel answer. The first draft of that pair **passed with the change
  reverted**, because the cancel window's last frame was also the move's last
  frame and the button route produced the same single start; the window closes
  two frames early now, so a cancel start (source interrupted, frames 3–6) is
  distinguishable from a button start (source spent, frame 7). Reverting is what
  found it, for the second time in this WP.
- `[ ]` **M1.1e The buffer window as an authored character field.** *(S)*
  `FighterData::inputBufferFrames` exists and the kernel honours it, but nothing
  sets it from a character file — every caller that wants buffering assigns the
  field directly, and `tests/test_one_frame.cpp` and `tests/test_gap_extent.cpp`
  both say so in a comment pointing here. Buffering is a mechanic, so it owes
  [ADR-011](adr/ADR-011-mechanics-are-fields.md)'s five parts: an appended
  `input_buffer_frames` under `engine` in `schema.v2.json` (engine-only, so the
  published prover ignores it), the load in `CharacterData.cpp` with its own
  A-assertion, the `MatchBuilder` copy into the kernel slot, a loss-ledger row,
  and a property test that a file authoring nothing gets zero. Split out rather
  than folded into M1.1d because it touches the schema the prover reads, and that
  is a contract change with its own review.
  **Done when:** a character file that authors `input_buffer_frames` produces a
  `FighterData` carrying it, a file that authors none produces zero, and an
  out-of-range value is a load error naming the key.
  **One thing already checked, so it is not re-litigated here.** `FightSession`'s
  `BuildDemonstration` spends a release tick and `continue`s past its cursor
  check, which looked like the same blindness that broke the three test drivers
  in M1.1d — a buffered press is consumed on a tick the trace is silent on. It
  is **not** a bug, and the reason is placement rather than luck: the builder
  releases only on the tick after an advance, when the fighter is one frame into
  a move it just started and the press that started it has already been consumed.
  `GameDemonstration.ABufferedPressIsSeenEvenWhenItLandsOnAReleaseTick` rehearses
  the witness against a buffering character and asserts that no move begins on a
  silent tick, so the invariant is checked rather than reasoned about. Written
  expecting it to fail; it passed against the unchanged builder, and the shipped
  code was left alone.
- `[ ]` **M1.2 Push boxes and the corner.** *(S–M)* Body separation between
  fighters and the stage edge as a wall; resolution order per NORTHSTAR Phase 2:
  pushbox separation → strikes (throws when they exist). Authored `pushbox`
  under `engine.boxes` (schema v3, appended field per ADR-006's rule); default
  from `MatchBuilder.h`'s `BodySpec`. Same integer box math as
  `Games/UntitledFighter/Kernel/include/cse/kernel/Combat.h`; separation splits
  the overlap with `scaleBy`'s rounding so it is mirror-symmetric.
  **Done when:** `P3Pushbox.FightersNeverOverlapAfterSeparation`,
  `.TheCornerIsAWallOnBothSides`, `.SeparationIsAnExactMirror`.
- `[ ]` **M1.3 Mechanics, pass 1 — the ones the showcase needs.** *(M–L)* Each
  with ADR-011's five parts (schema field appended · `MoveDef`/`FighterData`
  slot · loss-ledger row · kernel property test · showcase variant):
  (a) **contact mask** on `CancelEdge` — `hit | block | whiff` bits replacing
  the collapsed `onHit`, so kara and whiff cancels are expressible;
  (b) **movement is a move** — jump, super jump, dash, backdash as authored
  moves with a `movement` field (per-frame velocity, gravity,
  `airborne_from_tick`, optional landing recovery); the kernel's hard-coded jump
  is deleted; a **jump cancel** and a **dash cancel** are ordinary cancel edges
  whose target is a movement move; `to: idle` is a legal empty cancel;
  (c) **counter-hit** — per-move `counter_hit {hitstun_bonus, damage_bonus}`
  applied when the defender was in startup; (d) **wall bounce / wall splat /
  launch vector** as per-hit `on_hit` reactions using the fields M1.1 reserved.
  Everything defaults off; `fighter_a` unpatched must hash exactly as before
  this WP except for the re-golden M1.1 already did.
  **Done when:** `P3Cancels.AKaraCancelFiresOnWhiffInsideItsWindow`,
  `P3Movement.AJumpIsAMoveAndAJumpCancelIsAnEdge`,
  `P3Reactions.CounterHitAddsTheAuthoredStun`,
  `P3Reactions.AWallBounceReturnsTheDefenderIntoRange`; the loss ledger has a
  row for each; the schema bumps to v3 with the fields appended.
- `[ ]` **M1.4 The kernel search, and the ground truth as the gate.** *(M)*
  Promote the cancel-graph walk out of `tests/test_gap_extent.cpp` into
  `CseGame` as **`ComboSearch`**: a bounded search over macro-actions — start
  move *m*, wait *k* frames, walk *k* frames, jump, dash — executed on the real
  kernel, de-duplicated by `Checksum()` of the state, with a budget; a search
  that hits its budget reports **UNRESOLVED**, never a verdict (ADR-011 decision
  8). One implementation for tests, cooker, showcase and panel. Rewrite
  `tests/test_ground_truth.cpp` and `tests/test_gap_extent.cpp` to assert
  properties: (a) every graph-prover `Infinite` witness reproduces — defender
  never actionable across N loops; (b) for every `Terminating` character no
  performable combo exceeds `maxHits` (search to `maxHits + k`), **or** the
  kernel search's counter-example is explained by a named loss-ledger row
  (microwalk → `walk_speed`/`gap_actions`); (c) every reported dead cancel never
  connects. Delete `NinetySevenOfThe121RunForever` when it is false — **and check
  M1.1d's note before trusting any restatement of it.** An earlier pass here
  reported the figure collapsing under edge detection; that was the test harness
  stalling, not the kernel, and the cycles still run. What genuinely moves is the
  *timing account*, by the latency of a re-pressed input.
  **Done when:** those tests pass on every shipped character and patch and on
  the three MUGEN fixtures in `tests/fixtures/characters/`; the search's cost
  per macro-action is measured and recorded here with a date.
- `[ ]` **M1.5 Character hot reload.** *(S)* Extract the mtime+size 0.25 s
  poller from `Engine/src/ui/UIAssetDocument.cpp` into `Engine/src/core/FileWatch.{h,cpp}`
  with an injectable clock; `UIAssetDocument` uses it (no second copy). Training
  mode watches the character file **and its patches**, rebuilds `MatchData`
  **between ticks**, keeps last-good on failure and shows the load report
  (naming the key), and the Combo Prover panel re-analyses on the same event.
  **Done when:** `TrainingMode.AnEditedCharacterFileTakesEffectWithinAQuarterSecond`
  (clock injected, no sleep) and `UIHotReload.*` still pass unchanged.
- `[ ]` **M1.6 The showcase: one fighter, many patches, a replay per verdict.** *(M)*
  **Carry in one finding from the trace work:** the witness-driving cursor now
  exists in **five** copies — `BuildDemonstration` in
  `Games/UntitledFighter/Game/src/FightSession.cpp`, and a `Driver` class in each
  of `tests/test_ground_truth.cpp`, `tests/test_game_core.cpp`,
  `tests/test_gap_extent.cpp` and `tests/test_one_frame.cpp`; the second is
  explicitly "copied out of" the first.
  **PROMOTE `test_gap_extent.cpp`'s, and this is not a style preference.** It is
  the only copy that checks whether the move has started BEFORE spending its
  release tick. The other four return early on a release, which is safe only
  while nothing can start on one — true today because they release exactly one
  tick after an advance, when the press that caused it has just been consumed,
  and because no character authors a buffer window yet (M1.1e). Set a window and
  the ordering starts to matter: it took `test_gap_extent` from 96 failing
  cycles to green in M1.1d, and it is the single line that differs. Whichever
  copy survives must carry it, and the comment saying "nothing can have started
  from an input of zero" must not survive at all — it states a conclusion whose
  premise is now conditional. The release-frame rule had to be written
  three times, and only
  `GameDemonstration.TheSeamProducesExactlyTheGroundTruthDriversTrace` kept them
  honest — it named the disagreement at tick 1, twice, while they were being
  aligned. MAINTENANCE.md's rule applies exactly: *if a comment says "MUST match
  X", make it call X*. `Showcase` is the third consumer, so promote the cursor
  into `CseGame` beside `ComboSearch` (E4) rather than writing it a fourth time.
  Variants are JSON merge patches (RFC 7386, `nlohmann::json::merge_patch`)
  under `Games/UntitledFighter/Assets/Characters/fighter_a/variants/`; <!-- docs-ok: this WP creates it -->
  `fighter_a_infinite.json` becomes `base + variants/infinite.json`. The eleven
  patches and what each shows are ADR-011 §4; ship them in the order their
  *Needs* column comes true. `BuildDemonstration`
  (`Games/UntitledFighter/Game/include/cse/game/FightSession.h`) already
  rehearses a frame-perfect attacker headlessly; build **`Showcase`** in
  `CseGame` on it and `ComboSearch`: for base + every patch record the graph
  prover's verdict, the kernel search's verdict, the named reason when they
  differ, `.csrp` replays (the loop for N cycles, the max-hits combo, each dead
  cancel attempted, corner and mid-screen), the input trace, and a `graph.dot`
  of the cancel graph with the loop highlighted — every replay verified
  bit-identical by `ReplayVerifier` before it is written. Wire **REPLAY** as a
  mode (`Games/UntitledFighter/Modes/`, named in `UntitledFighterModes.cpp`)
  playing a `.csrp` through the shared presentation with an **on-screen input
  display** and the patch's one-line description on screen. Add
  `--replay <file>` and `--mode <name>` to the Player (`Player/src/PlayerMain.cpp`
  reads argv via `Engine/src/core/Main.h`). Ship the generator as
  `AssetCooker showcase <root>` (same fail-closed protocol as `validate`).
  **Done when:** `Showcase.EveryCatalogueEntryReplaysBitIdentically`;
  `Showcase.EveryPatchChangesTheVerdictItClaimsTo`;
  `Player --replay Exported/Showcase/fighter_a/microwalk.csrp` plays the walk
  steps with the input display; the catalogue is generated in CI and its
  verification is a test.
- `[ ]` **M1.7 Authoring telemetry.** *(S)* One JSON line per prover run from
  the panel and the cooker (content hash, move/cancel counts, resource ranges,
  `explored`, wall-clock ms, verdict, changed-since-last). NORTHSTAR §5 says it
  is worthless retroactively. **Done when:** the file grows by one line per run
  and a test parses it.
- `[ ]` **M1.8 Housekeeping.** *(S)* `constexpr scaleBy` as one helper in the
  kernel with `static_assert(scaleBy(-3, 1, 2) == -2)` beside it (the rule is
  inline in `MatchBuilder.cpp` today); delete dead code after confirming zero
  callers — `Engine/src/core/Mesh.h` (commented-out skinning),
  `Engine/src/core/EventBus.h` + `Event.h` (only `Engine/include/Engine.h`
  includes them), `Scene::RenderShadowDepth`; drop `.fchar` from any doc.
  **Done when:** builds clean in four configurations; nothing links against the
  removed symbols.

**M1 gate (demo + CI):** in training mode, edit `fighter_a.json` (or a patch)
and watch the change land; press Demonstrate and the prover's INFINITE plays
itself; the `microwalk` and `jump-cancel` patches play their loops with the
input display on; **CI asserts every verdict — graph prover and kernel search —
against the running kernel for base + every patch** and generates a verified
replay catalogue.

---

## M2 — Two people, one match *(size L)* — ARCHITECTURE Phase 4

Everything here wires things that already exist. Nothing here touches the
kernel.

- `[ ]` **M2.1 Transport — spike, then ADR-011.** *(M)* GekkoNet is built with
  `GEKKONET_NO_ASIO` and takes a `GekkoNetAdapter` (`send_data` /
  `receive_data` function pointers). Default: a plain UDP adapter in `Net/src/`
  behind a `CreateGekkoRemoteSession(config, localPort, remoteEndpoint)`
  factory — Winsock/BSD sockets behind one `#ifdef`, no new dependency. Spend at
  most one day comparing with vendoring asio; write the answer as
  `docs/adr/ADR-012-transport.md`. <!-- docs-ok: this WP writes it --> Two configure-time guards in
  `Net/CMakeLists.txt` stay: no interface leak, no `gekkonet.h` reachable.
  **Done when:** `SessionUdp.TwoSessionsOnLoopbackAgreeForATenMinuteMatch`
  (3,600+ ticks) with injected 100 ms / 5 % loss.
- `[ ]` **M2.2 Handshake.** *(S)* Before the first tick, exchange: schema
  version, `HashMatchData` (`Games/UntitledFighter/Game/include/cse/game/Replay.h`
  — written for exactly this), and a build id (git sha baked at configure via
  `configure_file`; none exists today). `Net/` sees opaque bytes; `Game/`
  computes them. Mismatch is a lobby error naming the reason, never a gameplay
  bug. **Done when:** `SessionHandshake.AContentMismatchIsRefusedByName`.
- `[ ]` **M2.3 Desync = abort + artifact.** *(S–M)* On `PollDesync`, stop the
  match and write both `GameState` blobs plus the input log since the last
  confirmed tick to `Builds/desync-<tick>.bin`; name the frame **and the
  field** — which needs the reflection table ARCHITECTURE D1 asked for: a
  `constexpr` array of `{name, offset, size}` over `GameState` in the kernel,
  which also feeds the editor's sim inspector and the field-level diff.
  **Done when:** `Desync.ACorruptedPeerIsNamedByFieldWithinEightTicks`.
- `[ ]` **M2.4 The session owns the tick count.** *(S–M)* Today
  `UntitledFighterMode::FixedTick` calls `session_.Tick()` once per
  `Application` fixed step, i.e. `FixedTimestep` (8-step cap, backlog dropped)
  decides how many ticks run. Under a network session **the session decides**:
  the mode pumps `ISession::Update` and runs exactly the ticks it is told, zero
  is legal, dropping is not. While a session is live: pause, time scale and
  scene swap are inert; UI focus never suppresses gameplay input; the producer
  keeps a sticky "pressed since last tick" mask outside the snapshot.
  **Done when:** `Session.ZeroTicksThisFrameIsLegalAndNoTickIsEverDropped`.
- `[ ]` **M2.5 VERSUS, and one presentation for three modes.** *(M)* Extract
  the fight presentation (`FightView`, `FightHud`, camera, box overlay) out of
  `UntitledFighterMode` into a `FightPresenter` that Training, Replay (M1.6)
  and Versus share — three modes, one drawing. Versus: local two-controller
  first (`SetInputSource` twice), then two processes on loopback, then two
  machines. Menu entry per `UntitledFighterModes.cpp`.
  **Done when:** two Players on one machine finish a match with zero checksum
  mismatches; the Windows ↔ Linux run is recorded here with a date.
- `[ ]` **M2.6 Play == Player, as a test.** *(S–M)* NORTHSTAR Q6 / ADR-002:
  the editor's Game view and the shipped Player must run the same code path.
  Extract the duplicated host setup (~90 lines each in `Player/src/PlayerMain.cpp`
  and `Editor/src/EditorApplication.cpp` as of 2026-08-12 — re-measure) into
  one shared helper, and add the CI test: same input log through both hosts,
  identical hash. **Done when:** `Hosts.TheEditorAndThePlayerHashTheSameMatch`.

**M2 gate:** a ten-minute match Windows ↔ Linux with zero checksum mismatches,
and a deliberately corrupted peer reports "desync at tick N, field F" within
eight ticks.

---

## M3 — Skinned fighters, frame-indexed *(size L)* — placeholders, not art

Mixamo rigs and clips are **placeholders** that make the showcase look like a
fighting game; they are not the art. Every rule of the frame-indexed design
(ADR-005 §4, made precise by [ADR-011](adr/ADR-011-mechanics-are-fields.md)
decision 6) holds and is the acceptance test for M3.2–M3.4: **pose is a pure
function of the state the sim already produces** — `f(moveId, moveFrame, posX,
posY, facing, stance/airborne, stun fields, tick)`; the authoritative window is
exactly `startup + active + recovery` frames and its clip and first frame are
authored in the frame data; **the tail** (return-to-idle, landing settle) is
presentation only, plays while the fighter stays idle and unmoving, and is
**interrupted instantly by any sim action** — a presentation-side blend of at
most 4 frames may smooth that interruption and can never delay a move, shift a
box or hold a fighter in place; idle/walk/air cycles are keyed by `(tick,
posX)` so they are stateless. Nothing about animation ever influences a tick.
Skinning is the one renderer feature NORTHSTAR §6's freeze admits, because the
showcase needs it.

- `[ ]` **M3.1 The event queue, before the first sound.** *(S–M)* Fill the
  ring M1.1 reserved: `Simulate` appends `{slot, kind, a, b}` events; a
  `Phase { Predicted, Confirmed }` parameter; `FightSession` drains events for
  confirmed ticks only, de-duplicated by `(tick, slot, kind)`; presentation
  never plays a sound or spawns a spark from inside a tick.
  **Done when:** `Events.ARollbackReplaysTicksButFiresEachEventOnce`.
- `[ ]` **M3.2 Frame-indexed clip player + skinning (engine).** *(L)*
  `Engine/src/anim/` (new) <!-- docs-ok: this WP creates it -->: `Skeleton`, `AnimationClip` (poses **sampled per
  60 Hz frame at import**, so runtime sampling is an integer index — no dt, no
  interpolation on the authoritative window), `SkinnedMeshComponent`
  (serialised, inspectable, in the component registry). `Model.h`'s `Vertex`
  gains `ivec4 boneIds` + `vec4 weights`; Assimp import reads bones and
  `aiAnimation`; a bone-palette UBO and a skinning branch in the forward shader
  **and the CSM depth pass** (a skinned mesh must cast a skinned shadow).
  **Done when:** `Skinning.APosedMeshMatchesTheCpuReference` (CPU-skinned
  vertices vs the GPU path, `gl` label) and a golden-frame render test of one
  posed Mixamo character.
- `[ ]` **M3.3 The Mixamo pipeline.** *(M)* Import FBX via Assimp into the
  engine's model + clips under `Games/UntitledFighter/Assets/Characters/<name>/`;
  the character file's `engine.anim` (today sprite-shaped in `schema.v2.json`)
  gains, **appended**, `clip` and `firstFrame` per move; load assertion:
  authored clip length ≥ `startup + active + recovery` and the authoritative
  window is the first N frames (ADR-005 §4.1). **Record the licence** for every
  imported asset in a `LICENSE.md` beside it, the way
  `tests/fixtures/characters/README.md` does; never commit an asset whose
  licence is not written down. All characters share Mixamo's rig, so no
  retargeting.
  **Done when:** `fighter_a` loads with clips and every move passes the length
  assertion.
- `[ ]` **M3.4 The presentation reconciler.** *(M)* `FightPresenter` writes
  `const GameState&` into `GameModeContext::scene`'s registry each frame —
  fighter transforms from `posX/posY/facing`, animation state from
  `moveId/moveFrame` (frozen during hitstop), camera from the pair — as a pure
  function of state; ties by slot index; the 2D overlay's `Renderer2D::BeginWorld`
  and the 3D camera agree on one projection so boxes sit on the mesh.
  **Done when:** `Presenter.TheSameStateProducesTheSameSceneTwice`,
  `Presenter.ATailIsInterruptedTheTickTheSimActs` (the blend never moves a box
  or delays a move), and the boxes visibly track the skinned mesh in the
  showcase replays.
- `[ ]` **M3.5 Feel and stage.** *(M)* Hit sparks and hitstop shake from the
  event queue, SFX through the existing `AudioWorld` (low-latency device
  setting), a stage scene using the engine's IBL/CSM/post stack, camera rules.
  **Done when:** the M1.6 catalogue re-recorded with skinned characters reads
  as a fighting game to someone who has not seen the repo.
- `[ ]` **M3.6 Roster and select.** *(M)* Second and third characters through
  the same pipeline; character select needs per-player nav scopes in `UIWorld`
  (one nav focus per document today) — an engine change worth its own short ADR.
  **Done when:** two players pick different characters and the handshake hashes
  both.

**M3 gate:** the showcase catalogue, re-recorded, looks like a fighting game;
CI exercises skinning under llvmpipe.

---

## M4 — Showcase and publish; then art *(size M + content)*

- `[ ]` **M4.1 The reel.** *(S–M)* `Player --replay <file> --dump-frames <dir>`
  writes a PNG sequence (encode offline with ffmpeg; do not vendor an encoder);
  overlay toggles; one script regenerates every reel from the catalogue.
- `[ ]` **M4.2 Paper artefacts.** *(S)* `AssetCooker combos <root> --json` (one
  record per character), the telemetry log, the ground-truth results, the
  cross-platform hash logs — written to `Builds/paper/` by one script, never
  into `docs/`.
- `[ ]` **M4.3 Publish the claims.** *(S)* README and website updated with
  numbers taken from CI output. NORTHSTAR Q7 forbade rollback claims until it
  ran; after M2 it runs.
- `[ ]` **M4.4 Real art.** *(content)* SF6-tier models and animation through the
  M3 pipeline; an art ADR when a modeller exists. This is content, not
  engineering, and it starts only after M4.1–M4.3 already sell the paper.

---

## Engine maintenance — done inside the milestones, not as a phase

Standing rule: **no new subsystem without deleting or unifying something**, and
every "MUST match X" comment becomes a call to X. These are the concrete items;
each is attached to the WP that first needs it.

| Item | What | Attached to |
|---|---|---|
| E1 | **Component registry table** for non-sim ECS components — `{name, toJson, fromJson, capture, apply, equal, drawInspector}` driving `SceneSerializer`, `UndoHistory` and the Inspector from one table, with a test that walks the registry and round-trips every type. Retires MAINTENANCE.md's "closed lists" invariant | M3.2 (the first new component) |
| E2 | **Shared host setup** for Player and Editor + the Play == Player hash test | M2.6 |
| E3 | **`FightPresenter`** — one presentation for Training / Replay / Versus | M2.5 |
| E4 | **`ComboSearch`** promoted from `tests/` into `CseGame` | M1.4 |
| E5 | **`FileWatch`** extracted from `UIAssetDocument` | M1.5 |
| E6 | **Reflection table over `GameState`** — inspector, desync field diff, serialiser from one artifact | M2.3 |
| E7 | **Dead code deleted** (`Mesh.h`, `EventBus.h`/`Event.h`, `RenderShadowDepth`) | M1.8 |
| E8 | **`Games/` and `Net/` targets to C++20** (`std::span`, designated initialisers, `<bit>`); engine stays C++17 | any WP that first wants one of those; record in `ARCHITECTURE.md` |

---

## Review points — what the author checks, with their own eyes

Every WP's **Done when** is a test, which answers *"is it correct"* and not
*"is it right"*. Those are different questions and only one of them can be
automated. This section is the other one: at each point below the simulation can
be **looked at**, and each says what to run, what should happen, and — the part
that matters — **what would mean it is wrong**. A green suite and a wrong game is
the outcome this whole plan exists to prevent.

A review point is not a gate. Nothing waits for it. It is a place where an hour
of the author's attention is worth more than an hour of anyone's code.

**Where things run.** Executables land in
`out/build/<preset>/build/bin/<Config>/`. The editor's Game view and the shipped
Player enter the *same* game mode, so either one works; the editor also has the
Combo Prover panel. Assets stage from `Games/UntitledFighter/Assets/` — edit the
source copy, and check which copy you are running before believing anything.

### R0 — Available now, before any of M1

| | |
|---|---|
| **Run** | `Editor.exe`, Game view. Or `Player.exe`. |
| **Do** | Move and attack. Toggle the box overlay. Pause, then frame-step through a hit. Open the **Combo Prover** panel and load `Exported/Characters/fighter_a.json`. Press **Demonstrate**. |
| **Should** | Boxes track the fighters; frame step advances exactly one tick; the panel prints a verdict with dead cancels and the settling index; Demonstrate plays the prover's own printed loop, frame-perfectly, with no human timing. |
| **Wrong if** | Frame step advances more than one tick, or the demonstration drops a link. Either means the mode is deciding tick counts rather than the session ([DETERMINISM.md](DETERMINISM.md) T1). |

**R0 has already earned itself.** Playing training mode and reading the HUD is
what found that attacks are labelled wrong — `air_mp` has its own dedicated
button instead of being *MP while airborne*. No test could have caught it: every
one of them passes, because the kernel does exactly what the binding table tells
it to, and the binding table is where the mistake is. That is **M1.1c**.

It then found a second, on the next play: **holding an attack button rapid-fires
it**. The kernel's own comment had been asking for the missing field for months
— *"the next field this file will want"* — and no test could report it, because
"held" is exactly what the code says it does. That is **M1.1d**, now closed.

### R0b — After M1.1d: a press is a press

Worth re-running R0 above with attention on the pad, because what changed is the
thing a hand notices before a test does.

| | |
|---|---|
| **Run** | `Editor.exe`, Game view, or `Player.exe`. |
| **Do** | Hold one attack button down for several seconds. Then mash the same button. Then press a button during another move's recovery — slightly *early*, on purpose — and watch whether the follow-up comes out. |
| **Should** | Holding gives **one** attack and then nothing. Mashing gives one attack per press. A press made a frame or two early still produces the follow-up, on the frame the window opens, rather than being dropped. |
| **Wrong if** | Holding still repeats, which means the mode is feeding edges instead of levels and the kernel is seeing a fresh press every tick. Or an early press produces **two** moves, which means a buffered press was aged rather than consumed. Or a *normal* comes out when you let go of a button — release is for specials that opt in, and no shipped normal opts in. |

Note the early-press behaviour is only visible where a buffer window is
authored, and today **no character file authors one** — that is M1.1e. Until it
lands, a fighter built from `fighter_a.json` has `inputBufferFrames` 0 and an
early press is correctly forgotten.

### R1 — After M1.1b: the file is the game

The first point where **frame data visibly beats a constant**.

| | |
|---|---|
| **Do** | Walk `fighter_a` across the stage and time it. Then edit `walk_speed` in the character file, rebuild the match, and walk again. Land a hit and watch a resource move. |
| **Should** | The fighter is **50% faster than it was in R0** before you change anything, because the file has always said 3 px/tick while the kernel used 2. After an edit, speed tracks the file. A hit changes `res[]`; a move with a resource guard refuses below its minimum. |
| **Wrong if** | Speed does not change with the file — the kernel is still reading a constant, which is the whole thing M1.1b removes. |

### R2 — After M1.2: the corner is real

| | |
|---|---|
| **Do** | Walk both fighters into each other. Walk one into the wall and keep pushing. Do it in both directions. |
| **Should** | They separate rather than overlap; the wall stops them; and the separation is a **mirror** — the same distances left and right, to the sub-unit. |
| **Wrong if** | Left and right differ by even one sub-unit. That is a rounding asymmetry, and it means a mirrored character loses reach its twin keeps ([DETERMINISM.md](DETERMINISM.md) K8). |

### R3 — After M1.3: every mechanic is a field

The point where the **paper's central claim becomes visible**: the same fighter,
different frame data, different game.

| | |
|---|---|
| **Do** | Load `fighter_a` unpatched, then each variant in turn. Try a kara cancel, a jump cancel, a counter-hit, a wall bounce. |
| **Should** | Unpatched `fighter_a` behaves **exactly as it did in R2** — every mechanic is off by default. Each variant turns on exactly one thing, and the diff between the files is one field. |
| **Wrong if** | Unpatched behaviour changed. A mechanic that alters a character which does not author it is a kernel rule wearing a field's clothes ([ADR-011](adr/ADR-011-mechanics-are-fields.md) decision 1). |

### R4 — After M1.4: the two provers, honestly labelled

| | |
|---|---|
| **Do** | Run the analysis on every shipped character and patch. Read where the graph prover and the kernel search **disagree**. |
| **Should** | Every disagreement is **named by a loss-ledger row** — microwalk is `walk_speed`/`gap_actions`, dropped by the corner-only model. A search that runs out of budget says `UNRESOLVED`, never a verdict. |
| **Wrong if** | A disagreement has no named reason, or a capped search prints a verdict. Either is the model quietly claiming more than it knows. |

### R5 — After M1.5: the authoring loop

| | |
|---|---|
| **Do** | With a match running in training mode, edit a move's `startup` in the character file and save. |
| **Should** | The change lands **within a quarter second**, between ticks, without restarting the match. A broken edit keeps the last good data and shows a load report naming the key. |
| **Wrong if** | You have to restart, or a typo empties the character. |

### R6 — After M1.6: **the showcase — the one to judge the project on**

| | |
|---|---|
| **Do** | `Player --replay Exported/Showcase/fighter_a/microwalk.csrp`, and every other entry. Watch with the input display on. |
| **Should** | One fighter, eleven patches, a different infinite in each — a link that becomes a loop from one extra frame of hitstun, a microwalk loop the corner-only prover cannot see, a jump-cancel air loop, a wall-bounce corner loop, a counter-hit-only link, a meter loop. Every replay verified bit-identical before it was written, and the on-screen input display shows timing no human could hit. |
| **Wrong if** | It does not *read* as a fighting game doing something remarkable. That is a judgement only the author can make, and it is the point of the milestone: if the catalogue does not sell the paper here, more art will not fix it. |

### R7 — After M2.5: two people, one match

| | |
|---|---|
| **Do** | Two Players on one machine, then two processes on loopback, then two machines — one Windows, one Linux. |
| **Should** | A ten-minute match with **zero checksum mismatches**. A deliberately corrupted peer stops the match and names the tick **and the field** within eight ticks. |
| **Wrong if** | A desync is silently corrected. In 2-player peer-to-peer there is no authority to correct from, and a silent correction is worse than a stop ([DETERMINISM.md](DETERMINISM.md) T6). |

### R8 — After M3.4 and M3.5: it looks like a fighting game

| | |
|---|---|
| **Do** | Re-watch the R6 catalogue with skinned placeholder characters. Interrupt a return-to-idle animation with an attack, repeatedly. |
| **Should** | Boxes sit on the mesh. A tail is interrupted **the tick** the simulation acts — never a frame later, never with the box lagging the pose. |
| **Wrong if** | An animation delays a move, holds a fighter in place, or moves a box. Pose is a pure function of state, and any of those means presentation has acquired state of its own ([DETERMINISM.md](DETERMINISM.md) P4). |

### R9 — After M4.1: the reel

| | |
|---|---|
| **Do** | Watch the rendered reel as an outsider would. |
| **Should** | Someone who has never seen this repository understands what was proved and why it is hard. |
| **Wrong if** | It needs you to narrate it. |

---

## Not scheduled, on purpose

Reasons and come-back triggers are in [ADR-010 §3.4](adr/ADR-010-one-roadmap-one-rule.md);
this is the list, so nobody re-proposes them by accident.

- The trigger expression language (Phase 5) — typed schema nouns instead.
- `SimId`, our own snapshot ring, our own input ring — GekkoNet owns them.
- Projectile pool — until a shipped character has one.
- Asset mounts (ADR-007) — after M2, on its triggers.
- Engine install/export (G4) — after M2.
- Cook/pack pipeline, Lua hardening, Jolt determinism, new renderer features
  beyond skinning — ARCHITECTURE §2 conditions.
- **A required job that HANGS rather than fails.** Recorded 2026-08-18: the
  Linux job's `apt-get` step sat in progress for 2h05m and would have consumed
  the job's whole 150-minute budget, while the other three jobs had long since
  gone green. Fixed at the source — `timeout-minutes: 10`, apt retries and a
  per-connection timeout, `DEBIAN_FRONTEND=noninteractive`. Kept on this list
  because the *class* is not closed: only one step is bounded, and any other
  network step could do the same. Comes back if a second step ever hangs; then
  every step gets a deadline rather than one at a time.
- Two open rows carried from the archived 2026-07 ledger: `EventBus` (deleted in
  M1.8) and gamepad verification on physical hardware (do it during M2.5).
- **Shipping a Linux binary** — an `$ORIGIN` rpath so executables launch without
  `LD_LIBRARY_PATH`, a Linux install layout in place of the Windows applocal
  deploy, and hiding the editor's redundant title-bar strip where the borderless
  install is a no-op. Carried from the folded `BUILDING_LINUX.md`. Comes back when
  something has to *run* on Linux rather than compile and test there; CI covers
  compile-and-test today, which is what the determinism gate needs.

---

## How to update this file

- Change a WP's box and, for `[x]`, append the commit sha; for `[-]`, the reason.
- Move the "Now" row when a WP starts; there is never more than one.
- A WP that grows a decision gets an ADR (Proposed, with a recommended default)
  and a one-line pointer here; a WP that splits stays under its number
  (`M1.5a`, `M1.5b`).
- Do not add prose about *why* here — that is the ADR's job. Do not add status
  anywhere else — that is this file's job.
- Bump `Verified:` at the top when you have re-read the whole file against the
  tree, not when you edit one line.
