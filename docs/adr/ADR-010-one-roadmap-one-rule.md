# ADR-010 — One roadmap, one rule

**Status.** Proposed 2026-08-17. Nothing below has been executed except the two
files this proposal creates alongside itself — [`ROADMAP.md`](../ROADMAP.md) (the
detailed, living work list) and the root [`CLAUDE.md`](../../CLAUDE.md) (the
operating protocol for an autonomous session) — both uncommitted. **Verified
against** `master` @ `99669cc`, 2026-08-17 — every path cited exists at that
commit unless marked *(new)*.

**Decides.** What the roadmap is and where it lives; the order of the work and
why; the shape of the documentation; and the rule that keeps it true. It edits
nothing in place; §5–§7 say how the existing documents fold into the new shape.

**Constraints from the author, 2026-08-17.** (1) The engine must look
impressive enough to *sell the paper* — the research is the customer. (2)
Everything is made **provable and showcased in action** before any art is made:
tool-assisted play produces replays that show every prover verdict, however
tight the timing. (3) Placeholder rigs from Mixamo first; SF6-tier art last,
and only once the showcase already convinces without it. (4) Modern design
principles, applied to reduce duplicated code and simplify maintenance. (5) The
plan must be detailed enough for a model to continue autonomously — that is what
`ROADMAP.md` and `CLAUDE.md` are for. (6) *Added later the same day:* the
showcase must load the same fighter with different frame data to show the
different infinites that frame data and the cancel graph make possible
(microwalk, jump cancels, the odd interactions that are simply how SF-style
games work); every 2.5D mechanic is opt-in per move per character; visuals are
driven by the frame data, and return-to-idle tails are always cancelable — that
is [ADR-011](ADR-011-mechanics-are-fields.md), which constrains M1 and M3.

**Reads.** [NORTHSTAR.md](../NORTHSTAR.md) §2 — the four properties and their
testable definitions of done — is the spine and is not changed. Everything else
in that file, and in [ARCHITECTURE.md](../ARCHITECTURE.md), is either done,
superseded, or a rule that belongs in a smaller file.

---

## 0. The one-paragraph summary

The engine has one goal it can state in a sentence, four properties it can test,
and — six days after the first kernel commit — has already delivered more of the
fighting game than any of its five roadmaps say. What it does not have is a
single page that says so, and no way for a session that starts cold to know
what to do next. The plan is: **five milestones** — consolidate the docs and
install the gate; close the measured gap between prover and kernel and build the
tool-assisted showcase; network it; skin it with placeholder rigs; publish, then
art — each work package with a test-named *done*; **six living documents**
instead of thirty-two, every rule in a file whose name says "rule", ADRs frozen
the day they are accepted, history in `docs/archive/`; and **one five-line
rule**, enforced by a small script in the CI job that already runs in seconds,
so a documentation lie fails the build the way a fast-math flag does.

---

## 1. Where things actually stand

Read from the tree, not from the documents.

### 1.1 The code, against ARCHITECTURE.md's phase numbers

