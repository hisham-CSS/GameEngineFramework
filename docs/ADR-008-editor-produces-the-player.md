# ADR-008 — The editor produces the player, and a dropdown is not a workflow

**Status.** Accepted 2026-08-16. It is a **correction**, not a proposal: the author read the
three-executable arrangement back and said it made no sense, and they are right. Amends
[`ADR-007`](ADR-007-asset-search-paths.md) §7 — a build profile is that document's trigger 3
arriving early — and adds the first item that scores on the **G4** row `ADR-007` §7 added to
[`ADR-005`](ADR-005-playable-priority.md) §1.

**The ask**, verbatim:

> *"this makes no sense - it should feel more like unity - the player doesn't actually make
> sense to be a separate build mode from visual studio - it should be something similar to the
> build profiles and build menu from unity that strips the editor - editor mode should
> basically be the full engine and it should have a step to build the player executable - this
> means that UI and everything that a game is - is fully authored in the engine - and when i go
> to build mode and set specific scenes in my build - those scenes get added and the editor
> tooling is stripped away (unless there are tools build directly in the games runtime)"*

*Every fact below was read at the cited line on 2026-08-16 and re-checked against the file after
the document was written — `ADR-001` §7's discipline. The CMake files are the artifact under
discussion; if one is edited, re-read before quoting.*

---

## 1. The reframe: the editor is the engine, the player is an artifact

Today there are three peer executables and you pick one in a dropdown. `Editor`
(`Editor/CMakeLists.txt:11`), `PlayerDebug` and `PlayerShipping` (`Player/CMakeLists.txt:6-7`),
each `add_subdirectory`'d from the root at `:103-105`, each built by every one of the four
configure presets in `CMakePresets.json`. "Build the player" means *change what Visual Studio is
pointing at, then press F7*.

**That is an engine developer's model, and it is correct for an engine developer.** Somebody
working on `Engine/src/renderer` genuinely does want to switch hosts and rebuild, because the
thing under test is the host. The arrangement was not built by mistake; it was built by the
person who is currently the only user, for the work they were currently doing.

**A game developer runs one application.** They open the editor, author, press Play, author
more, and eventually press Build. They never learn the names of the executables, never learn
that there are two of them, and never open a C++ IDE — and the reason that matters here is not
convenience. It is that **the act of producing a game becomes a thing the project can reason
about**: it has inputs (which scenes), a destination, a record of what it produced, and a place
to refuse. A dropdown has none of those. There is nowhere to hang a check that says *"this scene
has no camera and the player you are about to ship will fall back to a debug fly-cam"* — a
failure this repository has already hit and already writes a paragraph about
(`Player/src/PlayerMain.cpp:616-635`).

So the reframe is one sentence with two halves, and the second half is the one that has work in
it: **the editor is the engine, and the player is something the editor produces.**

---

## 2. What was already true: the stripping is structural, not a flag

"Strips the editor away" sounds like the work. It is mostly already done, and saying so
precisely is worth more than restating the goal.

**Measured, from the link lines.**

| Target | Direct link line | Where |
|---|---|---|
| `Engine` | glfw, glad, assimp, glm, yoga, pugixml, EnTT, nlohmann_json, meshoptimizer | `Engine/CMakeLists.txt:313-321` |
| `Editor` | `Engine`, `imgui::imgui`, `imguizmo::imguizmo`, + `UntitledFighterEditor`, `UntitledFighterModes` | `Editor/CMakeLists.txt:40-42`; root `:178`, `:197` |
| `PlayerDebug` / `PlayerShipping` | `Engine`, `UntitledFighterModes`, `UntitledFighterFrontEnd` | `Player/CMakeLists.txt:10`; root `:198-199`, `:218-219` |

**ImGui reaches exactly two targets in this repository**, and both are tools: `Editor`
(`Editor/CMakeLists.txt:41-42`) and `UntitledFighterEditor`
(`Games/UntitledFighter/Editor/CMakeLists.txt:95`), which only `Editor` links. `Engine` does not
link it at any visibility. So a shipping player contains no ImGui, no gizmo, no asset browser,
no inspector — not because a macro compiled them out, but because **no path exists from the
player's link line to that code.** That is the strongest form the property can take, and it is
already the form it has.

