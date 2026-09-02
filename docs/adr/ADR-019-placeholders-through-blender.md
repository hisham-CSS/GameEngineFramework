# ADR-019: Placeholders through Blender — the art pipeline, decided before the first bone

Status: Proposed (2026-09-02), with recommended defaults. Nothing below is
implemented; the decisions here are what ROADMAP M3 executes. Every default is
reversible by `git revert` of the WP that lands it, except the one marked
otherwise (the content licence, D10), which the human must answer before the
first committed asset. The M2/M3 ordering that decides WHEN this runs is
[ADR-020](ADR-020-the-bounded-lift.md), not this document.

## Context

The human asked (2026-09-01) to plan "the art pipeline using Blender and its
MCP server to build the actual characters and training room background — at
least one shoto style character fully modeled and working so we can validate
this with actual animations rather than just hitboxes."

**What exists.** The title already has the two things an art pipeline needs
most. `fighter_a.json` is a complete shoto move list — 22 moves (six standing,
six crouching, six air normals, fireball, uppercut, dash punch, one super) whose
clip lengths are FIXED by data: the kernel runs a move for at most
`startup + active + recovery` ticks (`Combat.cpp` `MoveDuration`), 628 ticks of
attack animation across the roster. And the contract for what a pose may be a
function of is already written in three places that agree: DETERMINISM P4,
ADR-011 decision 6 and ADR-005 §4.1 — pose is a pure function of
`(moveId, moveFrame, posX, posY, facing, stance, the stun fields, tick)`; while
a move runs the clip is sampled at integer `moveFrame`, never at wall-clock;
the authored clip length must equal `MoveDuration` as a load assertion beside
A01–A20, proven by mutation.

**What does not.** The engine is a static-mesh pipeline end to end. The live
`Vertex` is position/normal/uv/tangent/bitangent with no bone data
(`Engine/src/core/Model.h`); `Mesh.h` carries bone ids and weights only as
commented-out LearnOpenGL code; `Model::Decode` reads no `mBones` and no
`mAnimations`, and `collectMeshes` copies vertices verbatim, ignoring
`aiNode::mTransformation`, so a glTF whose meshes sit under transformed nodes
would import mis-placed. `AssetIndex::classify` recognises only `.obj` as a
model. The GL context is 3.3 core: uniform buffers and texture buffers exist,
shader storage buffers do not, and nothing in the tree uses a UBO today —
`Shader` exposes single-value setters only. `vertex.glsl` is linked into both
the depth prepass and the colour pass with `invariant gl_Position` for the
GL_EQUAL test. Draw submission sorts `DrawItem`s and collapses runs into one
instanced draw with one bind, so two fighters sharing a model would render with
the first one's pose unless a pose identity joins the run key; the AABB is
computed once from the rest mesh and is what both culls read; cascades
re-render on transform dirtiness, so a fighter animating in place would leave
them stale. `CameraComponent` is `{fovDeg, nearClip, farClip, priority,
enabled}` and the only projection the renderer builds is a perspective one.
`GameModeContext` is `{app, scene, font, contentRoot}` — a mode has no handle
that produces a `Model`. The schema's `engine.anim` block is SPRITE-shaped
(atlas, frames, sprites) and deliberately unread.

