# ADR-001 — Does the declarative fragment fit a real fighting-game character?

**Status:** Accepted, with two amendments to `docs/ARCHITECTURE.md` and one blocking item.
**Phase:** 0 (`ARCHITECTURE.md:282-294`). Zero engine code written: no C++ under `Engine/`, no CMake change.
**Date:** 2026-08-12
**Decides:** D2, D7, D8, §5.2, §5.3, and the Phase 5 scope.

Claims marked **[V]** were re-verified for this record by reading the cited file and re-running the
cited command, not carried over from working notes. Two citations in the working notes were off by a
few lines and are corrected here; see §7.

---

## 1. THE QUESTION, AND THE ANSWER

> Can a real fighting-game character be expressed as declarative data in the fragment the combo
> prover decides over? If yes, the behaviour layer can be data rather than a scripting language,
> snapshots become a POD `memcpy`, and the prover reads the shipping game directly.

**Yes — for what a move *does*. No — for the schema we drafted, and no for how an agent *chooses* a
move.**

58 of 59 transcribed moves needed no code and no expression language to say what they do. One did.
That is the number D7 is about, and it passes with an order of magnitude to spare.

But 23 of those 59 needed a field the drafted v1 schema does not have, and 26 of 247 cancel edges
needed a predicate over the *defender* that no schema field can express. **D2 and D7 stand. The v1
schema cannot be frozen as drafted, and Phase 5 is scoped for half the language it needs.**

The plan's own gate anticipated exactly this fork. `D7:216` says a rate above 20% means "either the
vocabulary is wrong (fixable) or the approach is (not)". Phase 0's answer is unambiguous: **the
vocabulary, and it is fixable with nine named fields.**

---

## 2. THE NUMBERS

Three characters transcribed from `C:/rw/corpus`, the only fighting-game corpus whose combat logic is
open, readable data. Every `[P]` field the prover reads was derived from a cited `.cns`/`.air` line.
Two quantities were estimated and are flagged as such everywhere they appear (§6.3).

| | Kung Fu Girl | Kung Fu Man | AOF2 Thug | total |
|---|---|---|---|---|
| moves transcribed | 25 | 24 | 10 | **59** |
| cancel edges | 134 | 87 | 26 | **247** |
| source states available | 43 | — | 5 attacks × 4 variants | |
| **moves needing a C++ effect or expression** | 0 | 1 | 0 | **1 (1.7%)** |
| **moves needing a schema field v1 lacks** | 15 | 8 | 0 | **23 (39.0%)** |
| **cancels needing an opponent predicate** | 0 | 0 | 26 | **26 (10.5%)** |
| verdict, midscreen | INFINITE | TERMINATING | TERMINATING | |
| verdict, corner | INFINITE | INFINITE | TERMINATING | |
| `Status::Unknown` | 0 | 0 | 0 | **0 of 6 runs** |
| `hasRanking` (terminating cases) | n/a | false | false | **0 of 2** |
| usable / dead cancels | 123 / 7 | 39 / 48 | 0 / 26 | |
| C++ `analyse` wall-clock (corner) | 0.033 ms | 0.041 ms | 0.009 ms | |
| Python `decide` wall-clock (midscreen) | 147 ms | 226 ms | 0.64 ms | |

Escape-hatch tally by kind, counted directly out of the shipped files rather than from a spreadsheet
**[V]**: `missing_schema_field` 9, `schema_v2_field` 7, `runtime_condition` 6, `expression_language`
1, `irreducible_cpp` **0**.

**Deliverables.** `schema.v1.json`, `kung_fu_girl.json`, `kung_fu_man.json`, `aof2_strength_training.json`.

> **Amendment (2026-08-13).** These four were authored into `Editor/src/Exported/Characters/` and now live
> in **`tests/fixtures/characters/`**. Nothing about the measurements changed; what changed is that they are
> transcriptions of third-party MUGEN characters, so they are *evidence, not content*, and a shipping build
> must not stage them. The shipping directory holds `schema.v2.json` and the project's own characters. The
> README beside the fixtures records the rule and what depends on them.

**Loader compatibility: 3 of 3 loaded by the unmodified `json_spec.load()` on the first attempt.**
No file was reshaped, no loader error, no fix. `raw.get(...)` ignores every unknown key
(`json_spec.py:63-109`), so the `engine` namespace and the inert `certain`/`caveat` fields pass
straight through. **There is no export step, and that is measured, not asserted:** deleting every key
named `engine` recursively and re-analysing produces the identical verdict on all three characters
**[V]**. The prover subset is 37% / 43% / 24% of each file by bytes.

