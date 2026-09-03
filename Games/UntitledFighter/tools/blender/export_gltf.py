"""Export the open .blend through the pinned options, with its sidecar.

ROADMAP M3.3a. The script of record for any hand-touched scene:

    blender --background file.blend --python Games/UntitledFighter/tools/blender/export_gltf.py -- \
        --out Games/UntitledFighter/Assets/Characters/fighter_a/model/fighter_a.gltf \
        [--armature Rig] [--manifest .../rig_manifest.json]
    blender --background --python .../export_gltf.py -- --selftest

Arguments after `--` are this script's; Blender ignores them. `--armature`
names the object whose stashed actions are the clips (default: the first
armature in the scene); `--manifest` refuses the export if the deform skeleton
drifted from rig_manifest.json (ADR-019 D6). `--selftest` only checks that the
installed exporter exposes every pinned option and prints the Blender version.
"""
import argparse
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    p = argparse.ArgumentParser(prog='export_gltf.py')
    p.add_argument('--out', help='destination .gltf (the .bin and the .clips.json sidecar land beside it)')
    p.add_argument('--armature', help='object whose stashed actions are the clips')
    p.add_argument('--manifest', help='rig_manifest.json to enforce before exporting')
    p.add_argument('--selftest', action='store_true', help='check the exporter options and exit')
    return p.parse_args(argv)


def main():
    args = parse_args()
    if args.selftest:
        print('export_gltf: selftest OK on Blender %s -- every pinned option is present' % common.selftest())
        return
    if not args.out:
        raise SystemExit('export_gltf: --out is required (or --selftest)')
    arm = None
    if args.armature:
        arm = bpy.data.objects.get(args.armature)
        if arm is None:
            raise SystemExit('export_gltf: no object named %r' % args.armature)
    else:
        for o in bpy.data.objects:
            if o.type == 'ARMATURE':
                arm = o
                break
    version, clips = common.export(args.out, animated_obj=arm, manifest_path=args.manifest)
    print('export_gltf: Blender %s wrote %s; clips: %s' % (version, args.out, clips))


if __name__ == '__main__':
    main()
