"""The mannequin: a Rigify deform skeleton wearing a primitive body, 60 units tall.

ROADMAP M3.3b; ADR-019 D5 (1 Blender unit = 1 kernel pixel, feet at 0), D6 (a
Rigify human metarig generated, non-deform bones removed, automatic weights
capped at four, the deform set pinned by rig_manifest.json), D10 (MIT, a
CREDITS.md beside the asset).

THE COMMITTED MODEL IS WRITTEN BY make_move_clips.py (ROADMAP M3.3c), which
imports this module, calls build_mannequin() for the rig and the bound body,
keys every clip from fighter_a.json and poses.json, and exports. Run alone,
this script exports the rig with a 2-frame idle for inspection, and only where
you point it -- it refuses to run without --out so it cannot overwrite the
committed clips with none:

    blender --background --python Games/UntitledFighter/tools/blender/make_mannequin.py
        -- --out <some directory> [--height 60]

It writes, beside each other:

    fighter_a.gltf / fighter_a.bin   the skinned body at rest, one 2-frame `idle`
    fighter_a.clips.json             {"idle": 2}  (common.export writes it)
    rig_manifest.json                the deform hierarchy, (name, parent) in order --
                                     WRITTEN on the first run, ENFORCED on every later one
    rig_bones.json                   semantic names -> deform bone names, so the pose
                                     library (M3.3c) never spells a Rigify name
    CREDITS.md                       author, licence, generator, Blender version

WHY THE BASIC HUMAN METARIG AND NOT THE FULL ONE. Measured on Blender 5.2.1:
`armature_basic_human_metarig_add` generates 35 deform bones; the full
`armature_human_metarig_add` generates 160, over the engine's palette cap of
128 (Engine/src/anim/Skeleton.h, kMaxSkeletonJoints). A placeholder has no
face to animate. The two breast bones are dropped too: 33 deform bones.

WHY THE CONTROL RIG IS DELETED. Rigify generates ~220 bones of which the
deform set is the only thing the engine reads (the exporter keeps use_deform
bones, common.deform_bones() lists the same set). The ORG/MCH/control layers
exist to drive an animator's IK; the pose library keys the deform bones
directly and steps them, so the control layer would be 190 bones of
constraints between the artist and the bytes. Deleting them and re-parenting
every deform bone along the METARIG's hierarchy (a segment under its chain,
every other bone under the chain end of its metarig parent) makes the
armature ONE tree rooted at the hips, and that tree the exported hierarchy:
what rig_manifest.json pins, PlaceholderRig.MatchesItsRigManifestBoneForBone
compares and PlaceholderRig.TheSkeletonIsOneTreeRootedAtTheHips holds. (The
first export re-parented to the nearest DEF- ancestor, which the pelves,
thighs, shoulders and upper arms do not have -- Rigify hangs them off ORG-,
MCH- and control bones -- and shipped a forest of nine roots.)

WHY THE BODY IS ONE CYLINDER PER BONE. bpy is good at primitives and bad at
organic surfaces (D6). One capsule-ish cylinder along every deform bone, a
sphere for the head, the soles flattened to z = 0 and the crown to z = height,
gives a body whose silhouette follows the skeleton by construction and whose
automatic weights land where they should. The modeled shoto (M3.3e) replaces
this mesh on the same manifest.

Rigify must be ENABLED THROUGH THE PREFERENCES OPERATOR. addon_utils.enable(
"rigify", default_set=False) does not add it to the preferences, Rigify's
own register() reads that entry and aborts halfway, and generation then fails
with "'RigifyParameters' object has no attribute 'make_custom_pivot'", which
looks like a Rigify bug and is not. Nothing is saved to the user's preferences
in --background.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
DEFAULT_OUT = os.path.join(REPO, 'Games', 'UntitledFighter', 'Assets', 'Characters', 'fighter_a', 'model')

# Engine/src/anim/Skeleton.h kMaxSkeletonJoints. Restated here so the generator
# refuses a rig the engine would refuse, before the exporter runs; the C++ test
# asserts the real constant on the committed bytes.
PALETTE_CAP = 128
INFLUENCE_CAP = 4

# Semantic names for the pose library (ADR-019 D6): `hips`, `chest`, `l_wrist`...
# never a rig's own spelling. The basic human's spine chain is four torso bones,
# two neck bones and the head; limbs are two segments per bone.
SEMANTIC = {
    'hips': 'DEF-spine', 'spine': 'DEF-spine.001', 'spine_upper': 'DEF-spine.002',
    'chest': 'DEF-spine.003', 'neck': 'DEF-spine.004', 'neck_upper': 'DEF-spine.005',
    'head': 'DEF-spine.006',
}
for side, S in (('l', 'L'), ('r', 'R')):
    SEMANTIC.update({
        f'{side}_hip': f'DEF-pelvis.{S}',
        f'{side}_thigh': f'DEF-thigh.{S}', f'{side}_thigh_lower': f'DEF-thigh.{S}.001',
        f'{side}_shin': f'DEF-shin.{S}', f'{side}_shin_lower': f'DEF-shin.{S}.001',
        f'{side}_foot': f'DEF-foot.{S}', f'{side}_toe': f'DEF-toe.{S}',
        f'{side}_shoulder': f'DEF-shoulder.{S}',
        f'{side}_upper_arm': f'DEF-upper_arm.{S}', f'{side}_upper_arm_lower': f'DEF-upper_arm.{S}.001',
        f'{side}_forearm': f'DEF-forearm.{S}', f'{side}_forearm_lower': f'DEF-forearm.{S}.001',
        f'{side}_wrist': f'DEF-hand.{S}',
    })
DROPPED_METARIG_BONES = ('breast.L', 'breast.R')


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out, height = DEFAULT_OUT, 60.0
    i = 0
    while i < len(argv):
        if argv[i] == '--out':
            out = os.path.abspath(argv[i + 1]); i += 2
        elif argv[i] == '--height':
            height = float(argv[i + 1]); i += 2
        else:
            raise SystemExit('make_mannequin: unknown argument %r' % argv[i])
    return out, height


def enable_rigify():
    r = bpy.ops.preferences.addon_enable(module='rigify')
    if 'FINISHED' not in r or 'rigify' not in bpy.context.preferences.addons.keys():
        raise SystemExit('make_mannequin: Rigify did not enable (%r); it ships with Blender under addons_core' % (r,))


def build_metarig(height):
    """The basic human metarig, scaled so its top is `height` and its feet are at z = 0,
    transforms applied BEFORE generation (ADR-019 D5)."""
    bpy.ops.object.armature_basic_human_metarig_add()
    meta = bpy.context.active_object
    bpy.ops.object.mode_set(mode='EDIT')
    for name in DROPPED_METARIG_BONES:
        eb = meta.data.edit_bones.get(name)
        if eb is not None:
            meta.data.edit_bones.remove(eb)
    bpy.ops.object.mode_set(mode='OBJECT')
    zs = [v for b in meta.data.bones for v in (b.head_local.z, b.tail_local.z)]
    s = height / (max(zs) - min(zs))
    meta.scale = (s, s, s)
    meta.location = (0.0, 0.0, -min(zs) * s)
    # FACE +X. Rigify's metarig faces Blender's -Y; the fighter faces +X
    # (ADR-019 D2: every clip is authored in place facing +X, Fighter::facing
    # == 0, and FightPresentation yaws it 180 degrees for the other side). A
    # +90-degree yaw about Z turns -Y onto +X; the exporter's +Y-up conversion
    # keeps X as X. The cylinders hid the first export's facing (the body is
    # left/right symmetric); a punch would not have.
    meta.rotation_euler = (0.0, 0.0, math.radians(90.0))
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return meta


def generate_deform_rig(meta):
    """rigify.generate, then keep only the deform bones as a plain FK hierarchy."""
    bpy.context.view_layer.objects.active = meta
    bpy.ops.pose.rigify_generate()
    rig = [o for o in bpy.data.objects if o.type == 'ARMATURE' and o is not meta][-1]
    rig.name = 'fighter_a_rig'
    rig.data.name = 'fighter_a_rig'

    # Constraints first: every DEF bone copies an ORG/MCH bone that is about to go.
    for pb in rig.pose.bones:
        for c in list(pb.constraints):
            pb.constraints.remove(c)
        pb.custom_shape = None

    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    ebs = rig.data.edit_bones

    meta_parent = {b.name: (b.parent.name if b.parent is not None else None)
                   for b in meta.data.bones}

    def chain_end(metarig_bone):
        """DEF-<bone>, or the last of its segments DEF-<bone>.001, .002 ...: the
        bone Rigify itself hangs the next deform bone off (DEF-shin.L sits under
        DEF-thigh.L.001). A name that is a metarig bone in its own right
        (spine.001 under spine) is not a segment and ends the chain."""
        eb = ebs.get('DEF-' + metarig_bone)
        i = 1
        while eb is not None:
            seg = '%s.%03d' % (metarig_bone, i)
            nxt = ebs.get('DEF-' + seg)
            if seg in meta_parent or nxt is None or not nxt.use_deform:
                return eb
            eb, i = nxt, i + 1
        return None

    def deform_parent_for(eb):
        """A segment stays under its chain (its parent is already a deform bone);
        every other deform bone takes the chain end of its METARIG parent.
        Rigify parents DEF-x to ORG-x, to MCH- bones or to controls (the pelves
        and shoulders hang off the `hips` and `chest` controls), all of which are
        about to go, and a walk that accepted DEF- ancestors only shipped a
        forest of nine roots. The metarig is the hierarchy the artist authored,
        so the metarig's is the hierarchy the export keeps."""
        if eb.parent is not None and eb.parent.use_deform:
            return eb.parent
        name = eb.name[len('DEF-'):]
        if name not in meta_parent:         # a segment: thigh.L.001 -> thigh.L
            name = name.rsplit('.', 1)[0]
        parent = meta_parent.get(name)
        while parent is not None:
            end = chain_end(parent)
            if end is not None and end is not eb:
                return end
            parent = meta_parent.get(parent)
        return None

    deform = [eb for eb in ebs if eb.use_deform]
    for eb in deform:
        target = deform_parent_for(eb)
        if eb.parent is not target:
            eb.use_connect = False
            eb.parent = target
    for eb in [eb for eb in ebs if not eb.use_deform]:
        ebs.remove(eb)
    bpy.ops.object.mode_set(mode='OBJECT')
    roots = [b.name for b in rig.data.bones if b.parent is None]
    if roots != ['DEF-spine']:
        raise SystemExit('make_mannequin: the deform skeleton is not one tree rooted at the hips; roots %r' % (roots,))

    # Every bone collection Rigify made for the control layers is now empty or
    # holds only deform bones; one plain collection is enough.
    for coll in list(rig.data.collections_all):
        rig.data.collections.remove(coll)
    deform_names = [b.name for b in rig.data.bones]
    if len(deform_names) > PALETTE_CAP:
        raise SystemExit('make_mannequin: %d deform bones exceed the %d-joint palette (Skeleton.h)'
                         % (len(deform_names), PALETTE_CAP))
    if any(not b.use_deform for b in rig.data.bones):
        raise SystemExit('make_mannequin: a non-deform bone survived the strip')
    return rig


