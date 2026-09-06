"""Every clip of the placeholder fighter, generated from the frame data and a pose library.

ROADMAP M3.3c; ADR-019 D2 (the clip contract), D6 (poses.json, capture_pose.py).
Run headless from the repository root:

    blender --background --python Games/UntitledFighter/tools/blender/make_move_clips.py
        [-- --character Games/UntitledFighter/Assets/Characters/fighter_a.json]
        [   --poses     Games/UntitledFighter/Assets/Characters/fighter_a/model/poses.json]
        [   --out       Games/UntitledFighter/Assets/Characters/fighter_a/model]
        [   --height 60] [--blend <where to save the scene for a viewport session>]

It builds the mannequin (make_mannequin.build_mannequin: the rig, the body, the
weights), keys ONE STEPPED POSE PER FRAME for every attack and every reserved
cycle, and exports through common.export. This is the script of record for the
committed fighter_a.gltf / .bin / .clips.json, rig_bones.json and CREDITS.md;
tests/test_shipped_clips.cpp holds the exported bytes to the frame data.

WHAT A CLIP IS MADE OF.

  An attack -- every entry of the character's `moves` -- is exactly
  startup + active + recovery frames (assertion A21; CharacterData.cpp and
  scripts/check_clips.py spell the same sum). [0, startup) blends the idle pose
  into the move's ANTICIPATION pose, reaching it on the last startup frame;
  [startup, startup + active) HOLDS the CONTACT pose, so the fist is inside the
  live hitbox for exactly the ticks the kernel says; the recovery blends into
  the RECOVERY pose over its first third, holds it, and its last quarter
  converges on idle (D2). The three poses come from poses.json `moves`:
  `_default`, overridden by `by_tag` for the move's engine.tags in the order
  crouch, air, kick, special, super, overridden by an entry under the move id.

  A reserved cycle (the fourteen of A22, in PoseKind order) takes its length
  from poses.json `cycles` -- an integer, or one of max_hitstun,
  max_air_hitstun, max_blockstun, max_knockdown, the largest matching counter
  in the character file, so a countdown cycle is as long as the longest count
  it answers (D2: `knockdown` is EXACTLY the largest knockdownTicks) -- and its
  keys, `[frame, pose]` with a negative frame counted from the end (-1 is the
  last frame). Between keys the pose blends frame by frame; a `loop` cycle
  blends from its last key back to its first.

  Every frame is a KEY with CONSTANT interpolation (common.pin_scene). The
  blends are computed HERE, per frame, so the exported clip is stepped and the
  engine samples key k as frame k; nothing interpolates at runtime.

HOW A POSE IS WRITTEN. poses.json `_axes` is the artist's statement; this is
the maths. A bone's value [elevation, azimuth, twist] is WHERE THE BONE POINTS
in the world (Blender axes: +X toward the opponent, +Z up, +Y the fighter's
left): elevation in degrees above the horizontal plane (90 straight up, -90
straight down), azimuth in degrees from +X toward the bone's own side (l_ and
center bones: +Y; r_ bones: -Y, so a mirrored pose is the same numbers; 180 is
away from the opponent), and a twist about the bone's own axis, relative to
what the parent handed down, counter-clockwise looking down the bone, mirrored
for r_ bones. Direction is ABSOLUTE and twist is RELATIVE on purpose: "the
fist points at the opponent" is what a fighting pose says, whatever the torso
did, while "the chest turns 5 more degrees than the hips" is how a turn is
felt. A first cut used Euler swings about the world axes; on Rigify's A-pose
arms, which point sideways at rest, a swing about the left axis was nearly a
twist and the jab's fist went 22 px to the side and 10 px forward.

  Resolved parent-first: with W_p the parent's accumulated rotation (the
  root's parent is the armature, identity), d the bone's rest direction
  (bone.matrix_local's Y axis) and t the target direction,
      Q = rotation_difference(d, W_p^-1 t) @ twist(d, roll),    W = W_p @ Q,
  so the bone's world direction W_p Q d is t exactly. An unspecified bone has
  Q = identity and follows its parent; a captured bone is its Q as a
  quaternion, {"q": [w, x, y, z]}, which capture_pose.py recovers from the
  basis. Q is applied in the parent's posed frame, and a pose bone's basis is
  Q conjugated into the bone's rest frame, B = R^-1 Q R (R the rest rotation
  of bone.matrix_local), which is exact: Blender's pose matrix is
  parent.matrix @ parent.rest^-1 @ rest @ B, so B = rest^-1 (T Q T^-1) rest
  with the translation cancelling. The root's `hips_drop` is a translation of
  (0, 0, -drop) in armature space, R^-1 of it in the basis; the root never
  moves in the ground plane (D2), and tests/test_shipped_clips.cpp checks that
  on the exported bytes. Between two poses the resolved Q's are slerped bone
  by bone.

DETERMINISM. Regenerating is byte-identical (make_mannequin.py's weight snap
and face sort, and per-frame maths in plain Python floats), so `git diff` on
the model directory means a change.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common          # noqa: E402
import make_mannequin  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
DEFAULT_CHARACTER = os.path.join(REPO, 'Games', 'UntitledFighter', 'Assets', 'Characters', 'fighter_a.json')
DEFAULT_OUT = make_mannequin.DEFAULT_OUT
DEFAULT_POSES = os.path.join(DEFAULT_OUT, 'poses.json')

# ADR-019 D2 / CharacterData.h kReservedCycleNames, in PoseKind order.
RESERVED_CYCLES = ('idle', 'walk_fwd', 'walk_back', 'crouch_idle', 'crouch_walk', 'jump_rise',
                   'jump_fall', 'hitstun_stand', 'hitstun_air', 'blockstun_stand', 'blockstun_crouch',
                   'knockdown', 'ko', 'win')
TAG_ORDER = ('crouch', 'air', 'kick', 'special', 'super')
PHASES = ('anticipation', 'contact', 'recovery')
IDENTITY = Quaternion((1.0, 0.0, 0.0, 0.0))


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'character': DEFAULT_CHARACTER, 'poses': DEFAULT_POSES, 'out': DEFAULT_OUT,
            'height': 60.0, 'blend': None}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip('-')
        if key not in opts or i + 1 >= len(argv):
            raise SystemExit('make_move_clips: unknown or valueless argument %r' % argv[i])
        opts[key] = float(argv[i + 1]) if key == 'height' else argv[i + 1]
        i += 2
    return opts


# --- the frame data ----------------------------------------------------------

def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def move_duration(move):
    """MoveDuration's per-component clamp (Combat.cpp), as A21 and check_clips.py spell it."""
    return sum(max(int(move.get(k, 0) or 0), 0) for k in ('startup', 'active', 'recovery'))


