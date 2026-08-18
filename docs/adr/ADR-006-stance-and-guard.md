# ADR-006 — Stance, guard height, priority and invincibility: six concepts wearing one and a half fields

**Status.** Accepted 2026-08-15 (§§1–10) · **Implemented**, **extended 2026-08-16** with `move.priority` and
`move.invincibility[]` after the author asked for ArcSys-style priority over aerials. The extension
lands in §1.6, §2, §3.5–3.7, §4, §6, §9 and §10 items 10–14, and reverses nothing already decided.
Grows `schema.v2.json` to v3 by **five new fields and one widened enum**, none of the five `[P]`.
Amends `ADR-001` §4's loss inventory, which never listed the transcription loss in §1.3, and amends
**`ADR-005:140`**, which lists priority under *"later, and separable"* — §6 says why it is batched
instead. **One blocking item** (§5), untouched by the extension.

**The question**, from the author, and it found a real hole:

> *"explain stance to me? it has any, ground, air — shouldn't it also have crouching as crouching
> normals are quite important to a 2D fighting game … also blocking should consider high and low
> blocking as well as cross ups? it seems that the character might be a little incomplete?"*

**The answer: yes, and the hole is bigger than the question.** Crouching was not omitted as a design
simplification. It was **destroyed in transcription, in one expression, and nothing in this project
recorded it.** Guard height was never omitted either, because it was never present to omit: the
schema has no concept of it at any nesting level.

**Then a second question, arriving after fields 1–3 had landed**, which found the same failure mode
a fourth time:

> *"to ensure anti airs properly work we should also probably consider the arc sys works style of
> priority over aerial attacks. This is a great idea but not necessary if it will cause a big change
> - however having a priority system that can be toggled depending on attack state might be a good
> idea. ie: moves specifically toggled as anti airs - will have priority over aerials. certain
> invincible reversals would have priority over meaty attacks (attacks on knockdown as the player
> gets up) - etc."*

**The answer: two mechanisms and not one, and the anti-air falls out of the second.**
`special_uppercut` has been labelled *"Uppercut (reversal, invincible 1-6)"* since the file was
written, and no program can read those six frames (§1.6). Who wins when both attacks land on one
tick is a plain integer (§3.5). Everything the request calls *"priority toggled depending on attack
state"* is built as **typed invincibility** instead (§3.6) — a reading of the request rather than a
transcription of it, and recorded as a reading because the next person is owed the fact that it was
a choice.

*`CharacterData.h` is cited by symbol and `schema.v2.json` by JSON path, never by line, because both
are being rewritten alongside this document and a line number into either would be stale before it
was read. Everything else is cited at the line it was read at, and re-checked against the file after
the document was written — the discipline `ADR-001` §7 exists to enforce.*

*A second discipline, forced by the extension and worth stating because it constrains every future
edit. Four files cite this document **by section number**, 34 times: `fighter_a.json` (17),
`fighter_a_infinite.json` (7), `schema.v2.json` (9) and `CharacterData.h` (1), naming §1.3, §1.5,
§2, §3.1, §5, §8, §9 and five separate items of §10. So **§§1–10 and §10's items 1–9 are frozen**:
the extension appends (§1.6, §3.5–3.7, §10 items 10–14) and renumbers nothing. Two headings were
reworded, which no citation depends on. Nothing cites this document by **filename** — checked — so
the filename stays too, and it is now two words short of what the document covers.*

---

## 1. What was found

### 1.1 The three-value enum, and the shortest description in the schema

`enum class Stance : std::uint8_t { Any, Ground, Air };` — `CharacterData.h`, and
`schema.v2.json` `$defs.move.properties.stance`:

```json
"stance": { "enum": ["any", "ground", "air"], "default": "any", "description": "[P] json_spec.py:72." }
```

Twenty-eight characters of description, in a file whose *other* field descriptions run to four
hundred words with measured counterexamples. `schema.v1.json:154` is byte-identical but for the line
number. v2 added nine fields and did not look at this one.

`fighter_a` is **16 `ground` + 2 `air`** across 18 moves. Six of those sixteen are crouching normals
— `crouch_lp`, `crouch_lk`, `crouch_mp`, `crouch_mk`, `crouch_hp`, `crouch_hk` — and they declare a
stance **identical** to their standing counterparts. `crouch_mk` and `stand_mk` are the same value.
The crouching-ness lives in the word "crouch" inside the id, which is a string, which is not data.

### 1.2 The prover has no stance at all — and this is stronger than "the C++ header lacks it"

`ThirdParty/comboprover/comboprover.hpp` is 737 lines and contains **zero** occurrences of *stance*,
*statetype* or *crouch*. `ARCHITECTURE.md:584` already records stance among the fields the C++ model
drops.

The Python reference is worse than absent, and this was measured rather than assumed:

| where | what it does |
|---|---|
| `importers/json_spec.py:72` | `stance=str(raw.get("stance", ANY))` — reads it |
| `model.py:120-121` | **`raise ValueError(f"move {self.id!r}: unknown stance {self.stance!r}")`** for anything outside `(ground, air, any)` |
| `model.py:489` | carries it into the position-modelled rebuild |
| `analysis.py`, `termination.py`, `ranking.py`, `bounds.py`, `lp.py`, `gap.py`, `report.py` | **no occurrence** |

So `stance` is **validated, carried, and consulted by nothing.** Its only effect on the reference
implementation today is the power to reject a file. That is §5.

And nothing enforces it engine-side either: `MatchBuilder.cpp:214-218` reports it as a
`KernelPermits` loss over every move — *"an air-only move is startable standing and a ground-only
move is startable in the air"* — 18 of 18 on `fighter_a` (`docs/manual/fighting-core.md:613`), 25 of
25 on Kung Fu Girl (`:429`). `tests/test_ground_truth.cpp:1501-1509` asserts the loss rather than
skipping past it. **Stance is authored by every file, read by no analysis, and enforced by no
runtime.**

### 1.3 Crouching was lost in one expression, and it is the line below a careful comment

MUGEN's `statetype` is **S / C / A / L**. `importers/mugen_cns.py:573-576`:

```python
stance_letter = (state.header.get("type", "S") or "S").strip().upper()
stance = AIR if stance_letter.startswith("A") else (
    GROUND if stance_letter.startswith(("S", "C")) else ANY
)
```

`("S", "C")` — standing and crouching, mapped to one value, in a tuple, with no comment. `L` falls
through to `ANY`, which is worse: a lying-down state becomes *no restriction at all*, the permissive
direction.

The six lines **immediately above** it reason carefully about why `min(ground_stun, air_stun)` is
the right projection for hitstun, *"since a combo has to work in whichever state the defender is
actually in."* One lossy projection got a paragraph. The one on the next line got a tuple.

The corpus cost: **Kung Fu Girl has 6 crouching moves and Kung Fu Man 4; all ten came through as
`ground`.** `ADR-001` §4 groups A–I do not mention it, and §6.2's "not transcribed" list does not
either. It is the one Phase-0 loss that was never written down — which is exactly why it is here now.

Meanwhile `p2statetype` is named **three times** as something Phase 5 must be able to read on the
*opponent* (`ADR-001:275`, `:587`, `ARCHITECTURE.md:467`). The project knew state type mattered while
having thrown its own away.

### 1.4 The schema has zero concept of block height

`grep -iE 'overhead|blockable|guard_height|attack_height|low' schema.v2.json` → **0 hits.** Not a
missing value in an enum; no enum, no field, no `engine` sub-object, nowhere.

