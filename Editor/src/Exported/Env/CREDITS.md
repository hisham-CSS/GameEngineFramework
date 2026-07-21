# Environment assets

## kloofendal_puresky_2k.hdr

- **Source:** [Poly Haven](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky)
  — "Kloofendal 48d Partly Cloudy (Pure Sky)"
- **Authors:** Greg Zaal (original), Jarod Guest (sky edits)
- **Licence:** CC0 (public domain). No attribution required; given anyway.
- **Resolution:** 2048 x 1024 equirectangular, 5.2 MB

This is the engine's default environment for new scenes. A **"pure sky"**
variant was chosen deliberately: it contains only sky, with no ground, terrain,
or buildings. A location-specific HDRi bakes someone else's horizon into every
scene you make, which reads as wrong the moment your own geometry does not
match it. Midday partly-cloudy also gives a neutral, fairly bright ambient that
suits most content rather than tinting everything a particular colour.

2k is the resolution that matters here: the environment cube is 512 per face,
so 2k gives enough source samples to downsample cleanly without aliasing, while
4k+ (20 MB and up) would be almost entirely wasted.

## studio.hdr

Generated, not downloaded — see `tools/make_example_env.py`. A synthetic studio
with three bright softbox panels, kept because those panels make a *specular*
IBL regression obvious: a smooth gradient looks fine even when the prefilter
chain is wrong, whereas rectangular highlights that smear from sharp to soft as
roughness rises only happen when it is correct. Used by `tests/test_ibl.cpp`.
