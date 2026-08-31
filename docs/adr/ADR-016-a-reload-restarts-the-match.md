# ADR-016: A character reload restarts the match

Status: Proposed (2026-08-31), with a recommended default the session proceeds
under: it is safe (no published claim changes, no data is destroyed) and
reversible (one mode file and one small data class; `git revert` undoes it).

## Context

NORTHSTAR property (c) ends "…and a frame-data edit lands in a running match",
and its proof cell has pointed at ROADMAP M1.5 since the doc was written. No
living document said what "lands" means. The archived 2026-08-12 NORTHSTAR
wanted a live swap ("takes effect in a running match within 0.25 s"), and the
archive cannot be cited as current — which turned out to matter, because every
contract written since then forbids exactly that reading:

- **The determinism property is per-MatchData.** DETERMINISM.md defines the
  protected property as "the same inputs against the same match data produce
  byte-identical GameState". A mid-match edit does not weaken that property;
  it partitions the timeline into two matches, each with its own proof.
- **`FightSession` never revalidates its data.** `FightSetup::data` is
  borrowed for the session's whole life, and `Restore` against state produced
  by a *different* MatchData is undefined by the header's own words
  (FightSession.h). A live swap combined with rollback re-simulates old ticks
  against new data — silently, because `HighWaterTick` only moves forward and
  nothing would flag the post-swap ticks `resimulated`.
- **A replay names one MatchData.** The CSRP header carries a single
  `matchDataHash`, latched at `ReplayRecorder::Begin`, and the verifier's
  whole diagnosis vocabulary ("THE CHARACTER FILE WAS EDITED SINCE this
  replay was recorded") assumes a recording never spans an edit. A live swap
  would produce files that validate cleanly and describe a match nobody
  simulated — the exact failure Replay.cpp's restarted-session refusal exists
  to catch one level down.

Meanwhile the repo already contains both halves of the alternative: the UI
toolkit's `UIAssetDocument` is a proven (mtime, size)-stamp poll with
keep-last-good semantics (0.25 s, tests/test_ui_hotreload.cpp), and
`UntitledFighterMode::startCharacter_` is already a full from-disk teardown
and rebuild, keyed to C for a *different* character.

## Decision (recommended default)

**An edit lands by restarting the match with the freshly built data, through
the one existing start path. Nothing ever swaps MatchData under a live
session.**

Concretely, in the training mode:

1. **Detection** is a poll of the loaded character file's (mtime, size) stamp
   — both halves, for the UI system's stated reason (coarse mtime
   granularity) — every 0.25 s of accumulated fixed-step time. The machinery
   is `cse::data::CharacterFileWatch`, which resolves its path through the
   same `PathIsContained` sandbox as every other authored read.
2. **Keep-last-good**: the changed file is loaded and built into
   *temporaries*. A broken edit (the normal state while typing) leaves the
   running match untouched and puts the loader's own error on the HUD; the
   stamps still refresh, so one bad save reports once and the fixing save
   reports again. `LoadCharacterFile` zeroes its output even on failure,
   which is why loading into the live `character_` is not an option.
3. **On success** the mode runs the same teardown-in-dependency-order and
   adopt that C runs, then re-Begins the session. `Begin` *is* the restart
   (state memset, tick index and high-water reset, observers kept) — the
   protocol test_training_mode.cpp already pins.
4. **The author's time posture survives**: pause and the slow-motion divisor
   are preserved across a reload, because the person saving the file is
   usually frame-stepping the very move they are editing. Everything that
   names an *absolute tick* of the old match is dropped, for resetMatch_'s
   own reasons: the demonstration, pending taps, pending frame-steps, and
   the latched advantage.
5. **A missing or broken file recovers by the same poll**: the watch stays
   bound even when a load failed, so staging or fixing the file revives the
   match without a keypress (previously only C — which advances to the NEXT
   character — could).

## Duties of a future host that records

The training mode records nothing today, so this is a contract for M2.x
rather than code: a host that hot-reloads while recording MUST re-Begin its
recorder at the reload (new `HashMatchData`, fresh input log). A replay file
never spans an edit; the recording made before the edit is finished or
discarded, never continued. This is the same duty `Replay.cpp` already
documents for round restarts, applied to the other thing that changes the
data a tick means.

## Alternatives rejected

- **Live-swapping the borrowed MatchData mid-session.** No crash — `Simulate`
  is pure over (state, inputs, data) — and that is what makes it poisonous:
  rollback across the edit boundary is UB by FightSession.h, `resimulated`
  never flags the changed ticks, and any recording validates while lying.
  The archived NORTHSTAR's 0.25 s promise is kept by the poll interval, not
  by the swap.
- **Reload only on a keypress.** The C key already exists and is kept, but as
  the *manual* form. Property (c) promises the edit lands; an authoring loop
  that requires alt-tabbing back to press a key is the current state plus a
  keybinding, and R5 (the authoring-loop review point) would be reviewing a
  feature that does not do the thing its sentence says.
- **Watching the authoring source under `Games/…/Assets/`.** The mode loads
  through its content-root sandbox, and the staged copy beside the executable
  is the only copy it may read. The watch therefore watches the staged copy;
  the manual says so, because "I edited the file and nothing happened" with
  the *source* copy open is the documented trap (CLAUDE.md, fighter-data
  memory) and the build restages it.

## Consequences

- The reload is a hitch inside a fixed tick, like C, and unlike C it happens
  at a moment the player chose only indirectly (by saving). Accepted: the
  person saving character files at a running training mode is the author the
  feature exists for.
- Health, position and combo history do not survive an edit. That is the
  point — after a frame-data change the old state describes a match that no
  longer exists, and every contract above says so.
- The property test lives at the seam the mode mirrors
  (tests/test_character_hotreload.cpp, against a real `FightSession` and real
  files); the mode's `pollHotReload_` is thin glue over it, verified by a
  human at R5.

**What would reverse this.** A session layer whose snapshots carry a data
identity — so `Restore` can refuse or reconcile a cross-data restore instead
of being undefined — and a replay format with per-segment data hashes. Both
exist nowhere and are planned nowhere; until they do, a live swap is not a
faster restart, it is a desync with a delay on it. That would be a new ADR.
