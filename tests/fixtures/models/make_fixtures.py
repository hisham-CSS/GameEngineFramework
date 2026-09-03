#!/usr/bin/env python3
"""Write the model fixtures the engine's decode tests read (ROADMAP M3.2a).

Standard library only, on purpose: CI has no Blender and no GPU, and an
engine work package must never wait on an artist's machine to prove that a
node transform lands where it should. Every byte here is regenerable by
running this file from the repository root:

    python tests/fixtures/models/make_fixtures.py

glTF buffers are embedded as data URIs so each fixture is one text file that
git diffs like any other; the one binary is a 2x2 PNG written by hand.

Fixtures, and what each pins:
  child_offset.gltf   a unit quad under a node translated (10, 0, 0) and scaled 2,
                      material "grid_heavy" -- node transforms reach the vertices
                      (collectMeshes ignored the hierarchy before M3.2a) and a
                      material keeps its authored name
  uv_quad.gltf        the same quad in glTF's UV convention (origin top-left) ...
  uv_quad.obj/.mtl    ... and in OBJ's (origin bottom-left), both pointing at
                      uv_quad.png -- after Model::Decode both must carry the SAME
                      texture coordinate at the same corner, whichever way the
                      importers and aiProcess_FlipUVs get there
  uv_quad.png         2x2 RGBA: red top-left, green top-right, blue bottom-left,
                      white bottom-right
"""
import base64
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def f32(values):
    return struct.pack('<%df' % len(values), *values)


def u16(values):
    return struct.pack('<%dH' % len(values), *values)


def data_uri(blob):
    return 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode('ascii')


def minmax(values, stride):
    cols = [values[i::stride] for i in range(stride)]
    return [min(c) for c in cols], [max(c) for c in cols]


def gltf_quad(positions, normals, uvs, indices, material_name, node_translation=None,
              node_scale=None, image_uri=None):
    """One mesh, one material, one node (optionally transformed), embedded buffer."""
    pos_blob = f32(positions)
    nrm_blob = f32(normals)
    uv_blob = f32(uvs)
    idx_blob = u16(indices)
    # 4-byte alignment for every view.
    def pad(b):
        return b + b'\0' * ((4 - len(b) % 4) % 4)
    blobs = [pad(pos_blob), pad(nrm_blob), pad(uv_blob), pad(idx_blob)]
    buffer = b''.join(blobs)
    offsets = []
    o = 0
    for b in blobs:
        offsets.append(o)
        o += len(b)
    pmin, pmax = minmax(positions, 3)

    doc = {
        'asset': {'version': '2.0', 'generator': 'tests/fixtures/models/make_fixtures.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'Root', 'children': [1]},
                  {'name': 'Child', 'mesh': 0}],
        'meshes': [{'name': 'Quad', 'primitives': [{
            'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
            'indices': 3, 'material': 0, 'mode': 4}]}],
        'materials': [{'name': material_name,
                       'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1],
                                                'metallicFactor': 0.0,
                                                'roughnessFactor': 1.0}}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': len(positions) // 3,
             'type': 'VEC3', 'min': pmin, 'max': pmax},
            {'bufferView': 1, 'componentType': 5126, 'count': len(normals) // 3, 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': len(uvs) // 2, 'type': 'VEC2'},
            {'bufferView': 3, 'componentType': 5123, 'count': len(indices), 'type': 'SCALAR'},
        ],
        'bufferViews': [
            {'buffer': 0, 'byteOffset': offsets[0], 'byteLength': len(pos_blob), 'target': 34962},
            {'buffer': 0, 'byteOffset': offsets[1], 'byteLength': len(nrm_blob), 'target': 34962},
            {'buffer': 0, 'byteOffset': offsets[2], 'byteLength': len(uv_blob), 'target': 34962},
            {'buffer': 0, 'byteOffset': offsets[3], 'byteLength': len(idx_blob), 'target': 34963},
        ],
        'buffers': [{'byteLength': len(buffer), 'uri': data_uri(buffer)}],
    }
    if node_translation is not None:
        doc['nodes'][1]['translation'] = node_translation
    if node_scale is not None:
        doc['nodes'][1]['scale'] = node_scale
    if image_uri is not None:
        doc['images'] = [{'uri': image_uri}]
        doc['samplers'] = [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}]
        doc['textures'] = [{'sampler': 0, 'source': 0}]
        doc['materials'][0]['pbrMetallicRoughness']['baseColorTexture'] = {'index': 0}
    return doc


