#!/usr/bin/env python3
"""Write the model fixtures the engine's decode tests read (ROADMAP M3.2a-c).

Standard library only, on purpose: CI has no Blender and no GPU, and an
engine work package must never wait on an artist's machine to prove that a
node transform lands where it should. Every byte here is regenerable by
running this file from the repository root:

    python tests/fixtures/models/make_fixtures.py

glTF buffers are embedded as data URIs so each fixture is one text file that
git diffs like any other; the one binary is a 2x2 PNG written by hand.

Fixtures, and what each pins:
  child_offset.gltf        a unit quad under a node translated (10, 0, 0) and scaled 2,
                           material "grid_heavy" -- node transforms reach the vertices
                           and a material keeps its authored name (M3.2a)
  uv_quad.gltf / .obj      the same quad in glTF's and OBJ's UV conventions, both
                           pointing at uv_quad.png -- after Model::Decode both carry
                           the same texture coordinate at the same corner (M3.2a)
  uv_quad.png              2x2 RGBA: red, green / blue, white
  two_bone_strip.gltf      a three-row strip skinned to root -> tip: the bottom row to
                           root, the top row to tip, the middle row 0.3/0.3 so the
                           decoder has to normalise (M3.2b); two animations on tip --
                           "fourteen" (14 STEP keys on the 60 Hz grid) and "held" (5
                           identical keys) -- and NO channel on root (M3.2c)
  two_meshes_one_skin.gltf two strips, two mesh nodes, ONE skin: one skeleton (M3.2b)
  too_many_joints.gltf     a 129-joint chain: over the palette cap, refused by name (M3.2b)
  off_grid.gltf            the strip with a key at 1/50 s: off the 60 Hz grid (M3.2c)
"""
import base64
import json
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 60

# glTF component types and targets
FLOAT, UBYTE, USHORT = 5126, 5121, 5123
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def f32(values):
    return struct.pack('<%df' % len(values), *values)


def u16(values):
    return struct.pack('<%dH' % len(values), *values)


def u8(values):
    return struct.pack('<%dB' % len(values), *values)