The transcription noticed anyway, and had nowhere to put it. AOF2's sweep,
`tests/fixtures/characters/aof2_strength_training.json:323`:

> `"stance": "derived: [Statedef 1022] type = S (States.st:381), so ground, even though attr = C, NA (States.st:392) and ground.type = Low (States.st:404)"`

MUGEN said **crouching** and it said **low**, in two separate fields, and both facts ended up inside
a prose derivation string because the schema had one slot and it was already spoken for. That is a
transcriber doing the right thing against a schema that could not receive it.

`fighter_a` does the same thing in its own labels: `crouch_lk` and `crouch_mk` are *"(low)"*,
`crouch_hk` is *"(sweep, knockdown)"*. Three of the character's own moves declare their guard height
in English, in a display string, where nothing can read it.

### 1.5 The naming trap: `move.guard` is already taken

`move.guard` exists and is a **resource minimum** — *"Minimum each resource must be for the move to
be allowed. Componentwise `>=` only"* (`schema.v2.json` `$defs.move.properties.guard`),
`ResourceVec guard` at `comboprover.hpp:129`, enforced by `meetsGuard` at `:256-262`, reported as
its own `KernelPermits` loss at `MatchBuilder.cpp:220-223`. It means *"this super costs a bar"* and
has nothing whatever to do with blocking.

Overloading it would have put two unrelated ideas on one word on one struct, with the resource one
there first. **The block-height field is `blocked_as` / `Move::blockedAs`.** This is the cheapest
decision in the document and the one most likely to be undone by somebody who did not read it.

### 1.6 Invincibility in a display string — and the honest version of that claim

`fighter_a`'s `special_uppercut` carries this label:

    "Uppercut (reversal, invincible 1-6)"

**The dramatic reading is that the frame numbers of the strongest property a move can have live in
English. The dramatic reading is wrong, and the true one is worse.** Measured against the file:
`move.engine.invuln[]` has existed since **v2** — field 6 of the nine — and that move authors it
correctly. `from_tick 1`, `ticks 6`, `kind: not_hit_by`, attributes `[NA, NP, SA, SP, HA, HP]`, with
a `source` string recording that throws are *deliberately* excluded. The numbers are not only in the
label. What is true is threefold:

1. **Nothing loads it.** `CharacterData.h` names `engine.invuln` among the fields deliberately not
   read, so the only copy of `1-6` any program can reach is the English one.
2. **Half of what the field has to say was never sayable in it.** `engine.invuln` speaks MUGEN —
   `NotHitBy`/`HitBy`, state types S/C/A, attribute classes NA/SP/HT — and **MUGEN's attribute
   vocabulary has no guard-height axis at all**; MUGEN carries block height in a different `HitDef`
   parameter entirely, transcribed here to `engine.reaction.attack_type`. *"Invincible to lows"*
   cannot be written in it however it is stretched, which is §1.4 arriving from the other direction.
3. **Two copies of one fact, one of them prose.** They agree today only because nobody has edited
   either. That is D8's failure mode — one quantity carried two ways, with no rule making them
   agree — moved out of arithmetic and into authoring.

So the defect here is not *"there was no field"*. It is *"there was a field in the wrong vocabulary
that nothing reads, and a human-readable copy beside it that everyone reads and nothing can parse."*
That is a commoner shape than the plain absences in §1.1 and §1.4, it is harder to see because the
file looks complete, and §9 says where the next one is.

---

## 2. The six concepts, and the two pairs that must stay orthogonal

One and a half fields were carrying four ideas. `stance` carried one and a half of them; the other
two and a half had no field at all. The second pass found two more, and the first of *those* had
something worse than nothing — §1.6.

| # | concept | asks | had |
|---|---|---|---|
| 1 | **attacker state** | where is the *attacker* when the move starts | `stance`, collapsed 4→3 |
| 2 | **guard height** | which block stops it | nothing |
| 3 | **hurtbox by state** | what shape is the attacker while it runs | nothing (`MatchBuilder.cpp:314-318`: no body at all) |
| 4 | **side / cross-up** | which way is *back* at the moment of contact | nothing, and correctly so — §3.4 |
| 5 | **same-tick ordering** | who wins when both attacks would land on one tick | nothing — `ResolveHits` trades unconditionally, `Combat.cpp:277-279` |
| 6 | **not being hit at all** | what cannot touch this move, and on which frames | `engine.invuln`, in MUGEN's letters, unread — §1.6 |

**1 and 2 must stay orthogonal, and the pressure to collapse them is real**, because a crouching
attack is *usually* a low and an aerial is *usually* an overhead. "Usually" is the word to refuse:

- a **crouching heavy punch** is commonly a **mid** anti-air — crouching, not low. `fighter_a`
  already ships one, labelled *"(anti-air launcher)"*, and it is the counterexample that settles it;
- a **standing overhead** is a standing move blocked **high**, in a cast where every other standing
  normal is mid;
- a **sweep** and a **crouching jab** are both crouching and only one is low.

Every cell of the (stance, blocked_as) grid is a move somebody has shipped. So the loader performs
**no cross-check between them** — not an error and deliberately not a warning. A diagnostic that
fires on correct data is how a diagnostic gets deleted, which this project has already learned once:
`schema.v2.json` `x-load-assertions.A04.scope_correction` records assertion A04 being authored
from reasoning, never run against the corpus, and rejecting all three characters.

**5 and 6 must stay orthogonal too, and here the pressure to collapse them is the request itself.**
The author asked for *"a priority system that can be toggled depending on attack state"* — one
field, with conditions on it. Two fields were built, because they answer different questions:

- **priority decides who wins a hit that both fighters landed.** It is an ordering on one instant.
- **invincibility decides that there was no hit to arbitrate.** It removes the incoming attack
  before priority is consulted at all.

**A reversal beats a meaty because it is invincible, not because it out-prioritises**, and those are
different games. An out-prioritising reversal still gets hit when it is a frame late; an invincible
one does not. Author one as the other and the frame data stops describing the fight — which is
exactly the class of error `ADR-005` §4 calls the worst possible bug in a fighting game.

The test that settled 1 and 2 settles this pair too: neither is expressible as the other, in either
direction. A super that beats a jab is **not invulnerable to it** — `super_beam` can be hit on any
other frame by that same jab, and making it invincible for 54 ticks would be a wildly stronger move
and a different design. An anti-air's startup is untouchable by an aerial and holds **no advantage
whatever** over a grounded jab. Two mechanisms, two shipped cases, neither one a special case of the
other. **Collapsing them would be the `stance`/`blocked_as` mistake one revision later and
knowingly**, which is the only version of it there would be no excuse for.

---

## 3. The model

### 3.1 Stance keeps `ground`, and that is not a fudge

`any | ground | standing | crouching | air`. **`ground` continues to mean GROUNDED, STANCE
UNSPECIFIED** — the move happens on the floor and the file declines to say which. That is an honest
description of what the MUGEN corpus *is* after §1.3: the importer genuinely cannot distinguish S
from C, and re-transcribing ten moves in three third-party fixtures is a Phase 3 data job, not a
migration. It preserves the property `ADR-001` §8 item 1 and `schema.v2.json` x-migration both record
as worth keeping — *a v1 file is already a valid v2 file*, now extended to v3.

**A new character uses `standing` or `crouching` and never `ground`.** The roster check (§8) is what
says so out loud instead of leaving it to a convention nobody reads.

Enum values are **appended**, not inserted beside `Ground` where they read better. Nothing observes
the integers today, but the day stance crosses into `cse::kernel::MoveDef` — which is what enforcing
a low requires — they become wire-visible under the connect handshake's hash (§6), and inserting
silently re-labels every move in every replay and on every peer.