---

## 3. THE GATES

### Gate 1 — escape-hatch rate < 20% (`ARCHITECTURE.md:289`)

**PASSED on D7's own definition of the term. FAILED on schema completeness. Both numbers matter and
neither should be quoted alone.**

D7 defines the escape hatch as "named C++ effects registered by the engine and invoked from data as
opaque rows" (`D7:191`). Under that definition the rate is **1 of 59 = 1.7%**, against a gate of 20%.

The one move is Kung Fu Man's `fuyo_reikyaku`. Its second `HitDef` sets
`air.velocity = -vel x, vel y-1.35-pos y/686.68` (`corpus/kfm/kfma4a_cns.txt:4060-4070`) — the launch
vector is arithmetic over the defender's live position and velocity at the instant of contact. No
constant substitutes for it. Note what is *not* affected: startup, active, recovery, hitstun, damage,
effect and guard are all ordinary, so **the prover's verdict is untouched**. It is the engine's hit
resolver that cannot be data here.

Under the broader reading — "needed something schema v1 cannot say" — the rate is **23 of 59 =
39.0%**, and it is above the gate. The gate exists to force the question the doc poses at `D7:216`,
so here is the answer to it: **all 22 of the remaining hatches are missing nouns, not missing verbs.**
Every one is a fact about a move that the schema had no slot to write down. None requires computation
at runtime. That is the strongest available confirmation of D7's *direction* and a flat refutation of
Phase 0's *deliverable*, which was "a frozen v1 schema."

**Consequence, stated plainly: the v1 schema does not get frozen. It absorbs the nine fields in §4
first.** Phase 3 builds the runtime that executes this data; every field discovered after that point
is a runtime redesign rather than a schema edit. This is precisely the stop the gate was placed to
cause — it just lands on the schema instead of on D7.

### Gate 2 — `Unknown` rate, "quantize meter to bars until all three resolve" (`ARCHITECTURE.md:290`)

**PASSED. The predicted problem did not occur, and the prescribed fix is actively harmful. Strike it
from the plan.**

`Status::Unknown` was never produced: 3 characters × 2 stages × 2 implementations, `capped = false`
everywhere. The C++ search explored **63, 79 and 10 configurations** against `limit = 200000`
(`comboprover.hpp:314` **[V]**) — over three orders of magnitude of headroom. The Python reference
peaked at 201.

The counterfactual was run anyway, because this is a *schema* decision that the doc says must be made
here. Re-quantizing Kung Fu Girl's meter 10× and 100× finer (down to 1 unit = 1 raw MUGEN power):
still INFINITE, still no `Unknown`, same loop, same runtime.

**Following the instruction literally would have corrupted the model in both rounding directions.**
Measured on `comboprover.hpp` with KFG's real numbers:

| meter unit | verdict | configs |
|---|---|---|
| 1 (raw MUGEN) | terminating | 2099 |
| 100 (1/10 bar) | terminating | 1148 |
| 1000 (bars), gains rounded up | **INFINITE — fabricated** | 17 |
| 1000 (bars), gains rounded down | terminating | 144 |

Rounding a 50-power normal *up* to a whole bar makes a normal refund the EX move that cancels into
it. Rounding it *down* deletes meter from the model so no super is ever reachable. Tenths are no
better: a special's `poweradd` of 30 is 0.3 tenths and quantizes to nothing.

The doc's stated mechanism is also wrong on its own terms. Dividing every value by a common factor is
a bijection on reachable states, so coarsening does not shrink the search; only the ceiling bounds the
range, and only *lossy* merging shrinks the count — which is exactly what breaks the model.

**Decision, made on engine grounds instead: 1 meter unit = 10 MUGEN power = 1/100 bar.** That is the
GCD of every power value across all three characters (10/20/30/40/50/60/80/100/120/1000), so it is
**exact, with no rounding anywhere**. Ceiling 300 = 3 bars. Exactness is worth more than coarseness
because it deletes a divergence class rather than bounding it, and it dodges a live trap: C++
`std::lround` rounds halves away from zero while Python `round()` is banker's, so at 1/10-bar
granularity a 50-power gain becomes 1 in the editor and 0 offline.

**Revisit if** a character ever resolves to `Unknown`. Then coarsen deliberately, as a re-authoring
with new integers written into the file — never as a loader flag, because a loader flag reintroduces
exactly the rounding divergence this decision removes.

