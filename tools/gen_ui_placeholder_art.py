#!/usr/bin/env python3
"""Regenerates the UI's placeholder art into Editor/src/Exported/UI/art/.

The PNGs are COMMITTED, so this exists to make them reproducible and easy to
replace: change a colour here, re-run, and the demo picks it up on the next
build (or immediately, if you write into the staged copy next to the exe).

Everything is deliberately small and obviously fake. These are placeholders to
be swapped for real art, not art — and a 4KB file that says PLACEHOLDER across
it is much harder to accidentally ship than a plausible-looking one.

Needs Pillow:  py -m pip install pillow
Run from the repo root:  py tools/gen_ui_placeholder_art.py
"""
import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: py -m pip install pillow")

OUT = os.path.join("Editor", "src", "Exported", "UI", "art")

# Cat Splat's UI palette, in the order the demo uses it.
INK      = (16, 18, 24)
PANEL    = (28, 32, 42)
ACCENT   = (255, 204, 68)
MUTED    = (120, 128, 148)


def _save(img, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    img.save(path, "PNG", optimize=True)
    print("wrote {} ({} bytes)".format(path, os.path.getsize(path)))


def menu_backdrop():
    """2560x1080 (21:9): a lit room, not a test pattern.

    THREE DECISIONS, all of them about what survives `background-size: cover`.

    AUTHORED AT 21:9, not 16:9. Cover scales to fill and crops the rest, so a
    16:9 source on a 32:9 monitor loses two thirds of its height. Authoring in
    the middle of the range every real display sits in means the crop is modest
    in both directions rather than severe in one.

    NOTHING SHARP. The old version drew a 64px grid, which is exactly the wrong
    thing here: a full-screen menu scales with the surface, so on a 4K screen
    those became 190px squares of visible banding, and it read as an engine
    test pattern rather than as art. Everything here is low-frequency, so
    cropping it, scaling it 4x, or letting bilinear filtering smear it all
    leave it looking the same.

    A LIGHT SOURCE, low and left. The verb column lives there, and the gold
    accents everywhere in the menu now have something to be motivated by. The
    vignette pulling the corners down is what keeps the footer legible over
    arbitrary art; the veil and ramp layers in menu.cxml do the rest.

    Rendered small and upscaled, because every gradient in it is smooth and a
    per-pixel Python loop over 2.7M pixels is not free. The PNG stays under
    100KB precisely because there is nothing sharp in it to encode.
    """
    sw, sh = 256, 108          # authored small, upscaled at the end
    w, h = 2560, 1080
    img = Image.new("RGB", (sw, sh))
    px = img.load()

    # The warm source, in small-image coordinates: low and left of centre.
    gx, gy = sw * 0.26, sh * 0.82
    grad = (sw * 0.30) ** 2

    for y in range(sh):
        v = y / (sh - 1)
        # Base: deep ink at the top easing to a cooler slate low down. Both
        # ends stay far below the type's luminance -- this is a floor, not a
        # midtone.
        br = 10 + 16 * v
        bg = 12 + 19 * v
        bb = 18 + 27 * v
        for x in range(sw):
            u = x / (sw - 1)
            d = ((x - gx) ** 2 + (y - gy) ** 2) / grad
            glow = 1.0 / (1.0 + d * d * d * 2.2)      # tight, fast-falling
            # Vignette: distance from centre, strongest in the corners.
            cx, cy = (u - 0.5) * 2.0, (v - 0.5) * 2.0
            vig = 1.0 - 0.55 * min(1.0, (cx * cx * 0.60 + cy * cy) ** 1.2)
            r = (br + 46 * glow) * vig
            g = (bg + 33 * glow) * vig
            b = (bb + 12 * glow) * vig
            px[x, y] = (int(min(255, r)), int(min(255, g)), int(min(255, b)))

    img = img.resize((w, h), Image.BICUBIC)
    _save(img, "menu_backdrop.png")


def logo():
    """256x256 mark. Deliberately a wordless glyph so it reads at any size."""
    s = 256
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([16, 16, s - 16, s - 16], fill=PANEL + (255,), outline=ACCENT + (255,), width=6)
    # Two ears and a splat dot: obviously a placeholder, obviously deliberate.
    d.polygon([(64, 74), (96, 24), (110, 78)], fill=ACCENT + (255,))
    d.polygon([(s - 64, 74), (s - 96, 24), (s - 110, 78)], fill=ACCENT + (255,))
    d.ellipse([s // 2 - 26, s // 2 - 10, s // 2 + 26, s // 2 + 42], fill=ACCENT + (255,))
    _save(img, "logo.png")


def placeholder_badge():
    """96x96 'PLACEHOLDER' swatch, for anywhere real art is still missing.

    Diagonal hazard stripes, so it is unmistakable in a screenshot.
    """
    s = 96
    img = Image.new("RGBA", (s, s), INK + (255,))
    d = ImageDraw.Draw(img)
    for i in range(-s, s * 2, 16):
        d.line([(i, 0), (i - s, s)], fill=ACCENT + (255,), width=6)
    d.rectangle([0, 0, s - 1, s - 1], outline=MUTED + (255,), width=2)
    _save(img, "placeholder.png")


def scrim_ramp():
    """256x16 left-to-right darkening ramp, stretched across the screen.

    There is no gradient PROPERTY in the stylesheet language, but a stretched
    linear ramp is still a linear ramp -- and stretching 256px across 1280 is
    exactly the case bilinear filtering handles perfectly, because the source is
    already a smooth gradient. 16px tall because it never varies vertically.

    This is what keeps the menu's type legible over ARBITRARY backdrop art:
    darkest under the verb column on the left, clear on the right where the art
    should show.
    """
    w, h = 256, 16
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for x in range(w):
        t = x / float(w - 1)
        # Eased rather than linear: a straight ramp reads as a visible band edge
        # where it meets the untouched art.
        a = int(round(235 * (1.0 - t) ** 1.6))
        for y in range(h):
            px[x, y] = INK + (a,)
    _save(img, "scrim_ramp.png")


if __name__ == "__main__":
    if not os.path.isdir("Editor"):
        sys.exit("run me from the repo root")
    menu_backdrop()
    logo()
    placeholder_badge()
    scrim_ramp()