### 3.2 High block stops high and mid; low block stops low and mid

```
a HIGH block (standing)  stops { high, mid }
a LOW  block (crouching) stops { low,  mid }
```

So a `high` attack goes through a low block — an **overhead** — and a `low` attack goes through a
high block. `mid` is stopped by either, is the default, and is therefore what every move in the
Phase-0 corpus becomes: the field did not exist when those files were written, so an absent value
must mean the case that changes nothing.

This is `ADR-002` CHOICE B's discipline applied one field over: **a closed three-value enum, not a
grammar.** There is nothing to parse; enforcement is one comparison against one bit of defender
state.

> **A consequence worth flagging now rather than discovering in playtest.** `fighter_a` has three
> lows and, on its own labels, **no overhead at all.** Under this model, blocking low would stop all
> 18 of its moves and blocking high would stop 15. Blocking low is strictly dominant, so the
> character has no high/low mixup — which is a **data** gap, not a model gap, and it is the first
> thing §8's sixth standing normal or a command normal should fix.

### 3.3 Low-profiling and hop-overs are emergent from geometry. Both.

These are the two sentences from the author that decide the shape, and neither survives as a boolean.

**Low-profiling comes from the crouching attack's own hurtbox.** A crouching attack ducks a high
attack because **its body is shorter for those frames** — not because somebody wrote
`low_profiles: true`. *Not every crouching attack low-profiles*, and a file that states a **shape**
never has to claim that it does. So a move may carry an optional **hurtbox override**
(`engine.hurtbox_sub` → `Move::hurtboxOverride`), in `cse::kernel::Box`'s own units, orientation and
field order.

A boolean answers exactly one question — the one its author happened to think of. A box answers all
of them with no second rule: which specific attacks clear it, whether an opponent's sweep still
catches it, whether a hop-over passes over it, how much of the character is left exposed.

**`Combat.h:297-299` already predicted this field and its shape**, before anyone asked the question:

> *"One box for the whole character today; per-frame hurtboxes (a crouch is a different shape) are
> Phase 3, and they land as a per-move override here, not as a field of `GameState`."*

The kernel named the mechanism, gave the crouch as its motivating example, and put it in the right
place. Only the data field was missing.

**Hop-overs come from a grounded move going airborne.** A grounded attack that leaves the floor
partway through its animation passes over a low for as long as it is off the ground — Ryu's f+HK is
the standard example. So a move may state **the tick it becomes airborne**
(`engine.airborne_from_tick` → `Move::airborneFromTick`), in `Move::startup`'s tick base so a move can
leave the ground *before* its hitbox appears, which is what a hop kick actually does.

That plus the geometry answers it **for every pair of moves, without either move naming the other.**
A `hops_over_lows: true` flag would be wrong the first time two sweeps had different heights.

Two facts make this less novel than it looks. First, the corpus already contains one: `ADR-001:234`
(§4 group D) records Kung Fu Girl's state 1100 leaving the ground mid-move via `StateTypeSet`
(`Specials.cns:693`). Second, **v2 already models the end of that airborne phase and not its start** —
`transition.kind: "on_land"` is field 5 of the nine, and the nearest thing to a takeoff tick is
`motion_physics.applies_from_tick`, which `schema.v2.json` `$defs.moveEngine.motion_physics`
declares with **no description at all**. The two halves of one airborne phase, and only the second
was written down.

**Stance is the entry condition; `airborne_from_tick` is a mid-move state change. They do not
interact.** A `standing` move that goes airborne on frame 9 is still a standing move, because stance
says what you must be in to *start* it.

### 3.4 Cross-ups get no field, and that is the finding, not an omission

A cross-up is not a property of a move. It is a function of the two fighters' **positions at the
moment of contact**: the attacker is on the far side, so "back" is the other direction and the
defender must hold the opposite way. Nothing about the move changes. **No schema field is correct
here, and adding one would be modelling the wrong noun.**

What it needs instead is *position-aware blocking* — and the state for that already exists:
`GameState.h:49` `posX` for both fighters and `:67-69` `facing`. No new field, which matters, because
`GameState` is a wire contract (`ADR-005` §3).

**The trap, and it is named in the file it will be sprung in.** `GameState.h:67-69` says `facing` is
*"0 = facing +X, 1 = facing -X. Not a sign multiplier: a sign would invite `pos * facing` and the
mirror-asymmetry bug D2 warns about."* A cross-up rule is precisely the place somebody reaches for
`pos * facing`. Compute "away from the opponent" from the sign of the position difference and never
from a stored facing multiplied into a coordinate.

The prover cannot see cross-ups twice over: it has no defender state, and the C++ header is
corner-only by construction (`comboprover.hpp:15-24`), where there is no side to be on.

### 3.5 Priority is a plain integer, and its default is the game that is already running

`move.priority` / `Move::priority`, `int16`, **default 0. Higher wins outright and the loser takes
nothing; EQUAL IS A TRADE and both land.**

The default is the entire reason this can land as data today. Every character ever authored leaves
the field absent, so every move compares 0 against 0, so every meeting is a tie, so every meeting is
a trade — **which is precisely what `ResolveHits` does now**, and says so at `Combat.cpp:277-279`:
*"the rule here is the symmetric one, and both fighters land."* A file that does not author this
field describes the shipped game exactly.

**Scope, stated narrowly because the word invites more.** It is not a measure of how good a move is.
It does not gate *starting* a move, it interrupts nothing, and it is consulted only in the one
instant two hitboxes both connect. A move that never meets another move on that tick never reads it.

**Signed, and sixteen bits, and neither is a free choice.** *"This move loses to everything"* is a
real thing to author — a committal heavy, a taunt, a move whose whole point is that it trades badly
— and it is naturally `-1`; with an unsigned type the only way to say *below the default* is to
renumber the entire cast upward, a data migration bought for one bit. The width is decided by the
destination: `cse::kernel::MoveDef` already carries an explicit 16-bit `pad_` (`Combat.h:227`),
placed there because hashing a struct with indeterminate padding compares two machines'
uninitialised bytes, and `sizeof(MoveDef) == 40` is a `static_assert` (`Combat.h:344-347`). **A
16-bit signed priority is the slot that already exists.** Assertion A17 therefore *refuses* an
out-of-range value rather than narrowing it, because narrowing is not a clamp — 32768 truncated to
16 bits is −32768, and the strongest move in the file silently becomes the weakest, in one build.

**The name is already in this schema twice and neither of them is this field.**
`engine.projectile.priority` is MUGEN's `ProjPriority`; `engine.reaction.priority` is MUGEN's
`HitDef` parameter, and the AOF2 thug authors **2** on its normals
(`tests/fixtures/characters/aof2_strength_training.json`). Both are `[E]` transcriptions of what a
source file said; this is the `[G]` designed rule. That is the identical split `blocked_as` already
has with `engine.reaction.attack_type`, established one field ago, so the name was **kept** rather
than invented around: the collision is two nesting levels down, not on one object the way `guard`
was in §1.5. **The hazard is not the name, it is the scale.** MUGEN's parameter has its default in
the *middle* of a small band, so a transcribed `2` means *below normal*, while `2` here means *above
everything*. The two numbers are spelled identically and produce opposite outcomes, and **no
assertion can separate them**, because both are legal integers. That is why it is written down
instead of checked, and why a transcriber must re-derive MUGEN's ordering against this field's
default rather than copying it across.

### 3.6 Typed invincibility, and why the condition table the request named was not built

