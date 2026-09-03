#!/usr/bin/env python3
"""The clips gate: an exported model's animations have the frame counts the data says.

ROADMAP M3.3a; docs/adr/ADR-019-placeholders-through-blender.md D2 enforcement
(3). Standard library only, so it runs in the Ubuntu job that has no Blender,
no compiler and no GPU. It reads the glTF's JSON only -- animation sampler
INPUT accessors must carry min/max by the glTF specification, so no buffer is
ever opened -- and derives every clip's frame count TWO ways:

    from the accessor:   count
    from the time span:  round((max - min) * fps) + 1

and fails if they disagree, because that is what a sampled clip with a dropped
or duplicated key looks like. Then, optionally, it compares the counts to the
sidecar the export script wrote (`<stem>.clips.json`) and to a character file
(each move's clip must be exactly startup + active + recovery frames, each
component clamped at zero as Combat.cpp's MoveDuration clamps it).

    python scripts/check_clips.py MODEL.gltf|MODEL.glb [--sidecar X.clips.json]
                                  [--character fighter.json] [--fps 60]
    python scripts/check_clips.py --self-test

A gate nobody has watched fail is not a gate: --self-test first feeds the
checker a clip whose count and span disagree and requires a refusal, then
reads the committed Blender-exported paddle and requires exactly
{"paddle_hold": 5, "paddle_swing": 14}, agreeing with its sidecar.
"""
import argparse
import json
import os
import struct
import sys

DEFAULT_FPS = 60
GLB_MAGIC = 0x46546C67
GLB_CHUNK_JSON = 0x4E4F534A


class ClipError(Exception):
    pass


def load_gltf_json(path):
    with open(path, 'rb') as f:
        head = f.read(12)
        if len(head) == 12 and struct.unpack('<I', head[:4])[0] == GLB_MAGIC:
            _, version, _length = struct.unpack('<III', head)
            if version != 2:
                raise ClipError('%s: glb version %d, expected 2' % (path, version))
            chunk_len, chunk_type = struct.unpack('<II', f.read(8))
            if chunk_type != GLB_CHUNK_JSON:
                raise ClipError('%s: first glb chunk is not JSON' % path)
            return json.loads(f.read(chunk_len).decode('utf-8'))
        f.seek(0)
        return json.loads(f.read().decode('utf-8'))


def clip_frames(doc, fps=DEFAULT_FPS):
    """{animation name: frame count}, refusing any clip whose channels disagree."""
    accessors = doc.get('accessors', [])
    out = {}
    for anim in doc.get('animations', []):
        name = anim.get('name', '?')
        counts = set()
        for i, sampler in enumerate(anim.get('samplers', [])):
            acc = accessors[sampler['input']]
            n = int(acc['count'])
            if 'min' not in acc or 'max' not in acc:
                raise ClipError('clip %r sampler %d: input accessor has no min/max (the glTF '
                                'specification requires them; this exporter is not the pinned one)'
                                % (name, i))
            span = int(round((acc['max'][0] - acc['min'][0]) * fps)) + 1
            if span != n:
                raise ClipError('clip %r sampler %d: %d samples but the time span %.6f..%.6f s at %d fps '
                                'says %d -- a key was dropped or duplicated, or the clip is off the grid'
                                % (name, i, n, acc['min'][0], acc['max'][0], fps, span))
            if abs(acc['min'][0]) > 1e-6:
                raise ClipError('clip %r sampler %d: first sample at %.6f s, not 0 (frame 0 is moveFrame 0)'
                                % (name, i, acc['min'][0]))
            counts.add(n)
        if not counts:
            raise ClipError('clip %r has no samplers' % name)
        if len(counts) != 1:
            raise ClipError('clip %r: its channels disagree on the frame count: %s' % (name, sorted(counts)))
        if name in out:
            raise ClipError('clip %r appears twice' % name)
        out[name] = counts.pop()
    return out


def compare_sidecar(clips, sidecar):
    if clips != sidecar:
        missing = sorted(set(sidecar) - set(clips))
        extra = sorted(set(clips) - set(sidecar))
        differ = sorted(k for k in clips if k in sidecar and clips[k] != sidecar[k])
        raise ClipError('the model and its sidecar disagree: missing from model %s, not in sidecar %s, '
                        'different counts %s' % (missing, extra,
                                                [(k, sidecar[k], clips[k]) for k in differ]))


def move_duration(move):
    return max(int(move.get('startup', 0)), 0) + max(int(move.get('active', 0)), 0) + \
        max(int(move.get('recovery', 0)), 0)


def compare_character(clips, character):
    problems = []
    for move in character.get('moves', []):
        move_id = move.get('id', '?')
        clip = ((move.get('engine') or {}).get('anim3d') or {}).get('clip', move_id)
        expected = move_duration(move)
        if clip not in clips:
            problems.append('move %r: no clip named %r' % (move_id, clip))
        elif clips[clip] != expected:
            problems.append('move %r: clip %r has %d frames, the frame data says %d (startup %s + active %s + recovery %s)'
                            % (move_id, clip, clips[clip], expected, move.get('startup'), move.get('active'),
                               move.get('recovery')))
    if problems:
        raise ClipError('\n  '.join(['the clips do not match the character file:'] + problems))


def self_test():
    # 1. The detector detects: count 14 against a 13-sample span must be refused.
    bad = {'accessors': [{'count': 14, 'min': [0.0], 'max': [12 / 60.0]}],
           'animations': [{'name': 'bad', 'samplers': [{'input': 0}]}]}
    try:
        clip_frames(bad)
    except ClipError:
        pass
    else:
        raise SystemExit('clips gate self-test FAILED: a count/span disagreement was not refused')
    # 2. The committed Blender-exported paddle reads as the contract says.
    here = os.path.dirname(os.path.abspath(__file__))
    fixture = os.path.join(here, '..', 'tests', 'fixtures', 'models', 'paddle_blender.gltf')
    clips = clip_frames(load_gltf_json(fixture))
    expected = {'paddle_hold': 5, 'paddle_swing': 14}
    if clips != expected:
        raise SystemExit('clips gate self-test FAILED: the paddle reads %s, expected %s' % (clips, expected))
    with open(os.path.splitext(fixture)[0] + '.clips.json', encoding='utf-8') as f:
        compare_sidecar(clips, json.load(f))
    print('clips gate self-test OK: the detector refuses a count/span disagreement, and the '
          'Blender-exported paddle reads %s, agreeing with its sidecar' % clips)


def main(argv=None):
    p = argparse.ArgumentParser(prog='check_clips.py', description=__doc__.split('\n')[0])
    p.add_argument('model', nargs='?', help='.gltf or .glb')
    p.add_argument('--sidecar', help='<stem>.clips.json written by the export script')
    p.add_argument('--character', help='character JSON whose moves the clips must match')
    p.add_argument('--fps', type=int, default=DEFAULT_FPS)
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args(argv)
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.model:
            p.error('a model path or --self-test is required')
        clips = clip_frames(load_gltf_json(args.model), args.fps)
        if args.sidecar:
            with open(args.sidecar, encoding='utf-8') as f:
                compare_sidecar(clips, json.load(f))
        if args.character:
            with open(args.character, encoding='utf-8') as f:
                compare_character(clips, json.load(f))
        print('clips gate: %s -- %d clip(s) on the %d Hz grid: %s'
              % (args.model, len(clips), args.fps, json.dumps(clips, sort_keys=True)))
        return 0
    except ClipError as e:
        print('clips gate FAILED: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
