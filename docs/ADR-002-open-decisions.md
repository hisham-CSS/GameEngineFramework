# ADR-002 — The eleven open decisions, answered

**Status:** Accepted. **Date:** 2026-08-12. **Decides:** `ARCHITECTURE.md` D9 CHOICE A–D, `NORTHSTAR.md` Q1–Q7.
**Supersedes:** the "genuinely torn" framing of D9 and the "only the author can decide" framing of §7.

Three of these were already settled by evidence and are recorded here for completeness. Eight were
open. Each answer says what would **reverse** it, because a decision without a reversal condition is
a preference.

Two answers have an irreducible owner component and are marked **[OWNER]** — I have given the
engineering answer, but a resourcing or business fact could override it and I cannot see that fact.

---

## Already settled

| | Answer | Settled by |
|---|---|---|
| **Q1** — Windows↔Linux crossplay in scope? | **(a) Yes** | Stated requirement: cross-platform multiplayer. Makes fixed-point mandatory rather than advisable, and the cross-toolchain hash test a release gate. |
| **Q3** — May Lua touch gameplay? | **(a) Banned from the simulation** | Stated: Lua was a drop-in placeholder. Costs nothing today — `Scene.cpp` never calls `ScriptWorld`. Lua stays for editor tooling and non-authoritative presentation. |
| **Q5** — Prover schema verbatim, or exporter? | **(a) Verbatim superset** | **Measured** in ADR-001: all three characters load in the unmodified `json_spec.load()`, and stripping every `engine` key changes no verdict. There is no export step. |

---

## D9 CHOICE A — Adopt the rollback session layer, or write it

**ANSWER: Adopt. Vendor GekkoNet. But the two-day spike is now a hard gate, not a formality.**

> **The read-only half of that spike has RUN. See [ADR-003](ADR-003-gekkonet-spike.md).**
> Two of the three gates below pass on real evidence — the API takes bytes not
> types, and its C++20 is quarantined behind a C header so our C++17 is untouched.
> **The float gate is NOT established**: the spike proved no float crosses the
> wire, which is not the question asked, and its negative-existence checks
> probably queried `float` when the house style is `f32`. Do not vendor yet.
>
> **Three factual errors in this section, corrected by ADR-003:** the licence is
> **BSD-2-Clause, not MIT**; it is **C++20, not C** (only the API is C); and
> "small" understates the vendoring surface by 3× — it bundles **asio 1.38.1**
> and **zpp_bits** (a 270 KB header), so adopting means three libraries and three
> licences. None of these reverses the answer; all three change what it costs.
>
> Also corrected: `enet` is **not in this project's manifest** (it is in the
> registry, a one-line addition), and the fallback prices at **6-9 weeks** rather
> than 6-12 — with the bulk being connection lifecycle, not rollback.

The plan's reasoning stands — input delay, prediction, confirm frames, rift adjustment, packet loss
and desync detection are 6–12 weeks of the riskiest code in the project and **none of it is the
contribution**. The contribution is the combo-termination proof. Burning a quarter of the budget on
re-deriving GGPO is how the paper does not get written.

**But the plan assumed this was a dependency away, and it is not.** Verified against the vcpkg
checkout: there is **no `gekkonet` and no `ggpo` port**. What exists is *transport* — `enet`,
`gamenetworkingsockets`, `libdatachannel`, `slikenet`, `kcp`, `libjuice`, `asio`. None of those is a
rollback session layer; they move bytes. So "adopt" means **vendoring** GekkoNet (MIT, small, C),
which is a cost the plan did not price.

It is still the right trade — a single-purpose C library is not Jolt, and the rejection of vendoring
in §2 was about a 100k-line physics engine that mutates `CMAKE_CXX_FLAGS` at directory scope. But the
spike must now answer three things before committing:

1. Does it build clean under **both** MSVC and gcc with our toolchain, and does it survive the
   determinism flag gate?
2. Does it impose **any** float on state we own? If it computes frame advantage in float and feeds
   that back into simulation timing, it is disqualified for a crossplay title (Q1 = a).
3. Does its API want to own `GameState`'s *type*, or only its *bytes*? Only-bytes is adoptable.

**Fallback if the spike fails:** write the session layer over **`enet`** — mature, in vcpkg, built
for game UDP. Write the *session*, never the *transport*.

**Non-negotiable either way:** a thin `ISession` seam, and `GameState` never appears in a
session-layer type. Violate that and game #2 forks the netcode.

