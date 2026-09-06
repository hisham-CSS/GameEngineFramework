"""The training room, generated in kernel units.

ROADMAP M3.5a; ADR-019 D5 (one Blender unit is one kernel pixel), D6 (bpy is
good at primitives), D10 (a CREDITS.md beside every committed model). Run
headless from the repository root:

    blender --background --python Games/UntitledFighter/tools/blender/make_training_room.py
        [-- --dims Games/UntitledFighter/Assets/UntitledFighter/Stage/stage_dims.json]
        [   --look Games/UntitledFighter/Assets/UntitledFighter/fight_look.json]
        [   --out  Games/UntitledFighter/Assets/UntitledFighter/Stage]

THE ROOM IS THE KERNEL'S STAGE, DRAWN. stage_dims.json gives the half width
(pinned by StageAsset.TheFloorSpansExactlyTheKernelsStage to kStageHalfWidthSub
/ 256 -- the side walls stand exactly where the wall clamp stops a body), the
grid cell, the heavy-line spacing (one reach unit, the 2D ruler's spacing) and
the wall height; fight_look.json gives room_depth_px and fighter_depth_px, so
the room the camera and the shadows were set up for (M3.4c) is the room that is
built. In Blender's axes (+X toward the opponent's side, +Z up, +Y away from
the camera; the exporter's +Y-up conversion turns Blender +Y into glTF -Z):

    floor      x in [-W, W], y in [-F, D], top at z = 0
    side walls x = -W and x = +W exactly, y in [-F, D], z in [0, H]
    back wall  y = D, x in [-W, W], z in [0, H]
    grid       a light line every `cell` on the floor and every wall, a heavy
               line every `heavy_every` -- R0c's ruler, so a distance can be
               read off the picture -- both as thin quads a hair off the surface
    centre     a red line at x = 0 along the floor
    marks      blue squares on the fighters' plane y = 0 at every heavy line

EVERY MESH IS BUILT FROM COORDINATES (mesh.from_pydata), never a primitive
operator: the UV sphere's face order drifted run to run in M3.3b, and a room
whose bytes change without a change is a diff nobody can read. One mesh per
material, and the materials are the contract the tests key on
(ModelCPUData::MaterialData::name, M3.2a): floor, wall, grid_light,
grid_heavy, centre_line, plane_mark. Flat colours, no textures; the toon
shading model is the renderer's (fight_look.json), not the file's.
"""
import json
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
ASSETS = os.path.join(REPO, 'Games', 'UntitledFighter', 'Assets', 'UntitledFighter')
DEFAULT_DIMS = os.path.join(ASSETS, 'Stage', 'stage_dims.json')
DEFAULT_LOOK = os.path.join(ASSETS, 'fight_look.json')
DEFAULT_OUT = os.path.join(ASSETS, 'Stage')

LIGHT_W, HEAVY_W, CENTRE_W, MARK = 0.5, 1.5, 2.0, 8.0   # px
LIFT = 0.05                                             # a hair off the surface, against z-fighting

MATERIALS = {
    'floor':       (0.62, 0.64, 0.66),
    'wall':        (0.72, 0.74, 0.78),
    'grid_light':  (0.46, 0.48, 0.51),
    'grid_heavy':  (0.24, 0.26, 0.30),
    'centre_line': (0.85, 0.15, 0.12),
    'plane_mark':  (0.20, 0.45, 0.90),
}


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'dims': DEFAULT_DIMS, 'look': DEFAULT_LOOK, 'out': DEFAULT_OUT}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip('-')
        if key not in opts or i + 1 >= len(argv):
            raise SystemExit('make_training_room: unknown or valueless argument %r' % argv[i])
        opts[key] = argv[i + 1]
        i += 2
    return opts


class MeshBuilder:
    """Quads for one material, in insertion order (deterministic bytes)."""
    def __init__(self):
        self.verts, self.faces = [], []

    def quad(self, a, b, c, d, normal):
        """A quad through a, b, c, d whose face normal points along `normal`."""
        n = (Vector(b) - Vector(a)).cross(Vector(c) - Vector(a))
        pts = [a, b, c, d] if n.dot(Vector(normal)) >= 0 else [a, d, c, b]
        base = len(self.verts)
        self.verts.extend(tuple(float(v) for v in p) for p in pts)
        self.faces.append((base, base + 1, base + 2, base + 3))

    def build(self, name, material):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], self.faces)
        mesh.validate()
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def flat_material(name, rgb):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if bsdf is not None:
        bsdf.inputs['Base Color'].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        bsdf.inputs['Roughness'].default_value = 0.9
        bsdf.inputs['Metallic'].default_value = 0.0
    return mat


def steps(lo, hi, step):
    """Multiples of `step` in [lo, hi], as integers in pixels."""
    import math
    return [k * step for k in range(int(math.ceil(lo / step)), int(math.floor(hi / step)) + 1)]