The related rule — that no general-purpose target names a title's internals — is a
configure-time `FATAL_ERROR` (root `CMakeLists.txt:337-363`). **It checks the direct
`LINK_LIBRARIES` property, not the transitive closure**, and the file argues for that at
`:327-336`: linking `UntitledFighterModes` does drag `CseGame`, `CseKernel` and `CseData` into
the player's closure, and none of it is *reachable from the host's sources* because the title's
libraries export no include directories. The rule is about what a general target may **name**,
and naming one is the only way to reach one. Verified, and it holds.

**Two things are worth stating that the claim does not cover.**

**The editor is not a strict superset of the player, and that is deliberate.** It links the
modes but not `UntitledFighterFrontEnd` (root `:202-220`), because `TitleFrontEndScene()`
answers *"what does this host START AT"* and an editor never asks it — a designer opens the
scene they want. So `CSE_HOST_TITLE_FRONT_END` is off in the editor, and an editor call site
added later fails to compile rather than quietly changing what the editor opens.

**And the leak, which is the whole reason this document exists: there is no stripping on the
CONTENT side at all.** The code boundary is a `FATAL_ERROR`; the content boundary is a
directory copy.

- `install(DIRECTORY ${CMAKE_SOURCE_DIR}/Editor/src/Exported/ DESTINATION Exported PATTERN
  "*.import" EXCLUDE)` (`Player/CMakeLists.txt:50-51`) ships the engine's whole asset root, less
  the `.import` sidecars.
- That root is **46 MB across 57 files**, of which `Model/` is **40 MB** — `backpack.obj` at
  7.2 MB and five textures totalling ~34 MB, the demo room's furniture.
- It also includes `Layouts/DefaultLayout.ini`, which is an **ImGui docking layout**, read only
  by `EditorImGuiLayer.cpp:138` and copied to a working `Layouts/` at `:153-154`. The editor's
  window arrangement ships inside the game.
- The title's own content is **708 KB across 8 files**
  (`Games/UntitledFighter/Assets/`).

So: **the arrangement was right and the workflow was missing.** Nobody has to build the
stripping. What is missing is an act that decides what a build *contains*, and a place for that
decision to live — which is why the honest description of this ADR is *"add the missing verb"*
rather than *"separate the editor from the runtime."*

---

## 3. A configuration, a target and a profile are three axes

Conflating them is how build systems become unexplainable, and this repository is one dropdown
away from doing it.

| Axis | What it decides | Who sets it today |
|---|---|---|
| **CMake configuration** | optimization, debug info, which build tree | `CMakePresets.json` — `x64-debug`, `x64-release`, `x64-relwithdebinfo`, `x64-relwithdebinfo-tests` |
| **Which player target** | console vs windowed subsystem, executable name, one message box | `Player/CMakeLists.txt:31-38` |
| **A build profile** | which scenes ship, which one boots, where the output goes — and a named point on the two axes above | **nothing — this is the gap** |

**They are independent, and the cross terms are all legitimate.** `PlayerShipping` built in
`Debug` is a real thing to want (unoptimized, no console, so you can attach a debugger to what a
tester runs). `PlayerDebug` in `Release` is equally real (optimized, console, so a performance
problem prints). Neither is expressible as a single dropdown entry, and the dropdown is what
currently asks you to express it.

**The naming already misleads.** `PlayerDebug` reads as *"the Debug configuration of the
player"* in a repository that also has an `x64-debug` preset, and it is not that. Measured, the
two targets differ in exactly three things: `WIN32_EXECUTABLE` and `OUTPUT_NAME "Player"`
(`Player/CMakeLists.txt:31-33`), and `MYCE_SHIPPING=1` (`:34`), whose single effect in the whole
tree is a `MessageBoxA` on a fatal startup error (`PlayerMain.cpp:41-43`). **They are the same
program with a different front door.** That is Unity's "Development Build" checkbox, not an axis.