**Reversed if:** the spike finds float in the state path, or GekkoNet proves unmaintained enough that
we are the maintainer.

---

## D9 CHOICE B — Trigger DSL in Phase 3, or data-only first

**ANSWER: Data-only first. Phase 0 turned this from a judgement call into a measured one.**

The measurement is decisive and it is not the one anyone expected. Moves needing an expression
language: **1 of 59 (1.7%)**. Moves needing a schema field that did not exist: **23 of 59 (39%)** —
and every one of them a **missing noun, not a missing verb**. Facts about a move with nowhere to
write them down. None required computation.

So a trigger DSL built in Phase 3 would have solved **1.7%** of the problem while the actual 39% sat
there as absent struct members. That is the strongest possible confirmation of the ordering: **nine
schema fields first, parser later.**

**One refinement Phase 0 forces.** The opponent predicates it found (26 of 247 cancels —
`p2bodydist x <= 30 && p2movetype != H`) look like a trigger language, and they are not, yet. They
have a **fixed shape**: an opponent property, a comparison, a constant. Encode them as *data* — an
enum selector plus a threshold — and the parser stays deferred. Reach for a grammar when authors need
to combine them in ways an enum cannot express, which is a Phase 5 question.

**Reversed if:** authors need arbitrary boolean composition over opponent state before Phase 5. Then
the grammar arrives early, and it arrives with the opponent namespace built in.

---

## D9 CHOICE C — Desync response: abort, or resync

**ANSWER: Abort the match, name the frame and the field, and write a repro artifact.**

Keep the detection half — a 4-byte checksum every 8 ticks. Drop the correction half entirely.
Authoritative resync would demote determinism from a **correctness requirement** to an
**optimization**, and everything else in this architecture is load-bearing on it being the former:
the integer kernel, the fixed-point rule, the fast-math CI gate, the whole crossplay claim.

For 2-player P2P there is also no authority to resync *from*. A resync hitch costs a round, and in a
fighting game a silently corrected position is worse than a stop — the player cannot tell a rollback
artifact from a lost interaction, so trust in the netcode dies quietly.

**One addition beyond the plan.** A desync must produce a **debuggable artifact**: both `GameState`
blobs and the input log from the last confirmed tick. This is nearly free precisely because D2 made
the state a POD — dumping it is `fwrite`. A desync you cannot reproduce is a desync you cannot fix,
and they surface on other people's machines.

**Reversed if:** the framework ever builds a server-authoritative title. Then resync is correct
there, and stays wrong here.

---

## D9 CHOICE D — Keep Jolt in the fighting-game build

**ANSWER: Yes, `CSE_WITH_JOLT` stays on, cosmetics only — and the boundary becomes a link error.**

Ragdolls on knockdown, stage debris and props are worth having and cannot desync anything that is not
in the authoritative state. Jolt and EnTT were chosen *for* this project's rollback needs and both
keep earning their place on the presentation side.

**But the plan names the real cost as "the standing temptation" to let gameplay leak into it, and
today proved how to handle that class of risk.** The fast-math gate works because a violation is a
*build failure*, not a code-review miss. Apply the same pattern: make the **gameplay kernel its own
target** that links neither Jolt, nor EnTT, nor Lua, nor anything pulling libm. Then "just query the
physics world for the hitbox" is not a bad idea someone talks you out of — it is an unresolved
symbol.

That is worth more than any paragraph in this document, and it is one CMake target.

**Reversed if:** never, as stated. If cosmetic physics ever costs frame budget, tier it down; do not
promote it.

---

## Q2 — 3D characters on a 2D plane, or 2D sprites **[OWNER]**

**ANSWER: (a) 3D on a 2D plane. The engine's existing strength is 3D and the art economics favour it.**

Verified cost: `Model.h:37-43` has five vertex attributes and no bone IDs or weights, and the skinning
lines sit **commented out** at `Mesh.h:30` and `:139` — started and abandoned. So the shape is known
and roughly 4 weeks of Phase 3 buys the vertex format, bone palette, shader branch and Assimp
`aiAnimation` import.

Against that: the 11-pass forward renderer, IBL, cascaded shadows, post-process stack and quality
tiers already exist and are built for 3D. Choosing 2D sprites discards the engine's single most
developed subsystem and replaces it with a `SpriteComponent`, an atlas format and a world-space 2D
pass that do not exist either.

**The decisive argument is art, and it points the same way.** A 2D fighting game is *more* art, not
less — thousands of hand-authored frames per character. 3D is a rig and a set of animation clips.
For a small team this is not close.

