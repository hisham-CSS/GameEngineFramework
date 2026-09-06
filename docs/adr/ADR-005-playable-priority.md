# ADR-005 — What to build first for a playable game, and why art is not it

**Status.** Accepted 2026-08-15 · order amended 2026-09-02 by
[ADR-020](ADR-020-the-bounded-lift.md) (P3's skinned placeholders precede P4's link; one
modeled body precedes M4) · **P0–P2 implemented** (P2 @ `41ea6e5`); P3–P5 are tracked as
[ROADMAP.md](../ROADMAP.md) M1–M3. Amended the build order in the then-current
`ARCHITECTURE.md` §6 Phase 3 — since rewritten, so read that section in
[`docs/archive/ARCHITECTURE-2026-08-12.md`](../archive/ARCHITECTURE-2026-08-12.md) — in light of
what Phases 0–2 actually delivered, and answers the question "what does the playable game need, in what order,
including 3D animation and placeholder assets".

**Context.** The user's ask: *"having the actual game so that playtesters can validate
the tool would be great … even if we could have a tool assisted player that could
validate the combos if they are very technically difficult would be the best idea. We
would want to validate and capture the replay so we can playback the combo."*

---

## 0. The two facts that set the order

**There is no skeletal animation in this engine.** No bones, no skinning path, no
`aiAnimation` import, no joint matrices — verified by search, not assumed. Any character
that moves is a new engine subsystem, and *nothing built so far depends on one*.

**`Renderer2D` is finished and idle.** `BeginWorld` (origin at the camera, +Y up),
`DrawQuad`, `DrawSprite`, `DrawSpriteRotated`, `DrawBox` with styles, `DrawText`,
batched, with GL state save/restore. A training-mode fight view can be built on it
**today, with no new engine feature at all.**

Those two facts together are the whole argument of this document: the cheapest thing to
build is also the thing that validates the research, and the expensive thing is not on
the critical path to anything that is currently blocked.

---

## 1. Three goals, three different critical paths

They are routinely conflated, and conflating them is how a project spends three months
on a character rig before discovering its combo counter was measuring nothing.