### Gate 3 — certificate availability (`ARCHITECTURE.md:291`)

**PASSED as predicted, and the prediction was incomplete. `hasRanking` is false in 2 of 2 terminating
cases — but for two different reasons, and the panel must not render them identically.**

The doc's mechanism is confirmed for Kung Fu Man: `spendOnly` is cleared (`comboprover.hpp:376-386`
**[V]**) because `stand_lp` carries `effect.meter = +5`, and the certificate is gated on
`spendOnly && resCount > 0` (`:563` **[V]**).

**AOF2 has `spendOnly = true` and still has no certificate.** Reading `:562-585`: past the
`spendOnly` gate the block requires a live edge, and AOF2 has zero usable cancels, so
`if (!anyEdge) break` (`:578-583` **[V]**) fires immediately and `rankingOrder` stays empty.

So "no ranking certificate" means either *the character builds meter* or *nothing loops at all* — and
the second is the better news, being the strongest possible termination result. **Build the editor
panel around `maxHits` / `maxFrames` / `maxDamage`, exactly as `§5.3` says, and give the two
no-certificate cases different words.**

### Gate 4 — timing (`ARCHITECTURE.md:292`)

**The header's claim HOLDS, with ~25× margin — for the corner. The midscreen path is four orders of
magnitude slower and that changes a design requirement from a nicety to a load-bearing one.**

"Well under a millisecond" (`comboprover.hpp:9-13`) is measured at **0.033 / 0.041 / 0.009 ms** in
C++. Live re-analysis on every keystroke is viable.

The Python midscreen reference measures **147 / 226 / 0.64 ms** **[V]**, and near a knife-edge it
degrades badly: a KFG variant with pushback scaled 1.05× took **10.1 seconds**. Midscreen is where
position and walking live, it is the model the C++ header does not implement (`:15-24`), and it is the
model the interesting verdict comes from. **`§5.3`'s "asynchronously with a budget" is required for
the midscreen path specifically, and the panel needs a cancel-and-supersede story, not just a budget.**

---

## 4. WHAT THE HATCHES NEEDED

This is the phase's most valuable output. Grouped by what the schema was missing, with the count of
moves and the citation that proves the need. **Every group in A–F is a declarative field. Groups G–I
are not, and they are the finding.**

### A. One move is more than one hit — `move.engine.hits[]` (4 moves)

The schema's `Move` is a single `(startup, active, hitstun, damage)` tuple; real states register
several `HitDef`s at different `animelem`s.

- KFM `f_lk_cancel`, state 275: two `HitDef`s at `animelem=4` and `animelem=10`
  (`kfma4a_cns.txt:767-884`). Transcribed with the first only, which is what `mugen_cns.py:530-535`
  does.
- KFM `kyuki` (two `HitDef`s), KFM `kungfu_zanko` (two `HitDef`s spanning four chained states, 1710
  and 1720; the 80-damage second hit is dropped).
- KFG `super_palm`: three `HitDef`s (`Supers.cns:82` firing at AnimElem 5 and 13, `:105` at
  AnimElem 21) collapsed into one record whose damage is their sum, 219.
- KFG `chop`: a *second hit profile* selected by whether the first whiffed — near at AnimElem 4
  (`Normal.cns:185-220`), far at AnimElem 5 gated on `triggerall = !movecontact && !movereversed`
  (`Normal.cns:223-225`), with different damage, hitstun and reach.

**Rejected alternative:** splitting into two prover `Move`s joined by an optional cancel. A *forced*
follow-up modelled as an *optional* edge gives the attacker a choice they do not have, which is the
direction that invents infinites.

### B. The attacker moves — `move.engine.motion[]` (8 moves)

Every KFG special and the super drives itself with `VelSet` / `VelMul` / `PosAdd` inside its statedef
— e.g. `palm` at `Specials.cns:29-46` (`PosAdd x=3` at AnimElem 2, `VelSet x=5.5` across elements
3-5, `VelMul x=.8` as friction, `VelSet x=0` at element 6). Schema v1 has `pushback` for the defender
and **nothing at all for the attacker's own displacement**, so the forward travel that decides whether
a special connects at range exists nowhere in the file. One array of `{tick, vel_x_sub, vel_y_sub}`
closes it.

### C. The move that runs depends on distance at input time — `proximity_variant` / `min_reach` (8 moves)

