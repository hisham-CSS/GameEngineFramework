# ADR-009 — How many fighters a match has, and who "the opponent" is

**Status.** Proposed 2026-08-16. Constrains the `ADR-005` §4 **P2** state expansion, which has not
yet been written, and must be settled *before* it rather than after. Changes no behaviour on its own.

**The question**, from the author, arriving with the instruction to do the P2 expansion:

> *"lets keep in mind this specific fighting game is going to be sf6 like - but I would like to build
> this system so I can build things like tag fighters or other style 2.5D fighters in the future so I
> can keep building new fighting games with this fighting game base - this can also allow me to build
> other game modes that enable these type of gamemodes inside a single player mode (like a world tour
> mode similar to SF6)"*

**The finding this document exists for: that requirement is not a later concern, it is a P2 blocker,
and it is one for a reason that has nothing to do with tag fighters.**

`ADR-005` §3 is the rule: `GameState` is a wire contract whose size, per-field offsets and
cross-toolchain golden checksum are asserted, *"so the cost of a kernel feature is not just the
feature — it is a re-golden and a re-proof … Do them as one deliberate state expansion, with one
re-golden, reviewed once."*

**`Fighter p[2]` (`GameState.h:103`) is the largest state-layout change this project will ever
make.** Doing P2 with two slots and widening afterwards pays the re-golden **twice**, and pays the
second one against a kernel that by then has seven more systems written against `1 - a`. The
requirement arrived exactly one turn before the batch it belongs in, which is luck, and the only way
to waste that luck is to defer it.

---

## 1. What is actually hardcoded, measured rather than assumed

`grep` over `Games/`, `tests/`, `Editor/` and `Player/` finds **424 sites** mentioning `p[0]` or
`p[1]`, across 32 files. That number is misleading in the helpful direction, and the distinction is
the whole reason this is affordable:

**Widening an array is source-compatible for indexed access.** `state.p[0]` and `state.p[1]` keep
compiling and keep meaning what they meant. Nothing in a test that pokes two fighters and asserts
about them has to change. What breaks is only code that hardcodes the **count**, or that derives the
opponent by **arithmetic on the slot index**. That set is small and it is nearly all in one file:

| site | what it assumes |
|---|---|
| `GameState.h:103` `Fighter p[2]` | the count |
| `GameState.h:123` `Input p[2]` | the count |
| `Combat.h:331` `FighterData p[2]` | the count |
| `Combat.cpp:280-333` | **three loops, `for a < 2` and `const int d = 1 - a`** — the opponent *is* arithmetic |
| `Simulate.cpp:107-118` | two `stepFighter` calls, and facing derived from `p[0].posX <= p[1].posX` |
| `Simulate.cpp:151-156` | `ResetMatch` writes two positions and two healths |
| `ComboWatcher.cpp:347` | `1 - a` |
| `FightSession.h:88,336`, `Replay.h:278,500`, `MatchBuilder.h:357-358` | two of something, per side |

Nine places, not 424. **The expensive part of this change is deciding what replaces `1 - a`, not
typing it.**

---

## 2. Eight slots, and the number is not chosen — it is measured off a field that already exists

`Fighter::alreadyHitBits` is a `std::uint8_t` and `Combat.cpp:29-31` indexes it as:

```cpp
std::uint8_t bitForSlot(int slot) { return static_cast<std::uint8_t>(1u << slot); }
```

**One bit per slot, in eight bits. `kMaxFighters = 8` is the capacity the multi-hit guard already
has**, and a ninth slot would shift a bit out of the mask silently — an attack that has already
connected reads as one that has not, which is the infinite-combo bug `Combat.cpp:284-288` exists to
prevent, reintroduced by a constant.

So the number is a fact about the code rather than a guess about the genre. It is also enough for
every shape actually asked for:

| shape | slots |
|---|---|
| SF6-like 1v1 — *the game being built* | 2 |
| Tekken Tag / 2v2 | 4 |
| KOF, MvC-style 3v3 | 6 |
| 4v4 | 8 |

