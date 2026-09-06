# fighter_a placeholder model

Project-authored placeholder (ROADMAP M3.3b, M3.3c) under the repository's
`LICENSE.txt` (**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `Games/UntitledFighter/tools/blender/make_move_clips.py`, run headless
  (the rig and body come from `make_mannequin.py`; the clips are keyed by `make_move_clips.py` from `fighter_a.json` (frame data) and `poses.json` (the pose library)).
- **Blender:** 5.2.1 LTS. The skeleton is the deform set of Rigify's basic human
  metarig (`rigify`, bundled with Blender); output created with Blender is the
  creator's own work, per Blender's licence FAQ. No third-party asset is used or
  derived from.
- **Skeleton:** 33 deform bones, one tree rooted at the hips, pinned by
  `rig_manifest.json` (`common.enforce_rig_manifest` refuses an export that
  drifts). Semantic names in `rig_bones.json`.
- **Body:** 938 vertices of primitives along the bones, 60 units tall
  with feet at y = 0 (1 unit = 1 kernel pixel), facing +X, one flat material, no
  textures.

| Clip | Frames | Source | Author |
|---|---|---|---|
| `air_hk` | 32 | `make_move_clips.py`: 9+3+20 frames from `fighter_a.json`, poses `air_kick_windup` / `air_kick_contact` / `air_kick_recover` | Hisham Ata |
| `air_hp` | 30 | `make_move_clips.py`: 8+4+18 frames from `fighter_a.json`, poses `air_windup` / `air_contact` / `air_recover` | Hisham Ata |
| `air_lk` | 20 | `make_move_clips.py`: 5+4+11 frames from `fighter_a.json`, poses `air_kick_windup` / `air_kick_contact` / `air_kick_recover` | Hisham Ata |
| `air_lp` | 19 | `make_move_clips.py`: 4+3+12 frames from `fighter_a.json`, poses `air_windup` / `air_contact` / `air_recover` | Hisham Ata |
| `air_mk` | 25 | `make_move_clips.py`: 7+4+14 frames from `fighter_a.json`, poses `air_kick_windup` / `air_kick_contact` / `air_kick_recover` | Hisham Ata |
| `air_mp` | 22 | `make_move_clips.py`: 6+4+12 frames from `fighter_a.json`, poses `air_windup` / `air_contact` / `air_recover` | Hisham Ata |
| `blockstun_crouch` | 26 | `make_move_clips.py`: cycle, 26 frame(s) (max_blockstun), poses `guard_crouch` / `guard_crouch` / `crouch` | Hisham Ata |
| `blockstun_stand` | 26 | `make_move_clips.py`: cycle, 26 frame(s) (max_blockstun), poses `guard_stand` / `guard_stand` / `idle_a` | Hisham Ata |
| `crouch_hk` | 39 | `make_move_clips.py`: 10+3+26 frames from `fighter_a.json`, poses `crouch_kick_windup` / `crouch_kick_contact` / `crouch_kick_recover` | Hisham Ata |
| `crouch_hp` | 34 | `make_move_clips.py`: 8+4+22 frames from `fighter_a.json`, poses `crouch_windup` / `crouch_contact` / `crouch_recover` | Hisham Ata |
| `crouch_idle` | 2 | `make_move_clips.py`: cycle, 2 frame(s) (2), poses `crouch` | Hisham Ata |
| `crouch_lk` | 16 | `make_move_clips.py`: 4+3+9 frames from `fighter_a.json`, poses `crouch_kick_windup` / `crouch_kick_contact` / `crouch_kick_recover` | Hisham Ata |
| `crouch_lp` | 14 | `make_move_clips.py`: 3+2+9 frames from `fighter_a.json`, poses `crouch_windup` / `crouch_contact` / `crouch_recover` | Hisham Ata |
| `crouch_mk` | 27 | `make_move_clips.py`: 7+4+16 frames from `fighter_a.json`, poses `crouch_kick_windup` / `crouch_kick_contact` / `crouch_kick_recover` | Hisham Ata |
| `crouch_mp` | 24 | `make_move_clips.py`: 6+3+15 frames from `fighter_a.json`, poses `crouch_windup` / `crouch_contact` / `crouch_recover` | Hisham Ata |
| `crouch_walk` | 12 | `make_move_clips.py`: cycle, 12 frame(s) (12), poses `crouch`, legs: a gait at 3 px per frame (the kernel's walk speed), stride 36 px | Hisham Ata |
| `hitstun_air` | 36 | `make_move_clips.py`: cycle, 36 frame(s) (max_air_hitstun), poses `flinch_air` | Hisham Ata |
| `hitstun_stand` | 32 | `make_move_clips.py`: cycle, 32 frame(s) (max_hitstun), poses `flinch_stand` / `flinch_stand` / `idle_a` | Hisham Ata |
| `idle` | 48 | `make_move_clips.py`: cycle, 48 frame(s) (48), poses `idle_a` / `idle_b` | Hisham Ata |
| `jump_fall` | 2 | `make_move_clips.py`: cycle, 2 frame(s) (2), poses `jump_fall` | Hisham Ata |
| `jump_rise` | 2 | `make_move_clips.py`: cycle, 2 frame(s) (2), poses `jump_rise` | Hisham Ata |
| `knockdown` | 28 | `make_move_clips.py`: cycle, 28 frame(s) (max_knockdown), poses `down` / `down` / `idle_a` | Hisham Ata |
| `ko` | 2 | `make_move_clips.py`: cycle, 2 frame(s) (2), poses `down` | Hisham Ata |
| `special_dash_punch` | 37 | `make_move_clips.py`: 11+4+22 frames from `fighter_a.json`, poses `special_windup` / `special_contact` / `special_recover` | Hisham Ata |
| `special_fireball` | 46 | `make_move_clips.py`: 13+1+32 frames from `fighter_a.json`, poses `special_windup` / `special_contact` / `special_recover` | Hisham Ata |
| `special_uppercut` | 40 | `make_move_clips.py`: 3+8+29 frames from `fighter_a.json`, poses `special_windup` / `special_contact` / `special_recover` | Hisham Ata |
| `stand_hk` | 40 | `make_move_clips.py`: 11+3+26 frames from `fighter_a.json`, poses `kick_windup` / `kick_contact` / `kick_recover` | Hisham Ata |
| `stand_hp` | 31 | `make_move_clips.py`: 9+3+19 frames from `fighter_a.json`, poses `punch_windup` / `punch_contact` / `punch_recover` | Hisham Ata |
| `stand_lk` | 16 | `make_move_clips.py`: 4+2+10 frames from `fighter_a.json`, poses `kick_windup` / `kick_contact` / `kick_recover` | Hisham Ata |
| `stand_lp` | 14 | `make_move_clips.py`: 3+2+9 frames from `fighter_a.json`, poses `jab_windup` / `jab_contact` / `jab_recover` | Hisham Ata |
| `stand_mk` | 25 | `make_move_clips.py`: 7+3+15 frames from `fighter_a.json`, poses `kick_windup` / `kick_contact` / `kick_recover` | Hisham Ata |
| `stand_mp` | 23 | `make_move_clips.py`: 6+3+14 frames from `fighter_a.json`, poses `punch_windup` / `punch_contact` / `punch_recover` | Hisham Ata |
| `super_beam` | 54 | `make_move_clips.py`: 8+2+44 frames from `fighter_a.json`, poses `super_windup` / `super_contact` / `super_recover` | Hisham Ata |
| `walk_back` | 12 | `make_move_clips.py`: cycle, 12 frame(s) (12), poses `walk_base`, legs: a gait at 3 px per frame (the kernel's walk speed), stride 36 px | Hisham Ata |
| `walk_fwd` | 12 | `make_move_clips.py`: cycle, 12 frame(s) (12), poses `walk_base`, legs: a gait at 3 px per frame (the kernel's walk speed), stride 36 px | Hisham Ata |
| `win` | 2 | `make_move_clips.py`: cycle, 2 frame(s) (2), poses `win` | Hisham Ata |

`fighter_a.clips.json` beside the model is the frame count per clip the
character loader asserts against (A21/A22); `fighter_a.json` names this model
under `engine.anim3d.model` (ROADMAP M3.3c).
