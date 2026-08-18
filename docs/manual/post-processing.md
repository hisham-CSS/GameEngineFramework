# Post-processing & Quality Tiers

Verified: 2026-08-17 @ e2f08bd

After the scene is shaded, the renderer runs a chain of screen-space effects.
They fall into two groups by the colour space they work in:

- **HDR effects** run *before* tonemap, on the linear `RGBA16F` scene buffer. **Bloom** is the only one today.
- **LDR effects** run *after* tonemap, on the gamma-space image: **ink outline → colour grade → vignette → FXAA**, in that order.

All of them are per-scene and **serialized** in the scene file — bloom, ink
outline, colour grade and vignette live on `Scene::PostFX()`; FXAA has its own
`Scene::GetAAEnabled()`/`SetAAEnabled()` flag, saved as `settings.aaEnabled`
alongside the `postFX` block rather than inside it — and each self-skips when
disabled.

## The LDR ping-pong chain

Tonemap normally writes straight to the output. But when one or more LDR effects
are enabled, tonemap instead writes into buffer **A** of a ping-pong pair, each
effect bounces the image A↔B, and the **last** enabled effect resolves to the
output. The pair (`ldrFBO_`/`ldrFBO2_`) is allocated only while the chain is
non-empty, so a scene with no post pays no memory.

The count that drives the routing is `Renderer::countLdrPostPasses_`, and it
does not re-derive anything: it asks each pass `IRenderPass::wantsLdrSlot`,
which is the same expression the pass's own `execute` guards on. That matters
because the two must never disagree — if the count is too **high**, no pass ever
sees itself as the last one, so the finished frame is left in an off-screen
ping-pong buffer and the window shows the clear colour, with nothing logged. A
post shader that failed to compile did exactly that when the count asked only
whether the effect was switched on.

So adding an LDR effect is a small, self-contained fullscreen pass (see
`VignettePass` for the template) plus a `wantsLdrSlot` override stating every
condition under which it will actually draw — a valid shader included. Passes
that are not chain stages, such as `UIPass`, inherit the base `false` and are
counted out automatically.

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

A four-tap cross edge-detect on the linearized scene depth — the left, right, up
and down neighbours are each compared against the centre sample, so silhouettes
and depth steps become dark contour lines. Pairs naturally with cel shading.
Uses a *relative* depth gradient so the sensitivity is scale-invariant (distant
geometry isn't blanket-outlined).

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
boot to the same tier. Because the shadow portion of a tier lives on the
`Renderer` and isn't itself serialized, the tier is re-applied at **every**
entry into a scene file: the editor's boot load, the editor's Open/Load path,
and Player boot. Without all three, opening the editor and then re-loading the
very same file visibly changed shadow quality, and only the second state matched
the shipped build.

**Editing a knob a tier owns demotes the tier to `Custom`.** Anti-aliasing, mesh
LOD and LOD distance scale, the projected-size cull (both the toggle and the
pixel threshold), the depth prepass, bloom, and the shadow cascade count and base
resolution each call the demotion when you change them. Nothing else happens —
the value you just chose stands; only the label changes, because `Custom` means
precisely "don't fan a preset over these". Aesthetic post (outline, colour grade,
vignette) is not tier-owned, so editing it leaves the tier alone.

Choose a tier from **Settings → Rendering → Quality**, or configure the
individual knobs below it.