def counters(character):
    """The largest counter of each kind the file authors: the lengths D2 ties the
    countdown cycles to. knockdownTicks is engine.reaction.fall_recover_ticks
    (MatchBuilder writes it into MoveDef::knockdownTicks)."""
    def reaction(m):
        return ((m.get('engine') or {}).get('reaction') or {})
    moves = character['moves']
    return {
        'max_hitstun':     max(int(m.get('hitstun') or 0) for m in moves),
        'max_air_hitstun': max(int(reaction(m).get('air_hitstun_ticks') or 0) for m in moves),
        'max_blockstun':   max(int(reaction(m).get('blockstun_ticks') or 0) for m in moves),
        'max_knockdown':   max(int(reaction(m).get('fall_recover_ticks') or 0) for m in moves),
    }


def walk_speed_px(character):
    """The speed the presentation indexes every walk cycle by: the kernel's
    FighterData::walkSpeedSub (FightPresentation::ClipFrameFor), which
    MatchBuilder takes from the character's walk speed -- engine.constants.
    walk_fwd_sub in sub-units, or walk_speed in reach units. One frame per
    that many pixels of posX, forward or back, so a planted foot must slide
    back exactly this far per frame for the picture not to skate."""
    constants = (character.get('engine') or {}).get('constants') or {}
    if constants.get('walk_fwd_sub'):
        return float(constants['walk_fwd_sub']) / 256.0
    units = (character.get('engine') or {}).get('units') or {}
    return float(character['walk_speed']) * float(units.get('pixels_per_reach_unit', 100))


# --- the walk: a gait the kernel's speed can index -------------------------