def build_room(dims, look):
    W = float(dims['half_width_px'])
    cell = float(dims['cell_px'])
    heavy = float(dims['heavy_every_px'])
    H = float(dims['wall_height_px'])
    D = float(look['room_depth_px'])
    F = float(look.get('fighter_depth_px', 0.0))
    if heavy % cell != 0:
        raise SystemExit('make_training_room: heavy_every_px %g is not a multiple of cell_px %g' % (heavy, cell))

    b = {name: MeshBuilder() for name in MATERIALS}
    UP, FWD, LEFT, RIGHT = (0, 0, 1), (0, -1, 0), (1, 0, 0), (-1, 0, 0)   # face normals, Blender axes

    # the floor and the three walls
    b['floor'].quad((-W, -F, 0), (W, -F, 0), (W, D, 0), (-W, D, 0), UP)
    b['wall'].quad((-W, D, 0), (W, D, 0), (W, D, H), (-W, D, H), FWD)          # back wall faces the camera
    b['wall'].quad((-W, -F, 0), (-W, D, 0), (-W, D, H), (-W, -F, H), LEFT)     # left wall faces +X (inward)
    b['wall'].quad((W, -F, 0), (W, D, 0), (W, D, H), (W, -F, H), RIGHT)        # right wall faces -X (inward)

    def line_kind(v):
        return 'grid_heavy' if v % heavy == 0 else 'grid_light'

    def width(kind):
        return HEAVY_W if kind == 'grid_heavy' else LIGHT_W

    # floor: lines along the depth at every x, and across at every y; x = 0 is the centre line
    for x in steps(-W, W, cell):
        if x == 0:
            continue
        k = line_kind(x); w = width(k) / 2.0
        b[k].quad((x - w, -F, LIFT), (x + w, -F, LIFT), (x + w, D, LIFT), (x - w, D, LIFT), UP)
    for y in steps(-F, D, cell):
        k = line_kind(y); w = width(k) / 2.0
        b[k].quad((-W, y - w, LIFT), (W, y - w, LIFT), (W, y + w, LIFT), (-W, y + w, LIFT), UP)
    b['centre_line'].quad((-CENTRE_W / 2, -F, 2 * LIFT), (CENTRE_W / 2, -F, 2 * LIFT),
                          (CENTRE_W / 2, D, 2 * LIFT), (-CENTRE_W / 2, D, 2 * LIFT), UP)

    # back wall: vertical lines at every x, horizontal at every z
    yb = D - LIFT
    for x in steps(-W, W, cell):
        k = line_kind(x); w = width(k) / 2.0
        b[k].quad((x - w, yb, 0), (x + w, yb, 0), (x + w, yb, H), (x - w, yb, H), FWD)
    for z in steps(0, H, cell):
        k = line_kind(z); w = width(k) / 2.0
        b[k].quad((-W, yb, z - w), (W, yb, z - w), (W, yb, z + w), (-W, yb, z + w), FWD)

    # side walls: vertical lines at every depth, horizontal at every z
    for xw, normal in ((-W + LIFT, LEFT), (W - LIFT, RIGHT)):
        for y in steps(-F, D, cell):
            k = line_kind(y); w = width(k) / 2.0
            b[k].quad((xw, y - w, 0), (xw, y + w, 0), (xw, y + w, H), (xw, y - w, H), normal)
        for z in steps(0, H, cell):
            k = line_kind(z); w = width(k) / 2.0
            b[k].quad((xw, -F, z - w), (xw, D, z - w), (xw, D, z + w), (xw, -F, z + w), normal)

    # the fighters' plane: a blue square at every heavy line on y = 0
    h = MARK / 2.0
    for x in steps(-W, W, heavy):
        b['plane_mark'].quad((x - h, -h, 3 * LIFT), (x + h, -h, 3 * LIFT), (x + h, h, 3 * LIFT), (x - h, h, 3 * LIFT), UP)

    objects = []
    for name, rgb in MATERIALS.items():
        objects.append(b[name].build('stage_' + name, flat_material(name, rgb)))
    return objects, dict(W=W, D=D, F=F, H=H, cell=cell, heavy=heavy)


def write_credits(path, version, geometry, dims_path):
    text = f"""# The training room

Project-authored stage (ROADMAP M3.5a) under the repository's `LICENSE.txt`
(**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `Games/UntitledFighter/tools/blender/make_training_room.py`, run
  headless; every mesh from coordinates, so a regeneration is byte-identical.
- **Blender:** {version}. No third-party asset is used or derived from; no textures.
- **Dimensions:** `{os.path.basename(dims_path)}` beside this file (half width
  {geometry['W']:g} px, cells {geometry['cell']:g} px, a heavy line every
  {geometry['heavy']:g} px, walls {geometry['H']:g} px) and `fight_look.json`'s
  `room_depth_px` {geometry['D']:g} and `fighter_depth_px` {geometry['F']:g}. One
  unit is one kernel pixel (ADR-019 D5); the side walls stand exactly where the
  kernel's wall clamp stops a body (`kStageHalfWidthSub`).
- **Materials:** `floor`, `wall`, `grid_light`, `grid_heavy`, `centre_line`,
  `plane_mark` -- the names `tests/test_stage_asset.cpp` keys on.
"""
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def main():
    opts = parse_args()
    with open(opts['dims'], encoding='utf-8') as f:
        dims = json.load(f)
    with open(opts['look'], encoding='utf-8') as f:
        look = json.load(f)
    os.makedirs(opts['out'], exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    common.pin_scene()
    objects, geometry = build_room(dims, look)
    gltf = os.path.join(opts['out'], 'training_room.gltf')
    version, _ = common.export(gltf, write_sidecar=False)
    write_credits(os.path.join(opts['out'], 'CREDITS.md'), version, geometry, opts['dims'])
    faces = sum(len(o.data.polygons) for o in objects)
    print('make_training_room: Blender %s wrote %s -- %d meshes, %d quads; floor x in [%g, %g], depth [%g, %g], '
          'walls %g tall, cells %g, heavy every %g' % (version, gltf, len(objects), faces, -geometry['W'],
                                                       geometry['W'], -geometry['F'], geometry['D'],
                                                       geometry['H'], geometry['cell'], geometry['heavy']))


if __name__ == '__main__':
    main()