**What binds.** Skinning is the one renderer feature the showcase freeze
admits (CLAUDE.md; ARCHITECTURE §2 says one more is "an ADR line, not a
drive-by"). A general time-based animator with blend trees is rejected
outright ("build the frame-indexed sampler"). Presentation never owns state the
sim did not produce; tails are always cancelable; a blend never moves a box.
Every committed asset has its licence written beside it — the tree's patterns
are `Editor/src/Exported/Fonts/README.md` and `Env/CREDITS.md`, and the
counter-example is `Model/source_attribution.txt`, which marks the sample
backpack DO NOT SHIP because no licence was recorded. CI has no Blender and no
GPU beyond llvmpipe for `gl`-labelled tests, so nothing in CI may invoke
Blender and no check may read a `.blend`.

**What the machine has.** Blender 5.2 is installed on the development machine.
The `blender-mcp` server (MIT, PyPI 1.9.0) is not registered; it is launched
with `uvx`, and `uv` is not installed here. Its `execute_blender_code` tool is
`exec()` of the string on Blender's main thread with the user's OS privileges,
and its telemetry is on by default and, with consent, uploads prompts,
generated code, scene metadata and viewport screenshots under an AI-training
licence; `DISABLE_TELEMETRY=true` in the server's environment turns it off.
Its add-on is a legacy `bl_info` add-on declared for Blender 3.0+; that it
loads on 5.2 is unverified.

**What the research settled and unsettled.** Blender's bundled glTF exporter
can export each action as a separate animation, sampled every frame at the
scene rate, with deform bones only, four influences per vertex, rest-position
armature and +Y up — but its "Optimize Animation Size" removes duplicate keys
and would silently shorten a held pose. Assimp 5.4.3 (the vcpkg pin) imports
glTF skins and animations: one `aiBone` list per mesh, `mTicksPerSecond`
hard-coded to 1000, a single static key synthesised for any un-animated T/R/S
component, and its glTF2 importer already flips V while `Model::Decode` adds
`aiProcess_FlipUVs` for OBJ. Mixamo files may not be redistributed in any form
and need an Adobe account. The two CC0 packs the research first named turned
out not to be free-and-account-less on inspection: Kenney's character pack is
a paid product currently listed unavailable, and Quaternius' rigged `.blend`
sources are in a paid tier (two models are free as glTF/FBX/OBJ). The Guilty
Gear Xrd practice — every frame a keyframe, no interpolation — maps directly
onto sampling one pose per 60 Hz tick, which is both the genre look and the
simplest deterministic-friendly presentation.

Three prior decisions are touched. ADR-010 §3 orders M2 (netcode) before M3
and schedules "real art" after M4.1–M4.3; ADR-005 places art at P5 after
netcode and warns against "three months on a character rig"; ADR-010's own §9
item 5 records the M2/M3 swap as the author's allowed choice. What changes in
the ORDER is ADR-020's. This ADR decides HOW, so that whichever order the
author picks, the pipeline is the same.

## Decisions (recommended defaults)

### D1 — Interchange: glTF 2.0, separate files, through the Assimp already in `vcpkg.json`

A character's presentation model is exported as glTF Separate — `<stem>.gltf`
plus `<stem>.bin` and any textures beside it — and imported through Assimp
5.4.3. No new dependency, no FBX (Assimp's weakest importer, with a long
pivot-node history), no Draco (the vcpkg port builds without it), no `.glb`
until the engine decodes embedded textures (it cannot today; the placeholder
is untextured, so this costs nothing now and the `.glb` question is one
appended decode path later). The import gains what the fixtures pin: node
transforms accumulated for unskinned meshes, the V flip made conditional on
the importer so a glTF and an OBJ of the same quad sample the same texel, a
material name carried from `AI_MATKEY_NAME`, and `*.gltf *.glb *.bin binary`
in `.gitattributes` so no checkout heuristic can corrupt a fixture.

*Reversed if:* Assimp's glTF path fails a fixture the stdlib writer produces
correctly — then a glTF-only library is an ADR plus a human, per CLAUDE.md.

### D2 — The clip contract

One statement, enforced on committed exported bytes and never on a `.blend`:

- **Names.** An attack clip is named exactly its move id (`stand_lp`,
  `crouch_mk`, `special_uppercut`, …) unless an appended, optional,
  presentation-only `move.engine.anim3d.clip` says otherwise. The reserved
  cycles are `idle`, `walk_fwd`, `walk_back`, `crouch_idle`, `crouch_walk`,
  `jump_rise`, `jump_fall`, `hitstun_stand`, `hitstun_air`, `blockstun_stand`,
  `blockstun_crouch`, `knockdown`, `ko`, `win`. The kernel lets a croucher
  walk, so `crouch_walk` exists; the kernel clears `crouching` while a fighter
  cannot act, so a crouched defender gets the standing hit reaction — a kernel
  fact, never a presentation inference.
- **Lengths.** An attack clip has EXACTLY `startup + active + recovery`
  frames. Blender frame `k` is `moveFrame k`, counted from 0, and the export
  pins it: `action.frame_range` set explicitly, "Limit to Playback Range" off,
  the first sample at `t = 0`. `[0, startup)` is anticipation, the contact pose
  is HELD over `[startup, startup + active)` — so the fist is inside the live
  hitbox for exactly the ticks the kernel says — and the rest is recovery
  whose last quarter converges on idle. `knockdown` is exactly the largest
  `knockdownTicks` the file authors; `hitstun_*` and `blockstun_*` are at least
  as long as the largest matching counter; all three are indexed FROM THE END
  (`frame = N − remaining`, clamped at 0) so the getup lands on counter 0
  whatever the authored duration. Cycles are any length ≥ 2 and are keyed
  statelessly — the walk by `posX` (floor-div and floor-mod, because half the
  stage is negative), the rest by `tick`.
- **Root motion.** Every clip is authored in place, facing +X
  (`Fighter::facing == 0`); the root never translates in the ground plane,
  because `posX`/`posY` belong to the kernel — the dash punch's 96 px is motion
  keys, the walk is `velX`.
- **Sampling.** The Blender scene runs at 60 fps; the export is pinned to
  Actions mode with every action stashed on the NLA, Always Sample at step 1,
  Optimize Animation Size OFF, deform bones only, rest-position armature, four
  influences. glTF stores seconds and Assimp reports them at 1000 ticks per
  second, so `Decode` asserts the grid — every key time × 60/1000 within 1e-3
  of an integer; every channel with more than one key carries exactly `N` keys
  and a single-key channel is constant — and refuses otherwise, naming clip
  and key. Sample `k` IS key `k`. Nothing interpolates, the runtime clip type
  carries integers only (a `static_assert` forbids a floating-point time
  member), the sampler has no overload that accepts a floating-point frame,
  and hitstop freezes the pose for free because `moveFrame` freezes.
- **Enforcement, four ways.** (1) The export script writes a sidecar
  `<stem>.clips.json` (`{clip: frames}`) from the same actions, and
  `CharacterData.cpp` asserts **A21** — `frames[clip(move)] == MoveDuration`
  for every move, error naming move, clip, expected and actual — and **A22** —
  every reserved cycle present — beside A01–A20 with nlohmann and no Assimp,
  exactly where ADR-005 put the load assertion, proven by mutation. (2)
  `tests/test_shipped_clips.cpp` decodes the committed model headlessly with
  Assimp and checks every move against `MoveDuration` from `CharacterData` +
  `MatchBuilder`, and the model against the sidecar clip for clip. (3)
  `scripts/check_clips.py`, stdlib Python in the Ubuntu job, derives each frame
  count from both the accessor count and the time span, fails if they
  disagree, and compares to the character file; it shares a fixture with the
  C++ that pins `MoveDuration`'s per-component clamp so the two
  implementations cannot drift. (4) The mode repeats the check on every load
  and hot reload, refusing a mismatching model with keep-last-good and a HUD
  line naming the move and both counts (ADR-016's path).

*Reversed if:* a shipped character needs a clip the kernel cannot select from
state. Then the missing state is a MECHANIC with ADR-011's five parts, never a
presentation inference.

### D3 — T0: presentation holds no state at all

While a move runs the pose is that move's clip at `moveFrame`. When `moveId`
returns to 0 the selected cycle plays at once, keyed by `tick` or `posX`. There
is no tail clip, no blend, no remembered palette. The reason is a kernel fact:
`Combat.cpp` zeroes `moveFrame` the tick a move ends (and on a cancel, and on
an interrupting hit), so "how long ago did the move end" is not in `GameState`,
and a tail needs presentation-owned memory of it — exactly the state CLAUDE.md
forbids presentation to own, and stricter than ADR-005 §4.1's tolerance. T0
turns P4 from a sentence into a bit-identical test:
`Presentation.HoldsNothingARestoreCannotRebuild`. Stepped poses make the
recovery-to-idle pop read as style, which is the Xrd look on purpose.

*Reversed if:* the pop is judged unacceptable at R8. Then **T1**: a tail memo
chained by TICK (never by rendered frame — under the mode's slow-motion divisor
a rendered-frame blend finishes in a fraction of a tick and under frame-step it
never advances), capped at ADR-011 §6.2's default of 4 frames, with its own
Restore test. A new ADR, one line.

### D4 — The fight camera is orthographic, and a projection mode is camera data

The whole point of this pass is judging the fist against the kernel's box and
counting the 20 px / 100 px ruler (R0c). A perspective camera agrees with the
`Camera2D` box overlay only on the `z = 0` plane, so a limb with depth would
visibly leave a box that is in fact correct. `CameraComponent` therefore gains
an appended projection mode and an orthographic half-height, driven by
`FightCamera`'s existing pure function (200 px half-width, 34 px deadzone,
42 px height, wall clamp), so the scene camera and the fight camera project
the fighter's origin to the same pixel within half a pixel across the stage.
This touches `Renderer.cpp`'s one `glm::perspective`, the CSM slice fit, the
culling frustum, the serializer (appended field) and `CameraDirector`. It is
the "ADR line" ARCHITECTURE §2 asks for: a projection mode is a field on the
camera the showcase needs, admitted beside skinning, and it is the only
renderer-side addition this ADR makes. A perspective toggle with the overlay
projected through the same view-projection is M3.5b, after the boxes have
been judged under the exact camera.

*Reversed if:* never silently; a second renderer addition is its own ADR line.

### D5 — Units: 1 Blender unit = 1 kernel pixel, and the renderer's metre-tuned constants are retuned for the fight

`FightView` already defines one world unit as one kernel pixel
(`WorldPx = sub / 256`), the glTF exporter has no unit-scale option, and
scaling an armature after animating is a known exporter breakage. So the
fighter is 60 units tall with feet at `y = 0` (crouch 34), the stage 960 units
wide with the side walls at exactly ±480 (the corner IS the wall), transforms
applied and the armature at identity before anything is animated. The
consequence the first picture would otherwise reveal: shadow distance, depth
margin, near/far and light ranges are tuned in metres, so the mode applies a
committed `fight_look.json` on adopt — shadow distance ≥ camera distance plus
room depth, near/far, cascade count, outline thresholds, sun, IBL, and a
camera priority that outranks every camera in the host scene — with a
headless property that the back wall is inside the shadow range. Facing left
is a 180° yaw with a positive determinant, never a negative scale; stated
plainly: a yaw shows an asymmetric body's far side when facing left (a
right-hand jab becomes the far hand), which SF-style mirroring avoids with a
negative X scale plus a per-draw front-face flip — a renderer change this ADR
does not make. The placeholder's poses are kept near-symmetric so they read
the same either way.

