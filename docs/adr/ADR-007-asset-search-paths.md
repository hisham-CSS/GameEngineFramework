# ADR-007 — Asset search paths: what a merge at copy time cannot express

**Status.** Direction **accepted** 2026-08-16, **unscheduled** — its trigger 3 has since fired
(`ADR-008`), so the work lands after [ROADMAP.md](../ROADMAP.md) M2. The split is deliberate and is
§7: the author decided the destination in the same sentence that accepted the interim, and
what is genuinely open is only *when*. Records the design so the interim cannot quietly
become the answer. Amends **`ADR-005` §1**, whose three-goal table has no row for the thing
this serves.

**The ask**, from the author, arriving in one breath with the acceptance:

> *"we should consider the editor being the base and the game being the scenes that we build
> - so rather than linking to the menu.json that is in the editor - shouldn't we be making
> our own menu.json that we load into?"*

and then:

> *"the game should own the whole front end - **i like the bandaid but we should loop back to
> the asset search-path system for a true engine behavior that I can actually ship to other
> users**"*

The first half shipped: the title owns its front end, its content lives under
`Games/UntitledFighter/Assets/`, and nothing of it sits in the engine's asset root any more.
This document is the second half — and the operative words in it are **ship to other users**,
because every failure below is a failure that only appears once somebody else is holding the
engine.

*Cited at the line each fact was read at on 2026-08-16, and re-checked against the files after
the document was written — `ADR-001` §7's discipline. `cmake/stage_runtime_assets.cmake` and
the root `CMakeLists.txt` are cited by line because they are the artifact under discussion; if
either is edited, re-read before quoting.*

---

## 1. What was built, and the single assumption inside it

`cmake/stage_runtime_assets.cmake` takes an **ordered list of source roots** and flattens them
into the one runtime `Exported/` beside the executables, later winning. The root
`CMakeLists.txt:189` sets `CSE_ASSET_ROOTS` to the engine's asset root, `:209-212` appends the
title's when the title is in the build, and `:238-244` is the single staging target that reads
it. `tests/CMakeLists.txt:64` and `:72` read the same variable, so the tests stage the tree the
game ships.

It bought exactly what it was meant to buy, and the list is short and real: the title's content
left the engine's asset root; **no loader changed**; every relative path that resolved before
resolves now, because the *runtime* layout is byte-for-byte what it was; and dropping the
title's `add_subdirectory` prunes the title's directories back out, so a general engine still
builds and still runs — the property the boundary assertion exists to protect.

Both files say in as many words that this is not the fix
(`stage_runtime_assets.cmake:14-21`, root `CMakeLists.txt:180-184`). This section is about the
*reason*, which neither states in a form you can test against.

**The assumption is that the set of roots is known at the moment this repository is
configured.** `CSE_ASSET_ROOTS` is a CMake variable, set in the one file allowed to know a
title exists, consumed by a custom target that runs at build time. That is not a limitation of
the script — it is what "merge at copy time" *means*. A copy has to know both operands.

Which gives the test that organizes the rest of this document:

> **Every case that breaks is a case where a root becomes known after that moment.**

Three of the four below are exactly that. The fourth is different in kind and is worse.

---

## 2. What breaks, concretely

### 2.1 A binary engine has no source tree to copy from

The packaging rule is `Player/CMakeLists.txt:50`:

```cmake
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Editor/src/Exported/ DESTINATION Exported ...)
```

`${CMAKE_SOURCE_DIR}` is **this checkout**. The engine's assets reach a bundle by being copied
out of the repository that built them, and the staging script is a `-P` script in that same
repository, invoked with absolute paths into it. A user holding `Engine.dll` and a set of
headers has no `Editor/src/Exported/` and no `stage_runtime_assets.cmake`, and there is no
mechanism by which they could say *"and here is my asset root"* — because saying it requires
re-running a configure of a tree they do not have.