`move.invincibility[]` — a list of `{from_tick, ticks, attack_kinds[]}`, each becoming a
`CharacterData.h` `InvincibilityWindow`. **Absent means the move has none. An absent or empty
`attack_kinds` means invincible to EVERYTHING**, because that list only ever *narrows* a window and
the identity element of a narrowing is everything.

**An anti-air is not a category of move.** It is a move that cannot be hit by an airborne attack
during its startup: one window, no condition, no toggle, no second vocabulary. A reversal is the
same field with a wider kind set. `fighter_a`'s `crouch_hp` has been labelled *"(anti-air launcher)"*
since the file was written and needs exactly `[{from_tick: 1, ticks: 8, attack_kinds: ["aerial"]}]`
and **no `priority` at all** — where the 8 is the move's own `startup`, read off the move rather than
chosen, so the invincibility ends on the tick the hitbox appears. That is the demonstration: the
behaviour the request asked for, with the field it asked for left at its default.

**Four reasons the general mechanism was built instead of the condition table**, recorded because
the condition table is the road that was asked for:

1. **A condition table is a grammar.** Every condition kind added to it is one more thing the kernel
   must evaluate at the exact instant two hitboxes overlap. `ADR-002` CHOICE B's discipline — a
   closed enum rather than an expression language wherever the closed form will do — applies here,
   and here the closed form does.
2. **The two mechanisms compose and the table does not.** Invincibility resolves *before* priority
   is consulted, so *"anti-airs beat aerials"* and *"this move beats that move"* are answered by
   different fields, neither knows about the other, and anything added later meets one mechanism at
   a time. A conditional priority grows a case per property forever.
3. **It is §2's lesson one revision on.** This document exists because one and a half fields were
   carrying four ideas.
4. **The general mechanism is useful without the special case.** Throw invincibility through
   recovery, a fully invincible super flash, a low-profiling move's immunity to highs — none of them
   is an anti-air, and all of them are this one field.

**What the reading gives up, because it is not nothing.** A priority that *varies with the kind of
the other attack* — "beats aerials, loses to grounded mediums" — cannot be written. Note how narrow
the gap actually is: *"wins the exchange but is still hittable"* is a priority with no window, and
*"cannot be touched by aerials"* is a window with no priority. Only the case where **the ordering
itself depends on the opponent's kind** is missing, and no character has asked for it. §10's second
*Reversed if* says what happens when one does.

**Windows may overlap, the meaning is the union, and nothing diagnoses it** — because the request's
own example *is* an overlap. *"Invincible 1-6, then throw-invincible through recovery"* is a window
of 1–6 naming everything and a window of 1–40 naming `throw`, overlapping on six ticks and meaning
exactly what it appears to mean. A diagnostic that fires on the motivating case is how a diagnostic
gets deleted; §2 already cites A04 for that mistake. There is no ambiguity to arbitrate because
union is commutative and idempotent, and **there is no whitelist form** — `engine.invuln` carries
`kind: not_hit_by | hit_by` because MUGEN has both controllers, and two overlapping whitelists have
no order-independent reading. `$defs.invincibilityWindow` has no such key and must never grow one.
The loader also does not merge the windows: the loaded list is the authored list, because a designer
reads the file, and normalising two authored windows into three would make the data a program sees
stop matching the document a human edits.

> **The scope check that nearly went wrong, recorded because getting it right was not obvious.**
> A07 requires `hits[].tick` strictly increasing and A08 requires the same of `motion[].tick`, so
> requiring it of these looks like consistency. **It does not transfer.** A hit record and a motion
> keyframe are *applied in sequence* and the result depends on which came last — that is the entire
> content of those two assertions. A window contributes to a *set*. Copying A07 here would have been
> a rule inherited from a neighbour's shape rather than derived from this field's meaning, which is
> precisely what A04's `scope_correction` records going wrong once already.

### 3.7 The token list spans two axes, which is convenient and is a trap

`attack_kinds` is closed, and two of its six words come from **different fields of the incoming
move**:

```
high, mid, low      the incoming ATTACK's   blocked_as   (§3.2)
aerial              the incoming ATTACKER's stance       (§3.1)
throw, projectile   RESERVED — legal to author, inert today
```

They sit in one list because *"what can I not be hit by"* is the sentence a designer writes, and it
does not respect the schema's field boundaries. **It is written down rather than left to be inferred
from the names**, because it reads as obvious today and is exactly the sort of thing that confuses a
reader in six months — §1.5's reason, one field over.

**The match is an intersection and not a containment, and that is the whole reason an anti-air
works.** An incoming attack carries one token from *each* axis at once: `air_mp` is stance `air` and
`blocked_as` `high`, so it arrives as `{aerial, high}`, and a window naming `{aerial}` stops it
because **one** token matches. Under a containment rule — the window must name every kind the attack
has — an anti-air would have to enumerate all three guard heights as well, and the field would be
useless for the one case it was added for. Meanwhile a grounded `stand_lp` carries `{mid}`, shares
nothing with that window, and beats `crouch_hp` clean through its startup, **which is the balance
property that makes an anti-air an anti-air rather than simply a good button.**

**The cost, stated rather than discovered:** a window naming `{mid}` also stops an *aerial* mid, and
there is no way to write *"invincible to grounded mids only"*. That needs a conjunction across the
two axes, a conjunction across axes is a predicate language, and `ADR-002` CHOICE B says six tokens
and one bitwise AND beat an expression tree nobody has data for.

**`throw` and `projectile` are reserved and are legal to author today.** The kernel can produce
neither — it has no throw at all, and although `engine.projectile` is real transcribed data on four
moves across two characters, nothing in `cse::kernel` spawns one. A window naming only reserved
tokens is **inert rather than wrong** and nothing warns about it, because a warning there would fire
on correct forward-looking data. Reserving them costs one enumerator each now; adding them later
costs a bit position in `CharacterData.h`'s `AttackKindMask`, which by then is inside a struct the
connect handshake hashes. **The enum order is append-only**, for the reason §3.1 gives for `Stance`.

What the loader refuses, and the decision inside each refusal:

| | refuses | because |
|---|---|---|
| **A17** | `priority` outside −32768…32767 | refuse, never narrow: the failure is a **sign flip at the boundary**, not a clamp, and a loader that clamped would be worse still — two peers clamping differently disagree about a hashed byte, and the desync presents as a rollback bug rather than a data error |
| **A18** | a window outside its own move, or of zero ticks | a zero-length window is not a short window, it is the empty set, and it sits in the file looking like protection while providing none — A15's zero-*area* hurtbox argument, in time. The upper bound is **A14's bound computed A14's way**, deliberately: two adjacent assertions disagreeing by one frame about where a move *ends* would be a worse defect than either being loose |
| **A19** | an unknown `attack_kinds` token | **the group's most important assertion, because of which way a typo fails.** Dropped, `["aerail"]` becomes `[]`, which means invincible to *everything* — the move silently becomes the strongest in the game. Kept as unknown, it means invincible to something no attack ever is — the move silently becomes invincible to *nothing* while the file still reads "invincible". There is no benign reading, so the token is refused and the message names all six legal spellings. The mutation that mattered was not the typo but `["air"]`: not a mistake at all, but a reasonable guess from `stance` one field up |
| **A20** | nothing — `invincibility: []` loads, means what an absent field means, and **warns** | `none` is already sayable by saying nothing, so `[]` has no second meaning left. But one level down an empty `attack_kinds` means *everything*, so `invincibility: []` is exactly what an author writes meaning "invincible always". A warning rather than an error, so a generator emitting the key uniformly keeps working — A16's severity argument in miniature |