**A profile is a named POINT on axes 1 and 2. It does not merge them and it must not invent a
third.** *Shipping* is (`Release`, `PlayerShipping`); *Development* is (`RelWithDebInfo`,
`PlayerDebug`) and is the sensible default, because it is the only combination that is both
playable and diagnosable; *Debug* is (`Debug`, `PlayerDebug`), which exists to attach a debugger
to the **player** rather than the editor. Naming the pair is what makes it explainable; letting a
profile turn on some fourth thing of its own is what would not be.

Three names do not span the cross product above, and they are not meant to — they are the points
worth having, and the axes stay separable underneath so that a fourth (say `Release` +
`PlayerDebug`, to print a performance problem from an optimized build) costs one entry rather
than a redesign. **A profile SELECTS on the axes; it does not merge them.** The day one of the
three names implies something that is neither a configuration nor a target, this section has
stopped being true.

**The price of letting a profile name axis 1 is a preflight, and it is not optional.**
`CMakePresets.json` uses the **Ninja** generator — single-config, `CMAKE_BUILD_TYPE` fixed at
configure time — so **one build directory produces exactly one configuration and `--config` is
meaningless there.** Meanwhile the editor is running out of
`${CMAKE_BINARY_DIR}/build/bin/<CONFIG>` (`Editor/CMakeLists.txt:59-63`,
`Engine/CMakeLists.txt:323-338`), which is the one tree guaranteed to exist and be current.
Asking a RelWithDebInfo editor for a Shipping build means configuring a second tree, possibly
running a vcpkg install, and failing for reasons the editor cannot explain.

So: **choosing a profile the current tree cannot produce is an error that names the preset to
configure — never a build that quietly ships the wrong configuration.** Unity gets away with
owning this axis outright because a Unity project has no other build system in play; this project
has one, it is the one the author already drives, and the preflight is what keeps the two from
disagreeing silently. On a multi-config generator the preflight passes trivially and every
profile is reachable from one tree.

---

## 4. The scene list is the unit of a build

**What it replaces is a build setting with a population of one.**
`ProjectSettings::startupScene` (`Engine/src/core/ProjectSettings.h:15`) is a single string,
defaulted to the engine demo's `Exported/scene.json`, written by one menu item — *File > Set
Current Scene as Player Startup* (`EditorApplication.cpp:2014-2018` → `setStartupScene_` at
`:2601-2616`) — and read by both hosts at boot (`EditorApplication.cpp:637-661`,
`PlayerMain.cpp:152-153`).

**The panel it needed was anticipated and never built, and the tree says so.**
`startupSceneDisplay_` and `startupSceneLoaded_` (`EditorApplication.h:222`, `:224`) are assigned
at `EditorApplication.cpp:2608-2609` and **read nowhere in the repository.** Two write-only
fields beside a live one (`buildSettingsStatus_`, read at `:2017`) are the shape of a feature
that got as far as "somewhere will want to display this" and stopped. The status string it does
show — *"Saved to Exported/project.json (ships with the game)"* — is a status line with no
screen behind it.

**There are already two answers to "what does this build start at", and the compiled-in one
wins.** `PlayerMain.cpp:196-200` resolves, in order: a scene on the command line, then
`TitleFrontEndScene()`, then `project.json`. The reasoning at `:182-190` is sound and must
survive this change — `project.json` is a **host preference** that predates titles, its default
is the engine demo, and letting it win would mean a shipped game boots the backpack room because
somebody once pressed a menu item.

**Which sets the constraint: a scene list must REPLACE one of those answers, not become a
third.** The resolution:

And the constraint has teeth for exactly the title that exists, because `TitleFrontEndScene()`
returns `UntitledFighter/menu.json`: **a build whose `scenes[0]` is anything else — written into
`startupScene`, then overruled at boot — produces a bundle that starts somewhere the panel did
not say.** Silently. That is the one outcome this feature cannot ship with.

**The invariant, whatever mechanism delivers it: `scenes[0]` is what the built game boots, or the
build refuses to produce it.** Two roads reach it, and either is acceptable — having neither is
not:

1. **The manifest wins.** A built bundle's `project.json` is a *build manifest*, not a preference
   file: the build writes it with a field saying so, and a marked manifest outranks
   `TitleFrontEndScene()`. An unmarked file keeps exactly today's precedence, so `:182-190`'s
   protection is untouched — it was always about files the build did **not** write. This is the
   road that lets a profile boot a level directly, which is most of what a scene list is for.
2. **The seam wins and the build validates.** `TitleFrontEndScene()` stays authoritative,
   `PlayerMain.cpp` changes not at all, and the **Build action refuses** a profile whose
   `scenes[0]` is not the title's front end, naming both paths. Cheaper, and it reduces the first
   entry to a formality for any title with a compiled-in front end.

Either way **the losing case is announced** — `PlayerMain.cpp:202-223` already models exactly
this message, and is already careful not to invent a conflict with a file nobody wrote.

**Where the data lives is a split, and the reason is stronger than "tidiness" — it is already a
bug waiting in the tree.** `project.json` ships inside the bundle (`ProjectSettings.h:10-13` says
so), and `MenuUIContent.cpp:200-206` does a `ProjectSettings` **load-modify-save from inside the
running game** every time a player moves the volume slider in a build with no host callback —
which is the shipped player. `ProjectSettings::Save` rewrites the file from the two fields the
struct models (`ProjectSettings.cpp:44-56`). **A scene list stored in `project.json` would
therefore be destroyed by somebody adjusting the volume**, silently, inside a bundle, where
nobody would look.

So: the profile list is a separate editor-side file, never copied into a bundle — the same class
as the `.import` sidecars both install rules already exclude (`Player/CMakeLists.txt:51`, `:69`)
— and the manifest is generated into the bundle by the build. `File > Set Current Scene as Player
Startup` becomes *"make this scene first in the profile"*, and the panel is the screen those two
dead fields were waiting for.

**And here is the honest scope, because "only the selected scenes" is two claims and only one of
them is deliverable now.**

| | Deliverable in v1 | Why |
|---|---|---|
| **Which scenes ship, and which boots** | **Yes, exactly** | scene files are enumerable and the list is authoritative |
| **Which bytes ship** | **No** | nothing can compute a scene's asset closure |

`ADR-007` §3.1 is the reason: there are **thirteen independent read sites in twelve files** that
turn a project-relative path into an open file, and no single place that knows what was asked
for. Dropping `Exported/scene.json` from a profile removes 96 KB and **does not remove the 40 MB
of backpack it referenced**, because nothing connects the two. Stating that plainly is the point
— a Build action that quietly shipped everything while a panel implied otherwise would be worse
than no panel.

There is a tempting shortcut and it should be refused as a default: record what a Play session
actually resolved and cull from that. It is a **trace, not a proof** — it misses every asset on
a code path nobody exercised, and it fails silently, in the shipped build, on the machine of
whoever is not the author. Culling waits for §7.

---

## 5. Build compiles, then assembles — and what that costs, measured

**The decision is taken and is not re-litigated here:** the author asked for *"a step to build
the player executable"*, the title libraries **are** the game the way a Unity project's scripts
are, and the toolchain is present because they are already building from Visual Studio.
Assembling from a stale prebuilt exe would be the wrong default; invoking the build is fast when
nothing changed.

**The mechanism to reuse already exists and the editor already drives it.**
`editor::Subprocess` spawns a child, captures stdout through a pipe, and **holds the process
handle so a hung child can be killed** — `CreateProcess` + `CreatePipe` on Windows
(`Subprocess.cpp:20-62`), `posix_spawn` + `pipe()` on POSIX (`:113-162`), with a dedicated
reader thread and a cancel path already written for the cooker
(`EditorApplication.cpp:2740-2759`, `cancelValidate_` at `:2762-2774`). Builds hang; that is
exactly the machinery a build needs, and `CREATE_NO_WINDOW` (`Subprocess.cpp:49`) is as right
for a compiler as it is for a cooker.

**Three things it cannot do yet, each measured rather than predicted.**