**And it is worse than "the assets are awkward", which is why this is first rather than last:
there is no installed engine at all.** Measured, by grepping every `CMakeLists.txt` and
`.cmake` in the tree: the only install rules are `install(TARGETS Engine RUNTIME DESTINATION
.)` (`Engine/CMakeLists.txt:365`), the lua DLL beside it at `:294`, and the Player bundle at
`Player/CMakeLists.txt:49-81`. **No `install(EXPORT)`, no `CseEngineConfig.cmake`, no header
installation, anywhere.** So "distributed as a binary" is not currently a supported state of
this project, and search paths are *necessary and not sufficient* for it. §6 says what else it
takes; saying so here keeps this document from promising that one feature ships an SDK.

### 2.2 A title shipped or updated separately

Two products from one engine is the shape that shows it. The composition block links **every**
title present into the same `Editor` and the same two `Player` executables
(`CMakeLists.txt:154-160`), and staging layers **every** title's root into the same runtime
`Exported/`. There is one output directory, so there is one product. Shipping title A without
title B's content is not a build option; it is a different checkout.

And a title *update* — one new character, one balance pass — is today: re-configure the engine
repo, re-run staging, re-run install. Nothing in that sequence is a patch. Worse, the staged
tree **mirrors**: a file with no source counterpart in any root is deleted and the removal
announced (`stage_runtime_assets.cmake:176-191`, `:225-258`). That mirror is correct and was
added for a good reason — three MUGEN-derived characters that may not be redistributed
survived in every build directory that had ever seen them, ready to be bundled by the next
`install`. But it means the runtime `Exported/` is **owned by the build**, and anything that
arrives in it by another route is transient by design. A title cannot be dropped into a
shipped game because the game's asset directory is a build output, not a container.

### 2.3 Mods, which need a root that did not exist at build time

`ARCHITECTURE.md:488` already scopes this as Phase 7: *"Mod folder loading through
`PathIsContained`"*, plus running `analyse` on a mod at load and warning the player before the
match. A mod root is the definitional case of §1's test — it exists on a player's machine,
chosen by them, after everything was built.

Under layered staging the only way to add one is to copy files into the runtime `Exported/`,
where **the mirror deletes them on the next build**, announcing the removal in a message
written to make deletions trustworthy. In a *shipped* game there is no next build, so they
survive — and that is the worse outcome, because a merged copy has **destroyed the
provenance**. Once a mod's files sit in `Exported/` they are indistinguishable from base
content: they cannot be listed, disabled, ordered, or blamed.

**Provenance is not a nicety here, it is the feature.** Phase 7 wants to *analyse a mod and
warn about it*, which requires knowing which files are the mod's. And the connect handshake
hashes the loaded character POD arrays (`ARCHITECTURE.md` §4.8), so a mod that silently
overrides a character is two peers disagreeing about a hashed byte — a desync whose diagnostic
needs to be able to say *which mount* the differing file came from. A merge cannot say it,
because after the merge there is nothing left that knows.

### 2.4 Override, which is not a scheduling problem but an expressiveness one

The other three wait for a root. This one is wrong today, and the script says so itself under
the heading **THE REMAINING HOLE** (`stage_runtime_assets.cmake:119-136`): an override that
*stops* existing can leave the title's copy staged, because the path is no longer in two roots
and CMake's coarse timestamp comparison may skip re-copying the engine's file. **Measured, not
predicted** — after dropping the title's root from a two-root fixture, the staged shader was
still the title's.

That hole is a symptom. The disease is that a merge at copy time has **one destination path**,
so *"the engine's shader, but mine wins"* can only be expressed by destroying the engine's
shader in the output. Everything that follows from having one copy follows:

- **The original is unreachable.** A title cannot ask for the engine's default alongside its
  own — to fall back to it, to diff against it, to offer the player a reset.
- **The editor cannot show that a loser existed.** The Assets panel is a view over the one
  staged tree (`AssetBrowserPanel.h:21-26` over `AssetIndex`), so it shows the winner and has
  no idea it won anything.