*Reversed if:* an asset authored in metres is wanted — an appended
presentation field `engine.anim3d.scale`, not a re-export; or if R8 judges the
far-side jab wrong — then the mirror path is an ADR line with a per-item
front-face flag.

### D6 — The placeholder body: a bpy-generated mannequin on a Rigify-derived deform skeleton

The body is GENERATED by a committed script: a proportioned mannequin blocked
out from primitives (bpy is good at this — primitives, mirroring, modifiers,
materials — and bad at organic surfaces), bound by automatic weights to the
deform bones of a Rigify human metarig generated by `rigify.generate`, with
non-deform bones removed, influences capped at four, and the deform set pinned
by `rig_manifest.json` (ordered names and parents) that the export script
enforces and CI asserts bone for bone. Semantic bone names live in
`rig_bones.json` so the pose library is written against `hips`, `chest`,
`l_wrist` … and never against a rig's own naming. No download, no account, no
money, no licence question: the mannequin is project-authored and regenerable
from the script alone. A CC0 base (Quaternius' free glTF tier, two models) is
an OPTION the human may take after checking the pack page; Kenney is a
purchase and therefore an ask; Mixamo is closed outright — no Mixamo file, raw
or converted, in this repository, because its terms forbid redistribution and
a public repository is distribution. The research's "free CC0 base" premise
did not survive inspection, which is why the default owes nothing to any pack.

*Reversed if:* the mannequin's silhouette is too poor to judge a contact pose
at 200 px half-width. Then the CC0 option, under its own CREDITS.md, on the
same manifest — a mesh swap the tests grade.

### D7 — The modeled shoto is the mannequin's mesh swap, on the pinned skeleton

The human's stated goal is a fully modeled shoto. This ADR keeps the promise
by construction: every clip, every test and the whole reconciler are written
against `rig_manifest.json`, so a sculpted, retopologised, textured body
replaces `fighter_a.gltf`'s mesh on the same deform skeleton and every clip
survives the swap unchanged. Claude drives what bpy does well — blockout,
mirroring, modifier stacks, UV seams, material slots, the export — and the
human's viewport hours are the sculpt, the face and the weight painting; an
honest anecdotal budget is 20–60 human hours for the body and 35–75 for the
full pose set. The body lands as ROADMAP **M3.3e**, after the pipeline is
proven on the mannequin at R8, so that no sculpt hour is spent before the
frame-data validation it exists to serve is already visible. Whether one
modeled body may precede M4 at all is ADR-020's second clause; this ADR makes
it a mesh swap so the answer costs nothing either way.

*Reversed if:* the body needs a bone the manifest lacks. Then the manifest
changes ONCE, every clip is re-exported by the generator, and the tests say
so.

### D8 — Blender and blender-mcp are development-machine tools, never a repository, CMake or CI dependency

Blender 5.2 LTS and `uv` on the development machine; the MCP server registered
at USER scope, never in the repository, with telemetry off:

```
claude mcp add --scope user -e DISABLE_TELEMETRY=true blender -- uvx blender-mcp
```

Every script that produces a committed byte lives under
`Games/UntitledFighter/tools/blender/` and runs HEADLESS with
`blender --background --python`; that is the path of record, and the MCP
server is an interactive aid for iteration, `get_object_info` and
`get_viewport_screenshot`. If the legacy add-on will not load on 5.2, the
headless path is the whole pipeline and nothing waits. The `.blend` is saved
before every `execute_blender_code` batch, because it is `exec()`. Poly Haven
(CC0, no key) is allowed for HDRIs and textures; Sketchfab, Poly Pizza, Hyper3D
and Hunyuan3D are off — keys, accounts or unrecorded licences. No `.blend` is
committed: every asset is regenerable from the scripts plus `poses.json`
(human-refined poses are written back by `capture_pose.py`), so the source of
truth is text the tests can read; `*.blend` and `*.blend1` are ignored, and
any base download sits in `Games/UntitledFighter/ArtSource/` outside every
asset root under a stated size budget. Git LFS is a repository-configuration
change with quota and would be its own ask.

*Reversed if:* hand-edited `.blend` files become the only source of an asset
the human wants kept. Then LFS, asked first.

### D9 — Presentation code lives in a GL-free library, and the mode gets a way to load a model

`PoseSelect` lives in `CseGame` and returns kinds and integers only — `{kind,
moveSlot, frame, remaining, tick, posXSub, posYSub, mirror, visible}` — no
names, no float, so nothing name-shaped enters the library DETERMINISM.md
holds to the sim's arithmetic rules (K3, K6 — the allocation rule K4 is
kernel-scoped and is NOT claimed for `CseGame`, which already holds vectors
and strings). Its precedence puts a fighter who ACTS after the round ends
ahead of `Ko`/`Win`: in the training host `roundState` becomes `kRoundOver`
and stays there while both fighters keep walking and attacking, and a winner
frozen in a win pose while the sim moves them would be the one thing this
plan exists to forbid. `FighterClips` (the `(kind, moveSlot) → clip` table,
rebuilt in `adoptPrepared_` because move slots renumber on reload) and
`FightPresentation` (matrices, palettes, camera agreement, overlay mode) live
in a GL-free static library that `UntitledFighterModes` links and tests link
headlessly — a one-hour spike proves the link in all three ctest jobs before
any acceptance test rides on it. `GameModeContext` gains an appended
`AssetManager*` so a mode can request a model through the engine's own cache
and poll `(mtime, size)` on the model and its sidecar in the hot-reload path.

