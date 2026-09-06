# Blender tools of record

Every script here runs **headless** — `blender --background [file.blend] --python <script>` —
and every byte it commits is regenerable from it. That is the path of record
([ADR-019](../../../../docs/adr/ADR-019-placeholders-through-blender.md) D8);
the `blender-mcp` server is an interactive aid for iteration and screenshots,
never a step a committed asset depends on. How to set either up, and the rules
that go with them, is [the art-pipeline manual page](../../../../docs/manual/art-pipeline.md).

| Script | Does |
|---|---|
| `common.py` | The one export function. Pins every glTF exporter option that could make a clip disagree with its frame data (sampling on, frame step 1, Optimize Animation Size **off**, no playback-range limit, slide to zero, deform bones only, rest-position armature, 4 influences, no morphs, no Draco, +Y up, one animation per action), writes the `<stem>.clips.json` sidecar, and refuses to run against an exporter that lacks a pinned option (`selftest()`) or a deform skeleton that drifted from `rig_manifest.json`. |
| `export_gltf.py` | Exports the open `.blend` through `common.py`: `blender --background file.blend --python export_gltf.py -- --out X.gltf [--armature Rig] [--manifest rig_manifest.json]`; `-- --selftest` checks the exporter only. |
| `make_paddle.py` | The committed Blender-exported witness beside the stdlib fixtures: a two-bone paddle with **two** stashed actions (`paddle_swing` 14 stepped keys, `paddle_hold` 5), written to `tests/fixtures/models/paddle_blender.gltf` with its `.bin` and sidecar. `scripts/check_clips.py --self-test` reads it in CI. |
| `make_mannequin.py` | The placeholder fighter (ROADMAP M3.3b): Rigify's **basic** human metarig scaled to 60 units with feet at z = 0, generated, the control layer deleted and the 33 deform bones re-parented along the metarig's hierarchy (one tree rooted at the hips), a body of primitives along the bones bound by automatic weights and capped at 4 influences, a 2-frame `idle`; written to `Games/UntitledFighter/Assets/Characters/fighter_a/model/` with `rig_manifest.json` (written on the first run, **enforced** after), `rig_bones.json` (semantic names for the pose library) and `CREDITS.md`. Rigify is enabled through the preferences operator — `addon_utils.enable(default_set=False)` leaves its parameters unregistered and generation fails with a misleading `make_custom_pivot` error. `tests/test_placeholder_rig.cpp` holds the committed bytes. Regeneration is byte-identical (weights snapped to 1/255, faces sorted by centroid — bone heat and the UV-sphere primitive were both measured to drift run to run otherwise), so `git diff` on the model directory means a change. |

**Frame zero.** Actions are keyed from Blender frame 0 with an explicit frame
range, so sample `k` is Blender frame `k` and the first sampled time is 0 —
`moveFrame 0`, the tick a move starts. New keys are CONSTANT (set through the
keyframe preference before insertion: Blender 5 stores F-curves under an
action's slots, not `Action.fcurves`).

**Self-test record.** `export_gltf.py -- --selftest` and `make_paddle.py`
passed on Blender 5.2.1 LTS (build 2026-08-25) on 2026-09-02; the exporter
exposed every pinned option. CI never runs Blender — its witness is the
committed paddle.