- **The only record is a build-log line.** `[stage] OVERRIDE ...` is printed once, into a log
  that scrolls. The script's own argument for printing it is that a silent override is the
  worst failure the directory can produce — *"the engine's file is visibly right there in the
  engine's source tree, the running game disagrees with it, and nothing anywhere says why"*
  (`:99-102`). A message is the best a copy can do, and it is not much.
- **Un-overriding is not a build step.** It is deleting a file out of a build tree by hand,
  which is what `:130-133` recommends, honestly, as the fix.

Search paths do not *detect* this case. They **remove it**: nothing is copied over anything, so
there is nothing to fail to un-copy.

---

## 3. What is actually in the way, measured

The resolver is the easy half. These three are the work.

### 3.1 Thirteen read sites in twelve files — and one of them already has the seam

Every one of these turns a project-relative path into an open file, independently:
`Shader.cpp:13-27`, `Model.cpp:530` (Assimp) and `Model.cpp:504` (its textures), `Font.cpp:41`,
`SceneSerializer.cpp:350`, `ScriptWorld.cpp:249`, `UIMarkup.cpp:1158`, `UIStyleSheet.cpp:964`,
`UITextureCache.cpp:45`, `IBLBaker.cpp:252`, `WindowIcon.cpp:19`, `ProjectSettings.cpp:12`,
`ImportSettings.cpp:20`. Three more *write*: `SceneSerializer.cpp:295`,
`ProjectSettings.cpp:49`, `ImportSettings.cpp:40`. Twenty-one files in `Engine/src` between
them hold 41 literal `"Exported/..."` strings.

**The seam already exists in exactly one place, and it is the model to copy.**
`ScriptWorld.h:64-65`:

```cpp
using SourceResolver = std::function<bool(const std::string& path, std::string& outSource)>;
void SetSourceResolver(SourceResolver r);
```

with `defaultResolve_` (`ScriptWorld.cpp:237-255`) doing containment-then-open against
`settings_.scriptDirectory`. It was built so headless tests could supply sources without a
filesystem, which is the same shape as supplying them from a different mount. One subsystem out
of twelve got a resolver because one subsystem needed to be testable; the argument for the
other eleven is this document.

And the title's own loaders are outside the engine —
`Games/UntitledFighter/Data/src/CharacterData.cpp:1664`,
`Games/UntitledFighter/Game/src/Replay.cpp:868` and `:1378`. They must resolve through the same
seam, or a title has to be edited when the engine's resolution changes, and the property that
*"a second title adds lines [to the root CMakeLists] and edits nothing under Engine/, Editor/
or Player/"* (`CMakeLists.txt:152-153`) has been broken from the other direction.

### 3.2 The root's name is inside the content — 411 authored strings

Two conventions live side by side right now, and `Games/UntitledFighter/Assets/README.md:63-73`
already tabulates them:

| Written in | Resolved against | Example |
|---|---|---|
| a scene's `uiDocument.markup`, a stylesheet's `background-image` | the host's **working directory** | `Exported/UntitledFighter/UI/menu.cxml` |
| `TitleFrontEndScene()`, a character path in a mode or a panel | the **content root**, joined by the host | `UntitledFighter/menu.json` |

**A path that names its own root cannot be searched.** The first convention bakes `Exported/`
into the file, so there is no relative path left for a resolver to try against each mount.
Counted as **quoted path strings**, which is the migratable population: **408 in the engine's
asset root** — 403 of them in `scene.json` alone, plus `menu.json` 2 and `UI/menu.cstyle` 3 —
and **3 in the title's content** (`UntitledFighter/menu.json` 2,
`UntitledFighter/UI/menu.cstyle` 1). *A further handful of `Exported/` mentions live in
stylesheet and markup **comments** (`UI/hud.cstyle:5`, `:66`, `UI/hud.cxml:46`,
`UI/menu.cxml:4`, `Scripts/spinner.lua:4`); they are prose, not paths, and need no migration —
but they will read stale afterwards, which is the cheap half of this job and the half most
likely to be skipped.*