*Reversed if:* the Modes link cannot be made headless — then the presentation
library is the seam and Modes is a thin caller, which is what D9 already
builds.

### D10 — The licence for project-authored assets is the human's, and it is answered before the first asset is committed

The tree distinguishes the code licence (`LICENSE.txt`, MIT) from asset
licences without saying what the author's own content carries. Default: MIT,
restated in a `CREDITS.md` beside each asset in the `Fonts/README.md` and
`Env/CREDITS.md` pattern — author, licence, generator script, Blender version,
source URL and download date for anything third-party, a per-clip source
column, and a mandatory `redistribution allowed: yes/no` line. CC0 or CC BY
4.0 are equally acceptable; what matters is that it is written. Test fixtures
(the paddle, the stdlib-generated models) are code-adjacent test data under
the repository's MIT and may land first. `Assets.EveryModelHasALicenceBesideIt`
covers `.obj`, `.gltf` and `.glb`, which means the unlicensed sample backpack
is removed or licensed in the same WP — the Player install ships the whole
`Exported/` tree (ADR-008), so DO NOT SHIP must stop being a text file.

*Not reversible:* a grant on copies already distributed cannot be withdrawn.
Hence the sequencing.

### D11 — The kernel is frozen for the art pass

No `engine.movement` authoring (the 39-tick placeholder arc stays), no
uppercut final key, no landing lag, no prejump, no crouch-hitstun, no
crouch-walk gating, no projectile. Jump clips are held poses chosen by the
sign of `velY`; countdown clips are end-aligned; so no clip depends on an arc
length or a stun length. The `anim3d` fields never enter `MatchData` or
`MoveDef`, so the handshake hash M2.2 will compute is untouched. Each frozen
item is a mechanic with ADR-011's five parts and moves the golden or the
catalogue's measured cycle counts; folding one into art is the scope creep
CLAUDE.md forbids. M3.3d opens with a kernel test that PINS the inferred
motion-key behaviour (a motion key owning velocity and suspending gravity
until the next key or the move ends), committed whichever way it comes out,
so the uppercut's clip is timed against what the kernel does rather than what
the file's prose says.

