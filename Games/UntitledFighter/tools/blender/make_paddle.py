"""The two-bone paddle: the Blender-exported witness beside the stdlib fixtures.

ROADMAP M3.3a. Everything tests/fixtures/models/make_fixtures.py proves, it
proves with the standard library; what it CANNOT prove is that the installed
Blender's glTF exporter, driven through common.py's pinned options, produces
what the contract says. This script is that proof, and its output is committed:

    blender --background --python Games/UntitledFighter/tools/blender/make_paddle.py
    python scripts/check_clips.py --self-test   # reads the committed output

Two bones (root -> tip), a thin box skinned half to each, and TWO actions
stashed on the NLA -- multi-action export is the path the whole roster rides on
(some forty clips on one armature) and a single-action paddle would have
proven nothing about it:

    paddle_swing   14 stepped keys, frames 0..13 -> 14 samples, t = 0 .. 13/60
    paddle_hold     5 stepped keys, frames 0..4  ->  5 samples, the same pose held

check_clips.py derives each count two ways (accessor count, time span) and
compares to the sidecar this script writes.
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
OUT = os.path.join(REPO, 'tests', 'fixtures', 'models', 'paddle_blender.gltf')


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = common.pin_scene()
    scene.frame_end = 13

    arm = bpy.data.armatures.new('PaddleArm')
    obj = bpy.data.objects.new('Paddle', arm)
    scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    root = arm.edit_bones.new('root')
    root.head, root.tail = (0, 0, 0), (0, 0, 1)
    tip = arm.edit_bones.new('tip')
    tip.head, tip.tail = (0, 0, 1), (0, 0, 2)
    tip.parent, tip.use_connect = root, True
    bpy.ops.object.mode_set(mode='OBJECT')

    mesh = bpy.data.meshes.new('PaddleMesh')
    mo = bpy.data.objects.new('PaddleMesh', mesh)
    verts = [(-0.1, -0.1, 0), (0.1, -0.1, 0), (0.1, 0.1, 0), (-0.1, 0.1, 0),
             (-0.1, -0.1, 2), (0.1, -0.1, 2), (0.1, 0.1, 2), (-0.1, 0.1, 2)]
    faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    scene.collection.objects.link(mo)
    mo.parent = obj
    mo.vertex_groups.new(name='root').add([0, 1, 2, 3], 1.0, 'REPLACE')
    mo.vertex_groups.new(name='tip').add([4, 5, 6, 7], 1.0, 'REPLACE')
    mo.modifiers.new('Armature', 'ARMATURE').object = obj

    obj.animation_data_create()

    def make_action(name, frames, angle_of):
        act = bpy.data.actions.new(name)
        obj.animation_data.action = act
        pb = obj.pose.bones['tip']
        pb.rotation_mode = 'XYZ'
        for f in range(frames):
            pb.rotation_euler = (angle_of(f), 0, 0)
            pb.keyframe_insert('rotation_euler', frame=f)
        common.stash_action(obj, act, frames)

    make_action('paddle_swing', 14, lambda f: math.radians(5 * f))
    make_action('paddle_hold', 5, lambda f: math.radians(30))
    return obj


def main():
    obj = build()
    version, clips = common.export(OUT, animated_obj=obj)
    print('make_paddle: Blender %s wrote %s with clips %s' % (version, OUT, clips))


if __name__ == '__main__':
    main()
