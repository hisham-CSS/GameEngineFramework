# Post-processing & Quality Tiers

After the scene is shaded, the renderer runs a chain of screen-space effects.
They fall into two groups by the colour space they work in:

- **HDR effects** run *before* tonemap, on the linear `RGBA16F` scene buffer. **Bloom** is the only one today.
- **LDR effects** run *after* tonemap, on the gamma-space image: **ink outline → colour grade → vignette → FXAA**, in that order.

All of them are per-scene settings on `Scene::PostFX()` and are **serialized** in
the scene file, and each self-skips when disabled.

## The LDR ping-pong chain

Tonemap normally writes straight to the output. But when one or more LDR effects
are enabled, tonemap instead writes into buffer **A** of a ping-pong pair, each
effect bounces the image A↔B, and the **last** enabled effect resolves to the
output. The pair (`ldrFBO_`/`ldrFBO2_`) is allocated only while the chain is
non-empty, so a scene with no post pays no memory. The count that drives the
routing is `Renderer::countLdrPostPasses_`; each effect's own enable predicate
must match it, or the routing desyncs.

This design means adding an LDR effect is a small, self-contained fullscreen
pass (see `VignettePass` for the template).

## Scene-depth texture

The HDR framebuffer's depth attachment is a sampleable `GL_DEPTH_COMPONENT24`
**texture** (`Renderer::makeDepthTex_`), published on the `PassContext` as
`hdrDepthTex`. That's what lets depth-driven post effects read scene depth —
the ink outline uses it today; depth-of-field and fog could reuse it.

## Effects

### Bloom (HDR)

Bright-pass with a soft knee → a half-resolution separable Gaussian blur
(ping-pong, several iterations for a wide glow) → **additive** composite back
into the HDR buffer so tonemap picks it up. Half-res keeps it cheap.

| Setting | Range | Meaning |
|---|---|---|
| `bloom.enabled` | — | on/off |
| `bloom.threshold` | 0–4 | HDR luminance above which pixels bloom |
| `bloom.intensity` | 0–2 | composite strength |

### Ink outline (LDR, depth-based)

A Sobel edge-detect on the linearized scene depth — silhouettes and depth steps
become dark contour lines. Pairs naturally with cel shading. Uses a *relative*
depth gradient so the sensitivity is scale-invariant (distant geometry isn't
blanket-outlined).

| Setting | Meaning |
|---|---|
| `outline.thickness` | neighbour tap offset (px) |
| `outline.threshold` | edge sensitivity |
| `outline.strength` | ink opacity |
| `outline.color` | ink colour |

### Colour grade (LDR)

Procedural white balance + lift/gain + contrast + saturation — a self-contained
stand-in for a LUT workflow, no external asset. Settings: `contrast`,
`saturation`, `temperature`, `tint`, `lift`, `gain`.

### Vignette (LDR)

Radial edge darkening. Settings: `intensity`, `roundness` (0 = frame rectangle,
1 = circular), `smoothness`.

### FXAA (LDR)

Post-process anti-aliasing on the final gamma-space image; always runs last in
the chain. Toggled by the scene's `aaEnabled`.

## Quality tiers

A single HDRP-lite preset configures the whole render budget at once.
`Scene::QualityLevel` is one of `Low` / `Medium` / `High` / `Custom`, and
`Renderer::ApplyQualityTier(level, scene)` fans it out into the perf-critical
knobs — anti-aliasing, mesh LOD + distance, projected-size culling, the depth
pre-pass, shadow cascades/resolution, and bloom.

Per the [performance measurements](performance.md), the target content is
**vertex/instance-bound** and fill-heavy post dominates the integrated GPU, so
tiers scale on **geometry** (LOD, culling) and gate **bloom** (the one expensive
post pass). The purely aesthetic effects (outline, colour grade, vignette) are
the author's choice and are left untouched by a tier. **Custom** applies no
preset — the individual settings stand as-is.

The chosen tier is stored on the scene, so the editor and the shipped Player
boot to the same tier. Because the shadow portion of a tier isn't itself
serialized, the tier is re-applied on scene load and at Player boot.

Choose a tier from **Settings → Rendering → Quality**, or configure the
individual knobs below it.
