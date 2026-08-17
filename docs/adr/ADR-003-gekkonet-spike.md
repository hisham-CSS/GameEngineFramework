# ADR-003 — GekkoNet: the spike

**Status:** Accepted 2026-08-12 · **Implemented.** The building spike ran and all three gates passed, so the standing verdict is **adopt GekkoNet, and vendor it** -- it is not in vcpkg, so adopting means a pinned submodule. Amends `ADR-002` CHOICE A.
**Date:** 2026-08-12. **Pinned commit:** `5924b5c7abb5b1156c3c5609c9c36e9bede58c1c` (2026-08-04).

> ## The building spike ran. Here is what it did and what it found.
>
> The read-only half of this document (below) reached "inconclusive" and named
> three things a real spike had to establish. All three were done, against a
> pinned SHA rather than a moving branch.
>
> **1. The grep, not the summary.** `float|double|f32|f64` across `GekkoLib`
> excluding `thirdparty`: **30 hits, all accounted for.** `sync.cpp`, `input.cpp`,
> `event.cpp` and `storage.cpp` have **zero** — the read-only spike's suspect
> claim turns out to be true, and the adversarial pass's suspicion that it had
> queried the wrong token was reasonable but wrong.
>
> **2. The float is provably terminal.** `GetAverageAdvantage()` (`backend.cpp:1273`)
> is called by exactly one thing, `FramesAhead()` (`game_session.cpp:210`), which
> is called by exactly one thing, the public `gekko_frames_ahead()`
> (`gekkonet.cpp:101`). **There is no internal consumer.** It leaves the library
> and never returns. The examples use it to compute a `delay_ns` sleep — local
> wall-clock pacing, outside the simulation entirely.
>
> **3. `running_ahead` is integer.** `_runahead_frames` is a `u8` the host sets
> explicitly via `gekko_set_runahead()`, and `game_session.cpp:690` is
> `for (u8 i = 0; i < _runahead_frames; i++)`. `running_ahead` is just
> `_runahead_frames > 0` (`:179`). The other timing predicate,
> `ShouldStallAdvance()` (`:651`), compares `Frame` against `INT32_MAX`. **No
> float decides how many ticks run.** The disqualifying condition does not hold.
>
> **4. It builds, first try.** MSVC, Ninja, RelWithDebInfo: 28/28, no errors, no
> warnings surfaced. Flags are `/EHsc /O2 /Ob1 /DNDEBUG -std:c++20 -MT -Zi`. **No
> fast-math of any kind** — our determinism gate passes over its tree.
>
> **5. THE TEST THAT MATTERS — it drives our kernel, and rollback is exact.**
> A harness linked `CseKernel` and `GekkoNet_STATIC.lib` and ran our real
> `GameState` (80 bytes) through GekkoNet's real event loop, with
> `memcpy(event->data.save.state, &live, sizeof(GameState))` on save and the
> reverse on load — `ARCHITECTURE.md` D4 verbatim, no adapter, no translation.
> Under `GekkoStressSession`, which rolls back continuously to hunt desyncs:
>
> | | |
> |---|---|
> | advances | 1857 |
> | saves | 1857 |
> | **loads (real rollbacks)** | **231** |
> | **re-simulated advances** | **1617** |
> | final state vs kernel-alone reference | **byte-identical** |
> | checksum both paths | `A8148EF4` |
>
> That is the whole design validated end to end: GekkoNet's API took our bytes,
> rolled us back 231 times, re-simulated 1617 ticks, and produced exactly what the
> kernel produces on its own. It also independently proves the kernel is
> deterministic under real rollback pressure rather than only under the synthetic
> rewind in `test_kernel.cpp`.
>
> **The one real friction, confirmed and priced.** `GekkoLib/CMakeLists.txt:10-15`
> sets `CMAKE_MSVC_RUNTIME_LIBRARY` from `BUILD_SHARED_LIBS` — static build gets
> `/MT`, and our Engine is `/MD`. That is a category error on their side (the CRT
> choice is independent of static-vs-shared) and it **overrides an external
> `-DCMAKE_MSVC_RUNTIME_LIBRARY`**, so it is a one-line patch to the vendored
> copy, not a configure flag. Also: consumers must define `GEKKONET_STATIC` or
> the header defaults to `__declspec(dllimport)` and the link fails with `__imp_`
> symbols (`gekkonet.h:35-38`).
>
> **Not established:** anything about gcc/Linux. There is no gcc on this machine;
> CI is the first place that gets tested.
>
> ---
>
> **Everything below is the earlier read-only pass, kept because its reasoning
> about what a spike could not settle was correct, and because the three
> corrections to ADR-002 still stand.**