| Phase | Status | Proof in the tree |
|---|---|---|
| 0 — does the model fit | **Done** | [ADR-001](ADR-001-fighting-core.md); `tests/fixtures/characters/` |
| 1 — determinism proven | **Done, CI-enforced** | `tests/test_determinism_crossplat.cpp` (golden hash re-checked by gcc 13); `scripts/check_determinism_flags.py`; `.github/workflows/ci.yml` (all four jobs required) |
| 2 — kernel + rollback contract | **Done** | `Games/UntitledFighter/Kernel/` links nothing (configure-time guard, `Games/UntitledFighter/Kernel/CMakeLists.txt`); `tests/test_kernel.cpp` proves snapshot → restore → re-simulate byte-identical at every depth |
| 3 — combat systems | **~2/3 done; the frontier** | Present: boxes, hitstun/blockstun, hitstop, pushback, knockdown, juggle, proration, hitstun decay, cancels, priority, invincibility windows, high/low/mid guard, chip, teams/tag, rounds, 8 fighter slots (`tests/test_p2_mechanics.cpp`, `tests/test_cancels.cpp`, `tests/test_combat.cpp`). **Missing:** meter live in the sim (`Fighter::meter` is declared and never written by any file in `Games/UntitledFighter/Kernel/src/`), push boxes, projectiles, per-frame hurtbox tracks, animation |
| 4 — netcode | **~40%** | `Net/` (`ISession` over vendored GekkoNet) proven under a stress session, `tests/test_session.cpp`. **No socket, no handshake, no host wiring** — `CseNet` has one consumer and it is a test |
| 5 — trigger language | **0%** | nothing, and §3.4 argues it should stay that way |
| 6 — editor panel + research | **Done, out of order** | `Games/UntitledFighter/Editor/src/ComboProverPanel.cpp`; `tests/test_ground_truth.cpp` executes the prover's printed loop |
| 7 — moddability, late-join | **0%** | — |
| *(unnumbered)* | **Built** | A whole game layer the phases never named: `Games/UntitledFighter/Game/` (session, tick-indexed input sources, `CSRP` replay format + verifier, live combo judge, and `BuildDemonstration` — a headless frame-perfect tool-assisted attacker) and `Games/UntitledFighter/Modes/` (training mode, running in both Player and Editor). Replay and versus are **libraries with no host** — `Games/UntitledFighter/Modes/src/UntitledFighterModes.cpp` names both as deliberately unbuilt menu entries |

The single most load-bearing gap is measured, not argued: with no resources in
the sim, `tests/test_gap_extent.cpp` asserts `NinetySevenOfThe121RunForever` —
the prover's graph is not yet the game. That is milestone M1.

### 1.2 The documentation

| Fact | Number |
|---|---|
| Markdown under `docs/` | 32 files, 15,271 lines (+ `README.md`, 240) |
| Places the roadmap / status lives | **6** — `NORTHSTAR.md` §4, `ARCHITECTURE.md` §3, `ADR-005-playable-priority.md` §4 (the operative order, which re-orders ARCHITECTURE Phase 3 without editing it), `ENGINE_AUDIT_2026-07.md` §4, `README.md` (three sections), `docs/manual/fighting-core.md` "Not there yet" |
| Homes for rules | **4** — `ARCHITECTURE.md` §4 (whose first line names a destination, `docs/DETERMINISM.md`, that was never created), the ADRs, `MAINTENANCE.md`, `NORTHSTAR.md` appendix. ~96 distinct rules; D8's two schema rules are stated three times each |
| Heavily-cited source paths that do not resolve | **7 of 12** — `Editor/src/Exported/Characters/` (7 cites), `Engine/src/gameplay/` (5, never existed), top-level `Kernel/` (21) and `Data/` (9), `tools/det_trace`, `docs/DETERMINISM.md`, `docs/00-research-program.md` |
| Amendment-by-annotation | `ARCHITECTURE.md`: 7 blockquote amendment blocks + 3 strike-throughs. `ADR-002` and `ADR-003` each keep a superseded verdict inline. `ADR-005` P2 shipped (`41ea6e5`) but reads as future; `ADR-009` says "Proposed … not yet written" and is implemented; `ADR-008` is implemented and reads as a plan; `ADR-007` says "unscheduled" while `ADR-008` says its trigger fired |
| Paragraphs repeated verbatim across the manual | 4× the `_HAS_EXCEPTIONS=0` isolation note; 4× the runtime-assets one-writer rule; 3× packaging; 2× the full scene-JSON schema |
| Shipped, undocumented | `Editor/src/panels/BuildSettingsPanel.cpp`; the game modes / training / replay layer. `docs/manual/fighting-core.md` lists blocking, juggle, hitstop, pushback and "nothing renders a fighter" as absent — all shipped |
| Docs checked by CI | **0**. No link check, no path check, no stamp. `docs/manual/` has no "verified" marker on any page |
| Repo instructions for an AI session | none before this ADR — no `CLAUDE.md`, no `CONTRIBUTING.md` |

Since 2026-08-12, 25 of 60 commits touched `docs/`, 14 of them alongside code —
the *habit* is good. The drift comes from having six places to update, not from
neglect.

---

## 2. The north star, on one screen

