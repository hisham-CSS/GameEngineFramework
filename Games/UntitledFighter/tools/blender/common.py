"""The one export function every Blender script of record calls.

ROADMAP M3.3a; the contract is docs/adr/ADR-019-placeholders-through-blender.md
D2 (the clip contract) and D8 (tooling posture). Runs inside Blender only:

    blender --background [file.blend] --python <script that imports this>

WHY ONE FUNCTION. The glTF exporter has an option that silently shortens a
held pose (Optimize Animation Size removes duplicate keys), an option that
limits the export to the playback range (and drops frames outside it), a
sampling switch, a frame step, a bone filter and an armature-rest switch. Each
is a way for a clip to disagree with the frame data it was authored to match,
and the whole point of the pipeline is that it cannot. So the options are
pinned HERE, once, and every script that produces a committed byte goes
through `export()`; a script that calls bpy.ops.export_scene.gltf itself is a
bug by definition.

FRAME ZERO. A clip's first sample is moveFrame 0, the tick the move starts.
Actions are keyed from Blender frame 0, their frame range is set explicitly,
"Limit to Playback Range" is OFF and "slide to zero" is ON, so the first
sampled time is exactly 0 and sample k is Blender frame k. The paddle fixture
(make_paddle.py) is the committed witness: 14 keys -> 14 samples, t = 0 .. 13/60.

STEPPED. New keyframes are CONSTANT (the Guilty Gear Xrd look, and the engine
never interpolates: sample k is key k). Blender 5 stores F-curves under an
action's slots rather than `Action.fcurves`, so the interpolation is set through
the keyframe preference BEFORE keys are inserted rather than patched afterwards.
"""
import json
import os

import bpy

FPS = 60

# The pinned exporter options. Names are Blender 5.2's; `selftest()` proves the
# installed exporter still exposes every one before an export is attempted.
PINNED = dict(
    export_format='GLTF_SEPARATE',            # .gltf + .bin + textures beside (ADR-019 D1)
    export_animations=True,
    export_animation_mode='ACTIONS',          # each action -> one glTF animation
    export_force_sampling=True,               # one sample per frame, always
    export_frame_step=1,
    export_optimize_animation_size=False,     # NEVER: it removes duplicate keys, i.e. held poses
    export_anim_slide_to_zero=True,           # first sample at t = 0
    export_frame_range=False,                 # never limit to the playback range
    export_nla_strips=True,                   # stashed actions export as separate animations
    export_negative_frame='SLIDE',
    export_def_bones=True,                    # deform bones only (Rigify's DEF-)
    export_rest_position_armature=True,       # bind pose is the rest pose
    export_skins=True,
    export_influence_nb=4,
    export_all_influences=False,
    export_morph=False,                       # shape keys would be a renderer feature beyond skinning
    export_draco_mesh_compression_enable=False,  # the vcpkg Assimp has no Draco
    export_apply=False,
    export_yup=True,
    export_leaf_bone=False,
    export_hierarchy_flatten_bones=False,
    export_armature_object_remove=False,
    export_merge_animation='ACTION',          # Blender 5 slotted actions: one animation per action
    use_selection=False,
)


def exporter_options():
    """Every option name the installed glTF exporter exposes."""
    return {p.identifier for p in bpy.ops.export_scene.gltf.get_rna_type().properties}


def selftest():
    """Refuse to run against an exporter that lacks a pinned option.

    A missing name would otherwise be a TypeError deep inside bpy, or worse, an
    option silently absent on an older build. Returns the Blender version string
    so a caller can record it beside the fixture it produced.
    """
    missing = sorted(k for k in PINNED if k not in exporter_options())
    if missing:
        raise SystemExit('common.py: the installed glTF exporter (Blender %s) lacks pinned option(s): %s'
                         % (bpy.app.version_string, ', '.join(missing)))
    return bpy.app.version_string


def pin_scene(scene=None):
    """60 fps, frame 0 first, CONSTANT interpolation for every new key."""
    scene = scene or bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    scene.frame_start = 0
    bpy.context.preferences.edit.keyframe_new_interpolation_type = 'CONSTANT'
    return scene


def stash_action(obj, action, frames):
    """Fix the action's range to [0, frames) and put it on its own NLA track.

    The exporter's ACTIONS mode exports the active action and every stashed
    one; leaving an action merely "assigned" exports only that one. The strip
    starts at frame 0 so the sampled times start at 0 too.
    """
    action.use_frame_range = True
    action.frame_start = 0
    action.frame_end = frames - 1
    if obj.animation_data is None:
        obj.animation_data_create()
    obj.animation_data.action = None
    track = obj.animation_data.nla_tracks.new()
    track.name = action.name
    track.strips.new(action.name, 0, action)
    return track


def action_frames(action):
    if not action.use_frame_range:
        raise SystemExit('common.py: action %r has no explicit frame range; stash it with stash_action()'
                         % action.name)
    return int(round(action.frame_end - action.frame_start)) + 1


def clips_of(obj):
    """{clip name: frame count} for every action stashed on `obj`'s NLA."""
    clips = {}
    if obj.animation_data is None:
        return clips
    for track in obj.animation_data.nla_tracks:
        for strip in track.strips:
            if strip.action is not None:
                clips[strip.action.name] = action_frames(strip.action)
    return clips


def deform_bones(arm_obj):
    """[(name, parent name or None)] for every deform bone, in armature order."""
    out = []
    for b in arm_obj.data.bones:
        if not b.use_deform:
            continue
        parent = b.parent
        while parent is not None and not parent.use_deform:
            parent = parent.parent
        out.append((b.name, parent.name if parent else None))
    return out


def enforce_rig_manifest(arm_obj, manifest_path):
    """Refuse an export whose deform skeleton drifted from rig_manifest.json.

    The manifest pins ordered deform-bone names and parents (ADR-019 D6/D7);
    every clip and every test is written against it, so a drift here would
    invalidate all of them silently. CI asserts the same thing on the exported
    bytes (PlaceholderRig.MatchesItsRigManifestBoneForBone, ROADMAP M3.3b).
    """
    with open(manifest_path, encoding='utf-8') as f:
        manifest = json.load(f)
    expected = [(b['name'], b.get('parent')) for b in manifest['bones']]
    actual = deform_bones(arm_obj)
    if expected != actual:
        raise SystemExit('common.py: the deform skeleton drifted from %s\n  expected: %s\n  actual:   %s'
                         % (manifest_path, expected, actual))


def write_manifest(arm_obj, manifest_path):
    bones = [{'name': n, 'parent': p} for n, p in deform_bones(arm_obj)]
    with open(manifest_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'bones': bones}, f, indent=1)
        f.write('\n')


def sidecar_path(gltf_path):
    stem, _ = os.path.splitext(gltf_path)
    return stem + '.clips.json'


def export(gltf_path, animated_obj=None, manifest_path=None):
    """Export the scene through the pinned options and write the sidecar.

    `animated_obj` is the object whose stashed actions are the clips (the
    armature); its `{clip: frames}` becomes `<stem>.clips.json`, the file
    CharacterData.cpp asserts against without Assimp (ADR-019 D2, A21/A22).
    Returns (blender version, clips).
    """
    version = selftest()
    if animated_obj is not None and manifest_path is not None:
        enforce_rig_manifest(animated_obj, manifest_path)
    os.makedirs(os.path.dirname(os.path.abspath(gltf_path)), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=gltf_path, **PINNED)
    clips = clips_of(animated_obj) if animated_obj is not None else {}
    with open(sidecar_path(gltf_path), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(clips, f, indent=1, sort_keys=True)
        f.write('\n')
    return version, clips
