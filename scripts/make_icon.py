#!/usr/bin/env python3
"""Regenerate the app icon from a source image. Pure stdlib + ffmpeg.

The committed icon assets are DERIVED — this script is the source of truth for
how they were made, so swapping the logo is a one-command operation:

    python scripts/make_icon.py path/to/logo.png \
        --crop 81,46,771,508 \
        --ico resources/CatSplat.ico \
        --png Editor/src/Exported/Icon/icon.png

- --crop x,y,w,h   optional; cuts the emblem out of a larger image (the shipped
                   icon crops the splat-cat emblem out of the full wordmark
                   logo scraped from catsplatstudios.com).
- --bg COLOR       tile colour behind the art (default: white). The shipped
                   logo is black ink on transparency, which would vanish on a
                   dark taskbar — the tile is what keeps it visible. Pass
                   "none" for a transparent background.
- --margin PCT     padding around the art inside the tile (default 10).

Output .ico contains classic 32-bit DIB entries for 16/24/32/48/64/128 plus a
PNG-compressed 256 entry (the Vista+ convention). The optional --png output is
the 256px tile, used by the engine at runtime for glfwSetWindowIcon (the
window-manager icon on Linux; on Windows the .rc resource already covers it).
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SIZES_DIB = [16, 24, 32, 48, 64, 128]
SIZE_PNG = 256


def run_ffmpeg(args):
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error"] + args,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"ffmpeg failed: {r.stderr.strip()}")


def render(src, crop, size, margin_pct, bg, out_path, pix_fmt):
    """Crop the art, centre it on a size x size tile, write raw/png output."""
    inner = size - 2 * max(0, round(size * margin_pct / 100.0))
    art = f"[0:v]crop={crop}[c];" if crop else "[0:v]null[c];"
    if bg.lower() == "none":
        # transparent tile: pad instead of overlaying onto a colour source
        graph = (f"{art}[c]scale=w={inner}:h={inner}:"
                 f"force_original_aspect_ratio=decrease:flags=lanczos,"
                 f"pad={size}:{size}:(ow-iw)/2:(oh-ih)/2:color=black@0.0,"
                 f"format={pix_fmt}")
    else:
        graph = (f"{art}color=c={bg}:s={size}x{size}[bg];"
                 f"[c]scale=w={inner}:h={inner}:"
                 f"force_original_aspect_ratio=decrease:flags=lanczos[a];"
                 f"[bg][a]overlay=(W-w)/2:(H-h)/2,format={pix_fmt}")
    fmt = ["-f", "rawvideo"] if str(out_path).endswith(".raw") else []
    run_ffmpeg(["-i", str(src), "-filter_complex", graph, "-frames:v", "1"]
               + fmt + [str(out_path)])


def dib_entry(size, bgra):
    """A classic ICO image entry: BITMAPINFOHEADER + bottom-up BGRA + AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    rows = [bgra[y * size * 4:(y + 1) * size * 4] for y in range(size)]
    pixels = b"".join(reversed(rows))                      # bottom-up
    mask_row = ((size + 31) // 32) * 4                      # 1bpp, 4B-aligned
    mask = b"\x00" * (mask_row * size)                      # alpha rules; mask empty
    return header + pixels + mask


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="source image (anything ffmpeg decodes)")
    ap.add_argument("--crop", default=None, help="x,y,w,h emblem crop in source pixels")
    ap.add_argument("--bg", default="white", help="tile colour, or 'none' (default: white)")
    ap.add_argument("--margin", type=float, default=10.0, help="art padding %% (default 10)")
    ap.add_argument("--ico", required=True, help="output .ico path")
    ap.add_argument("--png", default=None, help="optional 256px png (runtime window icon)")
    a = ap.parse_args()

    crop = None
    if a.crop:
        x, y, w, h = (int(v) for v in a.crop.split(","))
        crop = f"{w}:{h}:{x}:{y}"

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        images = []  # (size, ico-entry payload, is_png)
        for s in SIZES_DIB:
            raw = td / f"{s}.raw"
            render(a.source, crop, s, a.margin, a.bg, raw, "bgra")
            images.append((s, dib_entry(s, raw.read_bytes()), False))
        png256 = td / "256.png"
        render(a.source, crop, SIZE_PNG, a.margin, a.bg, png256, "rgba")
        images.append((SIZE_PNG, png256.read_bytes(), True))

        # assemble: ICONDIR + one ICONDIRENTRY per image + payloads
        out = struct.pack("<HHH", 0, 1, len(images))
        offset = 6 + 16 * len(images)
        entries, payloads = b"", b""
        for size, data, _ in images:
            b = 0 if size == 256 else size
            entries += struct.pack("<BBBBHHII", b, b, 0, 0, 1, 32, len(data), offset)
            payloads += data
            offset += len(data)
        Path(a.ico).parent.mkdir(parents=True, exist_ok=True)
        Path(a.ico).write_bytes(out + entries + payloads)
        print(f"wrote {a.ico} ({len(out + entries + payloads)} bytes, "
              f"{len(images)} sizes)")

        if a.png:
            Path(a.png).parent.mkdir(parents=True, exist_ok=True)
            Path(a.png).write_bytes(png256.read_bytes())
            print(f"wrote {a.png}")


if __name__ == "__main__":
    main()