def remove_rigify_leftovers(meta):
    """The metarig and Rigify's widget objects: use_selection=False exports the
    whole scene, so anything left is in the file."""
    for o in list(bpy.data.objects):
        if o is meta or o.name.startswith('WGT'):
            bpy.data.objects.remove(o, do_unlink=True)
    for coll in list(bpy.data.collections):
        if coll.name.startswith('WGTS') or coll.name.startswith('Widgets'):
            bpy.data.collections.remove(coll)


def radius_for(bone_name, length):
    n = bone_name
    if 'spine.006' in n:     return 4.0        # head bone: the sphere below covers it
    if 'spine.004' in n or 'spine.005' in n: return 1.9   # neck
    if 'spine' in n:         return max(3.6, length * 0.55)  # torso
    if 'pelvis' in n:        return 2.4
    if 'shoulder' in n:      return 1.6
    if 'thigh' in n:         return 2.3
    if 'shin' in n:          return 1.8
    if 'upper_arm' in n:     return 1.7
    if 'forearm' in n:       return 1.4
    if 'hand' in n:          return 1.3
    if 'foot' in n:          return 1.5
    if 'toe' in n:           return 1.1
    return max(1.0, length * 0.15)


def build_body(rig, height):
    """One cylinder per deform bone, a sphere for the head, soles and crown flattened."""
    parts = []
    bpy.ops.object.select_all(action='DESELECT')
    for b in rig.data.bones:
        h = rig.matrix_world @ b.head_local
        t = rig.matrix_world @ b.tail_local
        length = (t - h).length
        if length < 1e-4:
            continue
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=radius_for(b.name, length),
                                            depth=length, location=(h + t) * 0.5)
        o = bpy.context.active_object
        o.rotation_mode = 'QUATERNION'
        o.rotation_quaternion = (t - h).to_track_quat('Z', 'Y')
        parts.append(o)
    head = rig.data.bones.get('DEF-spine.006')
    if head is not None:
        top = (rig.matrix_world @ head.tail_local).z
        r = 5.0
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, radius=r,
                                             location=Vector((0.0, 0.0, top - r * 0.85)))
        parts.append(bpy.context.active_object)
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.active_object
    body.name = 'fighter_a_body'
    body.data.name = 'fighter_a_body'
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Feet at exactly 0, crown at exactly `height` (ADR-019 D5), flattened rather
    # than scaled so the skeleton and the skin keep the same proportions.
    for v in body.data.vertices:
        if v.co.z < 0.0:
            v.co.z = 0.0
        if v.co.z > height:
            v.co.z = height
    # ORDER THE FACES BY GEOMETRY. Measured on Blender 5.2.1: the UV sphere
    # primitive's 288 faces come out of Blender in a different order on every
    # run (same vertices, same set of triangles), and the exporter writes
    # triangles in face order, so the index buffer -- and the .bin's hash --
    # changed on each regeneration while nothing about the model had. Sorting
    # by centroid makes the exported bytes a function of the geometry alone,
    # which is what lets a diff on this directory mean a change.
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bm.faces.ensure_lookup_table()
    bm.faces.index_update()
    by_centroid = sorted(range(len(bm.faces)),
                         key=lambda i: tuple(round(c, 4) for c in bm.faces[i].calc_center_median()))
    rank = {face_index: r for r, face_index in enumerate(by_centroid)}
    bm.faces.sort(key=lambda f: rank[f.index])
    bm.to_mesh(body.data)
    bm.free()
    mat = bpy.data.materials.new('mannequin')
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if bsdf is not None:
        bsdf.inputs['Base Color'].default_value = (0.72, 0.72, 0.75, 1.0)
        bsdf.inputs['Roughness'].default_value = 0.9
        bsdf.inputs['Metallic'].default_value = 0.0
    body.data.materials.append(mat)
    return body


