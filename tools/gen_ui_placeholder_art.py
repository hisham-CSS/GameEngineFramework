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
    """1280x720 vertical gradient with a faint grid.

    `background-size: cover` on the menu root. Sized at 720p rather than 4K on
    purpose: it is a soft gradient, so it survives being scaled up, and a
    committed 4K PNG would dwarf every other asset in the repo.
    """
    w, h = 1280, 720
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        # Dark at the top, slightly warmer and lighter toward the bottom.
        d.line([(0, y), (w, y)],
               fill=(int(12 + 26 * t), int(14 + 28 * t), int(22 + 34 * t), 255))
    for x in range(0, w, 64):
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 8))
    for y in range(0, h, 64):
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 8))
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