This is the migration, and it is larger than the resolver. It also has a **writer**:
`SceneSerializer.cpp:295` writes those strings back, so old scenes must keep loading while new
ones are written the new way, and every editor-authored scene sitting in somebody's build tree
was written by the old writer and will never be re-saved.

Two roads, and the second is the one to take:

1. **Migrate the strings and be done.** Clean, and it strands every already-authored scene —
   including `project.json`, which is editor-written and deliberately absent from the source
   tree (`PlayerMain.cpp:208-214`), so nobody can migrate the copies that exist.
2. **Accept both spellings for one release: the resolver strips a leading `Exported/`, warns
   once per distinct path, and the writer emits the root-less form.** The cost, stated rather
   than discovered: it makes `Exported` a **reserved first path component**, and a mount that
   legitimately contains a directory of that name becomes unaddressable. That is a real price
   and it is small, because `Exported` is this engine's own word for the thing being replaced.

The warn-once requirement is not decoration: see §4.5.

### 3.3 A model resolves its own textures relative to where *it* was found

`Model.cpp:570-571` derives `cpu.directory` from the model path it was handed and passes it to
every texture slot (`:592-594`). So resolution must return **where** a file was found, not just
its bytes.

That decides the return type before any archive question is asked: **`Resolve` yields a path**.
A resolver that returned a stream could not serve Assimp at all — `importer.ReadFile` takes a
path, and the material texture paths inside the file are relative to it. §4.4 is where this
lands.

---

## 4. The design

### 4.1 An ordered mount list, resolved at load

A **mount** is `{ id, directory, writable, trusted }`. The list is held in the **same order as
`CSE_ASSET_ROOTS`** — engine first, each title next, mods last — because that order is already
written down in two files and a second ordering convention is a bug generator. `Resolve` walks
it **backwards**, so later still wins, and the reversal lives in exactly one function.

Resolution is first-hit from the high-priority end. There is no merge, no copy, and no moment
at which two files with the same relative path are reduced to one.

### 4.2 It lives beside `PathIsContained`, and it is the only way to get a path

In `Engine/src/core/`, next to `PathSandbox.h`, because the containment check and the search
must be **one call**. Roughly:

```cpp
struct AssetMount { std::string id, dir; bool writable = false, trusted = true; };

class ENGINE_API AssetMounts {
public:
    bool Resolve(const std::string& rel, std::filesystem::path& out,
                 const AssetMount** outMount = nullptr) const;   // reads
    bool ResolveForWrite(const std::string& rel, std::filesystem::path& out) const;
    std::string DescribeMiss(const std::string& rel) const;      // §4.5
};
```

**`Resolve` performs the containment check itself, per mount, and there is no other way to
obtain a path from it.** That is the whole security argument in one sentence, and it is
available *because* all thirteen call sites have to change anyway: today each one has to
remember to call `PathIsContained`, and `UITextureCache.cpp:37-39` documents a site that
deliberately does not, relying on the stylesheet having checked earlier. A rule that depends on
thirteen authors remembering is a rule that will be broken; a function that cannot return an
unchecked path is not.

Mounts are added **by the host, in C++** — the same shape as `TitleFrontEndScene()`
(`TitleFrontEnd.h`). `GameModeContext::contentRoot` (`GameMode.h:88`) becomes a reference to
the mount list, and `PlayerMain.cpp:164-170`'s one join — already flagged in its own comment as
*"the only line that changes the day the asset search path lands"* — becomes the mount lookup.

### 4.3 Containment and search paths interact, and getting it wrong is a security bug

`PathIsContained` has two halves and they behave differently under mounts.

**The lexical half is root-independent and runs ONCE, before the search.** `PathSandbox.cpp:11-17`
refuses absolute paths, root names, root directories and any `..` component *before any
filesystem access* — deliberately, so a symlink cannot be used to slip past canonicalization
(`PathSandbox.h:25-27`). None of that depends on which base you are joining onto, so it is done
once, up front, and a rejected path never reaches the loop.