def bind(body, rig):
    """Automatic weights, then capped at four influences per vertex and renormalised."""
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    r = bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    if 'FINISHED' not in r:
        raise SystemExit('make_mannequin: automatic weights failed: %r' % (r,))
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=INFLUENCE_CAP)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    # QUANTISE, so a regeneration is byte-identical. Measured: bone heat's
    # solve differs run to run in the last bit (max 1e-7), and the exporter
    # then splits and orders vertices differently, so the .bin changed on
    # every run while every other file did not. Snapping each weight to 1/255
    # (what an 8-bit normalised weight buffer carries anyway) and giving the
    # residual to the heaviest influence removes the noise; the caps above
    # still hold because a zero stays zero and the count cannot grow.
    for v in body.data.vertices:
        groups = [g for g in v.groups if g.weight > 1e-5]
        if not groups:
            continue
        snapped = [round(g.weight * 255.0) / 255.0 for g in groups]
        heaviest = max(range(len(groups)), key=lambda i: snapped[i])
        snapped[heaviest] += 1.0 - sum(snapped)
        for g, w in zip(groups, snapped):
            body.vertex_groups[g.group].add([v.index], max(w, 0.0), 'REPLACE')
    influences = [sum(1 for g in v.groups if g.weight > 1e-5) for v in body.data.vertices]
    if max(influences) > INFLUENCE_CAP:
        raise SystemExit('make_mannequin: a vertex still has %d influences' % max(influences))
    if min(influences) == 0:
        raise SystemExit('make_mannequin: %d vertices have no weight' % influences.count(0))
    return influences