**What eight does not buy: projectiles.** `GameState.h:80-84` already anticipates *"D4's 32
projectile slots each need their own bit"*, and 8 fighters plus 32 projectiles is 40 bits, which
does not fit a `uint32` either. **A projectile system needs its own mask regardless of what is
decided here**, so widening `alreadyHitBits` now would buy nothing and cost three bytes in the group
`GameState.h:90-93` warns about. Recorded so the next reader does not re-derive it.

---

## 3. Two teams, but the *test* is not "two"

`Fighter` gains `team`. The opponent test becomes:

```
a may hit d   iff   a.team != d.team   &&   d.active
```

**Written as inequality rather than as `d.team == 1 - a.team` deliberately.** Both are correct for
two sides; only the first is still correct for three, and the difference costs nothing today. A
free-for-all is **not** in scope and is not being built — but the shape of the test is the one place
where allowing for it is free, so it is spent there and nowhere else.

What genuinely assumes two sides, and is therefore where a free-for-all would have to pay:
`GameState::roundsWon[2]` and the round-end rule that reads it. That is the honest boundary and it is
one field wide.

**`active` is state, not data.** A benched tag partner keeps its health and its meter and has no
position, no boxes, and no turn — and a tag swap is a tick writing that field, which is precisely
`GameState.h:24-26`'s rule for what belongs in the state. An inactive fighter is skipped by
movement, by hit resolution in both directions, and by facing.

---

## 4. Facing, which is the one rule that genuinely has to be rewritten

`Simulate.cpp:112-118` derives facing from a comparison between exactly two fighters. With N there is
no such comparison, and the replacement has to satisfy a constraint the current rule gets for free:
**it must not depend on iteration order.**

> Each active fighter faces the **nearest active opposing fighter**, measured by `posX` difference,
> **ties broken by the lowest slot index**, evaluated after every fighter has moved.

The tie-break is not decoration. Two opponents exactly equidistant is a reachable state — it is the
opening position of a symmetric 2v2 — and *"whichever the loop saw first"* is the shape of desync
this repository has already been bitten by twice, recorded in `Simulate.cpp:103-106` as the
hash-ordering hazard that hit `SimplePhysicsBackend` and `ScriptWorld`. **An index tie-break is a
rule two peers can both follow.**

And the trap `ADR-006` §3.4 already named applies here word for word: compute "which way is the
opponent" from **the sign of the position difference**, never from a stored `facing` multiplied into
a coordinate. `GameState.h:67-69` says why.

---

## 5. World Tour is a `ResetMatch` problem, and that is the whole of it

The single-player-mode half of the request looks like a large ask and is not, because the fight
kernel is already headless, already has no engine dependency, and is already driven by a host that
owns no clock (`ADR-005` P0). A World Tour fight is *a fight with different starting conditions and
an outcome someone reads*.

What actually blocks it today is four lines. `Simulate.cpp:151-156`:

```cpp
state.p[0].posX   = -100 * kSubUnitsPerPixel;
state.p[1].posX   =  100 * kSubUnitsPerPixel;
state.p[0].health = 1000;
state.p[1].health = 1000;
```

**Every fight in the game starts at ±100 pixels with 1000 health, because the only way to start one
says so.** An RPG progression that raises a character's health, a story fight that begins cornered,
a handicap, a 3v3 with three start positions — none of them is expressible, and none of them needs a
new *system*.

So `ResetMatch` gains a `MatchSetup` overload carrying per-slot starting conditions, and the existing
two-argument form becomes the default 1v1 setup expressed in terms of it — **one implementation, so
the default cannot drift from the general case.**

**Where per-fighter *constants* go is the other half, and it is not `GameState`.** `maxHealth` is
read-only for the whole match, so by the rule `Combat.h:11-15` quotes out of `CharacterData.h` —
*"if a tick WRITES it, it is an integer field in GameState; if a tick only READS it, it lives [in
character data] and the state holds an index"* — it belongs in
`FighterData`, on the connect-handshake side of the wire. That is also the correct place for it on
the merits: two peers fighting a World Tour battle **must** agree that this opponent has 1400 health,
and the handshake is what proves it.

---

## 6. What this costs, arithmetically

`Fighter` grows from 36 bytes to **52** (§7), and `GameState` from 80 to **436**:

```
Fighter    = 7 x int32 (28) + 8 x 16-bit (16) + 8 x uint8 (8)   = 52
GameState  = 20 byte header + 8 x Fighter (416)                 = 436
```

Both remain multiples of 4, which is the alignment of their widest member, so a conforming
implementation still inserts no tail padding — the property `test_determinism_crossplat.cpp:128-136`
does the same arithmetic for.

`test_kernel.cpp:58` asserts `sizeof(GameState) < 4096`. **436 passes with room to spare**, and the
128-slot ring costs 56 KB rather than 10 KB. The rollback budget in `ARCHITECTURE.md` D4 is a
`memcpy` bandwidth claim, and 436 bytes is still comfortably inside it.

**The honest cost is that a 1v1 match hashes six zeroed fighters every tick.** That is real and it is
accepted: the slots are zeroed by `ResetMatch`, so they are deterministic, and the alternative — a
state whose *size* depends on the match type — is a wire format with a variable layout, which is
strictly worse than 312 wasted bytes.

---

## 7. The decision

1. **`kMaxFighters = 8`**, because `Fighter::alreadyHitBits` is a `uint8` indexed by `1u << slot` and
   eight is the capacity that mask already has. Not a genre guess. A ninth slot is a silent
   multi-hit-guard failure, so the constant and the mask width are asserted against each other.
2. **`GameState::p` becomes `Fighter p[kMaxFighters]`, and `MatchData::p` and `InputPair::p` widen
   with it.** Indexed access at every existing site keeps compiling and keeps meaning the same thing;
   only the nine count-and-opponent sites in §1 change.
3. **`GameState` gains `fighterCount`.** Slots at or above it are not simulated, and `ResetMatch`
   zeroes them, so the hash over unused slots is stable.
4. **`Fighter` gains `team` and `active`.** The opponent test is `a.team != d.team && d.active`,
   written as an inequality so a third side would be a constant change rather than a logic one.
5. **Facing is "nearest active opposing fighter by `posX`, ties to the lowest slot index".** The
   tie-break is required, not defensive: symmetric 2v2 opens on an exact tie, and *"whichever the
   loop saw first"* is the hash-ordering desync `Simulate.cpp:103-106` already names twice.
   Direction comes from the sign of a position difference and never from `pos * facing`
   (`ADR-006` §3.4, `GameState.h:67-69`).
6. **Two teams, and `roundsWon[2]` is the one field that says so.** Free-for-all is out of scope and
   is not being built; the cost of not foreclosing it was one operator in §3 and it was paid there.
7. **`ResetMatch` gains a `MatchSetup` overload** carrying per-slot team, active, start position and
   start health. The existing seed-only form is re-expressed in terms of it so the 1v1 default and
   the general path cannot drift. **This is the entire kernel-side cost of World Tour-style modes**;
   the rest is a game mode, which `GameModeRegistry` already hosts.
8. **`maxHealth` goes in `FighterData`, not `Fighter`** — a tick reads it and never writes it, which
   is the read/write rule `Combat.h:11-15` quotes, and two peers must agree on it, which is what the
   connect handshake is for.
9. **`alreadyHitBits` stays a `uint8`.** Widening it buys eight slots' worth of nothing, because a
   projectile system needs a separate mask at any width.
10. **This lands in the P2 batch or not at all.** Splitting it out is the one outcome `ADR-005` §3
    forbids by name, and it is the only reason this document was written before the code rather than
    after it.

**Reversed if:** a mode needs more than eight bodies simultaneously — at which point
`alreadyHitBits` becomes a `uint16`/`uint32`, `Fighter` grows into the padding that widening creates,
and it is a re-golden. Note that *rosters* larger than eight are **not** this case: a 5v5 with three
benched per side is ten characters and never more than a few active, and what the mask counts is
slots in the state, not names on a select screen. The distinction is worth stating because it is the
likeliest way somebody talks themselves into reopening this.

**Not reversed by:** adding projectiles, which needs its own mask regardless (§2); or by a
free-for-all mode, which needs `roundsWon` and one round-end rule and nothing in §§2–5.
