"""Draw a move's kernel boxes as wire boxes in the open scene, at its contact frame.

ROADMAP M3.3c. For a viewport session on the scene make_move_clips.py saved
with --blend:

    blender <scene.blend> --python Games/UntitledFighter/tools/blender/draw_boxes.py
        -- --move stand_lp [--character <fighter_a.json>] [--depth 8]

Adds wire cubes `box_hurt_<move>` and `box_hit_<move>` where MatchBuilder puts
the kernel's boxes and sets the frame to the move's first active frame (contact
= startup, ADR-019 D2), so a screenshot shows FIT, not opinion. Solo the move's
NLA track (or assign its action) to see its pose under the boxes; run again for
another move -- an earlier move's boxes are removed first.

WHERE THE BOXES ARE. fighter_a.json authors no per-move boxes (its anim_note
says so); MatchBuilder.cpp builds them from the body and the reach:

  hurtbox  x in [-hw, hw], y in [0, height_px], hw = default_pushbox_sub's half
           width (13 px; kDefaultBodyHalfWidthSub in MatchBuilder.h agrees);
           crouch_height_px tall for a crouching move; or the move's own
           engine.hurtbox_sub {x0, y0, x1, y1} in sub-units when it authors one.
  hitbox   x in [hw, hw + reach * pixels_per_reach_unit], the whole body height
           (the build's move.hitbox.y loss row: a low kick hits a standing
           body), live over [startup, startup + active).

Kernel y is up from the floor, one pixel is one Blender unit (D5), and the
fighter faces +X; the boxes are `depth` units deep about the sagittal plane.
"""
import json
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_move_clips  # noqa: E402

SUB = 256.0


def parse_args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'move': None, 'character': make_move_clips.DEFAULT_CHARACTER, 'depth': 8.0}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip('-')
        if key not in opts or i + 1 >= len(argv):
            raise SystemExit('draw_boxes: unknown or valueless argument %r' % argv[i])
        opts[key] = float(argv[i + 1]) if key == 'depth' else argv[i + 1]
        i += 2
    if not opts['move']:
        raise SystemExit('draw_boxes: --move <id> is required')
    return opts


def boxes_for(character, move):
    """((x0, y0, x1, y1) hurt, (x0, y0, x1, y1) hit) in pixels, MatchBuilder's derivation."""
    constants = character['engine']['constants']
    units = character['engine']['units']
    push = constants.get('default_pushbox_sub')
    hw = abs(push[2]) / SUB if push else 13.0
    height = float(constants['height_px'])
    tags = set((move.get('engine') or {}).get('tags') or [])
    if 'crouch' in tags or move.get('stance') == 'crouching':
        height = float(constants.get('crouch_height_px', height))
    hurt = (-hw, 0.0, hw, height)
    own = (move.get('engine') or {}).get('hurtbox_sub')
    if isinstance(own, dict):
        hurt = (own['x0'] / SUB, own['y0'] / SUB, own['x1'] / SUB, own['y1'] / SUB)
    reach = move.get('reach')
    hit = None
    if reach is not None:
        hit = (hw, 0.0, hw + float(reach) * float(units.get('pixels_per_reach_unit', 100)), height)
    return hurt, hit


def wire_box(name, box, depth, color):
    x0, y0, x1, y1 = box
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=((x0 + x1) / 2.0, 0.0, (y0 + y1) / 2.0))
    o = bpy.context.active_object
    o.name = name
    o.scale = (max(x1 - x0, 0.01), depth, max(y1 - y0, 0.01))
    o.display_type = 'WIRE'
    o.color = color
    o.show_in_front = True
    return o


def main():
    opts = parse_args()
    with open(opts['character'], encoding='utf-8') as f:
        character = json.load(f)
    move = next((m for m in character['moves'] if m['id'] == opts['move']), None)
    if move is None:
        raise SystemExit('draw_boxes: %s has no move %r' % (opts['character'], opts['move']))
    for o in [o for o in bpy.data.objects if o.name.startswith('box_hurt_') or o.name.startswith('box_hit_')]:
        bpy.data.objects.remove(o, do_unlink=True)
    hurt, hit = boxes_for(character, move)
    wire_box('box_hurt_%s' % move['id'], hurt, opts['depth'], (0.2, 0.6, 1.0, 1.0))
    if hit is not None:
        wire_box('box_hit_%s' % move['id'], hit, opts['depth'], (1.0, 0.25, 0.2, 1.0))
    startup = max(int(move.get('startup') or 0), 0)
    bpy.context.scene.frame_set(startup)
    print('draw_boxes: %s -- hurtbox x %.1f..%.1f y %.1f..%.1f; %s; frame %d (contact = startup; active %s frame(s))'
          % (move['id'], hurt[0], hurt[2], hurt[1], hurt[3],
             'hitbox x %.1f..%.1f y %.1f..%.1f' % (hit[0], hit[2], hit[1], hit[3]) if hit else 'no reach: no hitbox',
             startup, move.get('active')))


if __name__ == '__main__':
    main()
