# ADR-017: Authoring telemetry is one appended line per prover run

Status: Proposed (2026-08-31), with a recommended default the session proceeds
under: it is safe (a new file beside the content root; no published claim, no
sim byte, no schema field changes) and reversible (one writer/reader pair and
one call site; `git revert` undoes it).

## Context

ROADMAP M1.7's living entry is one sentence — "what the author actually needed
to know, recorded while authoring rather than reconstructed after" — with no
Done-when. That is an accident of history, not a decision: the entry as
written (`59e09dd`, 2026-08-17) carried a full contract — *one JSON line per
prover run (content hash, move/cancel counts, resource ranges, `explored`,
wall-clock ms, verdict, changed-since-last); done when the file grows by one
line per run and a test parses it* — and the 1698→441 ROADMAP rewrite
(`9c4dbd1`) stripped it while its own message claimed every Done-when was
kept. The archived NORTHSTAR (2026-08-12, informing but not citable) wanted
the same thing and said why the timing matters: *"Instrument this from the
panel's first day — it is worthless retroactively."*

The panel already measures everything and records nothing. `runIfStale_`
times each real analysis (`runs_`, last/worst/mean ms), times the resource
check *separately* so the analyse latency stays comparable — the distribution
ARCHITECTURE §3 lists among what the paper harvests — and gates on a content
fingerprint that already answers "changed since last". All of it is in-memory,
shown in the footer, and lost at editor exit. Meanwhile the title's only
durable authoring record is the catalogue cooker's output directory, and its
`catalogue.txt` is truncate-on-cook — a reconstruction *after* the fact, the
exact thing M1.7's sentence rejects.

## Decision (recommended default)

**Each real analysis run appends one JSON line, at the run site, through a
sandboxed append-only writer in CseData.**

- **The line**: wall time (unix seconds), the authored path read (empty when
  the character came from the caller — the staged-copy trap taught that a
  record must name *which file*), character name, the **nonce-free** content
  fingerprint, changed-since-last (computed against that nonce-free hash, so
  pressing Re-run on the same bytes records `false` — "the author edited" and
  "the author pressed the button again" are different facts), move / cancel
  counts, the resource ranges (name, initial, floor), `explored` and `capped`
  from the result, the run's ms, the resource-check's ms **as its own field**
  (folding it in would corrupt the latency distribution the panel's own
  comment protects), and the verdict.
- **The writer**: `cse::data::AppendProverRun` — `PathIsContained` with an
  absolutized base (the MSVC `weakly_canonical` lesson from M1.5), parent
  directories created, the whole line serialized before the file is opened,
  one write. The reader `ReadProverRuns` parses lines back and
  **skips-and-counts** malformed ones rather than failing the file — a crash
  mid-append must cost one line, not the log.
- **The sink**: `telemetry/prover_runs.jsonl` relative to the editor's
  working directory — a *sibling* of the staged `Exported/`, never inside it
  (restaging clobbers that tree, and `CharacterFileWatch` polls files there).
- **The failure mode is honest and quiet**: an append that fails latches one
  line into the panel footer; it never blocks the analysis or the draw.

CseData is the home for the same reason `CharacterFileWatch` lives there: it
is the layer that already compiles the path sandbox and (privately) the JSON
library, and it is outside every determinism glob.

## Rejected

- **Recording in `Game/` or `Kernel/`.** The kernel's include allowlist
  forbids it outright; `Game/` bans `<chrono>` and float, and telemetry
  wants both wall time and the panel's measured `double` ms. The writer takes
  caller-supplied numbers, and the only caller with a clock is Editor-side.
- **Cooker-side only.** The catalogue's directory is durable and CI-audited,
  but a per-cook record is reconstruction after the fact. It stays as the
  *gate* record; M1.7 is the *authoring* record. (The archive's "gate
  telemetry" item also named `AssetCooker`, which may not know a title —
  that role now belongs to `UntitledFighterCatalogue`.)
- **A summary table in docs.** Status tables live in ROADMAP only; the log is
  data for a future harvest, and any published number from it needs a CI test
  behind it first.

## Consequences and honest limits

- Instrumentation begins 2026-08-31 — later than "the panel's first day" the
  archive asked for. Any harvested claim can honestly span only from here;
  "how long infinites survived before the tool existed" is not measurable
  from this log and must never be claimed off it.
- The log is per-machine and per-working-directory, and a clean build wipe
  deletes it with the build tree. Accepted for (S) scope: the harvest copies
  the file out; a configurable sink is the first extension, not this WP.
- The verdict is computed with floats; two machines can disagree
  (ARCHITECTURE §3). Lines aggregated across machines can show verdict flips
  that are toolchain artifacts, and a harvest must bucket by machine.

**What would reverse this.** An engine-owned logging seam (none exists — 26
files of ad-hoc `std::cerr`) or an editor config system that owns sink paths;
either would subsume the writer's location decision and get a new ADR. A
paper harvest needing per-keystroke resolution would also reopen the "what is
a run" definition, which today is the fingerprint gate's.