def key_idle(rig):
    """The 2-frame `idle` (ADR-019 D2: a cycle is any length >= 2, stepped): rest at
    frame 0, a breath at frame 1. Every deform bone is keyed so every joint has
    a channel; M3.3c replaces this with the generated set."""
    rig.animation_data_create()
    act = bpy.data.actions.new('idle')
    rig.animation_data.action = act
    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    for frame in (0, 1):
        for pb in rig.pose.bones:
            pb.location = (0.0, 0.0, 0.0)
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.scale = (1.0, 1.0, 1.0)
        if frame == 1:
            chest = rig.pose.bones.get('DEF-spine.003')
            if chest is not None:
                chest.rotation_quaternion = Vector((0.99991, 0.01309, 0.0, 0.0)).normalized()  # 1.5 degrees
        for pb in rig.pose.bones:
            pb.keyframe_insert('location', frame=frame)
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            pb.keyframe_insert('scale', frame=frame)
    common.stash_action(rig, act, 2)


def write_rig_bones(rig, path):
    missing = [v for v in SEMANTIC.values() if v not in rig.data.bones]
    if missing:
        raise SystemExit('make_mannequin: semantic map names bones the rig lacks: %s' % missing)
    doc = {
        '_note': 'Semantic names for the pose library (ADR-019 D6): poses.json is written against '
                 'these keys and never against a Rigify name. Values are the deform bones of '
                 'fighter_a.gltf as pinned by rig_manifest.json.',
        'bones': SEMANTIC,
    }
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write('\n')


