# The Art Pipeline

Verified: 2026-09-02 @ 3d8c055

How a character or a stage gets from Blender into the engine, and the rules
that keep the picture honest to the frame data. The decisions are
[ADR-019](../adr/ADR-019-placeholders-through-blender.md); this page is how to
use them. Where the work stands is [ROADMAP.md](../ROADMAP.md) M3 and nowhere
else.

## The shape in one paragraph

Blender exports **glTF** (`.gltf` + `.bin` + textures beside it), and the engine
reads it through the Assimp it already links ([Assets](assets.md)). A
character's clips are actions on one armature, exported one animation per
action at **60 fps, one sample per frame, never interpolated** — an attack clip
has exactly `startup + active + recovery` frames, frame 0 is the tick the move
starts, and the contact pose is held over the active window so the fist is
inside the live hitbox for exactly the ticks the kernel says. The export script
writes a sidecar `<stem>.clips.json` (`{clip: frames}`) that the character
loader asserts against without Assimp — assertions A21 (every move's clip is
exactly its duration) and A22 (every reserved cycle present), run whenever a
character authors `engine.anim3d.model` ([fighting-core.md](fighting-core.md),
the load assertions); `scripts/check_clips.py` re-derives every count from the
glTF itself. The mode watches the model and the sidecar like the character file,
so a re-export lands as a hot reload and a disagreeing one keeps the last good
match with the loader's words on the HUD. Presentation holds no state: the pose on screen is
a pure function of `GameState` ([fighting-core.md](fighting-core.md), `PoseSelect`;
[DETERMINISM.md](../DETERMINISM.md) P4). The renderer's metre-tuned constants are
retuned for a world where one unit is one pixel by the committed
`Games/UntitledFighter/Assets/UntitledFighter/fight_look.json`, which the mode
applies on adopt and a headless test holds to ADR-019 D5 ([fighting-core.md](fighting-core.md),
the reconciler).

## Tools on the development machine

Neither is a repository, CMake or CI dependency. CI has no Blender and never
needs one: every check reads committed exported bytes.

- **Blender 5.2 LTS** (installed at `C:\Program Files\Blender Foundation\Blender 5.2` on the
  development machine). Every script of record runs headless:

  ```
  "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --python Games/UntitledFighter/tools/blender/make_paddle.py
  ```

- **`blender-mcp`** (MIT), for iteration only — `get_object_info`,
  `get_viewport_screenshot`, quick experiments through `execute_blender_code`.
  Register it at **user** scope, never in the repository, with telemetry off
  (on by default; with consent it uploads prompts, generated code and viewport
  screenshots under an AI-training licence):

  ```
  claude mcp add --scope user -e DISABLE_TELEMETRY=true blender -- uvx blender-mcp
  ```

  It needs `uv` installed. Its Blender side is a legacy add-on: download the
  project's `addon.py`, install it in Blender (Edit → Preferences → Add-ons →
  Install from Disk), enable it, and click **Start MCP Server** in the sidebar
  each session. `execute_blender_code` is `exec()` of the string on Blender's
  main thread with your OS privileges: **save the `.blend` before every batch.**
  Poly Haven (CC0, no key) is the one asset integration allowed; Sketchfab, Poly
  Pizza, Hyper3D and Hunyuan3D are off — keys, accounts or unrecorded licences.

## The rules

- **Scripts of record run headless.** Anything that produces a committed byte
  lives under `Games/UntitledFighter/tools/blender/` and calls `common.export()`;
  a script that calls the glTF exporter itself is a bug. The MCP path is for
  looking, not for shipping.
- **The export is pinned once**, in `Games/UntitledFighter/tools/blender/common.py`:
  sampling on at frame step 1, *Optimize Animation Size* off (it deletes
  duplicate keys — a held pose), no playback-range limit, slide to zero, deform
  bones only, rest-position armature, four influences, no morph targets, no
  Draco, +Y up, one animation per action. `common.selftest()` refuses to run
  against an exporter missing any of them.
- **Frame zero is Blender frame 0.** Key from frame 0 with an explicit action
  frame range; `common.stash_action()` does both and puts the action on its own
  NLA track, which is what makes it export in *Actions* mode.
- **Clips are authored in place, facing +X.** The kernel owns `posX`/`posY`; a
  dash's travel is motion keys, a walk is `velX`. A root that translates in the
  ground plane is a bug the shipped-clips test refuses.
- **No `.blend` is committed.** Every asset is regenerable from the scripts plus
  the pose data they read; `*.blend` and `*.blend1` are ignored. A base download
  or a hand-edited source, if one is ever kept, sits outside every asset root
  under a stated size budget, and Git LFS would be its own decision.
- **Every committed asset has its licence beside it** in a `CREDITS.md` (the
  `Editor/src/Exported/Env/CREDITS.md` pattern), with a per-clip source column
  and a `redistribution allowed: yes/no` line. Test fixtures are code-adjacent
  data under the repository's MIT. The licence for project-authored *content* is
  the human's call ([ADR-019](../adr/ADR-019-placeholders-through-blender.md)
  D10) and is answered before the first such asset is committed.
- **Mixamo is closed.** Its files may not be redistributed in any form, and a
  public repository is distribution.

## The gates

- `python scripts/check_clips.py MODEL.gltf [--sidecar X.clips.json] [--character fighter.json]`
  derives every clip's frame count from the accessor count *and* the time span,
  fails if they disagree or the first sample is not at 0, and compares to the
  sidecar and to each move's `startup + active + recovery`. Standard library
  only; `--self-test` runs in the Ubuntu job on the committed paddle.
- `tests/fixtures/models/` holds the stdlib-written fixtures and the
  Blender-exported paddle (`make_paddle.py`), the one witness that the installed
  exporter driven through the pinned options produces what the contract says:
  two stashed actions → two animations, 14 and 5 samples, `t = 0 … (N−1)/60`.

## A session, in order

1. Open Blender; if using the MCP server, click **Start MCP Server**.
2. `blender --background --python Games/UntitledFighter/tools/blender/export_gltf.py -- --selftest`
   after any Blender update — the exporter's option names are the contract's
   weakest link.
3. Author or refine; save the `.blend` before every `execute_blender_code`.
4. Export through the script of record for the asset (`export_gltf.py`, or the
   asset's own generator), never through the File menu.
5. `python scripts/check_clips.py <exported.gltf> --sidecar <exported.clips.json>`.
6. Commit the exported files with their `CREDITS.md`; the build restages them
   beside the executable (`cmake/stage_runtime_assets.cmake`).