---

## The answer in one paragraph

GekkoNet clears two of ADR-002's three gates on real evidence, fails to establish the third, and
turns out to be described wrongly in three factual particulars by ADR-002 itself. **The
recommendation to adopt still stands as the working assumption, and it is not yet safe to act on.**
A read-only evaluation cannot close the question it was asked, and saying so is the finding.

---

## What the spike established, with evidence

| Gate (`ADR-002:44-52`) | Result |
|---|---|
| **API takes bytes, not types** | **PASSED.** `GekkoSave`/`GekkoLoad` carry `unsigned char* state` plus a length. Their own example is `memcpy(event->data.save.state, &gs.state, sizeof(State))` — which is `ARCHITECTURE.md` D4 verbatim. No template, no inheritance, no allocator hook, no allocation of our state. |
| **Builds under MSVC and gcc; no fast-math** | **PASSED, with one known clash.** CMake ≥3.15. Its C++20 is quarantined behind a C public header, so our C++17 is untouched and its `set(CMAKE_CXX_STANDARD 20)` stays in its own directory scope — unlike the `Jolt.cmake` mechanism ADR-002 §2 objected to, which is `include()`d and therefore leaks. No fast-math flag anywhere. The MSVC `/MT` vs `/MD` runtime clash is a **certainty to plan for**, not a risk to discover. |
| **No float in the state path** | **NOT ESTABLISHED.** See below. |

**Also free, and worth having:** `GekkoDesynced { frame, local_checksum, remote_checksum, remote_handle }`
with a `GekkoDesyncDetected` event, driven by `config.desync_detection` + `check_distance`. That is
ADR-002 CHOICE C's detect-and-abort, already built.

**One constraint on the seam that ADR-002 did not name.** GekkoNet **drives the loop** — it emits
Advance/Save/Load events and the host switches on them. So `ISession` must be an **event-pump**
shape, not a "call me to roll back" shape. If the fallback is ever taken, the hand-written session
must emit the same event stream. Writing that into the seam's contract costs nothing today and is a
rewrite later.

---

## Why the float gate is not closed

ADR-002's disqualifying condition is precise: float that **feeds back into simulation timing**.

The spike proved something adjacent and easier — that **no float crosses the wire**. The advantage
history is `i8`, the `InputAckMsg.frame_advantage` field is `i8`, and the one `f32`
(`GetAverageAdvantage`) is computed locally at the last step from that integer history. All true, all
verified against quoted source, and **not what was asked.**

Three defects in the method, found by the adversarial pass:

1. **No commit SHA anywhere.** Everything was read from a default-branch HEAD, which moves. The
   subsystem under examination was *itself redesigned inside the read window* — a commit titled
   "Redesign time sync to track frame advantage per peer" lands in the same period. Nothing in the
   spike can be re-verified by anyone, including its author.
2. **The negative-existence claims likely queried the wrong token.** "`sync.cpp` contains no float"
   is not evidence when the house style is `f32` — the spike's own quoted code is
   `f32 sum_local = 0.f;`. A summarizing reader asked about `float` can answer "there are none"
   truthfully while `f32` is on every third line. Treat those three files as **unexamined**.
3. **`running_ahead` is unexplained.** `GekkoAdvance` carries a `bool running_ahead` alongside
   `rolling_back`. Runahead means *simulating extra ticks*, and how many is exactly the kind of
   decision that a frame-advantage number could be feeding. Nothing in the read-only pass rules that
   out, and it is the single most likely place for the disqualifying pattern to live.