One button, two states, chosen by range: `value = 250 + (Abs(P2BodyDist X) <= 15) * 1`
(`Kung Fu Girl.cmd:802` **[V]**, same idiom at `:774` and `:756`-family), and KFM's `.cmd` picking
state 210 under `p2bodydist x < 26` and 220 otherwise.

**The prover half of this is not closable, and that is a fragment limit rather than a schema gap.**
`move.reach` expresses a *maximum* gap because guard is `resource >= n` and space is defined as
`max_reach − gap` (`model.py:453-458`). A *minimum* gap is an upper bound on a resource, which the
guard vocabulary cannot state. Consequence: the model lets the attacker use a far normal point-blank
— the permissive direction, which can invent an infinite but cannot hide one.

### D. The state ends on a physics predicate, not a frame count — `on_land` / `exit_condition` (3 moves)

KFG state 1100 leaves the ground mid-move (`StateTypeSet`, `Specials.cns:693`) and both it and 1105
end on `pos y > -vel y` (`Specials.cns:776`, `:895`). KFM `kyuki` exits on
`time>18 && (pos y+vel y>=0) && (vel y>0)`. v1 transitions are frame-indexed only, so "this state ends
when you land" cannot be written, and the move's real duration depends on the trajectory.

### E. Per-frame properties beyond boxes — `invuln`, screen-freeze (1 move)

KFG `super_palm` has per-frame invulnerability by attack attribute: `NotHitBy` with
`value = ,NA,SA,AT` for 11 ticks from AnimElem 2, `value2 = C,NA` thereafter (`Supers.cns:34-43`).
v1 has hurt boxes but no invulnerability track. Same move needs `SuperPause time 30 with
poweradd -1000` (`Supers.cns:17-23`) — simultaneously the meter charge and 30 ticks during which
nothing advances.

### F. Projectiles — `move.engine.projectile` (2 moves)

KFM `hasyo` and `suiten_hasyo` attack via a `Projectile` controller, not a `HitDef`
(`kfma4a_cns.txt:2338-2477`). **The engine half is a schema field; the prover half is not closable.**
`Move.startup` is a constant, but a fireball's frame of contact is a function of distance, so
"startup 15" means only "released on frame 15". A related gap: `suiten_hasyo` charges its -1000 power
at `animelem=5` and releases at `animelem=10`, and `move.effect` is applied once, at move start.

### G. Predicates over the DEFENDER — the group that changes Phase 5's scope

**This is the finding.** It appears independently in two of the three characters, in two different
places, and no field in groups A–F touches it.

*Hit eligibility.* Every one of Kung Fu Girl's 17 normals gates its `HitDef` on
`trigger1 = !var(16) && var(15) < 1` (`Normal.cns:27`, byte-identical on the other 16). `var(15)`
counts hits since the defender was last grounded, `var(16)` flags a hit-fall state
(`System.cns:2611-2666`), and the character **disables MUGEN's own juggle system to run these
instead** (`AssertSpecial nojugglecheck`, `System.cns:2590-2593`; `[Data] airjuggle = 0`,
`System.cns:7`). Meaning: a normal does not connect against an already-juggled defender.

*Transition eligibility.* All 26 AOF2 cancels re-enter from `[Statedef 1010]` under
`p2bodydist x <= 30 && p2movetype != H` (`States.st:190`, `:197`). The second clause literally reads
*only attack when the defender is not in hitstun* — an explicit, authored anti-combo rule.

*Defender state as an outcome.* AOF2's sweep sets `fall = 1` with `fall.recovertime = 12`
(`States.st:419-421`): the combo ends because the defender enters knockdown, not because frame
advantage ran out. `comboprover` has no state for the defender at all.

**Phase 5's trigger language needs an OPPONENT namespace — `p2statetype`, `p2movetype`, `p2bodydist`
— not just a self namespace.** `ARCHITECTURE.md:344` scopes Phase 5 at "~10 atom kinds, 6 comparison
operators… an integer variable bank," ported from `triggers.py:407-519`. That is the self half.

### H. The transition is a policy, not a rule — not a trigger-language problem at all

AOF2 selects its attack with `random < 50` evaluated **every tick** (`States.st:189`, `:196`), then a
nested `ifelse` over further rolls (`:191`, `:198`). A deterministic rollback sim cannot use MUGEN's
random at all, and the prover's model has no probability. The declarative reading authored — "this
edge exists, at the minimum delay the state machine costs" — loses the distribution entirely.