Unchanged from [NORTHSTAR.md](../NORTHSTAR.md) §2; only the status column is new.
This becomes the rewritten `NORTHSTAR.md` in M0.4.

**Goal.** An SF6-like fighting game — 3D on a 2D plane, two players, 60 Hz,
rollback, characters as files — on a framework the author reuses for the next
game, which proves the combo-termination research by running the *published*
prover on the *shipped* files, and shows every verdict running.

| Property | Done means | Today | Proof |
|---|---|---|---|
| **(a) Deterministic** | T1 same-binary rerun byte-identical; T2 rollback rerun byte-identical; T3 cross-toolchain hash identical | **Done** | `tests/test_kernel.cpp` (T1, T2), `tests/test_determinism_crossplat.cpp` on both CI legs (T3) |
| **(b) Rollback** | `memcpy` save/restore, ≤8 re-sims inside a frame, checksum + desync log | **Done locally; never networked** | `tests/test_session.cpp` (`SurvivesHundredsOfRealRollbacks`); ADR-003's stress figure — 231 rollbacks / 1617 re-simulated ticks, byte-identical |
| **(c) Data-driven** | a character is a file; the prover reads it unmodified; unknown key = load error; frame data edits land in a running match | **Files, schema, prover: done. Hot reload: not built. The sim ignores resources** | `Games/UntitledFighter/Assets/Characters/`, `Games/UntitledFighter/Data/`, load assertions A01–A20; `tests/test_gap_extent.cpp` for the gap |
| **(d) Reusable** | a second game links `Engine` with no engine edits; no fighter type in engine headers; Play == Player | **Enforced** — a title may depend on the engine, never the reverse, at configure time. Play == Player is not yet a test | `Games/UntitledFighter/CMakeLists.txt`; root `CMakeLists.txt` composes the title in one line |
| **The paper** | contribution #9: the analysis inside a working editor, plus ground-truth reproduction, plus a showcase anyone can watch | **Panel and ground truth delivered; the residual gap is (c); the showcase is M1** | `Games/UntitledFighter/Editor/src/ComboProverPanel.cpp`; `tests/test_ground_truth.cpp` |

---

## 3. The roadmap's shape, and why

The work list — every package with its *where*, *do* and *done when* — is
[`ROADMAP.md`](../ROADMAP.md). This section records the shape and the reasons, so
they are not re-litigated.

**Five milestones, one in flight at a time, each ending in a demo and a CI
gate.** Calendar estimates are deliberately absent: the six days since the first
kernel commit (`a3cc8c7`, 2026-08-12) delivered roughly two thirds of a phase
budgeted at "8–10 weeks". Sizes are relative.

| | Milestone | Why it is here, in this position |
|---|---|---|
| **M0** | One roadmap, one rule | Cheap, and every later package is read by someone — or something — that starts cold from these files. It also installs the gate that keeps them true |
| **M1** | The graph is the game | The paper's central claim: the prover's verdicts must be *true of the running kernel*. Meter live in the sim closes the measured gap; the mechanics pass makes every mechanic an opt-in field (ADR-011); the kernel's own bounded search finds what the corner-only graph prover cannot (microwalk, jump loops) and names the loss that explains the difference; the tool-assisted **showcase** — one fighter, many frame-data patches, a verified replay per verdict — is how the claim becomes watchable. Character hot reload makes the editor loop real. Nothing here needs art |
| **M2** | Two people, one match | Completes "provable": the determinism claims run over a real link, cross-platform. Almost entirely wiring of libraries that exist. Placed before skinning because the author's order is *provable and showcased first*, and a networked replay of the same catalogue is a stronger showcase than a prettier local one |
| **M3** | Skinned fighters, frame-indexed | Placeholder Mixamo rigs make the showcase look like a fighting game without spending art. Every ADR-005 rule holds: pose = pure function of `moveFrame`, animation never influences a tick. Skinning is the one renderer feature NORTHSTAR §6's freeze admits, because the showcase needs it. Skeletal-first here reverses ADR-005's "greybox now, skeletal later" **only in the sense that the placeholder is a rig instead of boxes**; the frame-indexed clip player it hangs off is the same design |
| **M4** | Showcase and publish; then art | The reel, the paper artefacts, the claims — then, and only then, SF6-tier art as content through the M3 pipeline |

