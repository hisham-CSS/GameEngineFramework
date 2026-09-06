# The training room

Project-authored stage (ROADMAP M3.5a) under the repository's `LICENSE.txt`
(**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `Games/UntitledFighter/tools/blender/make_training_room.py`, run
  headless; every mesh from coordinates, so a regeneration is byte-identical.
- **Blender:** 5.2.1 LTS. No third-party asset is used or derived from; no textures.
- **Dimensions:** `stage_dims.json` beside this file (half width
  480 px, cells 20 px, a heavy line every
  100 px, walls 200 px) and `fight_look.json`'s
  `room_depth_px` 240 and `fighter_depth_px` 60. One
  unit is one kernel pixel (ADR-019 D5); the side walls stand exactly where the
  kernel's wall clamp stops a body (`kStageHalfWidthSub`).
- **Materials:** `floor`, `wall`, `grid_light`, `grid_heavy`, `centre_line`,
  `plane_mark` -- the names `tests/test_stage_asset.cpp` keys on.