def pad4(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


class Gltf:
    """A minimal glTF 2.0 document builder with one embedded buffer."""

    def __init__(self, generator='tests/fixtures/models/make_fixtures.py'):
        self.doc = {
            'asset': {'version': '2.0', 'generator': generator},
            'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [],
            'accessors': [], 'bufferViews': [], 'buffers': [],
        }
        self.blobs = []

    def accessor(self, blob, component_type, count, kind, min_=None, max_=None, target=None):
        blob = pad4(blob)
        offset = sum(len(b) for b in self.blobs)
        self.blobs.append(blob)
        view = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(blob)}
        if target is not None:
            view['target'] = target
        self.doc['bufferViews'].append(view)
        acc = {'bufferView': len(self.doc['bufferViews']) - 1, 'componentType': component_type,
               'count': count, 'type': kind}
        if min_ is not None:
            acc['min'] = min_
        if max_ is not None:
            acc['max'] = max_
        self.doc['accessors'].append(acc)
        return len(self.doc['accessors']) - 1

    def node(self, name, **kw):
        n = {'name': name}
        n.update(kw)
        self.doc['nodes'].append(n)
        return len(self.doc['nodes']) - 1

    def material(self, name):
        self.doc['materials'].append({'name': name, 'pbrMetallicRoughness': {
            'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0, 'roughnessFactor': 1.0}})
        return len(self.doc['materials']) - 1

    def mesh(self, name, positions, normals, uvs, indices, material, joints=None, weights=None):
        pmin = [min(positions[i::3]) for i in range(3)]
        pmax = [max(positions[i::3]) for i in range(3)]
        n = len(positions) // 3
        attrs = {
            'POSITION': self.accessor(f32(positions), FLOAT, n, 'VEC3', pmin, pmax, ARRAY_BUFFER),
            'NORMAL': self.accessor(f32(normals), FLOAT, n, 'VEC3', target=ARRAY_BUFFER),
            'TEXCOORD_0': self.accessor(f32(uvs), FLOAT, n, 'VEC2', target=ARRAY_BUFFER),
        }
        if joints is not None:
            attrs['JOINTS_0'] = self.accessor(u8(joints), UBYTE, n, 'VEC4', target=ARRAY_BUFFER)
            attrs['WEIGHTS_0'] = self.accessor(f32(weights), FLOAT, n, 'VEC4', target=ARRAY_BUFFER)
        idx = self.accessor(u16(indices), USHORT, len(indices), 'SCALAR', target=ELEMENT_ARRAY_BUFFER)
        self.doc['meshes'].append({'name': name, 'primitives': [{
            'attributes': attrs, 'indices': idx, 'material': material, 'mode': 4}]})
        return len(self.doc['meshes']) - 1

    def skin(self, joint_nodes, inverse_binds, skeleton):
        mats = []
        for m in inverse_binds:
            mats.extend(m)
        ibm = self.accessor(f32(mats), FLOAT, len(inverse_binds), 'MAT4')
        self.doc.setdefault('skins', []).append({'joints': joint_nodes, 'inverseBindMatrices': ibm,
                                                 'skeleton': skeleton})
        return len(self.doc['skins']) - 1

    def animation(self, name, node, times, quats, interpolation='STEP'):
        tin = self.accessor(f32(times), FLOAT, len(times), 'SCALAR', [min(times)], [max(times)])
        flat = []
        for q in quats:
            flat.extend(q)
        tout = self.accessor(f32(flat), FLOAT, len(quats), 'VEC4')
        self.doc.setdefault('animations', []).append({
            'name': name,
            'samplers': [{'input': tin, 'output': tout, 'interpolation': interpolation}],
            'channels': [{'sampler': 0, 'target': {'node': node, 'path': 'rotation'}}],
        })

    def image_texture(self, uri, material):
        self.doc['images'] = [{'uri': uri}]
        self.doc['samplers'] = [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}]
        self.doc['textures'] = [{'sampler': 0, 'source': 0}]
        self.doc['materials'][material]['pbrMetallicRoughness']['baseColorTexture'] = {'index': 0}

    def write(self, path):
        buffer = b''.join(self.blobs)
        self.doc['buffers'] = [{'byteLength': len(buffer),
                                'uri': 'data:application/octet-stream;base64,' +
                                       base64.b64encode(buffer).decode('ascii')}]
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(self.doc, f, indent=1)
            f.write('\n')


def identity():
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def translate(x, y, z):
    # column-major, as glTF stores matrices
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]


def quat_x(degrees):
    h = math.radians(degrees) / 2.0
    return [math.sin(h), 0.0, 0.0, math.cos(h)]


# --- the quad (M3.2a) ------------------------------------------------------

QUAD_POS = [-0.5, 0.0, -0.5, 0.5, 0.0, -0.5, 0.5, 0.0, 0.5, -0.5, 0.0, 0.5]
QUAD_NRM = [0.0, 1.0, 0.0] * 4
QUAD_IDX = [0, 2, 1, 0, 3, 2]   # counter-clockwise seen from +Y


def write_quad(path, uvs, material_name, translation=None, scale=None, image=None):
    g = Gltf()
    mat = g.material(material_name)
    mesh = g.mesh('Quad', QUAD_POS, QUAD_NRM, uvs, QUAD_IDX, mat)
    child = {'mesh': mesh}
    if translation is not None:
        child['translation'] = translation
    if scale is not None:
        child['scale'] = scale
    c = g.node('Child', **child)
    r = g.node('Root', children=[c])
    g.doc['scenes'][0]['nodes'] = [r]
    if image is not None:
        g.image_texture(image, mat)
    g.write(path)


def write_png_2x2(path):
    """RGBA8 2x2: red, green / blue, white -- row 0 is the TOP row in PNG."""
    rows = [bytes([0]) + bytes([255, 0, 0, 255, 0, 255, 0, 255]),
            bytes([0]) + bytes([0, 0, 255, 255, 255, 255, 255, 255])]
    raw = b''.join(rows)

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


# --- the skinned strip (M3.2b / M3.2c) ---------------------------------------

def strip_geometry(x_offset=0.0):
    """Three rows of four vertices at y = 0, 1, 2 (a 0.5-wide strip), quads between."""
    positions, normals, uvs = [], [], []
    for row, y in enumerate((0.0, 1.0, 2.0)):
        for col, x in enumerate((-0.25, 0.25)):
            positions += [x + x_offset, y, 0.0]
            normals += [0.0, 0.0, 1.0]
            uvs += [col, row / 2.0]
    indices = []
    for row in range(2):
        a, b = 2 * row, 2 * row + 1
        c, d = a + 2, b + 2
        indices += [a, b, d, a, d, c]
    return positions, normals, uvs, indices


def strip_skin():
    """Row 0 -> root (1.0); row 1 -> root 0.3 + tip 0.3 (UNNORMALISED, on purpose); row 2 -> tip (1.0)."""
    joints, weights = [], []
    for row in range(3):
        for _ in range(2):
            if row == 0:
                joints += [0, 0, 0, 0]
                weights += [1.0, 0.0, 0.0, 0.0]
            elif row == 1:
                joints += [0, 1, 0, 0]
                weights += [0.3, 0.3, 0.0, 0.0]
            else:
                joints += [1, 0, 0, 0]
                weights += [1.0, 0.0, 0.0, 0.0]
    return joints, weights


def write_strip(path, animations=(), extra_mesh=False):
    g = Gltf()
    mat = g.material('strip')
    pos, nrm, uv, idx = strip_geometry()
    joints, weights = strip_skin()
    mesh = g.mesh('Strip', pos, nrm, uv, idx, mat, joints, weights)
    tip = g.node('tip', translation=[0.0, 1.0, 0.0])
    root = g.node('root', children=[tip])
    skin = g.skin([root, tip], [identity(), translate(0.0, -1.0, 0.0)], root)
    mesh_node = g.node('Strip', mesh=mesh, skin=skin)
    top = [root, mesh_node]
    if extra_mesh:
        pos2, nrm2, uv2, idx2 = strip_geometry(x_offset=2.0)
        mesh2 = g.mesh('StripB', pos2, nrm2, uv2, idx2, mat, joints, weights)
        top.append(g.node('StripB', mesh=mesh2, skin=skin))
    g.doc['scenes'][0]['nodes'] = top
    for name, times, quats in animations:
        g.animation(name, tip, times, quats)
    g.write(path)


def write_too_many_joints(path, count=129):
    g = Gltf()
    mat = g.material('chain')
    n = 3
    pos = [0.0, 0.0, 0.0, 0.1, 0.0, 0.0, 0.0, 0.1, 0.0]
    nrm = [0.0, 0.0, 1.0] * n
    uv = [0.0, 0.0] * n
    joints = [0, 0, 0, 0] * n
    weights = [1.0, 0.0, 0.0, 0.0] * n
    mesh = g.mesh('Tri', pos, nrm, uv, [0, 1, 2], mat, joints, weights)
    # A chain, leaf first so each node can name its child.
    node_ids = []
    child = None
    for i in reversed(range(count)):
        kw = {'translation': [0.0, 0.01, 0.0]}
        if child is not None:
            kw['children'] = [child]
        child = g.node('joint_%03d' % i, **kw)
        node_ids.append(child)
    node_ids.reverse()   # now root first
    skin = g.skin(node_ids, [identity()] * count, node_ids[0])
    mesh_node = g.node('Tri', mesh=mesh, skin=skin)
    g.doc['scenes'][0]['nodes'] = [node_ids[0], mesh_node]
    g.write(path)


def main():
    # M3.2a
    write_quad(os.path.join(HERE, 'child_offset.gltf'),
               [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], 'grid_heavy',
               translation=[10.0, 0.0, 0.0], scale=[2.0, 2.0, 2.0])
    gltf_uvs = [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0]   # far-left = image top-left
    write_quad(os.path.join(HERE, 'uv_quad.gltf'), gltf_uvs, 'uv_check', image='uv_quad.png')
    with open(os.path.join(HERE, 'uv_quad.obj'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# generated by tests/fixtures/models/make_fixtures.py -- the glTF quad, in OBJ\'s UV convention\n')
        f.write('mtllib uv_quad.mtl\n')
        for i in range(4):
            f.write('v %g %g %g\n' % tuple(QUAD_POS[3 * i:3 * i + 3]))
        for i in range(4):
            u, v = gltf_uvs[2 * i], gltf_uvs[2 * i + 1]
            f.write('vt %g %g\n' % (u, 1.0 - v))
        f.write('vn 0 1 0\n')
        f.write('usemtl uv_check\n')
        for a, b, c in (QUAD_IDX[0:3], QUAD_IDX[3:6]):
            f.write('f %d/%d/1 %d/%d/1 %d/%d/1\n' % (a + 1, a + 1, b + 1, b + 1, c + 1, c + 1))
    with open(os.path.join(HERE, 'uv_quad.mtl'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('newmtl uv_check\nKd 1 1 1\nmap_Kd uv_quad.png\n')
    write_png_2x2(os.path.join(HERE, 'uv_quad.png'))

    # M3.2b / M3.2c
    fourteen = ('fourteen', [k / FPS for k in range(14)], [quat_x(5.0 * k) for k in range(14)])
    held = ('held', [k / FPS for k in range(5)], [quat_x(30.0)] * 5)
    write_strip(os.path.join(HERE, 'two_bone_strip.gltf'), animations=(fourteen, held))
    write_strip(os.path.join(HERE, 'two_meshes_one_skin.gltf'), extra_mesh=True)
    write_strip(os.path.join(HERE, 'off_grid.gltf'),
                animations=(('offgrid', [0.0, 1.0 / 50.0], [quat_x(0.0), quat_x(10.0)]),))
    write_too_many_joints(os.path.join(HERE, 'too_many_joints.gltf'))
    print('wrote the M3.2a quads, uv_quad.png, two_bone_strip, two_meshes_one_skin, off_grid, too_many_joints in', HERE)


if __name__ == '__main__':
    main()
