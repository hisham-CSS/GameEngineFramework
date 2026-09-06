# Model/

What ships from this directory, and what does not (ADR-019 D10: a CREDITS.md
beside every committed model; `Assets.EveryModelHasALicenceBesideIt` and the
asset validator hold it).

- **`plane.obj`, `plane.mtl`, `white.png`** — first-party, under the
  repository's `LICENSE.txt` (MIT). Redistribution allowed: **yes**.
- **`backpack.obj`, `backpack.mtl`, `ao.jpg`, `diffuse.jpg`, `normal.png`,
  `roughness.jpg`, `specular.jpg`** — third-party placeholder (model by Berk
  Gedik, see `source_attribution.txt`); **no licence recorded, NOT SHIPPED**.
  `Player/unshipped_assets.txt` lists them, and both install routes in
  `Player/CMakeLists.txt` leave them out of the bundle (D10, answered
  2026-09-06). They stay here for the editor's demo scene (`scene.json`) only;
  a bundle whose startup scene references them draws nothing where they were.