**`D7`'s escape hatch is "a named C++ effect"; it does not cover "the transition itself is a policy."**
Recommendation: **AI selection should be a separate authored artifact from the move/cancel data**, not
an escape hatch inside it. The declarative fragment models *what a move does*; it does not model *why
an agent chose it*, and conflating the two puts a policy where the prover expects a rule.

### I. One `.def` is not one character — no home in the schema at all

- AOF2 is a **spawner plus four disjoint sub-characters**. The root player has no attacks, is
  `movetype=I`, and is made unhittable (`States.st:1382-1387`); its only job is
  `type = helper, stateno = 1000, ID = 1000, name = Thug` (`States.st:5-44`). `var(0) = random % 4`
  is rolled once at spawn (`States.st:71-73`) and never changes, and `anim = base + 100*var(0)`, so
  the file is four disjoint sub-automata. Moves and cancels can be duplicated per variant; **a
  `gap_action` cannot** — `gap_actions`, `walk_speed`, `decay`, `scaling` and `resources` are
  character-global.
- KFM's `var(3)` adds and removes whole moves (`nishi` and `meikyo` require `var(3)=2` and are
  absent). **A mode variable that changes the move *list* is not a per-move field.**
- KFM's `var(5)` doubles every special into light/strong with different damage and `poweradd`; only
  the light set is transcribed.

### The nine fields, as the v2 backlog

Carried in `schema.v1.json` → `x-phase-0-findings.v2_backlog` **[V]** so the list is queryable rather
than prose: `move.engine.motion[]`, `move.engine.hits[]`, `proximity_variant` + `min_reach`,
`move.engine.projectile`, `on_land` transition kind, per-frame `invuln`, `move.hit_condition`,
`cancel.engine.condition` over an opponent namespace, and per-variant `gap_actions`.

---

## 5. WHAT THIS MEANS FOR THE PLAN

### D2 — survives, strengthened

No move needed a float to say what it does. Every quantity the simulation integrates over time is an
integer, and the meter quantization is *exact* rather than merely bounded (§3, gate 2). The narrow
claim `D2` retreated to after its adjudication is the one the corpus supports.

### D7 — survives as a decision; its Phase 0 deliverable does not

The mechanism (authored data + a trigger language + named C++ effects) is not refuted: 58 of 59 moves
needed no code. **What is refuted is "a frozen v1 schema" as this phase's output.** Nine fields go in
before Phase 3 starts, and Phase 5's scope grows an opponent namespace it was not budgeted for.

`CHOICE B` (data-only first, DSL in Phase 5) is **vindicated by the exercise itself**: had the parser
been written from imagination in Phase 3, it would have had a self namespace and no opponent
namespace, because that is what `triggers.py:407-519` foregrounds. The transcription is what surfaced
group G.

### D8 — the hitstun gap is closed outright, and a NEW hard rule is added

`D8` proposes mitigating the float-decay divergence by preferring `Decay::Kind::Table` with integer
multipliers plus an adapter assertion. **Measurement says close it instead: forbid
`kind: "multiplicative"` in the schema.** `comboprover.hpp:82-88` computes it by repeated float
multiply then `static_cast<int>` and `model.py:262-263` by `base*ratio**n` then `int()`; an integer
kernel on `scaleBy` reproduces neither. Both `none` and `linear` are pure integer arithmetic in both
implementations (`model.py:260`, `comboprover.hpp:81` **[V]**), so forbidding one kind removes the
divergence class rather than bounding it.

**NEW RULE, found by measurement and now written into `schema.v1.json`: `decay.floor` must be ≤ the
smallest `hitstun` in the file.** Both implementations compute `max(floor, base − step·n)`, so a floor
*above* a move's authored hitstun **raises** it and fabricates frame advantage. The project's own
draft house rule — linear, step 2, floor 10 — does exactly this on two of three characters:

| KFG midscreen, decay | verdict |
|---|---|
| `none` (truthful for MUGEN 1.0) | INFINITE |
| `linear` step 2 floor 6 / floor 9 / step 1 floor 6 | TERMINATING |
| **`linear` step 2 floor 10 (the draft house rule)** | **INFINITE — fabricated** |

Floor 10 exceeds `stand_lp`'s authored `ground.hittime = 9` (`Normal.cns:48` **[V]**).

A second, subtler consequence: **both implementations evaluate every edge at the *settled* hitstun**
(`comboprover.hpp:336-339`). With step 2 the settling index is 14 hits, so every KFG move collapses to
the floor and 128 real cancels get reported dead. On KFM the same rule settles at 7 hits with every
hitstun landing on exactly 10, which is why 48 of 87 edges are reported dead. **Inventing a decay rule
for a game that has none deletes the game.** All three files ship what MUGEN actually does.
*Recommendation: the editor panel should show the pre-decay dead-cancel list too.*