---

## 4. Why the prover gets away without any of this — and the one thing it does not

This is the subtle part, and it is genuinely defensible rather than an oversight.

**A combo, in this model, is a sequence in which the defender cannot act.** The prover models one
attacker against an abstract defender that has no state at all (`ADR-001:271-273`, and
`schema.v2.json` `x-known-over-approximations.defender_has_no_state` names it as an
over-approximation in the *safe* direction).

**Blocking is an action, and you cannot act in hitstun.** So inside a combo, guard height is never
consulted: the second hit and every hit after it land because the defender is stunned, not because
they guessed a height. Guard height decides whether the **first** hit lands — whether the loop is
*entered* — and not whether the loop *closes*. Termination is a property of the cycle, and the cycle
is entirely inside hitstun. `ADR-001:273` puts the underlying fact in four words: *"`comboprover` has
no state for the defender at all."*

**So the termination result stays sound, and the fields in §3 cannot move a verdict.** Stronger: the
one place guard height *would* matter is the opening, and the model already defers that to the author
rather than deriving it — `starters` is *"move ids a combo may open with"*
(`schema.v2.json` `properties.starters`), an authored list. The gap is in a place the fragment had already declined to reason about.

**What it cannot stay sound about is blockstrings**, and the schema is already half-aware of this.
`Contact::Block` exists — `on: "block"` in the schema, four values kept deliberately in
`CharacterData.h Contact` *"because collapsing at load destroys the distinction the adapter has to
make."* It then collapses twice, in **opposite directions**:

| consumer | what `on: block` becomes | why |
|---|---|---|
| the prover | **dropped** — only `hit` and `always` continue a combo (`schema.v2.json` `$defs.cancel.properties.on`, `model.py:81 COMBO_CONTACT`) | a block does not extend a combo |
| the kernel | **taken after a hit** — `onHit`, `Combat.h:273-279` | *"the kernel has no blocking at all — `Fighter::blockstun` exists and nothing ever writes it"* |

The same authored edge is **invisible to the analysis and over-available in the game**, and
`MatchBuilder.cpp:160-167` counts it as `KernelPermits`. That is not a guard-height problem; it is
the pre-existing evidence that the *blockstring* question has never been asked. `blocked_as` is the
field that question would need, and asking it is not this document's business — the paper's claim is
about combo termination and stays exactly as sound as it was.

**Fields 5 and 6 change none of this, and `priority` is the clearest case in the document.**
Invincibility is a property of a defender, and the prover's defender has no state that could be made
invulnerable. Priority is a property of **two fighters meeting on one tick**, and the prover analyses
one character's cancel graph against an abstraction — there is no instant in its model at which the
field could be consulted *even in principle*. So §10 item 9's regression test now covers six fields
instead of four without weakening by a word: nothing here is a loss the fragment could have carried
and did not.

---

## 5. BLOCKING ITEM — the reference implementation rejects the new stance values

**Measured, not predicted**, and it was not visible from inside the repository.

`model.py:120-121` validates `stance` against a closed tuple and **raises**:

```python
if self.stance not in (GROUND, AIR, ANY):
    raise ValueError(f"move {self.id!r}: unknown stance {self.stance!r}")
```

So **a v3 file authoring `stance: "crouching"` is unloadable by the unmodified reference
implementation.** That breaks `schema.v2.json`'s central `$comment` claim — *"v2 remains a strict
SUPERSET of the prover's native JSON format … no export step appears"* — which `ADR-001:70-75`
established by measurement and treated as a property worth having. And the damage is not cosmetic:
the C++ header is corner-only, so **Python is the only midscreen path** (`ADR-001` gate 4), and a v3
character could not be analysed midscreen at all.

Note what is *not* the problem. `json_spec.py:72` is a bare `str()` and validates nothing;
`blocked_as`, `engine.hurtbox_sub` and `engine.airborne_from_tick` are all invisible to it by the
same argument v2 made for `hit_condition` (`schema.v2.json` `$defs.move.properties.hit_condition`) —
`_move()` reads thirteen named keys and enumerates none. `move.priority` and `move.invincibility[]`
join that list unchanged, so the extension adds nothing at all to this item. **Five of the six new
fields cost nothing. The sixth is a two-word tuple.**

Four options, and they are not equivalent:

1. **Widen `model.py:120`'s tuple to five values. Recommended.** It is one line, and it is
   **provably verdict-neutral**: §1.2 measured that no decision procedure in the reference reads the
   field, so a value the validator now accepts cannot reach anything that decides. The superset claim
   narrows honestly to *"superset modulo a one-line enum widening that provably cannot move a
   verdict"*, and that is a sentence with a measurement behind it.
2. Put the precise values under `engine` and leave `[P] stance` at three. Preserves the superset
   exactly — and reintroduces the split this whole document exists to close, with the attacker's
   state in two places at two nesting levels.
3. Author both: `stance: "ground"` for the prover plus a precise value elsewhere. Option 2 with the
   redundancy made explicit, and therefore with two things to keep in sync by hand. This is D8's
   failure mode wearing a different hat.
4. Ship and accept the break. **Rejected**: it silently converts "no export step" into "an export
   step nobody wrote", and the first symptom is a `ValueError` from a tool a designer runs, not a
   test.

**Do 1, and re-run the three fixtures to confirm the byte-identical verdicts `ADR-001` §2 recorded.**
Until that is done, the fixtures must keep authoring `ground` — which §3.1 already requires of them
for an unrelated reason, so nothing is blocked on it *except* authoring the first `crouching` move.

---

## 6. What is data now, and what is enforcement later

**The fields land now. The kernel enforcing them is `ADR-005` P2, where blocking already sits.**

The reason is not sequencing convenience. **Guard height is meaningless until there is blockstun to
gate**, and there is none: `Fighter::blockstun` exists at `GameState.h:65` and nothing writes it —
stated in five separate files, and `ComboWatcher.h:78-79` deliberately writes the rule against a
field it knows is dead. A `blocked_as` the kernel consults would be consulting it about an event that
cannot occur. `ADR-005` P2 item 2 already ranks blocking second on the list, immediately after
resources, for its own reasons: *"without it there is no defence, so every sequence is a 'combo' and
the combo counter and live verdict are measuring nothing."* Guard height rides that item; it does not
create a new one.

What each of the six costs when the time comes:

| field | to enforce it, the kernel needs |
|---|---|
| `stance` = air / ground | **nothing new.** `Fighter::airborne` exists (`GameState.h:70`) and `Simulate.cpp:58-77` already jumps, integrates and lands. |
| `stance` = standing / crouching | a crouch state. `kInputDown` exists as a bit (`GameState.h:112`) and no line reads it. Because the kernel takes buttons **held** rather than pressed, a *level* read may need no new `GameState` field at all — worth measuring, and cheap if true. |
| `blocked_as` | blocking, blockstun, and the defender's guard height — i.e. `ADR-005` P2 item 2 entire. |
| `hurtbox_sub` + `airborne_from_tick` | per-move hurtbox selection, which `Combat.h:297-299` already scopes to Phase 3 as a `FighterData` override and explicitly not as `GameState`. |
| `priority` | **no new `GameState` field.** `ResolveHits` already reads `moveId`, `moveFrame` and `alreadyHitBits`; the value itself rides `MoveDef`'s existing 16-bit `pad_`. |
| `invincibility[]` | the windows in `MoveDef`, plus the attacker's `stance` and `blocked_as` there too so an incoming attack can be classified. A `MatchData` growth, and still not a `GameState` one. |

**Cross-ups come after blocking, not with it.** Position-aware blocking presumes there is blocking.