| | Goal | Critical path runs through |
|---|---|---|
| **G1** | Playtesters validate the combo tool (paper contributions #4 and #9) | the training host, and **blocking** |
| **G2** | It is a fighting game rather than a hitbox demo | **the kernel's missing systems** |
| **G3** | It ships | netcode, content volume, real art |

**G1 needs no art and no animation.** It needs boxes, frame numbers, and a defender who
can block. That is the entire dependency set, and it is nearly all built.

---

## 2. The single highest-value item is resources, and it is not close

`GroundTruthGap` and `test_gap_extent.cpp` measured this on 2026-08-13: `fighter_a` is
certified `TERMINATING` **with a ranking certificate**, and the certificate's content is
*juggle runs down*. The kernel has no juggle. It has no resources at all. All 41 of the
character's cycles are ended by juggle in the model and **33 of them run forever in the
engine.**

Putting resources in the kernel is the only item on this roadmap that scores on four
axes at once:

1. it closes a measured 33-cycle correctness gap in the shipping character;
2. it makes `BuildReport::playsAsAnalysed` **reachable for the first time** — today it is
   false for every character the schema can express;
3. it is what makes juggle-based combo design possible at all, which is most of what a
   modern fighting game's combo system *is*;
4. it converts a caveat in the paper into a closed loop: the analysis and the game agree.

Nothing else on the list scores on more than two. Sequence accordingly.

---

## 3. `GameState` is a wire contract — batch the expansions

Every kernel system below needs new fields in the 80-byte POD. Its size, per-field
offsets and a **golden cross-toolchain checksum** are asserted by
`tests/test_kernel.cpp` and `tests/test_determinism_crossplat.cpp`, and that golden is
the evidence that MSVC and gcc agree byte for byte. Re-recording it is how that evidence
gets destroyed.

So the cost of a kernel feature is not just the feature — it is a re-golden and a
re-proof. **Doing seven of them as seven separate passes pays that cost seven times and
gives seven opportunities to quietly re-record a hash instead of understanding why it
moved.** Do them as one deliberate state expansion, with one re-golden, reviewed once.

This is the same reasoning `Combat.h` already used to keep the cancel system out of
`GameState`, and it is why the cancel window is an absolute `[earliest, latest]` pair
resolved at load rather than a contact tick stored in the state.

---

## 4. The order

### P0 — Game module core *(in flight)*

`IInputSource` seam (local / scripted / replay), `FightSession` fixed-60 Hz host,
deterministic replay capture and playback, `ComboWatcher` live verdict. Headless, no
Engine dependency, so every claim is testable without a GL context.

Replay is nearly free and it is worth saying why: because the simulation is
deterministic and provably identical across toolchains, **a replay is the input log plus
the initial conditions**, and playback is re-simulation. Tiny files, exact reproduction,
and periodic state checksums that catch a divergence *at the tick it happens*. A replay
that diverges mid-playback is not a corrupt file — it means the engine changed since it
was recorded, which makes the format a regression detector nobody had to build.

### P1 — The validation loop *(no new engine subsystems)*

Everything here rests on parts that already exist.

1. **Training host** — `Renderer2D` box view (hurtbox, hitbox, active/startup/recovery
   colouring), local input into the kernel, `UIDocument` HUD.
2. **The readouts that make it a tool** — startup / active / recovery, hitstun remaining,
   frame advantage, combo counter, cancel-window indicator, resource bars.
3. **Frame step, slow-mo, reset.** Free: the host decides when to call `Tick`, and
   `FightSession` owns no clock.
4. **Demonstrate** — the tool-assisted player performs the prover's printed loop
   perfectly, then hands control back.
5. **Replay save / load / share**, so a tester can post one.

**Unlocks G1 entirely.** This is the cheapest tier and the one the research is waiting on.

### P2 — One `GameState` expansion, in value order

1. **Resources — juggle and meter.** §2. Highest value on the list.
2. **Blocking and blockstun.** *Without it there is no defence, so every sequence is a
   "combo" and the combo counter and live verdict are measuring nothing.* It also
   un-collapses a documented projection loss: `CancelEdge::onHit` currently maps
   `Contact::Hit` and `Contact::Block` to the same bit **because the kernel has no
   blocking**, and `MatchBuilder` counts the edges that reading moves.
3. **Hitstop.** `Combat.h` is explicit that this is not a box question — it is a rule
   about which fighters advance on which ticks, and it *changes the meaning of every
   frame number in the character data*. It must land with this batch, never after it.
4. **Pushback.** Range starts to matter — and this is the first step out of the prover's
   corner-only scope (`comboprover.hpp:15-24`), which is the largest standing caveat on
   every verdict the panel prints.
5. **Proration / damage scaling.**
6. **Hitstun decay.** The schema field exists and assertion A01 already guards the trap:
   this project's own draft rule (linear, step 2, floor 10) *fabricated an infinite* on
   Kung Fu Girl.
7. **KO, rounds, match flow.**

Later, and separable: throws, per-frame boxes, push boxes, priority and trade resolution.

**Unlocks G2, and closes the 33-cycle gap from §2.**

### P3 — Legibility: placeholder assets

Only now, and this is the answer to "placeholder assets eventually".

**Greybox characters: jointed primitives, posed per frame.** A torso, head, two arms and
two legs as boxes or capsules, each with a per-frame transform. It needs **no skinning,
no rig, no animation subsystem** — the 3D renderer already draws meshes with transforms —
and it reads dramatically better than flat rectangles: a playtester can see a limb
extend, which is most of what makes frame data legible.

This is also exactly what `ARCHITECTURE.md:441` already asks for — *"frame-indexed
animation with per-frame events"* — so it is on-plan rather than a detour.

Then: hit sparks, hitstop feel, audio cues, a floor and corner markers.

### P4 — Animation: DECIDED

**Frame-indexed poses now; full SF6-style skeletal animation once the combo tool has been
validated.** Settled by the author 2026-08-15, not deferred. §4.1 is why it costs nothing
to start on the cheap side: the two share a contract and differ only in what produces a
pose.

The two options, and they differ by weeks:

| | Frame-indexed poses *(recommended)* | Full skeletal animation |
|---|---|---|
| Needs | per-frame transforms in the schema | Assimp skeleton + clip import, skinning shader, new vertex format, bone-matrix UBO, sampler, blend tree, Mixamo retargeting |
| Frame data | **is** the animation | authored separately, must be kept in sync by hand |
| Determinism risk | none — integers | float sampling; hazardous the moment anything reads it back |
| On the existing plan | yes (`ARCHITECTURE.md:441`) | no |

**Recommendation: frame-indexed now, skeletal after validation** — which is the author's
own call, and §4.1 below shows the two are the same contract with a different sampler, so
the first is not throwaway work.

It keeps frame data the single source of truth, which is the same principle
`MatchBuilder` exists to enforce — and the failure mode of the alternative is D8
reappearing at the presentation layer, where the game *looks* like it has 5 frames of
startup and *behaves* like 7. That is the worst possible bug in a fighting game, because
players learn from what they see.

The "SF6-like" north star is not in conflict with this: SF6 characters are 3D **meshes**;
what matters here is that poses are discrete per tick rather than continuously blended.
3D models with frame-indexed poses is a coherent and much cheaper target.

> **Hard rule, whichever is chosen.** *Animation is presentation-only and must never
> influence a tick.* The moment a float from an animation sampler can change a
> simulation outcome, rollback is broken and the cross-toolchain golden is a lie. This
> is the same one-way flow `MatchBuilder` enforces between data and kernel.

#### 4.1 The two halves of a clip, and why only one of them is frame data

*Added 2026-08-15 from the author's description of how SF6 actually behaves, which is
sharper than the model above and changes the schema.*

A move's animation is **two things stitched together**, and conflating them is how the
picture and the frame data drift apart:

| | The authoritative window | The tail |
|---|---|---|
| Covers | `startup + active + recovery` | everything after |
| Length | **exactly `MoveDuration()`**, no exceptions | whatever looks good |
| Authored in | the character's frame data | the animation asset |
| Simulation state | the fighter is **busy** | the fighter is **idle and actionable** |
| Interruptible | no — the kernel decides | **yes, by anything** |
| Mostly consists of | the attack | returning to stance |

The tail is the return-to-idle: the arm coming back, the stance resettling. It is
usually most of the visible motion and **none of the frame data**.

**The kernel needs nothing for this, and that is the whole point.** "The tail can be
cancelled by any action" is not a cancel rule at all — by the time it is playing, the
simulation already has `moveId == 0` and the fighter can start any move it has a button
for. There is nothing in `StepAttack` to change, no new `CancelEdge`, no new field in
`GameState`. The tail is *only* a picture, and interrupting it is just the renderer being
told to draw something else. A design where the engine had to model tail-cancelling
would be a design that had put the tail in the simulation by mistake.

So the presentation layer owns state in exactly one situation:

```
sim says moveId != 0   ->  pose is a PURE FUNCTION of moveFrame. No state, no blending,
                           no interpolation. The picture cannot drift from the frame
                           data because it is derived from it every tick.

sim says moveId == 0   ->  presentation runs its own tail/idle logic, and abandons it
                           the instant the sim starts a move.
```

That split is what makes the whole thing safe: the only place presentation holds state is
the one place the simulation does not care.

**The invariant that has to be a load assertion, not a convention.** The authored pose
count for a move must equal `startup + active + recovery`. Not "about that" — exactly
that. If a move authors 14 frames of pose against 12 frames of data, the game shows the
player a 14-frame move and punishes them on a 12-frame one, and they will learn the wrong
number and blame themselves. That is D8 relocated to the screen, where it is *worse* than
in the analysis, because a player cannot read the file.

It belongs beside A01–A08 in `Data/src/CharacterData.cpp`, and it should be proven the
way those were: by mutating a real character until it fires.

**The migration to SF6-style is then a change of sampler, not a change of contract.**
When the frame-indexed poses are replaced with real skeletal clips:

- the authoritative window is still **exactly `MoveDuration()` ticks** and is still
  **sampled at integer `moveFrame`**, never at accumulated wall-clock seconds. A clip
  sampled by elapsed time makes a 5-frame startup look like 5 frames only when the
  frame rate cooperates;
- the tail is where blending is allowed, because it is the only part that owns state —
  cross-fading out of a partially-played tail into a new move's frame 0 is exactly the
  smoothing the author describes, and it is confined to the half of the system that
  cannot affect the fight;
- the load assertion above does not change at all. It just measures a clip length
  instead of a pose count.

Which means the short-term frame-indexed version is not throwaway work. It establishes
the contract, the assertion and the sim/presentation split; the eventual upgrade swaps
what produces a pose and leaves all three standing.

**Still open, and each is a tail variant rather than a new mechanism:** whiff versus
connected recovery, landing from an air move, and whether a tail interrupted very early
should blend differently from one nearly finished. All of them are decisions for whoever
builds P3, and none of them reaches the kernel.

> **Rollback note — ACCEPTED, not merely tolerated.** A rollback rewinds the simulation,
> not the picture. Because pose is derived from sim state whenever a move is running, a
> rollback can visibly pop. The author's ruling (2026-08-15): *the pop happens regardless
> of how animation is built, so it is not a cost this decision incurs* — every rollback
> netcode game pops when a prediction is corrected, and hiding it is a separate feature
> (visual smoothing) rather than a reason to shape the animation system differently.
>
> The alternative — presentation state that survives a rewind — is state the simulation
> does not have, and therefore state two peers can disagree about. A brief pop is cheaper
> than a class of desync that only appears online.

### P5 — Ship

Netcode (`ARCHITECTURE.md` Phase 4 — `ISession` and vendored GekkoNet exist and are
proven by construction, but have never carried a real match), a second and third
character, real art.

---

## 5. What this changes about `ARCHITECTURE.md` §6

Phase 3 as written bundles animation, boxes, hit priority, hitstop, pushback, juggle and
proration into one 8–10 week block. That bundle is still right in scope, but the **order
inside it was never stated**, and the measurement in §2 now dictates one: resources and
blocking first, because they are what make the combo tool's verdict meaningful, and
because everything else in the block is more pleasant to tune once a fight has defence.

Phase 3 also lists animation first. On the evidence above it should be **last** in the
block, and its placeholder form should be greybox primitives rather than anything rigged.

Nothing else in §6 moves.