**The canonical half is per-mount, and must be run against the mount the file was found in.**
`PathSandbox.cpp:19-37` canonicalizes `base / rel` and prefix-checks it against the canonical
base. Running that against mount 0 while opening out of mount 2 approves a path contained in
neither. This is the bug to not write, and it is easy to write as an optimization, because the
lexical half has already caught everything obvious and the canonical half looks redundant. It
is not: a mod mount is a directory a **user controls**, so it will contain symlinks, and the
prefix check on the weakly-canonical result is the only thing that catches one pointing out.

**And the thing that actually changes: "contained" stops meaning "inside the install."** Today
every reachable file is under the game's own directory, so containment doubles as *the attacker
cannot reach your documents*. A mod mount at a user-scoped path is outside the install **on
purpose**. So:

> **A mount is a capability. Mounting is the security decision, and containment only enforces
> the boundary of a mount already granted.**

Three rules fall out of that sentence, and each is a thing to refuse rather than a thing to
build:

- **Content may never add a mount.** Not a scene, not a stylesheet, not a character file, not a
  mod manifest. A mount declared by content is `..` with extra steps, and it defeats the
  lexical check that the rest of this section rests on.
- **A mod mount is `trusted = false`, and the flag has to *do* something.** `Model.cpp:540-551`
  records that Assimp's importers have shipped heap overflows and that *"the only real defence
  there is not opening attacker-chosen files, which the containment gate provides."* Once a
  user can mount a directory they **are** choosing the files, and that defence is gone. This is
  a genuine cost of the feature, not an argument against it — the answer is that a mod is
  validated by the cooker before it is mounted, which `ARCHITECTURE.md:488` already half-asks
  for when it says to run `analyse` on a mod at load. §5 prices it.
- **Never resolve a write through the search path.** `ResolveForWrite` names exactly one mount.
  All three writers would otherwise write into whichever mount answered the read — the editor
  saving a scene into the title's source tree, or into a read-only mod directory. There is one
  sharp case: `ImportSettingsPathFor` (`ImportSettings.h:32`) derives `foo.png.import` from
  `foo.png`, so a sidecar for an asset resolved out of a read-only mount has nowhere to go. It
  goes to the writable mount under the same relative path, which shadows nothing, because a
  `.import` is not an asset and no loader searches for one.

### 4.4 Directories now; archives are a mount *backend*, and later

`Resolve` returns a path (§3.3), and a path into a zip is not a path. Three roads:

1. **Directory mounts only.** Everything above works; no loader learns anything beyond
   resolve-then-open.
2. **A stream interface.** Every loader changes twice — once for search, once for streams —
   Assimp needs an `IOSystem`, stbi needs its callbacks, and `Model.cpp:570-571`'s texture
   derivation has nothing left to derive from.
3. **Archive mounts that extract to a cache directory on first use**, with `Resolve` returning
   the cache path. Bounded, and it keeps road 1's loaders.

**Take 1, and keep 3 open by returning a mount alongside the path** — that pointer is where an
archive backend later hangs, and adding it now costs one out-parameter. `AssetIndex.h:19-23`
already lists *"a pack-file/VFS backend for shipped builds"* among the things this seam exists
to make possible; the ordering claim this ADR adds is that it comes **after** the mount list,
because a VFS with one mount is not a VFS. Archives buy load time and single-file
distribution, and neither is a problem this project has measured.

### 4.5 A miss names every mount it searched

A failed `Resolve` produces one line: the relative path, and **every mount tried, in the order
tried, by id**. Not "file not found".

This repository has paid for that lesson three separate times and all three receipts are in one
file. The staging script's `REMOVING` message exists because a build that silently deletes is a
trap (`:191`); its `KEEPING` message exists because *"an authored change ... never reaches a
tree that has already been built once ... That cost a whole debugging session on a `menu.json`
scale setting that 'did nothing'"* (`:309-315`); and the whole every-subdirectory rule exists
because a hardcoded list twice shipped a feature whose assets never arrived, where *"the file
is visibly right there in the source tree, and the engine reports it missing"* (`:38-41`).
**Under a search path that symptom is the default failure**, not an occasional one, because the
file genuinely is somewhere — just not on the list that was consulted.

