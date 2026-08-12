# Maintenance guide

How to change this engine without breaking it, and how to keep its documentation
true. Everything here is a practice the repository already follows — it was
written by reading the code, not by deciding what would be nice.

Its companion is [STYLE.md](STYLE.md), which covers how to write the code once
you know what you are changing.

---

## The loop

Every change, however small, goes through the same four steps.

**1. Write the failing test first, or as close to first as the change allows.**
Not for ceremony: a test written after the fix tends to assert what the code now
does rather than what it should do. See [STYLE.md](STYLE.md) for what a good one
looks like here.

**2. Make it pass.**

**3. PROVE IT by reverting the fix.** Put the old behaviour back, rebuild, and
watch your test fail *with the message you wrote for it*. This is the single
highest-value habit in this repository, and it catches two things nothing else
does:

- a test that could never have failed — a slider test that "moved by one notch"
  when 0.5 already *was* a multiple of the step, an assertion whose value was
  correct before the change;
- a test that fails for a different reason than you think, which means the
  diagnostic you wrote will mislead the next person.

Then restore the fix and confirm the suite is green again. `grep -c REVERTED`
before you commit — a stray marker left in is worse than no proof.

**4. Build all four configurations and run the whole suite.**

CI (`.github/workflows/ci.yml`) does the first part of this for you on every push
and PR, but it is deliberately NOT a substitute for running the suite locally:
the required job excludes the 11 tests labelled `gl` (a GitHub runner has no
OpenGL context) and the `perf` budget test (a shared vCPU cannot measure it).
Those eleven cover the render passes, the post-process chain, IBL and the UI
pass -- which is to say, the places where failures are silent. Run them.

```bash
ctest --preset x64-relwithdebinfo-tests            # everything, locally
```

```bash
ctest --preset x64-relwithdebinfo-tests -LE "perf|gl"   # what CI gates on
```

If you add a test that creates a GL context, add it to the `gl` label list in
`tests/CMakeLists.txt` -- otherwise CI will try to run it headless and fail for
a reason that has nothing to do with your change.

```bash
cmake --build --preset x64-relwithdebinfo-tests
```

```bash
ctest --preset x64-relwithdebinfo-tests --output-on-failure
```

The four presets are `x64-debug`, `x64-release`, `x64-relwithdebinfo` and
`x64-relwithdebinfo-tests`. Only the last builds tests; the first three build
the Editor, both Players and the Cooker.

**Building only the test config is not enough.** A missing symbol shows up when
the *Player* links, not the Engine — a deleted `Font::AppendUTF8` compiled and
tested clean and failed only in `PlayerMain.cpp`. Configurations disagree about
what they instantiate.

---

## Keeping the documentation true

`docs/manual/` is a promise. A manual that is 90% right is worse than none,
because the 10% costs a debugging session each time somebody trusts it.

### The audit is adversarial, and that is the point

The process that works here, and has now found drift twice at scale:

1. **Fan out readers**, one per manual page or subsystem, each told to report
   only statements the source *contradicts* — not missing sections, not style.
2. **Hand every claim to a different agent whose job is to REFUTE it.** Default
   to refuted when uncertain, or when the claimed "reality" is itself wrong.
3. **Spot-check the survivors yourself** against source before acting on them.

The most recent run is preserved verbatim in
[AUDIT_FINDINGS.md](AUDIT_FINDINGS.md) -- 12 readers, 85 agents, 52 findings
that survived refutation, all since fixed. Keep it: the WHY sections are the
best record this repository has of what actually goes wrong here, and several
of the invariants below were written from them. If you run the process again,
file the results the same way -- one entry per finding, the verifier's
reasoning intact, a marker added when it is closed.

Step 2 is what makes it worth doing. Reviewers are confidently wrong often
enough that an unfiltered list wastes more time than it saves; a refutation pass
turns a pile of maybes into a short list of facts. Step 3 is not optional
either — of 24 confirmed items in one pass, the four riskiest were re-checked by
hand and all four held, but that was worth knowing rather than assuming.

### What drift actually looks like

Real examples, so you know the shape:

- **A default changed and the prose did not.** `Notify::Poll` is `Observe`'s
  default, and the manual's per-frame cost claim silently stopped holding.