def write_credits(path, version, deform_count, vertex_count, clips, sources, generator, poses_note):
    """CREDITS.md per ADR-019 D10: author, licence, generator, Blender version, and
    one row per clip naming where its poses came from (`sources[clip]`)."""
    rows = '\n'.join('| `%s` | %d | %s | Hisham Ata |' % (n, c, sources.get(n, '`%s`' % generator))
                     for n, c in sorted(clips.items()))
    text = f"""# fighter_a placeholder model

Project-authored placeholder (ROADMAP M3.3b, M3.3c) under the repository's
`LICENSE.txt` (**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `Games/UntitledFighter/tools/blender/{generator}`, run headless
  (the rig and body come from `make_mannequin.py`; the clips are {poses_note}).
- **Blender:** {version}. The skeleton is the deform set of Rigify's basic human
  metarig (`rigify`, bundled with Blender); output created with Blender is the
  creator's own work, per Blender's licence FAQ. No third-party asset is used or
  derived from.
- **Skeleton:** {deform_count} deform bones, one tree rooted at the hips, pinned by
  `rig_manifest.json` (`common.enforce_rig_manifest` refuses an export that
  drifts). Semantic names in `rig_bones.json`.
- **Body:** {vertex_count} vertices of primitives along the bones, 60 units tall
  with feet at y = 0 (1 unit = 1 kernel pixel), facing +X, one flat material, no
  textures.

| Clip | Frames | Source | Author |
|---|---|---|---|
{rows}

`fighter_a.clips.json` beside the model is the frame count per clip the
character loader asserts against (A21/A22); `fighter_a.json` names this model
under `engine.anim3d.model` (ROADMAP M3.3c).
"""
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def build_mannequin(height):
    """A fresh scene holding the rig and its bound body: what make_move_clips.py
    keys every clip onto, and what this script alone exports for inspection.
    Returns (rig, body, influences per vertex)."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    enable_rigify()
    common.pin_scene()
    meta = build_metarig(height)
    rig = generate_deform_rig(meta)
    remove_rigify_leftovers(meta)
    body = build_body(rig, height)
    influences = bind(body, rig)
    return rig, body, influences


def write_model_files(out, rig, body, influences, sources, generator, poses_note):
    """rig_manifest.json (first run only), rig_bones.json, the export, CREDITS.md."""
    manifest = os.path.join(out, 'rig_manifest.json')
    if not os.path.exists(manifest):
        common.write_manifest(rig, manifest)
        print('%s: wrote %s (first run; later runs are held to it)' % (generator, manifest))
    write_rig_bones(rig, os.path.join(out, 'rig_bones.json'))
    gltf = os.path.join(out, 'fighter_a.gltf')
    version, clips = common.export(gltf, animated_obj=rig, manifest_path=manifest)
    write_credits(os.path.join(out, 'CREDITS.md'), version, len(rig.data.bones),
                  len(body.data.vertices), clips, sources, generator, poses_note)
    zs = [v.co.z for v in body.data.vertices]
    print('%s: Blender %s wrote %s -- %d deform bones, %d vertices, height %.2f..%.2f, '
          'max influences %d, %d clip(s), %d frames'
          % (generator, version, gltf, len(rig.data.bones), len(body.data.vertices),
             min(zs), max(zs), max(influences), len(clips), sum(clips.values())))
    return version, clips


def main():
    out, height = parse_args()
    if '--out' not in sys.argv:
        raise SystemExit('make_mannequin: --out is required. The committed model under %s is written by '
                         'make_move_clips.py, which builds this mannequin and keys every clip; this script '
                         'alone exports the rig with a 2-frame idle, for inspection.' % DEFAULT_OUT)
    os.makedirs(out, exist_ok=True)
    rig, body, influences = build_mannequin(height)
    key_idle(rig)
    write_model_files(out, rig, body, influences, {'idle': '`make_mannequin.py` (`key_idle`)'},
                      'make_mannequin.py', 'the rig-only export: a 2-frame idle and nothing else')


if __name__ == '__main__':
    main()
