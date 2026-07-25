# Fonts

## Roboto.ttf

The engine's default UI font.

| | |
|---|---|
| **Source** | <https://github.com/google/fonts/tree/main/ofl/roboto> (`Roboto[wdth,wght].ttf`) |
| **License** | SIL Open Font License 1.1 — full text in `LICENSE-Roboto.txt` |
| **Copyright** | 2011 The Roboto Project Authors |

**It is a variable font** (`wdth`, `wght` axes). Google no longer publishes a
static Regular for Roboto, and `stb_truetype` — which bakes our glyph atlas —
has no variable-axis support, so it rasterises the font's **default instance**
(Regular). That is exactly what we want for a default UI font; it just means the
weight/width axes are not reachable. Shipping an additional static file per
weight is the way to get bold/light, not an axis setting.

The OFL permits bundling and redistribution, including in a commercial game,
provided the licence text travels with the font — which is why
`LICENSE-Roboto.txt` sits next to it and is staged and packaged along with it.
The OFL's one hard restriction is that the font must not be sold on its own, and
that any *modified* version must be renamed (it may not use the "Roboto"
reserved name). Neither applies to shipping it unmodified inside a game.

## Adding your own

Drop a `.ttf` in this directory and load it with `Font::LoadFromFile`. Everything
in here is staged next to the executables and included in the packaged bundle.
Fonts are baked at one pixel height, so load a second `Font` for a size far from
the first rather than scaling a small atlas up.