### §5.2 — the `ceiling` row's prescribed fix is now refuted; use the other one

The table offers two fixes: quantize meter to bars so the ceiling is unreachable, **or** pre-clamp
`effect` rows. **The first is dead** (gate 2 — bars corrupt the model). So:

**`comboprover.hpp` has no ceiling at all.** `Character` (`:148-174`) carries no floors and no
ceilings, and `nonNegative` (`:264-269`) hardcodes floor 0 **[V]**, while `model.py:192` has one and
`termination.py:200,245,252` clamps against it. The editor panel and the offline run therefore search
**different state spaces**. This is unsoundness, not rounding.

Two fixes exist and **they are not equivalent**:

1. **Clamp in the adapter's own search loop. Sound. Do this.**
2. Lower the ceiling into the prover's vocabulary as `model.py:495-498` does for distance, carrying a
   `meter_room` resource with equal-and-opposite effects so `nonNegative` enforces the bound with no
   header change. **Rejected: this is a hard wall, not a clamp.** It *forbids* the gain instead of
   saturating it, under-approximating the attacker, so it can **miss an infinite**.

### §5.2 — one row to add: resource order is a build-wide contract

`comboprover::Character` keys resources **positionally** (`:56`, `:128-129`, `:152`). Resource order
is therefore part of the schema contract across an entire build, not a per-file choice. All three
Phase-0 files declare `meter, juggle` in that order even where a resource is inert.

### §5.3 — the corner caveat is not strong enough

`§5.3` requires the panel to say *"Corner-pinned defender… away from the wall this verdict is an
under-approximation."* True, and insufficient. **The midscreen verdict rests on a number no source
file contains.**

MUGEN records `ground.velocity` and `ground.slidetime` and **never a displacement**, so pushback is a
house rule — `|velocity| × slidetime / 2`, applied uniformly across all three characters and flagged
on every move. Perturbing KFG midscreen one input at a time:

| perturbation | verdict |
|---|---|
| as authored | INFINITE (`stand_hk_close > stand_mp`) |
| pushback ×1.02 | INFINITE, same loop |
| pushback ×1.05 / ×1.10 | INFINITE, *different* loop, 10.1 s |
| **pushback ×1.20** | **TERMINATING** |
| pushback ×0.95 | INFINITE, `stand_lp` loop |
| **walk_speed −17%** | **TERMINATING** |

**A ±20% error in an unmeasurable house rule flips the midscreen verdict.** The KFG midscreen loop's
space budget closes by **one unit**: pushback costs 50 per cycle and the 17 slack frames recover
17×3 = 51.

The corner verdicts do not depend on it at all — position is dropped (`model.py:453-457`) — which is
**another reason the corner-only C++ header is the sound thing to run live in the editor**, beyond
speed.

**Amendment:** the panel must show *which stage it is answering*, and must mark any midscreen verdict
as conditional on the pushback rule. For Kung Fu Man the two stages disagree (TERMINATING midscreen,
INFINITE corner) **and both are correct**.

### The infinites are real, not artefacts of the model

Checked back against the `.cns` line by line, because a Phase-0 infinite that turns out to be a
transcription bug would invalidate the exercise.

**KFG corner — `stand_lp` into itself.** `Kung Fu Girl.cmd:756` `value = 200` with
`:761 trigger2 = (stateno=200||stateno=231) && var(5) && Movehit` **[V]** — the self-chain is
authored. `Normal.cns:48 ground.hittime = 9` **[V]**, delay 2, startup 3 → **+4 frames of slack**. And
`Normal.cns:52 ground.cornerPush.velOff = 0` **[V]** on this exact move, so there is genuinely no
corner pushback to fight. This is a real corner infinite in a shipped character.

**KFG midscreen — `stand_hk_close > stand_mp`.** Both edges exist in the source
(`Kung Fu Girl.cmd:807` and `:770` **[V]**), and the frame arithmetic closes with
`Normal.cns:121` and `:878` both `ground.hittime = 27` **[V]** against delays 13 and 8 and startups 7
and 9. Real, but see the one-unit space margin above.

