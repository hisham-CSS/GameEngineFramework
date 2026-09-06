# ADR-023 — One game-mode host: the block both executables write is one engine unit, and Play == Player is a test through it

**Status.** Proposed 2026-09-06 · recommended default **enacted** under
CLAUDE.md's safe-and-reversible clause (an extraction inside the engine; no
dependency, no wire contract, no title type crosses into the engine) — the
human confirms or reverses by editing this line.

## Context

ROADMAP M2.6 is "Play == Player, as a hash test". ADR-010's row (d) records
the property as "not yet a test", and its E2 asks for one shared host setup
for the Player and the Editor with a hash test. Three readings of the two
hosts on 2026-09-06 found the same ten blocks written twice — the
`GameModeContext` factory, the `SetFixedUpdate`/`AddUpdate` lambdas that call
the active mode's `FixedTick`/`Update`, `RegisterTitleGameModes` under its
macro, `menuHooks_.modes` and the `onEnterMode` body, the owns-screen branch
of the UI draw, `DrainExitRequest`, the capture-provider term, and the `Leave`
after the run loop — near-verbatim in `Player/src/PlayerMain.cpp` and
`Editor/src/EditorApplication.cpp`, down to comments that cite each other by
line numbers that have since drifted.

Two facts decide what a test can be:

- **Neither executable can be driven by a test.** Both are `add_executable`
  targets whose whole host loop is one function's locals; `Application`'s
  constructor opens a real, visible window; the Player names no game key on
  purpose and the Editor parses no arguments at all; a mode is entered only by
  a menu click, and the Editor only while playing. A spawned-process golden
  would need new flags in both hosts, Mesa copied beside them, a visible
  window under llvmpipe that nothing in this repository has proven, and it
  could run only in the GL job — and it would still compare a keyless match.
- **The mode's bytes are a function of tick index and inputs only**
  (DETERMINISM.md T1, T5; the M2.4 and M2.5 tests). The two hosts can differ
  only in how many `FixedTick` calls a wall-second yields and where the pad
  comes from — never in the bytes at tick N. So "Play == Player" is exactly:
  the wiring that feeds the mode is one implementation, and its every host
  convenience (gameplay gated off until Play, the pad following focus, pause
  and time scale, the clock reset on Play) leaves the bytes at tick N alone.

## Decision (recommended default), in four parts

**D1. One engine unit owns the block.** `Engine/src/core/GameModeHost.h/.cpp`
holds what both hosts wrote: it registers the title's modes, wires the two
tick hooks to the active mode, enters a mode from the menu through the host's
context factory and a `canEnter` predicate, drains exit requests, draws an
owning or an overlay mode, answers the capture-provider term, and leaves. It
is written against a **narrow hook sink** — function slots the host fills
from `Application` and a test fills from lambdas — not against `Application`
itself, because `Application` opens a window and the test must not. Both
hosts call it; what stays in each host is what genuinely differs: the Editor's
`playing_` refusal (its predicate), where each host drains (the Player in the
game renderer's UI draw, the Editor in `Application::SetUIDraw` because the
Game panel's pass does not run while closed), the Editor's extra `Leave`s on
Stop and scene swap, and each host's pre-draw housekeeping.

**D2. The hash test is through the unit, headless, in the CI-gated set.**
`tests/test_game_mode_host.cpp` builds the host twice with the real registry
and the real `UntitledFighterMode`: once with the Player's parameters
(gameplay always on, the pad never suppressed, no clock reset) and once with
the Editor's (gameplay off for the first frames, then Play with a clock reset,
the pad following focus through `GateFrame`, pause and a fractional time
scale applied as the run loop applies them), drives both through the same
emulated run loop over the Replay intent (the committed `base.csrp`, so no
pad is needed and every checkpoint is a witness), and asserts
`cse::kernel::Checksum` of the state is equal at every equal `CurrentTick`,
that both reach the file's end, and that the replay's verifier disagrees with
neither. A second test holds the unit's own contract: a refused `canEnter`
enters nothing and returns the host's sentence, an exit request drained
leaves, and the owns-screen branch draws once and tells the caller to return.

**D3. A structural gate keeps the duplication from returning.**
`scripts/check_host_setup.py`, in the determinism job beside `check_docs.py`,
with a `--self-test`: each host names `GameModeHost` exactly once and contains
none of the lines the unit now owns (`->FixedTick(`, `->Update(dt)`,
`modes_.Enter(`, `RegisterTitleGameModes(`). It proves no bytes; it proves the
block cannot quietly grow a second copy, which is the failure ADR-010's
"one implementation per concept" names.

**D4. What is left to the human.** The two executables, each with its real
`InputMap` (GLFW-polled in the Player, ImGui-polled in the Editor) and its
real window, are review point R9's; no test in CI drives them, and the manual
says so rather than claiming it.

## What this does not decide

A headless `Application` (a hidden-window seam on `Window`), a command-line
door into a mode for either host, the Editor's Fixed Tick Hz slider (not in
`FrameGate`; byte-safe because it changes ticks per second, never the bytes
at a tick), and whether the Player's front-end join should read the host's
one `contentRoot` constant — recorded as risks, not decided here.

## Reversed if

A host convenience is found that the emulated run loop cannot express (the
test would then be lying by omission, and the answer is a hidden-window
`Application` and a GL-labelled test through the same unit); or a second
title's host needs a different block, in which case the unit grows a
parameter rather than the host a copy.