It must be **cached, including negatively**, and for a reason already written down:
`UITextureCache.cpp:27-29` caches failures because a missing file re-attempted every frame
*"would cost a file open per element per frame and print the same line forever."* A resolver
that searches N mounts multiplies that by N. One entry, one line, once.

### 4.6 Two caches, and conflating them is the trap

`AssetIndex` is **not** the resolver and must not become it. It is a throttled, versioned,
depth-capped, optionally-async *view for the editor* whose whole purpose is that the Assets
panel never touches `std::filesystem` (`AssetIndex.h:14-34`). Its tree is stale by up to
`rescanInterval()`, defaulting to **2.0 seconds** (`AssetIndex.h:92`). A resolver answering
from it would fail to find a file that exists — the single least acceptable failure a resolver
has.

So: the resolver keeps its own `rel -> (mount, absolute path)` map, negative entries included.
Invalidation is the honest part — **there is no file watcher** (`AssetIndex.h:19-23` lists them
as future work), so the cache is cleared on the events the editor already forces a rescan on
(`forceRescan()`, `AssetIndex.h:65`) and on any change to the mount list. In a shipped player
the mounts do not change after boot and the cache is never cleared, which is the case where the
performance matters.

`AssetIndex` itself does change, and it is not free: `AssetIndex(std::string root =
"Exported")` (`:49`) takes **one** root. It must scan every mount and present the **union with
the winner marked**, or the editor shows a file the game does not load — the exact failure the
`OVERRIDE` message exists to prevent, relocated from the build log into the tool, where it is
harder to notice because a tool looks authoritative.

### 4.7 What is bought: override stops being destructive

Nothing is copied over anything, so the engine's file is still on disk and still addressable;
the browser can render *"overridden by `untitled-fighter`"* against it; un-overriding is
deleting one file from one mount; and `stage_runtime_assets.cmake:119-136`'s remaining hole
does not need detecting because there is no stale copy to detect. That is the same list §2.4
said a copy cannot produce, and it is produced by removing the copy rather than by adding
machinery.

---

## 5. What it costs and what it touches

| Area | What changes | Honest size |
|---|---|---|
| **Loaders** | 13 read sites → resolve-then-open; 5 **files** lose their own `PathIsContained` calls (`SceneSerializer`, `ScriptWorld`, `UIStyleSheet`, `UIMarkup`, `IBLBaker`); 3 write sites → `ResolveForWrite`; 41 `"Exported/"` literals across 21 files | Mechanical, **not small**, and every site is a place a bug is *silent*: a file found in the wrong mount works perfectly until that mount is dropped |
| **Content** | 411 authored path strings, and a writer that emits them (§3.2) | The largest single item, and the one that wants the compatibility rule |
| **Editor** | `AssetIndex` scans N roots; `AssetBrowserPanel` shows union + provenance; drag-drop payload (`kAssetPayload`, `AssetBrowserPanel.h:40`) carries a relative path, which is already right | Moderate; the provenance UI is new work, not a port |
| **Cooker** | `AssetCooker validate <assetRoot>` takes **one** root (`CookerMain.cpp:38`, `:42-50`). It must validate the **resolved** tree, or it checks a base file the game will never open and skips the override that will actually be parsed | Small change, and it is the piece §4.3 needs to gate an untrusted mount. Its fail-closed check (`is_directory`, `:47-50`) is exactly the right shape and needs a mount-list analogue |
| **Packaging** | Emit a mount list; copy each mount to its own directory | **Simpler than what is there now**, and this is where the work pays immediately — see below |
| **Staging** | Back to one root per mount, or gone entirely; the script says so at `:14-21` | Deletion |
| **Tests** | `tests/CMakeLists.txt:41-50` stages the same roots so tests read the shipped artifact; five test files walk up to a source-tree characters directory as a fallback | The fallback *improves*: "walk up looking for a directory" becomes "mount the source tree" |

