# ADR-004 — Choronos considered as the session layer, and not taken

**Status:** Accepted 2026-08-12 · stands. **Date:** 2026-08-12. **Decides:** whether to port the author's own C#/MonoGame
rollback library instead of adopting GekkoNet. **Relates to:** `ADR-002` CHOICE A, `ADR-003`.

**Answer: adopt GekkoNet. Keep Choronos, and use it as the fallback's specification.**

The reasoning is not "Choronos is worse." It is that **the half of Choronos that is most valuable is
the half that does not survive translation to C++**, and the half that ports cleanly is the half
`ADR-003` already prices at about eight days to write from scratch.

---

## What is actually there

Measured, not estimated: `Chronos.Rollback` **841 LOC**, `Chronos.Core` **79**, `Chronos.Net`
**1,626**, `Chronos.RelayServer` **167** — **2,713 total**. Of `Chronos.Rollback`, 133 lines are
replay JSON, so the rollback algorithm proper is roughly **400 lines of substance**.

| Piece | State |
|---|---|
| Rollback loop (restore at mispredicted−1, resim, re-save) | **Complete**, and correct |
| Prediction by repeat-last-confirmed | **Complete**, consistent in both forward and resim paths |
| Connection lifecycle — heartbeat, timeout, join/version handshake, ping/RTT, LAN discovery, chunked reassembly, relay | **The best part**, 1,626 LOC |
| Replays (seed + input history) | Present, JSON |
| **Input delay** | **ABSENT.** `RollbackConfig.cs:21` declares `InputDelayFrames = 2` and **nothing reads it** — one grep hit repo-wide, its own declaration **[V]** |
| Time sync / frame advantage | 7 lines, one-sided: client chases host, host never adjusts |
| Desync detection | Present, but compares against a store that has not yet been rolled back, so it fires on ordinary mispredictions |
| Reliability (ack/resend) | Absent except input redundancy — a dropped `StartGame` and that client never starts |
| Spectators | Absent |

---

## What Choronos gets RIGHT, and it is not a small list

This matters because it is evidence about the author rather than about the library, and the author is
the person who will maintain whatever is chosen.

- **Integer simulation with sub-units.** `IntVector2` with a sub-pixel scale of 100. That is D2's
  rule, arrived at independently.
- **Float quantized once, at load.** `speedPerFrame = (int)(PlayerSpeed * SubpixelScale * FixedTimeStep)`
  — one truncation at construction, integers forever after. That is **D8's rule**, also arrived at
  independently, and D8 is the rule ADR-001 later found the project's own draft violating.
- **xorshift32** for the PRNG — bit-for-bit the same algorithm the kernel independently chose, and
  for the same reason the standard engines are unusable.
- **The packed-order hazard is handled correctly.** `ComponentPool.Remove` is swap-and-pop — the
  exact instability D1 names in EnTT's `sparse_set` — and `CaptureState` saves the entity list *in
  packed order* so a restore reproduces the permutation exactly. This is the subtle one, and it is
  right.

Three of this architecture's load-bearing rules were discovered independently by the same person, in
a shipped game. That is the strongest signal in this document, and it is an argument about who should
own the session layer rather than about which session layer to own.

---

## The three findings that decide it

**1. The PRNG is not in the snapshot, and the author knew.** `GameStateSnapshot`'s constructor
captures frame, next-entity-id and the pools — not the RNG. `Simulation.RestoreState` is fifteen
lines of comment admitting it and doing nothing **[V]**, ending *"For full determinism, RNG should be
captured."* Compare `Kernel/include/cse/kernel/GameState.h:75-77`, which makes `rng` a snapshot field
with a comment explaining it is the **only** way a re-simulated tick reproduces its first run.

**2. It has never mattered, and that is the problem.** `Rng` is touched at exactly three places:
its declaration, its construction, and `GenerateMap()` **[V]**. `Update()` never reads it. So
**Choronos's rollback loop has never once run against a simulation that consumes randomness inside a
tick.** The bug is latent because the workload never provoked it. A fighting game provokes it
immediately — ADR-001 found a corpus character selecting its attack by a die roll evaluated every
tick.

**3. Nothing pins the property the whole thing rests on.** There is no "run to N, snapshot, re-run,
restore, re-run, assert identical" test anywhere. `PerformanceTests` runs 36,000 frames with forced
rollbacks and then asserts nothing — its assertion section is four lines of commentary. That test is
`ARCHITECTURE.md` Phase 2 test #1, and `Kernel/tests/test_kernel.cpp` now has it.

**And one architectural collision.** When a correction is older than the snapshot store, Choronos's
host serializes the entire world and ships it. That is **periodic authoritative state resync** —
precisely what `ADR-002` CHOICE C rejects for 2-player P2P, on the grounds that it demotes
determinism from a correctness requirement to an optimization. Choronos implements the correction
half this project decided not to want.

---

## The port cost, and why it loses

| | |
|---|---|
| Rollback loop + prediction + misprediction detection | 1 wk |
| Snapshot ring — new code per D4, not ported | 2 d |
| Input ring as flat arrays, **plus the input delay that does not exist** | 3-4 d |
| Transport rewrite on asio/enet (`System.Net`, `IPEndPoint` as a dict key, `UdpClient` polling) | 1 wk |
| Wire format re-pinning + tests (`BinaryWriter` endianness/bool guarantees do not carry to a C++ struct) | 3-4 d |
| Connection lifecycle port — 1,626 LOC, mechanical but bulky | 1.5-2 wk |
| Symmetric frame advantage — 7 one-sided lines today, so effectively new | 1 wk to write, indefinite to tune |
| Whole-struct checksum + abort semantics | 2-3 d |
| Replays, binary (reflection-based serialization has no C++17 equivalent) | 3-4 d |
| Integration, and a rewrite's own bug budget | 2 wk |
| **Total** | **8-11 weeks** |

`ADR-003` prices writing the session layer from scratch at **6-9 weeks**. **Porting is worse than
writing.** The reason is structural: the parts that port cheaply (the ~400-line rollback loop) are
the parts already cheap to write, and the part that is genuinely expensive — connection lifecycle —
is both the least portable code in the repository and expensive either way.

**The biggest single risk if it were attempted:** `IGameSimulation<TInput,TState>` has the simulation
construct an opaque `TState` object. D4 wants bytes and a length. All 841 lines of `Chronos.Rollback`
are written against the object shape, so changing it is not a port — it is writing a new layer while
believing you are porting one.

---

## DECISION

**Adopt GekkoNet** (`ADR-003`: all three gates passed, and a stress session rolled our real
`GameState` back 231 times with a byte-identical result). **Do not port Choronos.**

**But do not delete it either, and do not treat it as rejected.** Two concrete uses:

1. **`Chronos.Net` is a written-down list of every connection-lifecycle case the author already hit
   in a game they shipped** — heartbeat cadence, timeout policy, join/version handshake, chunked
   reassembly, LAN discovery, host relay. `ADR-003` prices that lifecycle at 3-5 weeks of failures
   that "only appear on real networks." A list of the ones you already found beats a blank page, and
   reading GekkoNet does not produce it. **Use it as a conformance checklist against what GekkoNet
   actually handles.** That is an afternoon.
2. **If GekkoNet is ever dropped**, `RollbackSystem.Step` + `ResimulationRunner.PerformRollback` are
   a working reference for the exact loop, written by the person who has to maintain the result.

**Amend Choronos's own repository regardless of what happens here**, because two of these are real
bugs in a shipped game and cost little to fix: put the RNG in the snapshot, and add the round-trip
determinism test. The first is latent only because Bomberman never rolls a die inside a tick.