**KFM corner — `stand_lp` into itself**, from the character's own 4-way light chain
`trigger2=(stateno=200||stateno=260||stateno=400||stateno=430) && (time>5||movecontact)`
(`kfma4a.cmd:569` **[V]**). Midscreen it dies to its own pushback; against the wall nothing stops it.
This is `corner_only.json`'s shape, arrived at from the CNS rather than constructed.

### The hardest single finding: cancel delays are not zero

The draft schema assumed MUGEN chain cancels are true cancels (delay 0) because the `.cmd` fires
`ChangeState` the moment `MoveHit` is true. **That is wrong for Kung Fu Girl and it would have
invented infinites everywhere.** Every chain and special cancel is *additionally* gated on `var(5)`,
and `System.cns:3007-3027` authors `var(5)`'s opening frame **per source state** as a 17-row table of
`AnimElemTime(N) >= 0 && Movecontact` **[V]**. So `delay = (tick element N begins) − startup`, and it
ranges from **2** (`stand_lp`) to **20** (`stand_hp_far`). Authoring these as 0 would have handed the
character 2-20 free frames on *every* edge.

The same table explains a structural fact that looks like a transcription omission and is not:
`crouch_hk` (state 450) has no outgoing chain because **it has no row in that table** **[V]**.

### The control run is clean

`python -m comboprover survey C:/rw/examples` — 8/8 ran, 0 failures, 0 `Unknown`, 4 infinite /
4 terminating, matching each example's designed intent. The odd results above are our characters, not
our tools.

---

## 6. WHAT WAS NOT DONE

### 6.1 Not built

- **The ~200-line `ComboProverAdapter` does not exist.** Phase 0 forbade engine code, so the C++
  numbers came from a throwaway harness generated from the JSON into the scratchpad. **The adapter's
  ceiling-clamping (§5) is the first real correctness decision of Phase 1 and it is not yet made in
  code.**
- **Per-frame hit/hurt/push boxes are not transcribed** for any of the 59 moves. 59 moves × ~10
  elements × ~3 boxes is a hitbox-editor job, and no prover field depends on them. The 4×`int16`
  sub-unit shape was validated against `.air:746-764` for one move while drafting the schema.
  `ARCHITECTURE.md:328` already budgets the editor in Phase 3; nothing here changes that estimate,
  but nothing here validates it either.
- **Ground-truth validation (`§5.5` item 4) is impossible until Phase 3.** Nobody has executed a
  printed loop. That is the contribution no offline tool can produce and it remains entirely unearned.

### 6.2 Not transcribed

18 of KFG's 43 attacking states (throw, Heavy Smash, six jumping normals, the medium/strong palm and
knee variants, EX Knee and the EX Shuffle chain); KFM's entire strong-variant special set (`var(5)`,
a second 8 moves) and its `var(3)=2` move list. Coverage was chosen for **complete cancel structure
over breadth**, which is the right trade for a termination question and the wrong one for a fit
percentage — the 39% is measured over a sample biased toward moves with interesting cancels.

**KFM has no blockstun anywhere.** `guard.ctrltime` is authored as
`id + 0*(var(31):=0 || var(30):=510562)` (`kfma4a_cns.txt:210`) — the character abuses the field as an
assignment expression to stash hit-spark coordinates, and its nominal value is the player id. Every
KFM reaction block carries `blockstun_ticks: null`. **No landing frames for air normals** were
derivable either, so no jump-in link edges were authored; inventing one is the error `gap.py:29-33`
says the project cannot afford.

### 6.3 Estimated, not derived — the two numbers, named

Everything else is derived with a `.cns`/`.air` citation carried per-field in
`move.engine.derivation`.

1. **Pushback, every move in all three characters.** `|ground.velocity| × ground.slidetime / 2`,
   half away from zero. **Not derivable from MUGEN at all** — it records a velocity and a friction,
   never a displacement. This is the weakest number in the corpus **and it is the number the
   midscreen verdict turns on** (§5).
2. **Hitstun decay.** Not present in MUGEN. Shipped as `none` for KFG and AOF2 after the house rule
   was measured and rejected (§5, D8). KFM ships the house rule with the floor lowered; its verdict
   is unchanged without decay, but its dead-cancel list is not.

One further honest hole: **"no loader ever rounds" is not fully achievable.** KFM's
`velocity.walk.fwd = 2.4 px/tick` (`kfma4a3.txt:32`) is sub-pixel and cannot be `(integer px)/100`.
The engine stores 614 sub-units/tick exactly; the prover's `walk_speed 0.024` goes through
`model.py:465,468`'s `int(round(value * 100))` **[V]** and lands on 2 px/tick, losing 0.4. **The loss
under-states the attacker's mobility, which can hide an infinite but cannot invent one** — sound, but
it is a documented hole in the quantization claim, not an absence of one.