**Provable before pretty.** M1–M2 produce evidence; M3 makes the evidence
legible; M4 publishes it. Real art starts when M4.1–M4.3 already sell the paper
without it. This is the author's stated order and the plan does not bend it.

**Placeholders are not art.** Mixamo rigs and clips are free to use in projects
under Adobe's terms; every imported asset gets its licence recorded beside it
(`tests/fixtures/characters/README.md` is the precedent), and nothing is
committed without that. All characters share one rig, so there is no
retargeting problem to solve.

**Tool-assisted play is the showcase mechanism.** `BuildDemonstration` already
rehearses a frame-perfect attacker headlessly; `ReplayRecorder`/`ReplayVerifier`
already write and prove replays. M1.5 turns them into a *catalogue* — one replay
per verdict — generated by the cooker, verified in CI, played by a REPLAY mode
with an on-screen input display, and bootable from the Player's command line so
capture is scriptable. Nothing about "insanely tight timing" is a problem for a
scripted input source; it is a problem only for humans, which is the point.

### 3.4 Not scheduled, on purpose

Each has a reason and a trigger. This table is the roadmap's most important
part, because every item is attractive.

| Not building | Because | Comes back when |
|---|---|---|
| **The trigger expression language** (Phase 5) | Phase 0 measured a 1.7% escape rate; ADR-006 closed the "missing nouns" with typed fields, not a grammar; 26/247 cancels gate on the defender and want two more *nouns* (`p2_state`, `p2_distance`) | a real character cannot be authored with typed fields. Then ADR-0NN, not a drive-by |
| **`SimId`, our own 128-slot snapshot ring, our own input ring** (D4–D6) | GekkoNet owns save/load/prediction; with `kMaxFighters = 8` fixed slots and no runtime spawns, the slot index *is* the id | we leave GekkoNet, or spawn at runtime. The designs stay in ARCHITECTURE as the plan for that day |
| **Projectile pool** | no shipped character has a projectile | the first one does; the 32-slot fixed design stands |
| **Asset mounts** (ADR-007) | interim staging works; the ADR names four triggers | one fires (ADR-008 says trigger 3 has — so: right after M2, not before) |
| **Engine install/export** (ADR-007's G4) | nobody outside the repo builds against it yet | after M2 |
| **Cook/pack pipeline, Lua hardening, Jolt determinism, renderer features beyond skinning** | ARCHITECTURE §2 and NORTHSTAR §6 already say why | their stated conditions |
| **Real art** | content, not engineering; and it must not precede the proof | after M4.1–M4.3 |

---

## 4. The principles that keep it short

"Modern" here means the ideas that make the plan small and the engine cheaper
to maintain. Each is already partly true of this repository; the second column
is the gap to close, and each gap is a numbered item in `ROADMAP.md`.

| Principle | Already here | Close this |
|---|---|---|
| **The build enforces; prose reminds.** A rule the compiler or CI checks cannot rot | kernel links nothing (configure guard); fast-math gate with `--self-test`; `CseNet` cannot leak GekkoNet headers | `static_assert`s incl. `has_unique_object_representations` into `GameState.h` (M1.1); `scripts/check_docs.py` *(new)* for the documents (M0.3); no kernel constant a file cannot set (ADR-011) |
| **Pure functions over stateful objects** | `Simulate(state, inputs, data)`; `FightSession` owns no clock; `FightView` is drawing with no members | presentation stays a pure function of state — the M3 pose rule, the reconciler (M3.4), and the event queue *as data in the state* (M3.1), not callbacks |
| **Plain data, fixed capacity, value semantics** | POD `GameState`, `memcpy` snapshot, `kMax*` caps everywhere | positional `res[]` (M1.1); one `constexpr scaleBy` (M1.8); movement as authored moves rather than kernel special cases (M1.3) |
| **Tables over parallel hand-maintained lists** | the loss ledger in `MatchBuilder`; the load-assertion table in the schema | a **component registry** driving serializer, undo and inspector from one table (E1); a **reflection table over `GameState`** feeding inspector, desync diff and serialiser (E6) — one artifact, three uses |
| **Types that cannot express the bug** | `int32` sub-units, no float; `ISession` moves bytes; `int FramesAhead()` at the seam | `enum class Phase` (M3.1); `[[nodiscard]]` on `Checksum()`/`Snapshot()`; typed schema *nouns* instead of an expression language — ADR-006 is the precedent |
| **One implementation per concept** | `Install*` shared by editor and player; "one predicate, one place" | one **`FightPresenter`** for Training/Replay/Versus (E3); one **shared host setup** for Player and Editor with a Play == Player hash test (E2); `ComboSearch` promoted from tests (E4); `FileWatch` extracted (E5) |
| **Tests are the spec, and they are properties** | byte-identical rerun, mirror symmetry, cross-toolchain golden, "assert what must NOT happen" | the paper's two properties become CI gates (M1.4); every showcase patch asserts the verdict change it claims (M1.6) |
| **Vertical slices — the thin thing, end to end** | training mode reaches a player in both hosts | replay and versus are libraries with no host: wire what exists before building what does not (M1.6, M2.5) |
| **Delete before you add, with a written reason** | ARCHITECTURE §2's rejection table with come-back conditions | apply it to our own design (§3.4); dead code out (E7); no new subsystem without deleting or unifying something (the standing rule in `ROADMAP.md`) |
| **One source of truth, for code and for docs** | `Install*`; the wire contract | six roadmaps, four rule homes, three copies of D8 — §5–§8 |

Optional, one line, decided deliberately: `Games/` and `Net/` targets to C++20
(`std::span` for input windows, designated initialisers for `MatchSetup`,
`<bit>` for the hash and the xorshift). GekkoNet already compiles as C++20 in
this tree; MSVC 2022 and gcc 13 both accept it. The engine stays C++17.

---

## 5. Documentation: the target shape

Six living documents, an index of frozen decisions, the manual, and an archive.

```
CLAUDE.md                              (created with this ADR) how to work here — the session loop, the nevers, the rule
README.md                              trimmed: pitch · one roadmap paragraph · build · run · ship · links. No status tables, no counts.
docs/
  ROADMAP.md                           (created with this ADR) THE status + plan page
  NORTHSTAR.md                         rewritten to ~1 screen: goal, four properties, done-tests, proofs (= §2 above)
  ARCHITECTURE.md                      D1–D9 with every amendment folded into the text; §2 rejections (+ NORTHSTAR §6 rows).
                                       No phases, no contract, no "first week", no adjudication appendix.
  DETERMINISM.md                       (new, M0.2) the contract as a table: rule · enforced by · where
  MAINTENANCE.md                       kept; its documentation section becomes §8; sim rules move to DETERMINISM.md
  STYLE.md                             kept, unchanged
  adr/README.md                        (new) index: number · title · one line · Status
  adr/ADR-001 … ADR-011                moved; frozen; one normalised Status line each
  manual/…                             kept; de-duplicated; paths fixed; two pages merged; one folded in; three sections added
  archive/README.md                    (new) "history — nothing here is current; ADR line citations resolve here"
  archive/NORTHSTAR-2026-08-12.md      verbatim original
  archive/ARCHITECTURE-2026-08-12.md   verbatim original
  archive/AUDIT_FINDINGS-2026-08-11.md
  archive/ENGINE_AUDIT-2026-07.md
```

**One fact, one home.** This table is the whole consolidation; §6 is its
application.

| Kind of fact | Home | Everyone else |
|---|---|---|
| What we are building and how we know it is done | `NORTHSTAR.md` | links |
| What is done, in flight, next, not scheduled | `ROADMAP.md` | links; README carries one paragraph and a link |
| Why a decision was made | the ADR (frozen), and `ARCHITECTURE.md` for the decisions' current shape | links |
| A rule the sim, the build or the data must obey | `DETERMINISM.md` | code comments say `DETERMINISM.md rule N`; MAINTENANCE links |
| How to change the repo | `MAINTENANCE.md`, `STYLE.md` | `CLAUDE.md` summarises and links |
| How to use a subsystem | one `manual/` page per subsystem | other pages link, never restate |
| A measurement | the test that asserts it, or `ROADMAP.md` with a date | prose quotes the number *with* the date |
| History | `archive/` | never edited, never cited as current |

Why the two verbatim archives: ADR-001 already cites `ARCHITECTURE.md` by line
numbers that are off by 39–113 lines, because amendments were inserted above
them. Freezing the originals and pointing frozen ADRs at them stops that class
of rot; the living `ARCHITECTURE.md` is then free to be rewritten. From here on,
**docs are cited by anchor, never by line** (§8).

---

## 6. File by file

| Today | Becomes |
|---|---|
| `README.md` | Keep pitch, "Where this is going" as one paragraph + link to `ROADMAP.md`, Documentation table (rewritten for the new tree), Building / Running / Shipping, License. **Delete** the feature matrix, "Not Yet Built" and the scale line (all → `ROADMAP.md`). Fix Project Structure (`Kernel/`, `Data/` live under `Games/UntitledFighter/`) |
| `docs/NORTHSTAR.md` | §2 → the new one-screen file (this ADR §2). §1 inventory (stale counts), §3 blockers (all resolved by design), §7 (answered in ADR-002) → archive only. §4 roadmap → `ROADMAP.md`. §5 research integration → the delivered parts into `manual/fighting-core.md`, the undelivered into `ROADMAP.md` M1. §6 "what not to do" → merged as rows into `ARCHITECTURE.md` §2. Appendix invariants → `DETERMINISM.md`. Original → `archive/NORTHSTAR-2026-08-12.md` |
| `docs/ARCHITECTURE.md` | Rewrite: §0, D1–D9 with ADR-001/002 amendments folded into the prose (no blockquotes, no strike-through; D9 becomes the four answers), §2 rejection table (+ NORTHSTAR §6), §5 plug-point in five paragraphs. **Remove** §3 build order (→ `ROADMAP.md`), §4 contract (→ `DETERMINISM.md`), §6 first week (done), appendix (→ archive). Fix `Engine/src/gameplay/` → `Games/UntitledFighter/Kernel/`. Original → `archive/ARCHITECTURE-2026-08-12.md` |
| the nine ADRs, `docs/adr/ADR-*.md` | `git mv` to `docs/adr/`. Frozen. Add or normalise the first line: `**Status.** Accepted <date> · Implemented @ <sha>` / `· Superseded by ADR-N`. Specifically: 005 → Implemented P0–P2, P3–P5 tracked in ROADMAP; 006 → Implemented; 007 → "trigger 3 fired (ADR-008)"; 008 → Implemented @ `1aaa2d1`; 009 → Implemented @ `41ea6e5`; 002/003 → the header states which verdict stands (adopt + vendor). Nothing else in them is edited |
| this ADR | → `docs/adr/`, Status → Implemented when M0 is done |
| `docs/ROADMAP.md`, `CLAUDE.md` | Exist (created with this ADR). Maintained per their own rules |
| `docs/AUDIT_FINDINGS.md` | → `archive/` (52/52 fixed; one inbound link, from MAINTENANCE — repointed) |
| `docs/ENGINE_AUDIT_2026-07.md` | → `archive/`. Its §5 "working on this laptop" → `manual/performance.md`; its two still-open ledger rows (EventBus dead code, gamepad on hardware) → `ROADMAP.md`; the five inbound citations repointed |
| `docs/BUILDING_LINUX.md` | → a "Linux" section in `manual/getting-started.md` (its preset table is a duplicate) |
| `docs/api-index.md`, `docs/CMakeLists.txt`, `docs/Doxyfile.in` | **Delete** (see §9.1). Doxygen is optional, never run in CI, covers only `Engine/`, and README advertises a reference that does not exist |
| `docs/MAINTENANCE.md` | Keep. "Keeping the documentation true" → §8's rule + the adversarial-audit process kept as the *periodic* check. "Never add a fast-math flag" and the renderer invariants that are sim rules → `DETERMINISM.md`, with a pointer |
| `docs/STYLE.md` | Unchanged |
| `docs/manual/index.md` | Add ROADMAP / DETERMINISM / adr pointers; "Status" section → one line + link; Lua chapter re-scoped (below) |
| `docs/manual/architecture.md` | Root now adds `ThirdParty`, `Net`, `Games/UntitledFighter`, `docs` — say so, and point at the game. Becomes the **canonical home** for the `_HAS_EXCEPTIONS=0` isolation note and the frame order; the other three copies become links |
| `docs/manual/fighting-core.md` | Fix ~30 path citations (`Games/UntitledFighter/…`). **Delete** "Not there yet" (→ `ROADMAP.md`) and the restated rules (→ `DETERMINISM.md`). **Add** the Game layer (`FightSession`, input sources, replay, combo watcher, `BuildDemonstration`) and the Modes (training mode, frame step, HUD readouts) — ~8,500 shipped lines with no manual page |
| `docs/manual/editor.md` | Add Build Settings panel (ADR-008), Combo Prover panel, and game modes in the Game view |
| `docs/manual/gameplay-scripting.md` + `docs/manual/lua-scripting.md` | **Merge** into one *Scripting* page: C++ hooks first; Lua as a section headed "presentation and tooling only — never the simulation (ARCHITECTURE D7)". No roadmap for Lua features |
| `docs/manual/getting-started.md` | "four subprojects" → the real list; absorb BUILDING_LINUX; the isolation note, staging rule and packaging become links to their canonical pages |
| `docs/manual/scenes-and-shipping.md` | Canonical home for the staging rule, the packaging steps and the scene-JSON schema; Build Settings is the shipping path, `cpack` the mechanism under it |
| `docs/manual/entities-and-components.md` | Scene-JSON schema section → link to scenes-and-shipping |
| `docs/manual/{assets, performance, physics, post-processing, rendering, ui}.md` | De-duplicate to links; `performance.md` gains the laptop section and dates on its numbers; each gets a `Verified:` stamp after a read-through |
| `Games/UntitledFighter/Assets/README.md`, `tests/fixtures/characters/README.md` | Fix the stale `Editor/src/Exported/Characters/` sentences |
| `scripts/check_docs.py` *(new)* | §8.2; one step in the `determinism-flags` job |

Expected result: ~32 files / 15.3k lines → **6 living top-level docs (~1.5k
lines), a frozen `adr/` (~4.4k), a de-duplicated manual (~7k), and an archive
nobody has to read.** Roughly a third of the reading, and one place per fact.

---

## 7. Execution order

`ROADMAP.md` M0.1–M0.6 is the executable version of this list. Additive first,
then repoint, then delete — every step leaves the tree consistent, one commit
each: **archive** the four originals → **create** `DETERMINISM.md` and the ADR
index → **add the gate, advisory** (`check_docs.py`; its first failure list is
the checklist) → **rewrite the top level** and move the ADRs → **the manual** →
**the entry points**, delete Doxygen, **make the gate required**, mark this ADR
Implemented. Steps 1–3 are an hour; the manual is the bulk and is safe to hand
to an agent under review. Two focused days end to end.

---

## 8. The rule that keeps documentation current

### 8.1 The rule (five lines — verbatim in `MAINTENANCE.md` and `CLAUDE.md`)

1. **One home per fact** (§5's table). To repeat is to fork; link instead.
2. **Same commit.** A change that makes a sentence false fixes the sentence.
   Search for the *old* path or behaviour, not the new name.
3. **Rewrite living docs; freeze ADRs.** No `AMENDED` / `STRUCK` / `Correction`
   blocks and no strike-through in a living document — edit the sentence. An
   accepted ADR is never edited except its Status line (`Implemented @ sha`,
   `Superseded by ADR-N`); a correction is a new ADR.
4. **Cite docs by anchor, code by path** (line numbers optional). Numbers live
   only where a test asserts them, or in `ROADMAP.md` with a date.
5. **Stamp and check.** Every living doc opens with `Verified: YYYY-MM-DD @ sha`,
   bumped only after re-reading the page against the code; and
   `scripts/check_docs.py` runs in CI.

The adversarial fan-out audit in MAINTENANCE.md stays as the *periodic* check —
before a release, before publishing a claim, or quarterly. The five lines above
are the daily one.

### 8.2 `scripts/check_docs.py` — what it checks (≈150 lines, same shape as `check_determinism_flags.py`)

Scans every `*.md` outside `ThirdParty/`, `out/`, `Builds/`, `docs/archive/`.
The link and marker checks skip fenced blocks and inline code spans (so an
example like this table's own `[text](path.md#anchor)` is not a finding); the
cited-path check reads inline code spans, because that is where paths live.

| Check | Fails when | Escape |
|---|---|---|
| **Links** | a relative `[text](path.md#anchor)` does not resolve (path; anchor optional at first) | — |
| **Cited paths** | a backticked token that looks like a repo path (`Engine/…`, `Games/…`, `Net/…`, `docs/…`, `tests/…`, `scripts/…`, `cmake/…`, `tools/…`, `ThirdParty/…`), with any `:line` suffix stripped, does not exist | `docs-ok` in a comment on that line, mirroring `det-ok` — for the deliberately absent (`Engine/src/gameplay/`, `tools/det_trace`) |
| **Stamps** | a living doc (`README.md`, `CLAUDE.md`, `docs/*.md`, `docs/manual/*.md`) has no `Verified: <date> @ <sha>` in its first ten lines; an ADR under `docs/adr/` has no `Status` line in its first ten | — |
| **Annotation markers** | `AMENDED`, `STRUCK`, `~~…~~`, `> **Amendment`, `Correction (` appear outside `docs/adr/` and `docs/archive/` | — |
| **`--self-test`** | any of the above fails to detect its own fixture — a gate nobody has watched fail is not a gate | — |

It runs as one more step in the existing seconds-long `determinism-flags` job in
`.github/workflows/ci.yml` — no toolchain, reports before the long jobs restore
their caches. It does **not** try to detect a stale *claim* (that needs a
reader); it detects the mechanical residue of one — dead paths, dead links,
missing stamps — which is what §1.2 shows drift actually leaves behind.

### 8.3 `CLAUDE.md` — so every session inherits the rule

Created with this ADR at the repository root. It holds the read-first order
(`ROADMAP` → `DETERMINISM` → `MAINTENANCE` → `STYLE` → `ARCHITECTURE`/ADRs), the
session loop (one WP in flight; test first; prove by reverting; docs in the same
commit; gates green; the commit message for `git blame`), the *nevers* with
where each is enforced, the *ask a human before* list (money, publishing,
history, licences, dependencies, anything `git revert` cannot undo), the
commands, the definition of done for a work package, and the five-line rule.
It is what makes "continue autonomously" mean the same thing in every session.

---

## 9. Not decided here

Each has a recommended default so the plan runs if unanswered.

1. **Doxygen.** Delete `docs/api-index.md`, `docs/CMakeLists.txt`, `docs/Doxyfile.in`
   and the README's "API Reference" row, or keep the optional target and stop
   advertising it. *Default: delete.*
2. **Lua page.** Merge into the scripting page (default) or keep a page with a
   D7 banner.
3. **M2 transport.** UDP `GekkoNetAdapter` versus vendoring asio. *Default: the
   adapter, decided by a one-day spike written up as ADR-012.*
4. **C++20 for `Games/` and `Net/`.** *Default: yes for those targets only,
   recorded as one line in `ARCHITECTURE.md`; the engine stays C++17.*
5. **Netcode before skinning (M2 before M3).** The plan follows "provable and
   showcased before art"; if the author would rather see skinned placeholders
   sooner, swap M2 and M3 — nothing in either depends on the other except that
   M3.1's event ring is reserved in M1.1 either way. *Default: M2 first.*
6. **What "impressive" needs beyond M3.** The renderer freeze (NORTHSTAR §6)
   admits skinning only. If the showcase turns out to need one more renderer
   feature — motion blur on the hit, a stage effect — it is an ADR line, not a
   drive-by. *Default: nothing else until M4.1 has been watched.*