**The packaging finding deserves its own paragraph, because it is a defect now.**
`Player/CMakeLists.txt:50` names `Editor/src/Exported/` by hand: **the packaging step never
learned about `CSE_ASSET_ROOTS`.** The title's content reaches a bundle only through the
`install(CODE)` block at `:63-81`, which is documented as bundling *editor-authored* content
and whose failure path warns that the package *"ships the source-tree defaults, NOT your saved
scene."* So a package containing the game is correct **by accident** — the runtime staged tree
happens to contain the title because `runtime_assets` ran. There are two sources of truth for
what ships and only one of them knows about titles. Under mounts there is one list and
packaging reads it.

---

## 6. Done means a third party ships without this repository

The shippability test, as steps, with what each actually requires:

1. **They install the engine.** Needs `install(EXPORT)`, a `CseEngineConfig.cmake`, and
   installed headers. **Measured: none of the three exists** (§2.1). *Not this ADR's work, and
   it blocks this ADR's work from being demonstrable.*
2. **They write a title against the public seams.** `RegisterTitleGameModes` (`GameMode.h`),
   `RegisterTitlePanels` (`EditorPanel.h`), `TitleFrontEndScene` (`TitleFrontEnd.h`) — declared
   by the engine, defined by the title, with the guard macro riding in as an INTERFACE compile
   definition so the link and the macro cannot disagree. **This step already passes.** The code
   boundary was built for exactly this and needs nothing.