def write_png_2x2(path):
    """RGBA8 2x2: red, green / blue, white -- row 0 is the TOP row in PNG."""
    rows = [
        bytes([0]) + bytes([255, 0, 0, 255, 0, 255, 0, 255]),      # top:    red, green
        bytes([0]) + bytes([0, 0, 255, 255, 255, 255, 255, 255]),  # bottom: blue, white
    ]
    raw = b''.join(rows)

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def write_json(path, doc):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1)
        f.write('\n')


def main():
    # A unit quad in the XZ plane, facing +Y. Corners named by where they sit
    # on the ground when +Z is "near" the viewer: far-left, far-right,
    # near-right, near-left.
    positions = [-0.5, 0.0, -0.5,
                  0.5, 0.0, -0.5,
                  0.5, 0.0,  0.5,
                 -0.5, 0.0,  0.5]
    normals = [0.0, 1.0, 0.0] * 4
    indices = [0, 2, 1, 0, 3, 2]   # counter-clockwise seen from +Y

    # 1. Node transform reaches the vertices; the material keeps its name.
    write_json(os.path.join(HERE, 'child_offset.gltf'),
               gltf_quad(positions, normals, [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], indices,
                         'grid_heavy', node_translation=[10.0, 0.0, 0.0], node_scale=[2.0, 2.0, 2.0]))

    # 2. The same texel at the same corner from both formats. The intended
    #    mapping: the image's TOP-left pixel (red) sits on the FAR-left corner,
    #    its BOTTOM-left (blue) on the NEAR-left corner.
    #    glTF: UV origin top-left, so far-left = (0,0), near-left = (0,1).
    gltf_uvs = [0.0, 0.0,   # far-left  -> top-left of the image
                1.0, 0.0,   # far-right -> top-right
                1.0, 1.0,   # near-right -> bottom-right
                0.0, 1.0]   # near-left  -> bottom-left
    write_json(os.path.join(HERE, 'uv_quad.gltf'),
               gltf_quad(positions, normals, gltf_uvs, indices, 'uv_check', image_uri='uv_quad.png'))

    #    OBJ: UV origin bottom-left, so the SAME corners read v = 1 - v_gltf.
    with open(os.path.join(HERE, 'uv_quad.obj'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# generated by tests/fixtures/models/make_fixtures.py -- the glTF quad, in OBJ\'s UV convention\n')
        f.write('mtllib uv_quad.mtl\n')
        for i in range(4):
            f.write('v %g %g %g\n' % tuple(positions[3 * i:3 * i + 3]))
        for i in range(4):
            u, v = gltf_uvs[2 * i], gltf_uvs[2 * i + 1]
            f.write('vt %g %g\n' % (u, 1.0 - v))
        f.write('vn 0 1 0\n')
        f.write('usemtl uv_check\n')
        # OBJ indices are 1-based; same winding as the glTF triangles.
        for a, b, c in (indices[0:3], indices[3:6]):
            f.write('f %d/%d/1 %d/%d/1 %d/%d/1\n' % (a + 1, a + 1, b + 1, b + 1, c + 1, c + 1))
    with open(os.path.join(HERE, 'uv_quad.mtl'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('newmtl uv_check\nKd 1 1 1\nmap_Kd uv_quad.png\n')

    write_png_2x2(os.path.join(HERE, 'uv_quad.png'))
    print('wrote child_offset.gltf, uv_quad.gltf, uv_quad.obj, uv_quad.mtl, uv_quad.png in', HERE)


if __name__ == '__main__':
    main()
