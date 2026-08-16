#!/usr/bin/env python3
"""Regenerates the title screen's placeholder art into
Games/UntitledFighter/Assets/UntitledFighter/UI/art/.

THE TITLE'S ART, NOT THE ENGINE'S, and that is the whole reason this file exists
beside tools/gen_ui_placeholder_art.py rather than being a fifth function inside
it. That script writes into Editor/src/Exported/UI/art/, which is the ENGINE
demo's asset root; this one writes into the game's, which the build layers on top
(see the composition block in the root CMakeLists.txt). Two scripts, two roots,
and neither one reaches into the other's.

ONE FILE. The engine demo needs a logo, a hazard swatch and a scrim ramp as well
as a backdrop; this menu needs only the backdrop, because its scrim is a two-stop
`background-gradient` in the stylesheet and its brand is type. The engine's ramp
predates `background-gradient` existing -- scrim_ramp's own docstring still says
"there is no gradient PROPERTY in the stylesheet language", which stopped being
true -- and an asset that a CSS declaration replaces is an asset worth not having.

IT IS A PLACEHOLDER AND IT IS SHAPED LIKE ONE. Real key art for a fighting game
is a character render, and ADR-005 puts art behind the combat systems on purpose.
So this is light and geometry only: two rim lights and a floor, no figure, nothing
that could be mistaken for a finished screen and nothing anyone has to license.
The PNG is COMMITTED so a fresh clone renders the menu with no asset pipeline;
this script exists to make it reproducible and cheap to replace.

Needs Pillow:  py -m pip install pillow
Run from the repo root:  py Games/UntitledFighter/tools/gen_title_art.py
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: py -m pip install pillow")

OUT = os.path.join("Games", "UntitledFighter", "Assets", "UntitledFighter",
                   "UI", "art")


def _save(img, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    img.save(path, "PNG", optimize=True)
    print("wrote {} ({} bytes)".format(path, os.path.getsize(path)))


def title_backdrop():
    """2560x1080 (21:9): an empty lit stage, waiting for two fighters.

    THE SAME FOUR RULES THE ENGINE DEMO'S BACKDROP OBEYS, for the same measured
    reasons -- authored at 21:9 so `background-size: cover` crops modestly in
    both directions rather than severely in one; nothing sharp anywhere, because
    a full-screen element scales with the surface and a 64px feature becomes a
    190px band of visible stepping on a 4K display; rendered at 256x108 and
    upscaled, because every gradient here is smooth and 2.7M Python pixel writes
    are not free; and everything kept far below the type's luminance, because
    this is a contrast FLOOR and not a midtone.

    WHAT IS DIFFERENT, and it is the point of the file: the engine's demo is a
    warm room lit from low-left. This is a STAGE -- two opposed rim lights, ember
    from the left and steel from the right, over a floor plane that catches both.
    That is the oldest lighting cliche in the genre and it is a cliche because it
    reads instantly: two sides, facing each other, nobody standing there yet.
    A menu that looked like the engine's demo with different words on it would
    have made the entire content separation invisible to the only person it is
    for, which is the player.
    """
    sw, sh = 256, 108           # authored small, upscaled at the end
    w, h = 2560, 1080

    # The floor, as a fraction of height. Low, so the verb column sits in the
    # dark air above it rather than on top of the brightest band on screen.
    horizon = 0.74

    img = Image.new("RGB", (sw, sh))
    px = img.load()

    # The two sources, in small-image coordinates. Both sit BELOW the horizon and
    # outside the frame's centre third, which is what keeps the middle -- where
    # the wordmark and the verbs live -- the darkest part of the picture.
    ember = (sw * 0.16, sh * 0.86, (sw * 0.34) ** 2, (150, 46, 22))
    steel = (sw * 0.88, sh * 0.80, (sw * 0.30) ** 2, (34, 68, 116))

    for y in range(sh):
        v = y / (sh - 1)
        # Base: ink at the top, barely lifting toward the floor. The step across
        # the horizon is a SMOOTH ramp over 12% of the height rather than an
        # edge, because an edge is exactly the thing that survives upscaling and
        # announces itself as a test pattern.
        floor = min(1.0, max(0.0, (v - horizon + 0.06) / 0.12))
        br = 7 + 9 * floor
        bg = 8 + 10 * floor
        bb = 12 + 14 * floor
        for x in range(sw):
            u = x / (sw - 1)
            r, g, b = br, bg, bb
            for lx, ly, spread, (cr, cg, cb) in (ember, steel):
                d = ((x - lx) ** 2 + (y - ly) ** 2) / spread
                # Tight and fast-falling, so a light is a light rather than a
                # wash over the whole frame.
                glow = 1.0 / (1.0 + d * d * 1.9)
                # The floor plane catches noticeably more than the air does --
                # one multiplier, and it is what makes the horizon read as a
                # surface rather than as a change of colour.
                lit = glow * (0.42 + 0.58 * floor)
                # 150 rather than 100: the menu over this puts NO flat veil on
                # top (see title.cstyle -- the scrim is a directional gradient
                # under the verb column only, because unlike the engine demo
                # this backdrop is known art rather than whatever a user drops
                # in). Without a veil eating half of it, the lights have to
                # actually be visible at 1080p on a laptop panel.
                r += cr * lit / 255.0 * 150.0
                g += cg * lit / 255.0 * 150.0
                b += cb * lit / 255.0 * 150.0
            # Vignette: strongest in the corners, and weighted so the wide crop
            # loses less horizontally than vertically.
            cx, cy = (u - 0.5) * 2.0, (v - 0.5) * 2.0
            vig = 1.0 - 0.58 * min(1.0, (cx * cx * 0.55 + cy * cy) ** 1.2)
            px[x, y] = (int(min(255, r * vig)),
                        int(min(255, g * vig)),
                        int(min(255, b * vig)))

    img = img.resize((w, h), Image.BICUBIC)
    _save(img, "title_backdrop.png")


if __name__ == "__main__":
    if not os.path.isdir(os.path.join("Games", "UntitledFighter")):
        sys.exit("run me from the repository root: py " +
                 os.path.join("Games", "UntitledFighter", "tools",
                              "gen_title_art.py"))
    title_backdrop()