*Reversed if:* never inside M3; each is its own data WP with one re-golden.

## Consequences

- The placeholder is genuinely a placeholder in ADR-010's own sense ("a rig
  instead of boxes"), and the modeled shoto is a mesh swap on it, so the human's
  goal and the roadmap's order stop being in tension: the pipeline is one
  pipeline.
- The first visible artifact needs no bone: the training room, generated in
  kernel units, spawned from the Assets panel, its walls at ±480 and heavy
  lines every 100 — proving Blender → glTF → Assimp → engine and the unit
  convention before skinning exists.
- The first shoto swinging on screen is the reconciler WP: the mannequin
  stands on the grid at `WorldPx(posX)` with the kernel's hurtbox drawn over
  it, walks at 3 px/tick, and LP shows a 14-tick stepped jab whose contact pose
  is on screen for exactly the two ticks the red hitbox is live, with the fist
  inside it; frame-step advances one pose per tick; hitstop freezes mesh and
  boxes together; a hot reload that changes a recovery changes the clip or is
  refused by name. The same picture appears in the Editor's Game view and in
  `Player.exe` because both enter the same mode.
- P4 flips from "not yet" to enforced by three tests, all headless: nothing
  survives a restore, a move start is never delayed, a box never moves with
  the pose.
- Every CI check reads committed exported bytes three ways (Assimp in C++,
  stdlib Python, the sidecar in `CseData`) and Blender appears nowhere in CI or
  CMake.