The one thing that must not happen in between: none of these six may be quietly enforced *half* way.
A `blocked_as` consulted by a kernel with no blockstun, or a stance enforced for `air` but not for
`crouching`, produces a game whose frame numbers mean something different from the file's — which is
D8, and `ADR-005` §4 calls its presentation-layer twin *"the worst possible bug in a fighting game,
because players learn from what they see."*

**Fields 5 and 6 ride the same batch, and the reason needs stating precisely, because the obvious
version of it is wrong.**

`ADR-005` §3 batches kernel work because **`GameState` is a wire contract** — 80 bytes, per-field
offsets, and a golden cross-toolchain checksum asserted by `tests/test_kernel.cpp` and
`tests/test_determinism_crossplat.cpp` (`:133-136`), where *"re-recording that golden is how the
evidence gets destroyed"* (`ADR-005:73-81`). **Neither of these two fields touches `GameState`.**
Priority is a comparison between two `MoveDef`s. Invincibility is checked against the defender's
`moveId` and `moveFrame`, which `Fighter` already carries (`GameState.h:56`, `:62`). That is the
same property `Combat.h:64-72` engineered deliberately for cancels — *"the cancel rule below is built
to need NO NEW FIELD IN GameState. It reads three things the state already carries — `moveId`,
`moveFrame` and `alreadyHitBits`"* — and it is not luck: hit resolution and cancels ask the same
three questions of the state.

**What they do touch is the other hash.** `MatchData` sits on the wire's side of the connect
handshake, which hashes the loaded POD arrays once (`ARCHITECTURE.md` §4.8, `Combat.h:340-351`), and
`sizeof(MoveDef) == 40` is a `static_assert` whose message is about a padding hole making two peers
with identical characters disagree. **Priority alone is free** — it is exactly `pad_`, and the 40
does not move. **Invincibility is not**, because a list cannot live in a fixed 40 bytes, and neither
are `stance` and `blocked_as`, which have to cross over to classify an incoming attack. **That is one
growth, and blocking needs the same one**: `blocked_as` in `MoveDef` is what P2 item 2 compares
against the defender's guard. So the batching argument survives intact — it is about the handshake
hash rather than the golden, and splitting the work pays for the handshake twice.

**This amends `ADR-005:140`**, which today lists *"throws, per-frame boxes, push boxes, priority and
trade resolution"* under **"Later, and separable."** Separable was right while priority had no field
and nobody had asked for one. It stops being right the moment the data ships, because the
alternative is a schema field with no reader for a whole phase — which is what `stance` already is,
and what §1.6 shows happening to `engine.invuln`. The item moves up into P2, beside blocking.

**Two facts the enforcement work will want, recorded now because they are true now and cheap to
lose.**

**(a) `ResolveHits` already decides before it applies, and it says why.** `Combat.cpp:264-334` is
three loops: **decide** every overlap from the same pre-hit state (`:282-296`), **apply** the
effects (`:298-320`), **then** apply the interruptions (`:322-333`). The reason is in its header
comment, and the reason is what makes room for priority:

> *"If p0's hit were applied before p1's overlap were tested, a trade would stop being a trade …
> That is not a rounding bug or a container-order bug — it is worse, because it is stable, so it
> never looks like nondeterminism locally and instead just makes player 1 lose trades."*

**Priority slots into the tail of the decide loop** — after both overlaps are known, before any
effect is applied — and **invincibility into its body**, as one more reason `lands[a]` is false.
Both are pure functions of the pre-hit state, so the property those three loops exist to preserve
survives unchanged. **The architecture anticipated this field by name:** the same comment ends
*"Priority and trade resolution proper — a move that beats another outright, clashes, counter-hits —
are Phase 3; the rule here is the symmetric one, and both fighters land"* (`Combat.cpp:277-279`).

**And the golden is not the test that proves it.** Measured, because the obvious assumption is
wrong: `tests/test_determinism_crossplat.cpp:319-332` drives the whole stun path with a scripted
stand-in *because* **"`Simulate()` has no hit detection"**, and its own comment says the goldens will
need re-recording when real detection arrives. The cross-toolchain golden never reaches
`ResolveHits` and cannot notice either field. The test that must not move is
`tests/test_combat.cpp:415-436`, `CombatHit.ASimultaneousTradeIsSymmetric` — which asserts exactly
the behaviour `priority: 0` is defined to reproduce. **"Every existing character plays identically
after enforcement" is therefore a test that already exists**, not a hope.

**(b) "Reversal beats meaty" cannot be finished, and what blocks it is not a field.** The request
names two cases. **The anti-air half lands complete** — `aerial` is a token, `stance` already types
the attacker as airborne, and nothing else is needed. **The second half cannot, because there is no
knockdown state at all.** Every field of `Fighter` that describes a fighter's *condition* rather than
its position or its move is `hitstun`, `blockstun`, `airborne` and `comboHits` (`GameState.h:48-95`):
no `Lying`, no wakeup, no getup timer, no concept of a defender on the floor. A *meaty* is an attack
timed against a state that does not exist, and there is nothing
for a reversal to have priority **over**. Neither new field may grow a `meaty` token to paper over
it: the order is knockdown state first, and then no schema field is needed at all, because an
invincible reversal on wakeup is this pair doing exactly what it does everywhere else.

**The root of that is already in this document, one letter along.** §1.3's expression maps
`("S", "C")` to one value — and MUGEN's `statetype` is **S / C / A / L**. The tuple dropped the
**C**, which §1.3 records; **the same expression drops the L**, into `ANY`, the permissive
direction. Crouching and lying were destroyed on one line and only one of them has been recovered.
`stance` deliberately gains **no** lying value: a stance is what you must be in to *start* a move,
and nobody starts a move lying down. The `L` belongs in `GameState`, where `GameState.h:90-93`
already prices it — *"a FIFTH uint8 added here must come with three more explicit bytes, or the
compiler inserts tail padding nobody initialises"* — which makes it a state-layout change, a
re-golden, and therefore P2's business rather than a field's.

---

## 7. A hypothesis to measure — flagged as a hypothesis

**Enforcing stance may close part of the 33-cycle gap on its own, independently of resources.**

The mechanism is specific and every step of it is already measured:

1. `air_mp` is `stance: "air"`, and `tests/test_gap_extent.cpp:17-19` and `:57-60` establish that
   **all 41 simple cycles in `fighter_a`'s usable graph contain it** — one is `air_mp` into itself,
   and every longer cycle leaves `air_mp` by one of its nine landing links. `air_hk` and `super_beam`
   have no outgoing cancels, so no cycle can route around it.
2. The kernel gates a move start on stun alone, so a held MP starts `air_mp` **from the ground**.
3. That is not a marginal route — it is **the** route. `test_gap_extent.cpp:70-86` measures that the
   33 execute because *"the kernel takes buttons HELD rather than PRESSED … so a trace holding the
   follow-up's button starts it on the very tick `air_mp` recovers"*, and 32 of the 33 depend on it
   (`UntitledFighterMode.cpp:550-554`).
4. Under stance enforcement that route requires `airborne == 1`. **A jump is 39 ticks** — re-derived
   here from `Simulate.cpp:15-18`'s impulse of 5 px and gravity of ¼ px/tick², landing at
   `posY <= 0` on `:73-77`; apex 47.5 px.
5. `air_mp` is 22 ticks long and the kernel repeats it at a period of about 11 (18 repetitions in 200
   ticks). **That is roughly 3 to 4 per jump.**
