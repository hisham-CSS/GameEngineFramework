# North star

Verified: 2026-09-02 @ 0e2d423

What this engine is for, and how anyone can check whether it has got there. One
screen on purpose. Where the work stands is [ROADMAP.md](ROADMAP.md) and nowhere
else; why the design is what it is, is [ARCHITECTURE.md](ARCHITECTURE.md) and the
[ADRs](adr/README.md); the rules the simulation must obey are
[DETERMINISM.md](DETERMINISM.md).

## The goal

An **SF6-like fighting game** — 3D characters on a 2D plane, two players, 60 Hz,
rollback netcode, characters authored as files rather than code — on a framework
the author reuses for the next game, which **proves the combo-termination
research** by running the *published* prover on the *shipped* character files
inside a working editor.

The engine's job is to make that proof **visible and convincing**. Every verdict
the prover prints must be demonstrable in the running game, frame-perfectly, by a
tool-assisted player, as a replay anyone can watch — and the **same fighter,
loaded with different frame data, must show different verdicts**, which the
cooked thirteen-row catalogue now does: a link that becomes a loop from seven
extra frames of hitstun, a microwalk loop the corner-only prover cannot see, a
jump-cancel string the model prices dead (executed worst case 7 → 13, the bound
held from the other side), a counter opening that revives a dead cancel, a
meter loop both instruments agree on, and two agreements-with-a-named-gap (the
wall bounce's corner null; the kara both proofs skip). That is possible because
every mechanic is an opt-in field on a move and never a rule in the kernel
([ADR-011](adr/ADR-011-mechanics-are-fields.md)).

Timing no human could hit is not a problem here: the input source is scripted.
That it is impossible for a human is the point.

## The four properties, and what "done" means

Each is a test, a number or a demo — never an adjective.

| Property | Done means | Proof |
|---|---|---|
| **(a) Deterministic** | **T1** the same binary re-run from the same state and input log ends byte-identical (not `EXPECT_NEAR`); **T2** snapshot → run → restore → re-run the same ticks is byte-identical; **T3** a state hash recorded under one toolchain is reproduced exactly by another | `tests/test_kernel.cpp` for T1 and T2; `tests/test_determinism_crossplat.cpp` for T3, re-checked by gcc 13 on the Linux CI leg. Only T3 settles the libm question — everything short of it is inference from source |
| **(b) Rollback** | save and restore are one `memcpy` of a fixed-layout POD; eight restores plus eight re-simulations fit inside one 16.6 ms frame; every confirmed tick carries a checksum, and a desync names the first divergent tick **and field** | `KernelRollback.EightTickRewindIsExactAtEveryDepth` in `tests/test_kernel.cpp`; `Session.SurvivesHundredsOfRealRollbacks` in `tests/test_session.cpp`. The field-level desync report needs the reflection table — ROADMAP M2.3 |
| **(c) Data-driven** | a character is a file dropped in a directory — zero recompiles, zero engine edits; an unknown key is a **load error naming the key**, never a silent default; the published prover reads the engine's own files unchanged, with no export step; and a frame-data edit lands in a running match | `Games/UntitledFighter/Assets/Characters/` and the load assertions A01–A22 in `Games/UntitledFighter/Data/src/CharacterData.cpp`; `tests/test_character_data.cpp`. Hot reload: the edit lands as a restart with the freshly built data — the rule is [DETERMINISM.md](DETERMINISM.md) T8, the authoring loop is [the manual's hot-reload section](manual/fighting-core.md); `tests/test_character_hotreload.cpp` |
| **(d) Reusable** | a second game links `Engine` with no edit to `Engine/`; no fighting-game type appears in an engine header; and the editor's Play mode and the shipped Player run the **same** code path | configure-time boundary guards in `Games/UntitledFighter/CMakeLists.txt` and the root `CMakeLists.txt` — a title may depend on the engine, never the reverse. Play == Player becomes a hash test in ROADMAP M2.6 |
| **The paper** | contribution #9 — the analysis inside a working editor — plus ground-truth reproduction of a printed loop, plus a showcase catalogue anyone can watch | `Games/UntitledFighter/Editor/src/ComboProverPanel.cpp`; `tests/test_ground_truth.cpp` executes the prover's own printed witness rather than a hand-written one. The catalogue is ROADMAP M1.6 |

## The order, and who may bend it

**Provable and showcased before any art.** Evidence first — the kernel's own
bounded search and a verified replay catalogue — then skinned placeholders
generated through Blender to make the evidence legible, then a real link
between two machines, then publication, and only then SF6-tier art, as
content, through a pipeline that already exists. The reasoning is
[ADR-010](adr/ADR-010-one-roadmap-one-rule.md) §3; the one bend in it — the
placeholders ahead of the link, and one modeled body ahead of publication — is
the author's own, recorded with its reversal condition in
[ADR-020](adr/ADR-020-the-bounded-lift.md); the milestones are
[ROADMAP.md](ROADMAP.md). Nobody else bends it.

Two consequences that are easy to lose and expensive to recover:

- **Nothing about animation ever influences a tick.** Pose is a pure function of
  the state the simulation already produced, and a return-to-idle tail is
  presentation only, interrupted the instant the simulation acts
  ([DETERMINISM.md](DETERMINISM.md) P4).
- **A claim goes out no earlier than the test under it.** Nothing in the README,
  on a website, or in the paper outruns something CI asserts.
- **A test answers "is it correct", not "is it right".** The second question
  needs a person, so [ROADMAP.md](ROADMAP.md)'s *review points* say when the
  simulation is worth looking at, what should happen, and what would mean it
  is wrong. R6 — the showcase catalogue — is the one to judge the project on.