def leg_geometry(rig, bones):
    """Per side: the hip joint at rest, thigh and shin lengths (both Rigify
    segments), and the ankle's rest height, all in armature space."""
    geo = {}
    for side in ('l', 'r'):
        thigh, thigh2 = rig.data.bones[bones['%s_thigh' % side]], rig.data.bones[bones['%s_thigh_lower' % side]]
        shin, shin2 = rig.data.bones[bones['%s_shin' % side]], rig.data.bones[bones['%s_shin_lower' % side]]
        geo[side] = {'hip': thigh.head_local.copy(), 'a': thigh.length + thigh2.length,
                     'b': shin.length + shin2.length, 'ankle_z': shin2.tail_local.z}
    return geo


def two_bone_ik(hip, ankle, a, b):
    """Thigh and shin directions that put the ankle on its target, knee forward
    (+X), solved in the leg's sagittal plane; an out-of-reach target is
    brought to the leg's length along the same line."""
    dx, dz = ankle.x - hip.x, ankle.z - hip.z
    d = math.hypot(dx, dz)
    d = min(max(d, abs(a - b) + 1e-3), a + b - 1e-3)
    theta = math.atan2(dx, -dz)                                   # from straight down, toward +X
    alpha = math.acos((a * a + d * d - b * b) / (2.0 * a * d))    # at the hip, thigh off the hip-ankle line
    t = theta + alpha
    thigh = Vector((math.sin(t), 0.0, -math.cos(t)))
    knee = hip + thigh * a
    reach = hip + Vector((math.sin(theta), 0.0, -math.cos(theta))) * d
    shin = (reach - knee).normalized()
    return thigh, shin


def aim_rotation(frame, W_parent, target):
    """Q (parent frame) pointing `frame`'s bone along the world direction `target`."""
    return frame.d.rotation_difference(W_parent.inverted() @ target).normalized()


def walk_gait(bases_per_frame, spec, frames, geo, v):
    """Overwrite the legs of every frame with a gait at v px per frame: each
    foot planted for half the cycle, sliding back exactly v per frame, then
    swinging forward along a lifted arc; the two feet half a cycle apart. The
    hips, torso and arms stay what the keys blended. Indexed by posX at v per
    frame (WalkCycleFrame), a planted foot holds its place in the world; played
    backward -- posX falling -- the same clip plants the same foot, so one gait
    serves walk_fwd and walk_back."""
    n = len(bases_per_frame)
    if n % 2:
        raise SystemExit('make_move_clips: a walk cycle needs an even frame count, not %d' % n)
    half = n // 2
    stride = half * v
    lift = float(spec.get('lift', 3.0))
    hips = frames['hips']
    Rq_hips = hips.R.to_quaternion()
    out = []
    for f, bases in enumerate(bases_per_frame):
        bases = dict(bases)
        q_hips, loc = bases['hips']
        W_hips = (Rq_hips @ q_hips @ Rq_hips.inverted()).normalized()
        offset = (hips.R @ loc) if loc is not None else Vector((0.0, 0.0, 0.0))
        for side, phase in (('l', 0), ('r', half)):
            g = geo[side]
            k = (f + phase) % n
            if k < half:
                x, z = stride / 2.0 - v * k, g['ankle_z']
            else:
                s = (k - half) / float(half)
                x, z = -stride / 2.0 + stride * s, g['ankle_z'] + lift * math.sin(math.pi * s)
            hip_rest = g['hip']
            root = frames['hips'].head
            hip = root + (W_hips @ (hip_rest - root)) + offset
            thigh_dir, shin_dir = two_bone_ik(hip, Vector((x, hip.y, z)), g['a'], g['b'])
            thigh, shin, foot = frames['%s_thigh' % side], frames['%s_shin' % side], frames['%s_foot' % side]
            q_thigh = aim_rotation(thigh, W_hips, thigh_dir)
            W_thigh = (W_hips @ q_thigh).normalized()
            q_shin = aim_rotation(shin, W_thigh, shin_dir)
            W_shin = (W_thigh @ q_shin).normalized()
            q_foot = aim_rotation(foot, W_shin, foot.d)          # the foot stays level, as at rest
            for semantic, frame, q in (('%s_thigh' % side, thigh, q_thigh), ('%s_shin' % side, shin, q_shin),
                                       ('%s_foot' % side, foot, q_foot)):
                Rq = frame.R.to_quaternion()
                bases[semantic] = ((Rq.inverted() @ q @ Rq).normalized(), None)
            for lower in ('%s_thigh_lower' % side, '%s_shin_lower' % side, '%s_toe' % side):
                bases[lower] = (IDENTITY.copy(), None)          # segments and toes follow their chain
        out.append(bases)
    return out