### 6.4 What would sharpen the answer, in priority order

1. **Derive pushback instead of estimating it.** Ikemen GO is open source; instrumenting a build and
   measuring displacement directly would convert every midscreen verdict from conditional to derived.
   **This is the single most valuable measurement not taken**, and it is a day of work, not a phase.
2. **Re-derive KFM `reikyaku`'s startup.** Its anim uses `Clsn1Default`, which `mugen_cns.py:304-330`
   explicitly says means the animation cannot answer when the move becomes dangerous. Its startup 4
   is the `HitDef`'s `time=0` plus 3 prep ticks and its active 2 is an importer fallback, not data. A
   4-frame launcher is exactly the shape that fabricates an infinite. It does not cause the KFM corner
   infinite (`stand_lp` does), but it is the number to re-derive first.
3. **Transcribe a fourth character from a non-MUGEN tradition.** All three characters are MUGEN, which
   is the only fighting game whose combat logic is open, readable data — that is why the corpus is
   what it is, and it is also a sampling bias. A modern commercial character would exercise real
   proration and juggle systems, which KFG *disables* (`System.cns:7`, `:2590-2593`) and AOF2 does not
   have. **Juggle is dead data in all three files**, so the juggle half of the resource vocabulary is
   entirely untested by Phase 0.
4. **Re-run the fit measurement after Phase 5**, as `§5.5` item 1 already requires. The prediction to
   hold Phase 5 to: the 22 vocabulary hatches drop to 0 by schema v2, and group G's opponent
   predicates drop to 0 by the trigger language. `ARCHITECTURE.md:346` sets that bar at ≥80%.

---

## 7. CORRECTIONS TO THE WORKING NOTES

A decision record is worth only its citations, so the two that drifted are recorded rather than
silently fixed.

| claim | notes said | verified |
|---|---|---|
| KFG `stand_mp → stand_hk_close` selector | `Kung Fu Girl.cmd:804` | `:802` (`value`), `:807` (`trigger2`) |
| KFG `stand_hk_close → stand_mp` selector | `Kung Fu Girl.cmd:766` | `:765` (`value`), `:770` (`trigger2`) |
| prover's reach scaling | `model.py:466` | `:465` (`scale = 100`), `:468` (`int(round(...))`) |

Every other citation in this document was re-read at the cited line.

One schema defect was found and fixed during Phase 0 by validating the schema against its own
characters: a draft required `cancel_window_ticks` to have `minItems: 2`, but the AOF2 thug's moves
correctly author `[]` because they have no cancel window. **A schema that cannot say "none" forces an
author to invent a value.** Now `maxItems: 2` with the empty case documented; all three files validate
clean against draft 2020-12.

---

## 8. DECISION

**Proceed to Phase 1 as planned. `D2`, `D7`, `D8` and `§5` stand, with these amendments:**

1. **Do not freeze schema v1.** Add the nine fields in §4 before Phase 3 begins. *(Blocking.)*
2. **Strike "quantize meter to bars" from `ARCHITECTURE.md:290`.** Replace with 1 unit = 10 MUGEN
   power, exact by GCD, chosen on D8 exactness grounds. `Unknown` never occurred.
3. **Forbid `decay.kind: "multiplicative"` in the schema**, and require `decay.floor ≤ min(hitstun)`
   asserted at load. This closes D8's one-frame gap outright rather than mitigating it.
4. **The adapter clamps resource ceilings in its own loop.** The `meter_room` alternative is unsound
   in the dangerous direction and is rejected on the record.
5. **Grow Phase 5's scope to an opponent namespace** (`p2statetype`, `p2movetype`, `p2bodydist`), and
   **add a separate authored artifact for AI selection policy** — it is not an escape hatch and D7
   does not cover it.
6. **The editor panel must name its stage**, mark midscreen verdicts as conditional on the pushback
   estimate, distinguish the two causes of a missing ranking certificate, and show the pre-decay
   dead-cancel list. Corner runs live in C++; midscreen runs async with a budget and a
   cancel-and-supersede path.
7. **Resource order is a build-wide contract.** Record it in `§5.2`'s loss table.

**A negative result here would have been a good outcome delivered cheaply. This is a mixed one: the
approach is sound and the artifact we promised to freeze is not ready.** Both halves are load-bearing
and the second one is what the week bought.
