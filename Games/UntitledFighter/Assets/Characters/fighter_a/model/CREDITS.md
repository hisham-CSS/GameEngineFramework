# fighter_a placeholder model

Project-authored placeholder (ROADMAP M3.3b) under the repository's
`LICENSE.txt` (**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `Games/UntitledFighter/tools/blender/make_mannequin.py`, run headless.
- **Blender:** 5.2.1 LTS. The skeleton is the deform set of Rigify's basic human
  metarig (`rigify`, bundled with Blender); output created with Blender is the
  creator's own work, per Blender's licence FAQ. No third-party asset is used or
  derived from.
- **Skeleton:** 33 deform bones, pinned by `rig_manifest.json`
  (`common.enforce_rig_manifest` refuses an export that drifts). Semantic names
  in `rig_bones.json`.
- **Body:** 938 vertices of primitives along the bones, 60 units tall
  with feet at y = 0 (1 unit = 1 kernel pixel), one flat material, no textures.

| Clip | Frames | Source | Author |
|---|---|---|---|
| `idle` | 2 | `make_mannequin.py` (`key_idle`) | Hisham Ata |

`fighter_a.clips.json` beside the model is the frame count per clip the
character loader asserts against (A21/A22) once `fighter_a.json` names this
model, which it does when every clip exists (ROADMAP M3.3c).