1. **It can only spawn a SIBLING.** Windows builds `".\\" + argv[0] + ".exe"`
   (`Subprocess.cpp:42`); POSIX builds `"./" + argv[0]` (`:139`). Both are deliberate — the
   header documents it at `Subprocess.h:17-19` as immunity from PATH-search rules — and both
   mean `AssetCooker`, which sits beside the editor, is the only kind of thing it can launch.
   **`cmake` is on PATH and is not beside the editor.** Extending the seam to take a resolved
   program path is the first piece of work, and it is small.

2. **The Windows branch does no argument quoting.** `cmd += " " + argv[i]` (`Subprocess.cpp:43`)
   joins with spaces and hands the result to `CreateProcessA`. The only caller today passes
   `{ "AssetCooker", "validate", "Exported" }` (`EditorApplication.cpp:2740-2741`) — three
   space-free tokens — so the defect has never been reachable. **A Build action passes a
   user-typed output directory, which is precisely where a space lives.** POSIX is unaffected: it
   passes a real `argv` array (`:139-144`). This is a Windows-only defect that ships the day the
   feature does, and it should be fixed *with* the feature, not after somebody's
   `D:\My Builds\` becomes two arguments.

3. **The build tree's location is derivable and fragile.** Every executable lands in
   `${CMAKE_BINARY_DIR}/build/bin/<CONFIG>` (`Editor/CMakeLists.txt:59-63`,
   `Player/CMakeLists.txt:18-22`), so the binary directory is three levels above the running
   editor. Deriving it works and depends on a convention held in three separate files. The
   alternative — bake `${CMAKE_COMMAND}` and `${CMAKE_BINARY_DIR}` in as compile definitions at
   configure time — is honest for an editor running out of its own build tree and **wrong for a
   shipped editor**, which has neither. Either way it is a fact about the *dev-tree* editor, and
   §7 is where that stops being true.

**Then it assembles, and the two assembly paths must not be two implementations.**
`install(TARGETS PlayerShipping)`, `install(DIRECTORY ...)` and the `install(CODE ...)` block
that layers editor-authored content over the source defaults already exist
(`Player/CMakeLists.txt:49-81`), with CPack behind them (root `:398-405`). A Build action should
invoke `cmake --install` rather than copy files itself — not for tidiness, but because
`X_VCPKG_APPLOCAL_DEPS_INSTALL ON` (root `:6`) deploys the third-party DLL closure **at install
time**, and a hand-rolled copier would produce a bundle with a different DLL set than `cpack`
does. Two ways to assemble means two bug reports.

---

## 6. Where the output goes, and the shape that keeps `PathIsContained` usable

**The house rule is that authored paths go through `MyCoreEngine::PathIsContained` before
opening. A build's output directory is the first case where the shape of the setting decides
whether the rule still applies at all**, so the shape is the decision.

`PathSandbox.cpp:12-13` rejects any absolute path, root name or root directory; `:16-17` rejects
any `..` component, lexically and before any filesystem access. **A free-text absolute
destination is therefore outside what the primitive can check** — and it is also the more
dangerous setting on its own merits, because a Build action creates and mirrors its destination,
which makes a text field into a directory-deleting instrument with a text field in front of it.

**So the output directory is stored RELATIVE and resolved against a root the pipeline supplies**
(defaulting to the project directory), and containment is used exactly as designed and unchanged:
`PathIsContained(outputRoot, outputDirectory, ...)` at load, re-checked against the real root
before anything is created, and again per written file. Every trap the primitive already catches
— `..`, drive letters, UNC roots — is caught for free, and there is no new security rule to get
right.

**The day an absolute destination is genuinely wanted**, the rule inverts rather than relaxes,
and it inverts the same way `ADR-007` §4.3 already did for mounts. That document's sentence is
*"a mount is a capability; mounting is the security decision, and containment only enforces the
boundary of a mount already granted."* The build's version would be: **choosing a destination is
the capability grant, and containment then enforces the boundary of the granted directory** —
with the base being the chosen root rather than the install. That is a deliberate second feature
with its own confirmation step, not a widening of the field above.

**Four refusals the Build action owes, because a build that writes somewhere unexpected is worse
than one that stops.**

- **Refuse the source tree.** A build writing into `Editor/src/Exported/` or
  `Games/<title>/Assets/` corrupts the inputs of the next one.
- **Refuse the staged runtime `Exported/`.** `cmake/stage_runtime_assets.cmake` **mirrors** —
  a file with no source counterpart is deleted and the removal announced
  (`ADR-007` §2.2). A bundle assembled there is deleted by the next build, loudly, which reads
  as data loss.
- **Refuse a non-empty directory the build did not create**, or require an explicit overwrite.
  An assemble step that clears its destination is a delete, and it will eventually be pointed at
  a Documents folder.
- **Never resolve the destination through a search path** when `ADR-007` lands. That ADR's
  decision 5 already says writes name exactly one destination; this is its first non-editor
  caller.

---

## 7. What this does not give you, and the sentence it does not reach

**"UI and everything that a game is - is fully authored in the engine" describes an aspiration
this change does not reach, and pretending otherwise would make the panel a lie.**

**Measured, this is what authoring UI means today.** The title's front end is a hand-written
`menu.cxml` of **304 lines** and a hand-written `menu.cstyle` of **459 lines**
(`Games/UntitledFighter/Assets/UntitledFighter/UI/`); the engine's own are `hud.cxml` 288,
`menu.cxml` 298, `hud.cstyle` 381, `menu.cstyle` 522 — **2,252 lines of markup and stylesheet
text, all of it written in a text editor.**

**The editor's entire UI authoring surface is two text boxes.**
`InspectorPanel.cpp:709` and `:712` are `ImGui::InputText` fields labelled *"Markup (.cxml)"* and
*"Stylesheet (.cstyle)"*, under a hint reading *"Project-relative, e.g. Exported/UI/hud.cxml"*
(`:716`). You type a path. The one genuinely good thing next to it is hot-reload
(`EditorApplication.cpp:688-690`): edit either file while the editor runs and the Game view
updates in place. **That is a fast edit-in-Notepad loop, and it is not authoring in the engine.**
A build pipeline does not change a single character of it.

**What a UI authoring tool would actually be**, so the size is on the record rather than
implied: a document tree view with reparenting; a canvas that hit-tests and drags elements
against the live yoga layout; a style inspector that writes `.cstyle` rules and shows which rule
won; a binding picker over `UIWorld`'s shared data source; preview at multiple resolutions; and
a serializer back to `.cxml`. That last one has a constraint measured from the files rather than
assumed: **between half and three-quarters of every one of these documents is comment** — 68% of
the title's `menu.cxml`, 72% of `hud.cxml`, 49% of the engine's `menu.cxml`, counted as lines
inside `<!-- -->`, with the title's front end spending its first 74 lines arguing before it
reaches an element. A naive serializer destroys all of it. That is a feature on the scale of the
existing panel set, and it is **its own ADR**.

**The build flow is a prerequisite for it, not a substitute.** A UI authoring tool's output is
content, and content only means something if there is a defined act that says *this content
ships*. Today there is no such act: authored files land in a tree that an install step copies
wholesale and a staging step mirrors away. Build first, and the tool has somewhere to put its
output and something to be validated by.

**And the same is true of the rest of the sentence, which is worth saying once so this document
does not over-claim.** Scripts are `.lua` text files; character data is hand-authored JSON
against `schema.v2.json`; scenes are the one thing genuinely authored in the editor. "Everything
a game is, authored in the engine" is a multi-year direction. This ADR delivers **one verb**.

---

## 8. How it interacts with ADR-007, and where it sits

**The search-path system is what makes a build folder assemblable without copying the whole
engine asset root.** Under `ADR-007` §4.1's ordered mounts, nothing is merged into one tree: a
build writes one directory per mount, and the profile says which mounts ship. That is `ADR-007`
decision 11 — *"packaging reads the mount list"* — finally acquiring a caller, and it deletes
the defect that document already found: `Player/CMakeLists.txt:50` names
`Editor/src/Exported/` by hand and **never learned about `CSE_ASSET_ROOTS`**, so a package
containing the title is correct only by accident.

*(A smaller symptom of the same neglect: `Player/CMakeLists.txt:40-42` still says the staging
target is *"defined in Editor/CMakeLists.txt"*. It moved to the root at `:299` when content
composition arrived. The packaging half of this repository is the half nobody re-reads.)*

**The interim's cost, in the numbers from §2:** a bundle for a game whose own content is 708 KB
ships 46 MB, 40 MB of it one demo backpack, plus the editor's ImGui docking layout. A Build
action written today inherits all of it. It is not a blocker — the build is *correct*, merely
fat — but it is the difference between a profile that decides what ships and one that decides
what boots.

**Which changes `ADR-007`'s schedule, and this is the amendment.** That document's §7 lists four
triggers, of which trigger 3 is *"a second title, or a second product from one title."* **A
build profile is a second product from one title** — that is what selecting a subset of scenes
into a named output *is*. So the mount work is no longer waiting on an external event; it is
waiting on this one, and the trigger fires the day a second profile exists.

**Against `ADR-005`'s ranking it scores zero, and that is the honest placement.** It closes none
of the measured 33 cycles, makes `playsAsAnalysed` no more reachable, and makes no frame number
legible. It scores on **G4** — *the engine can be given to somebody else* — where it sits
alongside `ADR-007` and is **cheaper and more immediately visible**: the author gets a Build menu
in the editor they use every day, whereas mounts buy a property nobody is yet waiting on. It also
shares no file with `ADR-005` P2's `GameState` expansion, so like `ADR-007` it costs the
critical path nothing to run in parallel.

---

## 9. The test that would prove it

**A built folder, moved to a machine with no source tree and no engine, runs the game.**

What that requires today, itemized, because four of the seven are already true and the value is
in knowing which three are not:

| | Requirement | Status |
|---|---|---|
| 1 | The executable — `Player.exe`, windowed subsystem | **Present** (`Player/CMakeLists.txt:31-33, 49`) |
| 2 | `Engine.dll` | **Present** (`Engine/CMakeLists.txt:365`, lua DLL at `:294`) |
| 3 | The third-party DLL closure | **Present at install time only** (root `:6`) — and see §5's warning about a second assembly path |
| 4 | Content: shaders, font, scenes, models | **Present, and 46 MB of it** (`Player/CMakeLists.txt:50`) |
| 5 | The MSVC runtime | **Not bundled.** The engine is `/MD` (root `:68`), so a clean machine needs the redistributable or app-local CRT DLLs. Untested. |
| 6 | A `project.json` naming the boot scene | **Editor-written and absent from the source tree** (`PlayerMain.cpp:208-214`). A build must **write** it, not hope for it. |
| 7 | An enabled `CameraComponent` in the boot scene | **Unchecked.** `PlayerMain.cpp:624-635` falls back to a free-fly debug camera and a message box. |

**Item 7 is the argument for making Build a validating step rather than a copy.** That failure
is already documented in the tree as *"a real, confusing failure"*, and it is exactly the class
of thing a dropdown has no room for: the scene list is the moment the tool knows which scene will
boot, so it is the only moment it can check. Run `AssetCooker validate` over the assembled
output while it is at it — the cooker already fails closed on a bad root
(`Cooker/src/CookerMain.cpp:44-50`) and the editor already knows how to spawn it and stream its
report.

**And the finding that should be uncomfortable: the packaging path has no CI coverage at all.**
Four jobs are green. The Windows job builds all three shipping configurations
(`.github/workflows/ci.yml:179-191`) and **never runs `cmake --install`, never runs `cpack`, and
never launches a player.** The one path this ADR promotes to a first-class feature is the one
path nothing verifies.

**The durable form is a CI job that assembles and runs.** Install to a temp prefix, then from a
directory with no access to the checkout, launch `Player.exe` on a profile scene under the
llvmpipe software GL the `gl-tests` job already installs (`ci.yml:242`), with a frame budget, and
assert it reaches `"PLAYER: rendering from scene camera."` (`PlayerMain.cpp:627`). That single
line is the difference between a bundle that starts and one that fell back to the debug camera.

**It is a different test from `ADR-007` §6's**, and neither implies the other: that one proves a
**third party can build against the engine**; this one proves a **bundle runs**. A bundle that
runs on a clean machine says nothing about whether anyone else could produce one, and an
exported engine says nothing about whether its output starts.

---

## 10. Decision

1. **The editor is the product; the player is an artifact it produces.** The three-executable
   dropdown stays as an *engine developer's* affordance and stops being the *game developer's*
   workflow.
2. **Build compiles, then assembles.** Invoke the real build for the running editor's
   configuration, then `cmake --install` to the chosen output. Never assemble from a prebuilt
   exe by default, and never hand-copy what `install` deploys — the DLL closure is an
   install-time behaviour.
3. **Three axes stay three.** A profile owns which scenes ship and where the output goes, and
   names a point on the other two — a CMake configuration and a player target. It turns on
   nothing else. **Choosing a profile the current build tree cannot produce is an error naming
   the preset to configure**, because `CMakePresets.json` uses Ninja and one build directory
   produces exactly one configuration.
4. **The ordered scene list replaces `ProjectSettings::startupScene`**, whose population is one.
   `scenes[0]` boots. The profile list is a separate editor-side file — `project.json` is
   load-modify-saved from inside the running game (`MenuUIContent.cpp:200-206`), so a scene list
   stored there is destroyed by a volume slider — and it never ships.
5. **`scenes[0]` is what the built game boots, or the build refuses to produce it.** Delivered
   either by a marked build manifest that outranks `TitleFrontEndScene()`, or by a Build action
   that refuses a profile disagreeing with the seam. An unmarked, hand-written `project.json`
   keeps exactly today's precedence either way, preserving `PlayerMain.cpp:182-190`'s protection
   against a stale preference booting the engine demo.
6. **v1 selects SCENES, not BYTES, and the panel must not imply otherwise.** No asset culling
   until `ADR-007`'s resolver can say what was asked for. A play-session resolution trace is not
   an acceptable substitute — it fails silently, in the shipped build, on somebody else's
   machine.
7. **The output directory is stored RELATIVE to a root the pipeline supplies**, so
   `PathIsContained` applies unchanged — at load, again against the real root before anything is
   created, and again per written file. A free-text absolute destination is a later feature that
   inverts the rule (`ADR-007` §4.3's capability argument), not a widening of the field. Refuse
   the source tree, refuse the staged runtime `Exported/` (the staging mirror deletes what is put
   there), and refuse a non-empty directory the build did not create.
8. **Reuse `Subprocess`, and fix the two things a build exposes**: give it a resolved program
   path (today it can only launch a sibling — `Subprocess.cpp:42`, `:139`) and quote arguments on
   Windows (`:43`, unreachable today only because the sole caller passes three space-free
   tokens). Both land with the feature, not after.
9. **Build validates before it declares success.** At minimum: the boot scene has an enabled
   `CameraComponent`, every profile scene loads, and `AssetCooker validate` is clean over the
   assembled output. A build is the only moment the tool knows what will ship.
10. **`ADR-007`'s trigger 3 has fired.** A build profile *is* a second product from one title, so
    the mount work is now downstream of a scheduled feature rather than of an external event.
11. **Done is §9's test, and its durable form is a CI job that installs and runs.** The
    packaging path is currently the only major path in this repository with no coverage, and that
    is worth discovering before a user does.

**Reversed if:** the engine is never used by anyone who does not build it from source. Then the
Visual Studio dropdown *is* the build menu, `cmake --install` is the Build action, and a profile
is one text field that already exists. The entire argument here is *"a game developer runs one
application"*; remove the game developer who is not also the engine developer, and three peer
executables are exactly the right shape.

**Reversed in part if** the title's C++ ever stops being the game — a scripting-only title, with
gameplay in Lua and no per-title libraries on the link line. Then "build compiles" loses its
justification, the binary becomes a fixed runtime, and assembling from a prebuilt exe is right
rather than lazy. Nothing in decisions 3–7 changes; only decision 2 does.