- The event ring (M3.1) is the ONE M3 item that re-goldens the crossplat hash,
  because `kernel::Checksum` folds the whole `GameState` including the ring;
  it stays after M2 in either ordering (ADR-020) and is re-goldened once, on
  both toolchains, with nothing else batched.
- Honest cost, anecdotal: engine 9–13 sessions (the camera mode, the import
  traps and skinned normals are why not 6–9); pipeline 3–4 sessions plus a
  human afternoon; reconciler and acceptance 4–5; the first swing 15–20
  sessions in; the remaining 33 clips 3–5 sessions of batching dominated by
  35–75 human hours at the viewport; the modeled body a further 20–60 human
  hours. "Legible" has a checkable bar so M3.3d cannot sprawl: contact pose
  inside the live hitbox within 2 px, silhouette distinct at 200 px half-width.
- What the human does with their hands, in order: install `uv`; register the
  MCP server at user scope; install the add-on ("Install from Disk" on 5.x)
  and click Start MCP Server each session; answer D10; confirm the exporter's
  option names on the installed build before the first paddle export; review
  the mannequin's deform hierarchy diff and weights; refine poses in Pose Mode
  and run `capture_pose.py`; look at R8.

## What would reverse this ADR as a whole

A shipped character that cannot be expressed as stepped poses at integer
`moveFrame` on a pinned deform skeleton — then the sampler, not the contract,
changes, and ADR-005 §4.1 already says so.