3. **They point the shipped player at their content.** Needs a mount list the host can be given
   **without a CMake configure of the engine repo** — i.e. a runtime-readable declaration (the
   game's own manifest naming its roots) *and* the C++ seam for a host that prefers to compile
   it in. **This is the deliverable.**
4. **They ship a bundle.** Needs a packaging step that does not name `Editor/src/Exported` (§5).
5. **Their users add mods.** Needs a user-scoped writable mount, `trusted = false`, and the
   validation gate of §4.3.

**Done is steps 2–5 with this repository absent.** And the useful property is that the test is
**runnable before any of it is finished**: install to a temp prefix, delete the checkout, try.
That rehearsal costs an afternoon and will fail at step 1, which is worth knowing *first*
because it reorders the work — there is no point resolving assets across mounts for a consumer
who cannot include a header.

The durable form is a **fifth CI job**: build the engine, install it to a prefix, then from a
different directory with no access to the source tree, build a toy title against the installed
prefix and run it headless. Four jobs are green today. This is the one that turns
*"shippable"* from an opinion into a status check — the same move the boundary assertion made
when it turned *"the engine may not depend on a title"* from an intention into a configure-time
`FATAL_ERROR`.

---

## 7. Where this sits, and what it competes with

**Against `ADR-005`'s ranking it scores zero, and pretending otherwise would be the dishonest
version of this section.** G1 is playtesters validating the combo tool; G2 is it being a
fighting game rather than a hitbox demo. Search paths close no cycle of the measured 33, make
`playsAsAnalysed` no more reachable, and make no frame number legible. A playtester cannot tell
whether the menu they are looking at came from a staged copy or a mount, and that is precisely
the point of the interim.

**So `ADR-005` §1's table needs a fourth row, and this document is the amendment.** All three
of its goals are about *the game*. The author has stated a fourth — *"a true engine behavior
that I can actually ship to other users"* — and this is the first item on its critical path:

| | Goal | Critical path runs through |
|---|---|---|
| **G4** | The engine can be given to somebody else | an installed/exported engine, **then** asset mounts, then packaging |

**What it competes with is other engine infrastructure, and it beats all of it on ordering
rather than on urgency.** Its natural neighbours are `AssetIndex`'s planned watchers, GUIDs and
pack files (`AssetIndex.h:19-23`), the cooker's unimplemented `cook-textures` and `pack`
(`CookerMain.cpp:19-22`), and `ARCHITECTURE.md` Phase 7's mod loading. **Three of those four are
downstream of this one** — a pack file is a mount backend, mod loading *is* a mount, and the
cooker's `pack` writes what a mount reads. Among engine work it is first because the others are
not sensibly done before it, not because anyone is waiting.

**When: after `ADR-005` P2, and it may run in parallel with P2 rather than after it.** P2 is
one deliberate `GameState` expansion with one re-golden — `GameState.h`, `Combat.*`,
`MatchData`, the kernel. This is `Engine/src/core`, the loaders, and CMake. **They share no
file.** That is an unusually clean parallelism and it is the strongest argument for starting
sooner than the priority suggests: it costs the critical path nothing.

**And the cost of waiting is bounded, which is the argument for not starting today.** Layered
staging works, its one hole is documented and measured, and its symptom is a single stale file
in a single build tree with a one-command fix. The cost of waiting is not *"the engine is
broken"* — it is *"the engine cannot be given away"*, and nobody is currently waiting to
receive it. What that cost grows with is small and countable: the number of authored `Exported/`
strings (411 today) and the number of hosts.

**So the trigger is an event, not a date.** Start this when the first of these happens:

1. somebody outside this repository wants to build against the engine;
2. a mod, or any content that must arrive after a build;
3. a second title, or a second product from one title;
4. the authored-string count grows materially past 411 — new scenes are where it grows.

Any one of them makes the interim the expensive option in the same week.

---

## 8. Decision

1. **The interim stays until a trigger in §7 fires**, and it is recorded here as an interim in a
   third place so it cannot become the answer by default.
2. **Ordered mounts resolved at LOAD time**, in the same order as `CSE_ASSET_ROOTS`, walked
   backwards so later still wins. No merge, no copy, one place that knows the direction.
3. **`Resolve` performs containment itself, per mount, and is the only way to obtain a path.**
   The lexical half of `PathIsContained` runs once before the search; the canonical prefix check
   runs against the mount the file was found in, never against a single nominal root.
4. **A mount is a capability.** Content may never declare one — not a scene, not a stylesheet,
   not a mod manifest. Mounts come from the host in C++, the same shape as
   `TitleFrontEndScene()`.
5. **Writes never go through the search path.** `ResolveForWrite` names exactly one writable
   mount; `.import` sidecars for read-only assets land there under the same relative path.
6. **A mod mount is `trusted = false`**, and the flag gates cooker validation before mounting,
   because containment stops implying "inside the install" the moment a user chooses a
   directory.
7. **Directory mounts only, and `Resolve` returns a path plus its mount.** Archives are a
   backend behind that pointer and come after, not with; a VFS with one mount is not a VFS.
8. **A miss names every mount tried, in order, once** — cached including negatively, for the
   reason `UITextureCache` already caches failures.
9. **`AssetIndex` is not the resolver.** It stays the editor's throttled view, is rebuilt over
   the union of mounts, and marks the winner; a resolver answering from a 2-second-stale tree is
   the one failure a resolver may not have.
10. **The authored `Exported/` prefix comes out of content**, with a one-release rule that
    strips a leading `Exported/` and warns once per path — which reserves `Exported` as a first
    path component, permanently and knowingly.
11. **Packaging reads the mount list**, and `Player/CMakeLists.txt:50`'s hand-named
    `Editor/src/Exported/` goes away. Two sources of truth for what ships is a defect today, not
    only after this lands.
12. **Done is the §6 test, and its durable form is a fifth CI job**: build, install to a prefix,
    then build and run a toy title against that prefix with no source tree. Step 1 of that test
    fails today for a reason that is not about assets — there is no exported engine — and that
    is worth discovering before the resolver is written, not after.

**Reversed if:** the engine is never distributed to anyone outside this repository. Then every
case in §2 collapses to §2.4 alone — override — which is real but is one documented, measured
hole with a one-command fix, and would not justify touching thirteen loaders and 411 authored
strings. The whole argument here is *"ship to other users"*; remove that goal and the bandaid
is simply the right answer.

**Reversed in part if** archives arrive before mods: a single-file distribution is the one thing
that would make road 3 of §4.4 (extract-to-cache) worth building early, because it changes what
a bundle *is* rather than only where its files come from. Even then §4.4's ordering holds — the
mount list first, the backend behind it.