6. The prover's ranking certificate permits **exactly 4** — `juggle` starts at 4, `air_mp` spends 1,
   `nonNegative` refuses the fifth (`docs/manual/fighting-core.md:594-598`).

**Say clearly what this is and is not.**

- It is **arithmetic over two numbers neither of which was measured for this purpose.** The 11-tick
  period is inferred from 18-in-200 rather than re-derived, and the 39-tick airtime comes from
  constants `Simulate.cpp:14` explicitly calls *"the SHAPE, not the balance"* — it moves the moment
  jump physics is tuned.
- If both limits land on 4, **that is a coincidence of two unrelated bounds, not a validation of
  either.** A resource running down and a jump ending are different reasons and must be reported as
  different reasons.
- It probably does **not** close the 33. The 32 longer cycles land into a grounded normal and come
  back up; stance forces a *jump* between repetitions, and 39 airborne ticks is a long time to leave
  a defender out of hitstun. So the prediction to test is **`ComboWatcher`'s verdict**, not a hit
  count: the loops likely survive as *sequences* and stop being *combos*, which is precisely the
  distinction that tool exists to draw.
- It does **not** displace resources. `ADR-005` §2's ranking survives intact — resources still score
  on four axes and nothing else scores on more than two. What is incomplete is §2's **attribution**:
  it presents the 33 as *the* resource gap, and `docs/manual/fighting-core.md:613` had already
  noticed otherwise, calling stance *"a second and independent reason this loop runs."* The 33 has at
  least two causes and closing either may move it.

`tests/test_gap_extent.cpp` is where this gets answered, and it is built to answer it: it already
decides performability per edge against `Combat.h`'s rules and then drives all 41 cycles tick by tick.

---

## 8. The 6/6/6 roster is a convention, and command normals are explicitly later

**Every character carries at least six grounded normals (3 punches, 3 kicks), six crouching normals,
and six aerials.** This is a **documented convention and a load-time WARNING naming what is missing —
never a refusal.** "At least" is a design standard, not a format rule, and a grappler with no aerials
must still load. The check is A16, warning severity, beside A01–A15 in `Data/src/CharacterData.cpp`.

It fires on the shipping character on the day it lands, which is the right way to prove a warning
works — `ADR-005` §4.1 asks for exactly this discipline, *"proven the way those were: by mutating a
real character until it fires."* Here nothing needs mutating:

Counted as **normals**, which is what the convention is about — air-stance *specials* do not satisfy
an aerial slot:

| character | standing | crouching | aerial | warns, naming |
|---|---|---|---|---|
| `fighter_a` | 6 ✓ | 6 ✓ | **2** — `air_mp`, `air_hk` | four missing aerials |
| Kung Fu Girl | 9 (close/far variants of 6 buttons) | 6 ✓ | **0** — its one air-stance move is `knee_followup`, a special | six missing aerials |
| Kung Fu Man | 6 ✓ (near/far variants) | **4** | 4 — `jump_lp/hp/lk/hk`; its other three air moves are specials | two crouching, two aerial |
| AOF2 thug | 0 | 0 | 0 | everything — 10 AI-selected moves, no button notation at all |

*Counted on the files as they stood on 2026-08-15, and **the `fighter_a` row is already stale**: a
pass adding aerial normals was in flight while the extension was written, and by 2026-08-16 that
file had grown from 18 moves to 22. `schema.v2.json` `x-load-assertions.A16.proved_by` carries the
same census and the same staleness. **Re-measure both before quoting either**, and expect
`fighter_a` to stop warning. What this section asserts is a property of the check; the counts are
the part that moves, which is an argument for the check and against the table.*

**The Kung Fu Girl row is the argument for shipping this check.** `ADR-001:500-501` already records
that 18 of her 43 attacking states were not transcribed, and lists **"six jumping normals"** among
them — by name, in prose, in a section nobody queries. The warning would surface exactly that gap,
automatically, at load, on the file itself. The three fixtures warning is therefore correct and
*informative* rather than noise: their coverage deliberately chose *"complete cancel structure over
breadth"* (`ADR-001` §6.2), and this turns that trade from a paragraph into a readout.

**Command normals (direction + punch or kick) are later, and the reason is a matcher rather than a
field.** The data slot already exists — `schema.v2.json` `$defs.moveEngine.properties.input` has
`engine.input.command / motion / buttons / hold / forbid / buffer_ticks`, and the AOF2 fixture
authors it (`aof2_strength_training.json:326`). What does not exist is a rule that can *select* one:
`UntitledFighterMode.cpp:70-79` states the mechanism precisely — `StepAttack` scans the move table in
**file order** and takes the first move all of whose bits are held, so *"a plain mask that appears
EARLIER always beats a direction-modified superset that appears later"*, and `fighter_a` authors
every standing normal before its crouching counterpart. Binding `crouch_lp` to DOWN+LP produces a key
that silently starts `stand_lp` forever.

So command normals need **longest-match / most-specific-wins** in `StepAttack`, not a schema field.
That is a kernel change under `ADR-005` §3's batching rule, and it is the same change that makes
`crouching` reachable by input at all. **The data already exists; the matcher does not.**

---

## 9. One more thing, because it is the same defect one field over

`MatchBuilder.h:185-190` already complains, about **buttons**:

> *"THE SCHEMA HAS NO INPUT NOTATION. Not a command list, not a motion, not a button name — move ids
> like `stand_lp` carry the information only in the English of their spelling, and reading a button
> out of a string suffix is precisely the '1123-line heuristic over a foreign format' that
> `ARCHITECTURE.md` D7 rejects."*

The project **named this exact failure mode and then repeated it for stance**, and then repeated it
again for guard height, where the information sits in a display label reading *"(low)"*. Three
instances of one defect: a fact about a move, present in the author's head and in the move's English,
absent from anything a program can read.

*(For accuracy, since a future reader will check: that comment is imprecise on one point. The
**schema** does have the input slot, at `$defs.moveEngine.input`. What is true is that `fighter_a`
authors none of it, the loader reads none of it, and the binding therefore comes from
`BuildOptions::bindings` where it is at least visible. The complaint is right about
`CharacterData`, not about the schema.)*

**And now a fourth instance, which is what turns three into a pattern.** §1.6: *"invincible 1-6"*,
in a label, on the move whose entire identity is those six frames. Buttons, stance, guard height,
invincibility — **four fields, one failure mode, found by four separate investigations that were
each looking for something else.** The project should expect to find it again. What follows is where
to look, and the useful part is that the search space is finite.

**`move.engine.tags` is the register.** `schema.v2.json` `$defs.moveEngine.properties.tags` defines
it as `{"type": "array", "items": {"type": "string"}}` and **gives it no description at all**, in a
file whose other field descriptions run to four hundred words with measured counterexamples.
`CharacterData.h` does not mention it. Nothing loads it, nothing validates it, and no two characters
have to agree on a spelling. `fighter_a` authors **31 distinct tags across 18 moves** — counted
2026-08-15, so §8's staleness note applies to these numbers as well — and **three of v3's five
changes were already words in that list before they were fields**:

| tag, with its count | became |
|---|---|
| `stand` (6), `crouch` (6), `air` (2) | `move.stance` — §3.1 |
| `low` (3) | `move.blocked_as` — §3.2 |
| `invincible` (1), `reversal` (1), `anti_air` (2) | `move.invincibility[]` — §3.6 |

**Which also says what kind of hole the tag list can find and what kind it cannot**, and that is
what makes it a register rather than a hunch. A tag is a **unary predicate about one move**.
`priority` was never a tag and could not have been — it is a *relation between two moves* — and
neither were `hurtbox_sub` or `airborne_from_tick`, because a tag carries no number. **So the
untyped tags name the remaining boolean-shaped facts, exhaustively, and nothing else.**

