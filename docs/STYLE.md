# Style guide

Verified: 2026-08-17 @ 9f518c2

How code is written in this engine. Every rule below is descriptive — it was
read out of the existing source, not invented — so following it makes new code
look like it belongs, and breaking it should be a deliberate decision you can
defend.

Its companion is [MAINTENANCE.md](MAINTENANCE.md), which covers the workflow
around a change rather than the change itself.

---

## Comments explain WHY, and name the bug they prevent

This is the strongest convention in the repository and the one worth protecting.
A comment here is not a restatement of the code. It is the reason the code is
shaped that way, and usually the specific failure that shaped it.

```cpp
// EDGES, not centres, decide what counts as the next row. A candidate is
// "below" only if it starts at or after where the current element ENDS, so
// same-row siblings are not below one another by construction -- which is
// what makes a row of chips cost one press to pass rather than three.
```

That comment is worth more than the ten lines under it, because the ten lines
are re-derivable and the reason is not.

**Write the comment when the reason is still in your head.** Nobody
reconstructs it later. If you spent an hour finding out why the obvious version
does not work, that hour belongs in the file.

**Long is fine.** Header comments here run to thirty lines and open with the
contract, the failure mode it exists to prevent, and what the alternatives cost.
Length is not a smell; length with no information is.

**Name the road not taken.** "Refusing to break it is the other defensible
choice and it is the wrong one here, because…" is more useful than the decision
alone — it stops the next person re-litigating it, or worse, silently changing
it back.

**Shout the load-bearing part.** A few words in CAPITALS to mark the sentence
that actually matters is normal here. Use it for the one thing a reader must not
miss, not for emphasis generally.

---

## Tests

### Name the property, not the function

```cpp
TEST(UINavRepeat, ANewDirectionFiresAtOnceThenPausesThenRuns)
TEST(UIRepeatAbsentSlot, ASlotThatFillsPicksItsBindingsBackUp)
TEST(SceneLoaderAsync, TheOutgoingSceneSurvivesUntilEveryModelHasSettled)
```

The name is a sentence about behaviour. A reader scanning failures should learn
what broke without opening the file.

### The failure message is for a stranger at 2am

Not `EXPECT_EQ(a, b)`. Say what went wrong and why it matters:

```cpp
EXPECT_FALSE(loader.DrainPendingSwap())
    << "the swap ran with models still in flight";
EXPECT_LE(chipStops, 1)
    << "the quality row cost more than one press: " << trail;
```

When a value would help, put it in the message. A failure that names the actual
sequence beats one that names a number.

### Assert what must NOT happen

The strongest tests here are the negative ones, because they pin the decision
rather than the implementation:

- balance may not change the line count;
- a soft hyphen must be inert while `hyphens: none`;
- the pad's prompt must not change when a text field takes focus;
- gating an absent slot must not silence the present one.

### Test the shipped assets, not a convenient fixture

`ShippedHud` and `ShippedMenu` stand up the real `.cxml` and `.cstyle` files, so
a typo in an asset fails the suite instead of shipping. That only works if the
harness matches what actually ships — `ShippedMenu` once hand-built its component
and left `scale` on the default, so every geometry assertion in that file had
been measuring a configuration no player would ever see.

**If a rig differs from production, it is testing fiction.** Load the scene file.

### Know what your test cannot see

Most UI tests run with **no font**, so text measures zero and anything about
wrapping is inert there. Word wrap needed tests in the GL-fixtured file before it
had any coverage at all. Ask what your test would still pass with the feature
removed.

---

## Diagnostics

### Name what exists

An unknown name is answered with the list of names that do:

```
data source 'scene' has no property 'menuLoading'
  (has: health, score, lowHealth, playerName, ...)
```

Without reflection, a typo is otherwise indistinguishable from a value that
never changes. This is the highest-value class of diagnostic in the system.

### Report once, on the path that can act