# --- the pose library --------------------------------------------------------

def resolve_pose(name, library, seen=()):
    """The pose as {bone: value, 'hips_drop': px}, with `base` and `mirror_of` applied."""
    if name not in library:
        raise SystemExit('make_move_clips: poses.json names no pose %r' % name)
    if name in seen:
        raise SystemExit('make_move_clips: poses.json: `base`/`mirror_of` cycle through %r' % name)
    raw = library[name]
    out = {}
    if 'mirror_of' in raw:
        src = resolve_pose(raw['mirror_of'], library, seen + (name,))
        for k, v in src.items():
            if k == 'hips_drop':
                out[k] = v
            elif k.startswith('l_'):
                out['r_' + k[2:]] = v
            elif k.startswith('r_'):
                out['l_' + k[2:]] = v
            elif isinstance(v, list):
                out[k] = [v[0], -v[1], -v[2]]       # a center bone leans and twists the other way
            else:
                q = Quaternion(v['q'])
                out[k] = {'q': [q.w, -q.x, q.y, -q.z]}   # the rotation reflected across the sagittal plane
    if 'base' in raw:
        out.update(resolve_pose(raw['base'], library, seen + (name,)))
    for k, v in raw.items():
        if k in ('base', 'mirror_of'):
            continue
        out[k] = v
    return out


class RestFrame:
    """One bone at rest: its rotation R, direction d and head in armature space,
    its parent's semantic name, and the sign that mirrors azimuth and twist for r_."""
    def __init__(self, R, d, head, parent, outward):
        self.R, self.d, self.head, self.parent, self.outward = R, d, head, parent, outward


def rest_frames(rig, bones):
    """{semantic: RestFrame}, plus the parent-first order the poses resolve in."""
    by_deform = {deform: semantic for semantic, deform in bones.items()}
    frames = {}
    for semantic, deform in bones.items():
        b = rig.data.bones.get(deform)
        if b is None:
            raise SystemExit('make_move_clips: rig_bones.json maps %r to %r, which the rig lacks' % (semantic, deform))
        R = b.matrix_local.to_3x3()
        d = (R @ Vector((0.0, 1.0, 0.0))).normalized()
        parent = by_deform.get(b.parent.name) if b.parent is not None else None
        if b.parent is not None and parent is None:
            raise SystemExit('make_move_clips: %r hangs off %r, which has no semantic name' % (deform, b.parent.name))
        frames[semantic] = RestFrame(R, d, b.head_local.copy(), parent, -1.0 if semantic.startswith('r_') else 1.0)
    order = []
    def visit(s):
        if s in order:
            return
        if frames[s].parent is not None:
            visit(frames[s].parent)
        order.append(s)
    for s in frames:
        visit(s)
    return frames, order


def aim(elevation, azimuth):
    e, a = math.radians(elevation), math.radians(azimuth)
    return Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))


def parent_frame_rotation(value, frame, W_parent):
    """Q for a pose value on `frame`, applied in the parent's posed frame (docstring)."""
    if isinstance(value, dict):
        return Quaternion(value['q']).normalized()
    elevation, azimuth, twist = (float(v) for v in value)
    target = aim(elevation, azimuth * frame.outward)
    local_target = W_parent.inverted() @ target
    q_aim = frame.d.rotation_difference(local_target)
    q_twist = Quaternion(frame.d, math.radians(twist * frame.outward))
    return (q_aim @ q_twist).normalized()


def pose_bases(pose, frames, order):
    """{semantic: (basis quaternion, basis location or None)} for a resolved pose;
    every bone the pose does not mention follows its parent."""
    for k in pose:
        if k not in frames and k != 'hips_drop':
            raise SystemExit('make_move_clips: poses.json names bone %r, which rig_bones.json lacks' % k)
    bases, W = {}, {}
    drop = float(pose.get('hips_drop', 0.0))
    for semantic in order:
        frame = frames[semantic]
        W_parent = W[frame.parent] if frame.parent is not None else IDENTITY
        value = pose.get(semantic)
        Q = parent_frame_rotation(value, frame, W_parent) if value is not None else IDENTITY
        W[semantic] = (W_parent @ Q).normalized()
        Rq = frame.R.to_quaternion()
        basis = (Rq.inverted() @ Q @ Rq).normalized()
        loc = None
        if semantic == 'hips':
            loc = frame.R.inverted() @ Vector((0.0, 0.0, -drop))
        bases[semantic] = (basis, loc)
    return bases


