"""poses.json on disk: load it, and write it back in the one layout it is kept in.

ROADMAP M3.3c; ADR-019 D6. bpy-free, so it runs under plain Python too. The
layout is what a person edits: objects one key per line, arrays of scalars and
arrays of short arrays on one line ([swing, side, turn], the cycle keys). Both
the committed file and capture_pose.py's rewrite go through dump(), so a capture
changes the pose it captured and nothing else -- a diff on poses.json is a pose.
"""
import json


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def _scalar(v):
    return v is None or isinstance(v, (bool, int, float, str))


def _inline(v):
    """A list that fits on one line: scalars, or short lists of scalars."""
    if not isinstance(v, list):
        return False
    return all(_scalar(x) or (isinstance(x, list) and len(x) <= 4 and all(_scalar(y) for y in x)) for x in v)


def dumps(obj, indent=1, level=0):
    pad = ' ' * indent * (level + 1)
    end = ' ' * indent * level
    if isinstance(obj, dict):
        if not obj:
            return '{}'
        items = ['%s%s: %s' % (pad, json.dumps(k), dumps(v, indent, level + 1)) for k, v in obj.items()]
        return '{\n' + ',\n'.join(items) + '\n' + end + '}'
    if isinstance(obj, list):
        if _inline(obj):
            return json.dumps(obj)
        items = ['%s%s' % (pad, dumps(v, indent, level + 1)) for v in obj]
        return '[\n' + ',\n'.join(items) + '\n' + end + ']'
    return json.dumps(obj)


def dump(obj, path):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(dumps(obj))
        f.write('\n')