**Mitigation that survives either answer:** round the advantage to an integer at the `ISession`
boundary and never let a float past it. That is one line in our code and it converts the question
from "is their pacing float-free" to "does our simulation ever see it" — which we control.

---

## Three corrections to ADR-002 CHOICE A

`ADR-002:38` describes GekkoNet as "(MIT, small, C)". All three are wrong:

- **Licence is BSD-2-Clause**, not MIT. Both permissive; the attribution clause differs. No obstacle,
  but the licence file we would ship is a different one.
- **It is C++20**, not C. Only the *API* is C — which is the good news of the build section, but it
  is not the same claim.
- **"Small" understates the vendoring surface by 3×.** GekkoNet is ~4,000-4,500 LOC, but
  `GekkoLib/thirdparty/` carries **asio 1.38.1** (BSL-1.0) and **zpp_bits** (MIT, a 270 KB single
  header whose C++20 requirement is what forces the standard). Vendoring GekkoNet means vendoring
  three libraries and tracking three licences.

None of these reverses the decision. All three change what "adopt" costs, and ADR-002 should not be
read as having priced them.

---

## The reservation that no spike can retire

**Bus factor 1.** 258 of 264 commits are by one person. `ADR-002:56-57` sets the reversal condition
as "GekkoNet proves unmaintained enough that we are the maintainer" — but **vendoring a pinned commit
of a single-author library makes us the maintainer of that pin immediately**, whatever the upstream
does. The reversal condition as written cannot fire, because the state it describes is the state we
enter on day one.

That is not an argument against adopting. It is an argument for pinning deliberately, reading the
pinned code once as if we wrote it, and budgeting for maintaining it — rather than for expecting
upstream to.

Against that: it is **used in production** by 3sx (a Street Fighter III: 3rd Strike PC port), bsnes
netplay, and RMG-K. A rollback library shipping in a 3rd Strike port is being exercised by exactly
the workload this project is.

---

## The alternative, priced honestly

`enet` is **not in this project's manifest** — `vcpkg.json` lists 18 dependencies and enet is not
among them. It is in the vcpkg registry, so it is a one-line addition, but ADR-002 should not be read
as "already there."

What a hand-written session actually costs, 2-player, from scratch:

| Piece | Estimate |
|---|---|
| Input ring, delay, prediction by repeat | 3-5 days (already specified at `ARCHITECTURE.md:179`) |
| State ring + save/load | ~2 days — D4 already made it a `memcpy` |
| Rollback loop from min-incorrect-frame | ~3 days |
| Checksum exchange + desync detect | ~2 days |
| Time sync / rift adjustment | ~1 week to write, **indefinite to tune** |
| Connection lifecycle: handshake, timeouts, disconnect consensus, resend/ack/batching, compression | **3-5 weeks** — GekkoNet's `backend.cpp` is 39 KB and almost entirely this |

So the fallback is roughly **6-9 weeks**, not the 6-12 ADR-002 quoted, and the bulk of it is the
connection lifecycle rather than the rollback — which is the part a fighting game cannot ship
without and the part that only fails on real networks.

---

## DECISION

**Do not vendor yet.** The read-only spike has done what it can and has changed the shape of the
question rather than answering it.

**The building spike's first job is not a grep over the whole repo. In order:**

1. **Pin a commit SHA.** Everything below is meaningless without it, and the current claims are
   attached to a moving branch.
2. **`grep -rn 'float\|double\|f32\|f64'` over the pinned tree**, and then read every hit in
   `sync.cpp`, `input.cpp`, `event.cpp` and `backend.cpp` by eye. The token, not the summary.
3. **Trace `running_ahead`** from where it is set to where it is consumed. If the number of ticks run
   ahead is a function of a float, the gate fails and the fallback is taken.
4. Build under MSVC and gcc 13, expect the `/MT` vs `/MD` clash, and run the flag gate over the
   generated compile lines.

Steps 1-3 are hours, not days, and they are the whole decision.

**If the gate fails:** write the session over `enet`, and keep the event-pump `ISession` shape
regardless, so the choice stays reversible in both directions.