**The deferral is real and should be used:** the frame-indexed clip player is the same code either
way. Build it first; the skinning decision can land weeks later without blocking anything.

**[OWNER] — reversed if:** you can source 2D character art and not 3D. That is a resourcing fact I
cannot see. Note it does not rescue a *thin* art budget: 2D costs more, not less.

---

## Q4 — Fighting sim in the EnTT registry, or its own flat `GameState`

**ANSWER: (b) Flat `GameState` beside the registry. Decisively.**

It is what every shipped rollback title does, the measured numbers demand it (sub-5 µs `memcpy`
versus ~907 µs for an `entt::snapshot` archive loop), and it makes the trivially-copyable
`static_assert` enforceable rather than aspirational. ADR-001 reinforces it: the character data is
integer and POD-shaped, and two fighters is ~2.8 KB — precisely the case `memcpy` is for.

An ECS mirror is written for rendering just before `Scene::UpdateTransforms()`; the registry never
holds authoritative state.

**Do the other half too.** Land a component registry — `{name, toJson, fromJson, snapshot, restore,
drawInspector}` — for the **non-simulation** components. It retires the eight-hand-edits-per-component
tax and the closed-list `EntitySnapshot` hazard that has bitten this repository before, where silent
data loss is the failure mode. Land it with a test that walks the registry and asserts every
registered type survives a serializer round-trip *and* a capture/restore cycle.

**Reversed if:** never for the simulation. `entt::snapshot` stays for late-join and spectate in
Phase 7, which is what it is good at.

---

## Q6 — Must Editor Play be bit-identical to the shipped Player

**ANSWER: (a) Yes, and it becomes a CI test rather than a promise.**

Extract the game-side ownership out of `EditorApplication` into a shared `PlayModeHost` both hosts
construct. Around 90 lines are written twice today (`PlayerMain.cpp:208-319` versus
`EditorApplication.cpp:543-627`) and **the two copies already differ** in input gating. The
`Install*` family's own comment states the goal: so "it works in Play" and "it works in the shipped
game" can never drift apart.

**The argument is stronger than it was when the plan was written.** Once determinism is a correctness
property, "works in Play, desyncs when shipped" becomes an *unfalsifiable* class of bug — every
investigation starts by asking which host you were in. And unlike most parity goals this one is
mechanically checkable: run the same input log through both hosts and compare the state hash. That is
a CI test in the same family as the determinism flag gate, and it is the pattern that has worked.

Days of work, not weeks; the seam exists.

**Reversed if:** never. If it is too expensive to keep, the Editor stops hosting gameplay.

---

## Q7 — Publish the inventory now, or after Phase 0 **[OWNER]**

**ANSWER: Publish now — and Phase 0 being done means you can say more than the question assumed.**

The §1 inventory numbers are verified and defensible. The engine is genuinely substantial and
undersold. Since the question was written, three further claims became true **and independently
checkable**: CI green across four jobs, a determinism gate that fails the build on any fast-math
flag, and a Linux build proven by a compiler rather than asserted by a README.

**Do not publish anything about rollback, netcode, or the research integration yet.** No simulation
kernel exists. A claim that outruns the code is the first thing a reviewer checks, and this project's
strongest asset right now is that its documentation is unusually honest — ADR-001 leads with a result
that is half negative.

Phase 0 itself is publishable *as a mixed result*, and the mixed-ness is the interesting part: the
declarative fragment fits real characters at 1.7%, and the schema drafted for it was 39% short. That
is a more credible research note than a clean success.

**[OWNER] — reversed if:** you have a reason to time an announcement around something I cannot see.

---

## What this changes in the plan

| Document | Change |
|---|---|
| `ARCHITECTURE.md` D9 | No longer open. A–D answered above; D adds a **structural** enforcement (kernel target links no Jolt/EnTT/Lua). |
| `ARCHITECTURE.md` Phase 4 | GekkoNet is **not in vcpkg** — the phase must budget vendoring, and the spike is a gate. |
| `NORTHSTAR.md` §7 | All seven answered. Q1/Q3/Q5 were already settled; Q5 by measurement. |
| Phase 3 | Unblocked only after ADR-001's nine schema fields exist. Build the frame-indexed clip player first — it is identical under either Q2 answer. |

**The first three work items, in order:** the nine schema fields (ADR-001's blocking item), the
gameplay-kernel CMake target that makes D2 a link error, and the two-day GekkoNet spike.