- **A restriction was lifted and the prose kept it.** `value` and `bind-value`
  became legal on `<Slider>`; the attribute table still said TextField-only.
- **A feature landed and made a sentence false.** "no built-in behaviour, not
  even Enter-to-click" stopped being true the day `ActivateFocused` shipped.
- **A list got a new member.** The Paint property row listed three of ten.
- **Something was described as reported that is not even detected.** At-rules
  were listed under "reported as errors"; the parser has no concept of `@`.

The last kind is the most expensive, because the manual actively tells you the
system will protect you.

### When you add a feature

Update, in the same commit:

- the manual page that now says something false — search for the *old*
  behaviour, not the new feature name;
- **"Not there yet"** on that page. A gap you just closed must stop being
  listed, and any gap your change *created* must start being;
- the file inventory table, if you added a header;
- the header comment of anything whose contract you changed.

---

## Invariants that keep biting

These are the ones that have cost real time. Each is enforced somewhere, but the
enforcement is easy to route around.

### Closed lists that must be updated together

`EntitySnapshot` is a **closed list**. A new ECS component must be wired into:

- `SceneSerializer` (save *and* load), and
- the editor's undo `capture` / `apply` / `snapEq` (`Editor/src/UndoHistory.*`).

Miss the second and the component silently vanishes on undo — no error, no
crash, just a field that resets.

### Authored paths are untrusted

Anything from scene content — a model, a script, a clip, an HDRi, markup, a
stylesheet — goes through `PathIsContained` **before** the file is opened.
Absolute paths and `..` are refused. These paths flow into parsers with a
history of memory-safety bugs.

### The staged asset trap

`cmake/stage_runtime_assets.cmake` re-copies **subdirectories** every build, but
seeds top-level `*.json` only when **missing** — because the editor writes those
back, and a blind copy would revert saved scenes on every build.

So an edit to `Exported/scene.json` or `Exported/menu.json` in the source tree
**never reaches a build directory that already has one**. It now prints
`[stage] KEEPING …` when the two differ; heed it. A `uiScaleMatch` fix once sat
unreachable for an entire debugging session behind this.

Tests read a *third* copy, under `out/build/x64-relwithdebinfo-tests/tests/Exported/`.
Verifying against the wrong copy is a genuine trap — check which one you edited.

### UI: authored units versus real pixels

`Style` is in **authored** units and scales. `ComputedLayout`, scroll offsets and
pointer positions are **real** pixels and do not. `el->style().width = Px(100)`
is authoring; `el->SetScrollOffset({0, 100})` is geometry. Mixing them produces
layouts that are correct at exactly one window size.

### UI: the structure epoch is process-wide

`UIElement::structureEpoch()` is global. A tree that adds or removes children
makes **every binder in every document** re-collect. That is why `repeat=` is a
fixed pool the data slides through, and why a hot reload in one document costs
every other one a re-collect.

### UI: re-cascading erases raw style writes

`UIStyleSheet::Recascade` assigns a fresh `Style{}` and re-applies rules and
bindings. Anything written straight into `style()` from C++ is lost on the next
state edge — a `:hover` is enough. Author it as a class, a binding or a rule.

### `Transform::modelMatrix` is a CACHE, not the pose

Only `Scene::UpdateTransforms` writes it, and hosts run that once per FRAME --
after the whole fixed-step loop. Anything that reads it mid-tick sees last
frame's pose, and right after a load it sees identity.

Read it only when `dirty` is false; otherwise resolve the live chain:

```c++
const glm::mat4 world = t.dirty ? ResolveWorldMatrix(reg, e) : t.modelMatrix;
```

This has now bitten four times: physics bodies built at the origin at unit
scale, light gathering, kinematic bodies chasing a stale target, and a scale set
during a tick being reverted by the next step of the same frame.

### One predicate, one place

When two pieces of code have to agree about whether something happens, do not
write the condition twice -- give one of them a way to ASK the other.

The LDR post chain counted "enabled effects" in the Renderer while each pass
also required a valid shader; one failed shader compile made the count too high,
so no pass ever saw itself as last and the whole frame was left in an off-screen
buffer. The count now calls `IRenderPass::wantsLdrSlot`, which is the same
expression `execute` guards on. The test file had a third copy of the predicate
and therefore agreed with the bug.

