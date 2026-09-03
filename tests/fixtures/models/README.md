# Model fixtures

Test evidence, not content: nothing here is staged next to an executable, and
everything here is written by `make_fixtures.py` with the Python standard
library alone — no Blender, no GPU — so an engine work package never waits on an
artist's machine to prove a decode property (ROADMAP M3.2a; ADR-019 D1).

Regenerate from the repository root:

```
python tests/fixtures/models/make_fixtures.py
```

The outputs are committed so CI needs no Python step to read them; the script
is the source of truth and a diff after regenerating is a finding.

| File | Pins |
|---|---|
| `child_offset.gltf` | A unit quad under a node translated (10, 0, 0) and scaled 2, material `grid_heavy`: node transforms reach the vertices, and a material keeps its authored name (`ModelDecodeGltf.AChildNodesTransformLandsItsVerticesInWorldSpace`). |
| `uv_quad.gltf`, `uv_quad.obj`, `uv_quad.mtl` | The same quad in glTF's UV convention (origin top-left) and OBJ's (origin bottom-left), both pointing at `uv_quad.png`: after `Model::Decode` both carry the same texture coordinate at the same corner (`ModelDecodeGltf.AGltfAndAnObjOfTheSameQuadSampleTheSameTexel`). |
| `uv_quad.png` | 2×2 RGBA — red top-left, green top-right, blue bottom-left, white bottom-right — written chunk by chunk with `zlib`. |
| `paddle_blender.gltf`, `paddle_blender.bin`, `paddle_blender.clips.json` | **The one Blender-exported file here**, written by `Games/UntitledFighter/tools/blender/make_paddle.py` through the pinned export: a two-bone paddle with two stashed actions, `paddle_swing` (14 stepped keys) and `paddle_hold` (5). `scripts/check_clips.py --self-test` derives both counts from the accessor count and the time span and compares them to the sidecar — the witness that the installed exporter, driven by `common.py`, produces what ADR-019 D2 says. Regenerate with `blender --background --python Games/UntitledFighter/tools/blender/make_paddle.py`; Blender 5.2.1 LTS wrote the committed copy on 2026-09-02. |

glTF buffers are embedded as data URIs so each fixture is one text file that
git diffs; `.gitattributes` keeps `.gltf` LF and `.glb`/`.bin`/`.png` binary.

**Licence.** Project-authored, code-adjacent test data under the repository's
`LICENSE.txt` (MIT). Author: Hisham Ata, via the generator above. Redistribution
allowed: yes. No third-party asset is used or derived from.
