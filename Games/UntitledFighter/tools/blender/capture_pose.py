"""Write the pose at the viewport back into poses.json (ROADMAP M3.3c; ADR-019 D6).

    blender --background <scene.blend> --python Games/UntitledFighter/tools/blender/capture_pose.py
        -- --pose <name> [--poses <poses.json>] [--armature fighter_a_rig] [--frame <n>] [--clip <name>]

The scene is the one make_move_clips.py saves with --blend, or any .blend that
holds the mannequin rig. With --clip the clip's action is assigned to the rig
first (the NLA's solo flag cannot be set from a script, so a headless read of a
stashed clip goes through the action); without it the pose is whatever the
scene evaluates -- the action you assigned, or the track you soloed, at the
viewport. At --frame (default: the scene's current frame) the armature's
evaluated pose is read bone by bone and written under poses.json
`poses.<name>` as {"q": [w, x, y, z]} per semantic bone -- the world-axes
rotation make_move_clips.py applies, recovered exactly from the pose basis
(Q = R B R^-1; the docstring there derives B = R^-1 Q R) -- plus `hips_drop`
from the root's translation. Bones at rest are left out. The next
make_move_clips.py run keys the refined pose; poselib.dump keeps the file's
layout, so the diff is the pose.

Refine a pose at the viewport by soloing the clip's NLA track (or assigning
its action), moving to the frame, posing, saving the .blend, and running this.
A pose captured from a blended frame is the blend -- capture at a key frame
(a clip's frame 0, a cycle's key) or from a pose you set yourself.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_mannequin  # noqa: E402
import poselib         # noqa: E402

DEFAULT_POSES = os.path.join(make_mannequin.DEFAULT_OUT, 'poses.json')
REST_EPSILON_DEG = 0.05


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'pose': None, 'poses': DEFAULT_POSES, 'armature': 'fighter_a_rig', 'frame': None, 'clip': None}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip('-')
        if key not in opts or i + 1 >= len(argv):
            raise SystemExit('capture_pose: unknown or valueless argument %r' % argv[i])
        opts[key] = int(argv[i + 1]) if key == 'frame' else argv[i + 1]
        i += 2
    if not opts['pose']:
        raise SystemExit('capture_pose: --pose <name> is required')
    return opts


def capture(rig, bones):
    """{semantic: {"q": [...]}, "hips_drop": px} for every bone away from rest."""
    pose = {}
    for semantic, deform in bones.items():
        pb = rig.pose.bones.get(deform)
        if pb is None:
            raise SystemExit('capture_pose: the armature has no bone %r (%s)' % (deform, semantic))
        R = pb.bone.matrix_local.to_3x3()
        Rq = R.to_quaternion()
        basis = pb.matrix_basis.to_quaternion().normalized()
        Q = (Rq @ basis @ Rq.inverted()).normalized()
        if math.degrees(Q.angle) > REST_EPSILON_DEG:
            pose[semantic] = {'q': [round(c, 6) for c in (Q.w, Q.x, Q.y, Q.z)]}
        if semantic == 'hips':
            offset = R @ Vector(pb.matrix_basis.to_translation())
            if abs(offset.x) > 1e-3 or abs(offset.y) > 1e-3:
                print('capture_pose: WARNING the hips are off the origin in the ground plane by (%.3f, %.3f); '
                      'D2 forbids root motion, only the drop is kept' % (offset.x, offset.y))
            drop = round(-offset.z, 3)
            if abs(drop) > 1e-3:
                pose['hips_drop'] = drop
    return pose


def main():
    opts = parse_args()
    rig = bpy.data.objects.get(opts['armature'])
    if rig is None or rig.type != 'ARMATURE':
        raise SystemExit('capture_pose: no armature object %r in this scene' % opts['armature'])
    if opts['clip'] is not None:
        act = bpy.data.actions.get(opts['clip'])
        if act is None:
            raise SystemExit('capture_pose: this scene has no action (clip) %r' % opts['clip'])
        if rig.animation_data is None:
            rig.animation_data_create()
        rig.animation_data.action = act
        rig.animation_data.action_slot = act.slots[0]
    if opts['frame'] is not None:
        bpy.context.scene.frame_set(opts['frame'])
    else:
        bpy.context.scene.frame_set(bpy.context.scene.frame_current)
    doc = poselib.load(opts['poses'])
    pose = capture(rig, make_mannequin.SEMANTIC)
    doc.setdefault('poses', {})[opts['pose']] = pose
    poselib.dump(doc, opts['poses'])
    print('capture_pose: wrote poses.%s (%d bone(s) away from rest%s) into %s at frame %d'
          % (opts['pose'], sum(1 for k in pose if k != 'hips_drop'),
             ', hips_drop %s' % pose['hips_drop'] if 'hips_drop' in pose else '',
             opts['poses'], bpy.context.scene.frame_current))


if __name__ == '__main__':
    main()