**Where to look next, ranked, with the evidence:**

1. **`knockdown`, and it is §1.6's shape rather than §1.1's — the data is already complete.**
   Tagged on three moves, and `engine.reaction.causes_knockdown` is authored **`true` on exactly
   those three** (`crouch_hk`, `air_hk`, `super_beam`) and `false` on the other fifteen. `crouch_hk`
   states it a third time in English, *"(sweep, knockdown)"*. **Three copies of one fact, perfect
   agreement, and nothing reads any of them** — `engine.reaction` is on `CharacterData.h`'s
   deliberately-unloaded list. This is the highest-value one to close, and not only because the data
   is free: §6(b) has already established that a knockdown *state* is what blocks half of the
   author's request.
2. **`launcher`, which is §1.1's shape — typed nowhere at all.** Tagged on `stand_hk`, `crouch_hp`
   and `special_uppercut`, labelled on two of them, and **no field anywhere says it**: `stand_hk`'s
   `effect` is `{}` and its `causes_knockdown` is `false`. Now measure one move over. `air_hk` is
   labelled *"(juggle ender)"* and its `effect` **is** `{"juggle": -2}`. **The file types the
   spending of the juggle resource and not the causing of the state that makes spending it
   possible.** That is a hole in the resource model rather than only in the naming, and it is
   invisible from the resource side, because a budget that is only ever spent looks complete.
3. **Move class and button, which is where this section started.** `normal` (14), `special` (3), `super` (1),
   `light`/`medium`/`heavy`, `punch`/`kick` — the same facts `MatchBuilder.h:185-190` complains are
   carried *"only in the English of their spelling"*, restated as tags that nothing reads either.
   `[G]` fields for these are cheap; the matcher §8 describes is not, and that is the reason to do
   these last rather than first.

---

## 10. Decision

1. **`stance` gains `standing` and `crouching`, appended.** `ground` keeps meaning **grounded,
   unspecified** — honest about the corpus, and it keeps every existing file valid. New characters
   use the precise values.
2. **Block height is `blocked_as` — `high` / `mid` / `low`, default `mid`.** Never `guard`, which is
   a resource minimum and got there first. High block stops {high, mid}; low block stops {low, mid}.
3. **Stance and `blocked_as` are orthogonal and the loader cross-checks them not at all** — not as an
   error, and deliberately not as a warning.
4. **Low-profiling and hop-overs are geometry**: an optional per-move hurtbox override in the
   kernel's own `Box` units, and an optional tick on which a grounded move goes airborne. No
   `low_profiles` boolean and no `hops_over_lows` boolean, in this schema or any later one.
5. **Cross-ups get no field.** They are a function of position at contact; `posX` and `facing` already
   exist, and `pos * facing` is the bug to refuse when the rule is written.
6. **All four fields are data now and enforcement is `ADR-005` P2**, with blocking, because guard
   height is meaningless without blockstun. Cross-ups come after blocking, not with it.
   *(Extended: items 10–12 add two more fields on the same schedule, for a different reason —
   §6.)*
7. **BLOCKING (§5): widen `model.py:120`'s stance tuple to five values before the first `crouching`
   move is authored**, and re-measure the three fixtures against `ADR-001` §2's verdicts. It is
   provably verdict-neutral because nothing in the reference reads the field, and skipping it makes
   every new character un-analysable midscreen.
8. **6/6/6 is a convention enforced as a load WARNING (A16) naming what is missing**, never a refusal.
   Command normals are later, and what blocks them is a most-specific-wins rule in `StepAttack`, not
   a schema field.
9. **The termination result is unaffected**, and no verdict in `ADR-001` §2 may move. None of the four
   fields is `[P]`; the three under `engine` are invisible to `json_spec.py` by the argument v2
   already measured, and `blocked_as` is invisible by the same one. **If any fixture's verdict
   changes, something in this list was implemented wrongly** — that is the regression test for this
   whole ADR. *(Extended: the same holds for items 10 and 11, by §4's last paragraph.)*
10. **`priority` is a plain integer — `int16`, default 0.** Higher wins outright and the loser takes
    nothing; **equal is a trade and both land**, which is what `ResolveHits` does today, so the
    default describes the shipped game and no existing character changes. It orders one instant and
    does nothing else: it does not gate starting a move and it interrupts nothing. Signed, because
    *"loses to everything"* is `-1` and the alternative is renumbering the cast; 16 bits, because
    `MoveDef::pad_` is the slot it lands in. A17 refuses an out-of-range value rather than narrowing
    it. **It is not `engine.reaction.priority`**, whose scale is centred and therefore means the
    opposite of this one — a hazard no assertion can catch, which is why §3.5 spends a paragraph on
    it.
11. **`invincibility[]` is a list of typed windows, and an anti-air is not a special case** — it is a
    move invincible to `aerial` during its startup. The author's *"priority toggled depending on
    attack state"* was **read** as immunity to named attack kinds rather than transcribed as a
    condition table, and that is recorded as a reading, not as the request. The kind list spans two
    axes — `high|mid|low` is the incoming attack's `blocked_as`, `aerial` is the attacker's `stance`
    — and the match is an intersection, which is what makes an anti-air work. Windows may overlap,
    the meaning is the union, nothing diagnoses it because the request's own example is an overlap,
    and **there is no whitelist form, ever**, because that is what keeps the union
    order-independent. An unknown token is refused, never dropped (A19).
12. **Both are data now; enforcement is `ADR-005` P2, beside blocking** — which **amends
    `ADR-005:140`**, where priority currently sits under *"later, and separable"*. The batching
    reason is the connect handshake's hash over `MatchData`, **not** `GameState`'s golden: neither
    field needs a state byte, and `stance`, `blocked_as`, `priority` and the windows all cross into
    `MoveDef` as one change. Priority slots into `ResolveHits`'s decide loop and invincibility into
    its body; `tests/test_combat.cpp:415-436` is the test that must not move, and the cross-toolchain
    golden cannot see either field because it never reaches hit detection.
13. **"Reversal beats meaty" is not blocked on a field, and no field may pretend otherwise.** There
    is no knockdown state: MUGEN's `L` was dropped by the same expression that dropped the `C`
    (§1.3, §6). So the anti-air half of the request lands complete now and the meaty half waits for
    `GameState`. Neither new field grows a `meaty` token, and `stance` gains no lying value.
14. **The next instance of §9's defect is `knockdown`, and `engine.tags` is where to look for the
    ones after it.** An undescribed free-string array that nothing loads, holding 31 distinct words
    across 18 moves, three of which v3 has just promoted to fields. It can only ever hold unary
    predicates about one move, which makes it a finite list rather than a suspicion.

**Reversed if:** a character needs guard height to depend on something other than the move — a
different height on a later hit of a multi-hit string, or on the defender's state — at which point
`blocked_as` becomes a property of `engine.hits[]` rather than of the move, and the three-value enum
becomes the first entry in a table rather than a scalar.

**Reversed if, for items 10 and 11:** a character genuinely needs an ordering that depends on the
opponent's kind — *"beats aerials, loses to grounded mediums"*, which §3.6 records as the one case
the reading gives up — at which point `priority` becomes a list of `(attack_kinds, value)` pairs
**reusing `$defs.invincibilityWindow`'s token enum rather than inventing a second one**, and the
integer becomes its default entry. That is the condition table the request literally named and §3.6
declined to build; it should arrive with the character that needs it, and not one revision earlier.