def blend(a, b, t):
    out = {}
    for k, (qa, la) in a.items():
        qb, lb = b[k]
        q = qa.slerp(qb, t) if t > 0.0 else qa.copy()
        l = None if la is None else la.lerp(lb, t)
        out[k] = (q, l)
    return out


# --- keying ------------------------------------------------------------------

def key_frame(rig, bones, bases, frame):
    for semantic, deform in bones.items():
        pb = rig.pose.bones[deform]
        q, loc = bases[semantic]
        pb.rotation_quaternion = q
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        if loc is not None:
            pb.location = loc
            pb.keyframe_insert('location', frame=frame)


def key_clip(rig, bones, name, frames_of_pose):
    """One action named `name`, one key per frame from the callable frame -> bases."""
    n = len(frames_of_pose)
    if n < 1:
        raise SystemExit('make_move_clips: clip %r would have no frames' % name)
    act = bpy.data.actions.new(name)
    rig.animation_data.action = act
    for f in range(n):
        key_frame(rig, bones, frames_of_pose[f], f)
    common.stash_action(rig, act, n)
    return n


def ramp(k, n):
    """k in [0, n) -> (k + 1) / n: the blend reaches 1 on the last of n frames."""
    return (k + 1) / float(n) if n > 0 else 1.0


def move_frames(move, poses, idle):
    """The per-frame bases of an attack, D2's three phases."""
    s = max(int(move.get('startup') or 0), 0)
    a = max(int(move.get('active') or 0), 0)
    r = max(int(move.get('recovery') or 0), 0)
    ant, con, rec = poses['anticipation'], poses['contact'], poses['recovery']
    out = []
    for k in range(s):
        out.append(blend(idle, ant, ramp(k, s)))
    for k in range(a):
        out.append(con)
    settle = max(1, int(math.ceil(r / 3.0)))       # contact -> recovery over the first third
    converge = int(math.ceil(r / 4.0))             # recovery -> idle over the last quarter
    for k in range(r):
        if k >= r - converge:
            out.append(blend(rec, idle, ramp(k - (r - converge), converge)))
        elif k < settle:
            out.append(blend(con, rec, ramp(k, settle)))
        else:
            out.append(rec)
    return out


def cycle_frames(spec, library_bases, counts, name):
    """The per-frame bases of a reserved cycle from its `frames` and `keys`."""
    n = spec['frames']
    if isinstance(n, str):
        if n not in counts:
            raise SystemExit('make_move_clips: cycle %r: unknown length %r (the choices are %s)'
                             % (name, n, ', '.join(sorted(counts))))
        n = counts[n]
    n = int(n)
    if n < 2:
        raise SystemExit('make_move_clips: cycle %r would have %d frame(s); a cycle is any length >= 2' % (name, n))
    keys = []
    for frame, pose in spec['keys']:
        f = int(frame)
        if f < 0:
            f = n + f
        if not 0 <= f < n:
            raise SystemExit('make_move_clips: cycle %r: key frame %r is outside [0, %d)' % (name, frame, n))
        keys.append((f, library_bases(pose)))
    keys.sort(key=lambda kv: kv[0])
    if not keys or keys[0][0] != 0:
        raise SystemExit('make_move_clips: cycle %r: the first key must be at frame 0' % name)
    out = []
    loop = bool(spec.get('loop', False))
    for f in range(n):
        i = max(j for j in range(len(keys)) if keys[j][0] <= f)
        f0, p0 = keys[i]
        if i + 1 < len(keys):
            f1, p1 = keys[i + 1]
        elif loop:
            f1, p1 = n, keys[0][1]
        else:
            out.append(p0)
            continue
        out.append(blend(p0, p1, (f - f0) / float(f1 - f0)) if f1 > f0 else p0)
    return out