If a comment says "MUST match X", that is the smell: make it call X.

### A class added is not a class applied

`UIElement::AddClass`/`RemoveClass` only record the class. The cascade has no
undo, so nothing restyles until something re-runs it -- `UIBinder`'s class
branch does, and so does `RecascadeAfterClassChange` for widgets managing their
own state. Two comments asserted the opposite and `.selected` on a tab header
was inert for as long as they did.

### State that outlives a scene swap

A swap replaces the registry, so anything holding handles or a last-rendered
value has to be reset in `Application::Run`'s `swappedThisFrame` block: the
fixed-step accumulator, pause and time scale, the camera director, and the CSM
cascades. The dirty-caster flow cannot express wholesale replacement -- the
departed casters have no `Transform` left to mark dirty -- so shadows need
`forceCSMUpdate` outright. A host caching anything else per scene resets it
through a `SceneLoader` observer, next to where the loader is created.

### Never add a fast-math flag

`/fp:fast`, `-ffast-math`, `-Ofast`, `-ffp-contract=fast` and their relatives
license the compiler to reassociate arithmetic, to contract `a*b+c` into a single
FMA (one rounding instead of two), and to assume no NaNs. Any of those makes the
simulation stop being bit-identical between two machines, which is the property
the whole fighting-game plan rests on — see
[ARCHITECTURE.md](ARCHITECTURE.md) D3.

Nothing fails loudly when one is added. The build succeeds, every test passes,
and two players drift apart. So it is checked mechanically:

```bash
python scripts/check_determinism_flags.py
```

CI runs it twice — once in seconds over the build configuration we author, and
again after configuring, over the GENERATED `build.ninja` and
`compile_commands.json`. The second pass is the one that catches a flag arriving
from a dependency's INTERFACE options, which is not hypothetical here: that is
exactly how `Jolt::Jolt`'s `_HAS_EXCEPTIONS=0` once reached every engine TU.

Two things worth knowing about it. It proves the **absence** of dangerous flags,
not the presence of `/fp:precise` — MSVC defaults to precise and emits no flag,
so there is nothing to assert positively. And `--self-test` exists because a gate
nobody has watched fail is not a gate; CI runs that first.

If a flag is genuinely wanted, put `det-ok` in a comment on that line with the
reason. The exemption then shows up in review instead of in a desync six months
later.

### Renderer

- A change to the **caster set** must force a CSM rebuild; a cached cascade
  against departed geometry renders shadows for objects that are gone.
- Transforms decompose through `DecomposeTRS` only.
- GLAD is **per-module**: a GL function pointer table is not shared across DLL
  boundaries, and the editor runs two renderers.
- Near/far comparisons use a **relative** epsilon.
- Ordering keys use the **entity index**, never the raw handle, or the order
  changes across a save/reload.

---

## Before you commit

- [ ] Four configurations build clean.
- [ ] 53/53 (or whatever the current count is — it should only ever go up).
- [ ] The fix was proved by reverting it, and no `REVERTED` marker survives.
- [ ] The manual page that now says something false has been fixed.
- [ ] "Not there yet" reflects what you closed and what you opened.
- [ ] Header comments match the contract you changed.

Write the commit message for the person who runs `git blame` in two years and
wants to know **why**. State what was wrong, what it cost, and what you decided
instead — including the choice you *did not* take and the reason. If a test
caught you mid-change, say so; that is the most useful sentence in the message.

---

## When something is wrong and you cannot see why

In rough order of how often it pays off here:

1. **Check which copy you are running.** Source, staged-next-to-the-exe, or the
   test copy. This is the single most common false trail.
2. **Check whether the thing even ran.** A diagnostic that cannot fire, an
   observer never subscribed, an action registered after the document bound.
3. **Reverse the assumption.** Ask what would have to be true for the behaviour
   you are seeing to be *correct*, then test that. The absent-repeat-slot bug
   looked like a binder failure and was a deliberate empty write.
4. **Read the header comment.** The reason is usually already written down by
   whoever hit it last.