Per-frame diagnostics are a per-frame allocation for a condition nobody can fix
without a reload. Latch them, and re-arm only when the situation genuinely
changes.

### Refuse loudly at load, degrade quietly at runtime

A malformed stylesheet or an unknown attribute is a **load error** naming the
line. A missing font, a missing GL context, a model that failed to import is a
**degradation** — boxes still lay out, the scene still loads, the collider is
still real. Nothing crashes because an artist deleted a file.

Where behaviour degrades, say so in a report the host can act on
(`SceneLoadReport::failedModels`) rather than only in a log line.

---

## API shape

**Equality-gate every setter.** `UIDataSource::SetNumber("health", v)` called
every frame costs one compare and wakes nothing. This is what makes the
push-don't-poll model viable, and it is why publishing prompt glyphs every frame
is free.

**Push, do not poll.** A polled property has no version to compare, so the
binder must re-apply every binding on that source every frame. `Notify::Poll` is
`Observe`'s default — pass `Notify::OnWrite` and call `MarkChanged` unless you
genuinely cannot.

**Return whether anything happened.** `bool SetValue(...)`, `bool Nudge(...)`,
`bool UpdateNav(...)`. Callers stay equality-gated, and a host can tell an
unhandled input from a handled one.

**One implementation of a concept.** The pad's B and the keyboard's Escape both
call `UIDocument::Back`. Two copies of "what backing out means" *will* drift —
and did, for one frame, until the scope sync was in both paths.

**Degrade to the simpler behaviour, never to nothing.** `RequestSwapAsync` with
no `JobSystem` *is* `RequestSwap`. A slider that declares only `key-step` keeps
the behaviour it always had. Adding a default must not break a document that
never mentioned it.

---

## Constants and magic numbers

Name them, put them where they are used, and say what they are in terms of:

```cpp
// The idle window has to clear the PAD's auto-repeat delay (0.40s in
// UINavRepeater) or a held d-pad would reset its own run between the first
// press and the second and never accelerate at all.
```

A constant whose value is coupled to another constant must say so. That is how
the next person avoids halving one of them in isolation.

---

## Threading

The contracts are narrow and absolute:

- **Worker threads** must never touch GL, the entt registry, or ImGui.
- **`onComplete` runs on the main thread**, inside `pumpCompletions`, with the
  GL context current. Uploads belong there.
- **Closures own their transient state.** Capturing a longer-lived object is
  safe only because of the drain guarantee at shutdown; anything submitted
  outside that window must be fully self-owning.

If work cannot leave the main thread, say why in the header rather than leaving
it to be rediscovered — the registry being single-threaded is *why* async scene
loading warms assets instead of parallelising the load.

---

## Authoring assets

`.cxml` and `.cstyle` are content, and their comments are documentation for the
person editing them. The shipped menu explains its own traps at the top of the
file — which units reject a suffix, which property writes all four insets, what
the parser reports the line of. Keep that up.

Two rules the parser will not forgive:

- `gap`, `flex-grow`, `flex-shrink` and the insets are **numbers** and reject a
  unit. `width`, `height`, `border-radius`, `border-width` are **lengths** and
  require one. A wrong unit is a parse error that kills the whole sheet and
  leaves the previous one running.
- The parser reports the **rule's** line, not the declaration's. Read every line
  in the block it names.

---

## What not to do

- **Do not add a feature the manual then lies about.** Same commit.
- **Do not silence a diagnostic to make a test pass.** The 42 errors the shipped
  menu emitted were real; the fix was the binder, not the assertion.
- **Do not write a test that cannot fail.** Prove it by reverting.
- **Do not leave a rig that differs from production.** It tests fiction.
- **Do not paper over a sharp edge you just found.** Either fix it, or write the
  explanation into the file so the next person recognises it — the two-way
  binding that clobbers an unpublished edit is documented in the test that hit
  it, because the product behaviour is correct and only the test was fragile.