def poses_for_move(move, moves_spec):
    """`_default`, then `by_tag` entries in TAG_ORDER, then `by_tag` entries that
    name several tags joined by `+` (all present; `crouch+kick` for a sweep,
    which is neither a standing roundhouse nor a crouching punch), then the
    move's own entry. Later wins."""
    chosen = dict(moves_spec.get('_default', {}))
    tags = set((move.get('engine') or {}).get('tags') or [])
    by_tag = moves_spec.get('by_tag', {})
    for tag in TAG_ORDER:
        if tag in tags and tag in by_tag:
            chosen.update(by_tag[tag])
    for key, poses in sorted(by_tag.items()):
        if '+' in key and all(t in tags for t in key.split('+')):
            chosen.update(poses)
    chosen.update(moves_spec.get(move['id'], {}))
    missing = [p for p in PHASES if p not in chosen]
    if missing:
        raise SystemExit('make_move_clips: move %r has no %s pose in poses.json' % (move['id'], ', '.join(missing)))
    return chosen


# --- main --------------------------------------------------------------------

def main():
    opts = parse_args()
    character = load_json(opts['character'])
    library = load_json(opts['poses'])
    out = opts['out']
    os.makedirs(out, exist_ok=True)

    rig, body, influences = make_mannequin.build_mannequin(opts['height'])
    bones = make_mannequin.SEMANTIC
    frames, order = rest_frames(rig, bones)
    rig.animation_data_create()
    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'

    cache = {}

    def library_bases(pose_name):
        if pose_name not in cache:
            cache[pose_name] = pose_bases(resolve_pose(pose_name, library['poses']), frames, order)
        return cache[pose_name]

    counts = counters(character)
    cycles_spec = library['cycles']
    missing = [c for c in RESERVED_CYCLES if c not in cycles_spec]
    if missing:
        raise SystemExit('make_move_clips: poses.json lacks the reserved cycle(s) %s (A22)' % ', '.join(missing))
    idle_pose = cycles_spec['idle']['keys'][0][1]
    idle = library_bases(idle_pose)

    sources = {}
    move_ids = set()
    for move in character['moves']:
        mid = move['id']
        clip = ((move.get('engine') or {}).get('anim3d') or {}).get('clip', mid)
        if clip in RESERVED_CYCLES:
            raise SystemExit('make_move_clips: move %r would take the reserved clip name %r' % (mid, clip))
        if clip in move_ids:
            raise SystemExit('make_move_clips: two moves share the clip name %r' % clip)
        move_ids.add(clip)
        chosen = poses_for_move(move, library['moves'])
        poses = {p: library_bases(chosen[p]) for p in PHASES}
        n = key_clip(rig, bones, clip, move_frames(move, poses, idle))
        if n != move_duration(move):
            raise SystemExit('make_move_clips: %r keyed %d frames, MoveDuration is %d' % (clip, n, move_duration(move)))
        sources[clip] = ('`make_move_clips.py`: %d+%d+%d frames from `fighter_a.json`, poses `%s` / `%s` / `%s`'
                         % (max(int(move.get('startup') or 0), 0), max(int(move.get('active') or 0), 0),
                            max(int(move.get('recovery') or 0), 0), chosen['anticipation'], chosen['contact'],
                            chosen['recovery']))
    geo = leg_geometry(rig, bones)
    speed = walk_speed_px(character)
    for cycle in RESERVED_CYCLES:
        spec = cycles_spec[cycle]
        per_frame = cycle_frames(spec, library_bases, counts, cycle)
        if 'walk' in spec:
            per_frame = walk_gait(per_frame, spec['walk'], frames, geo, speed)
        n = key_clip(rig, bones, cycle, per_frame)
        length = spec['frames'] if isinstance(spec['frames'], str) else str(spec['frames'])
        gait = (', legs: a gait at %.4g px per frame (the kernel\'s walk speed), stride %.4g px'
                % (speed, n * speed)) if 'walk' in spec else ''
        sources[cycle] = ('`make_move_clips.py`: cycle, %s frame(s) (%s), poses %s%s'
                         % (n, length, ' / '.join('`%s`' % k[1] for k in spec['keys']), gait))
    if opts['blend']:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(opts['blend']))
        print('make_move_clips: saved the scene to %s (rig, body, one NLA track per clip)' % opts['blend'])

    version, clips = make_mannequin.write_model_files(
        out, rig, body, influences, sources, 'make_move_clips.py',
        'keyed by `make_move_clips.py` from `fighter_a.json` (frame data) and `poses.json` (the pose library)')
    print('make_move_clips: counters %s' % counts)


if __name__ == '__main__':
    main()
