# Rendering

The renderer draws one frame as a short, fixed sequence of passes: cascaded
shadow maps, a forward opaque pass into an HDR target, then a tonemap blit to
the output framebuffer. Everything else — culling, LOD, instancing, batching —
happens inside the scene traversal that the forward pass drives.

This page explains that pipeline, the knobs that control it, and — importantly
— which of those knobs actually change your frame time. The last section is
measurement, not intuition, and it contradicts the obvious guess.

## The pass pipeline

`Renderer` (`Engine/src/core/Renderer.h`) owns the passes, the HDR targets, and
the render settings. It owns nothing else: the window, input, camera, timing and
main loop live in `Application`.

```c++
void Setup(int fbWidth, int fbHeight);

void RenderFrame(Scene& scene, Shader& shader, Camera& camera,
                 int fbWidth, int fbHeight, float deltaTime,
                 unsigned targetFBO = 0);
```

`Setup` requires a current GL context with GLAD loaded (`Application::InitGL`
does both). `RenderFrame` renders into `targetFBO` — `0` is the window
backbuffer; the editor passes a `RenderTarget`'s FBO so the scene appears inside
its Viewport panel.

Passes are held in a `RenderPipeline` (`Engine/src/render/RenderPipeline.h`), a
flat vector of `IRenderPass` that runs in insertion order:

| Order | Pass | Source | Does |
|---|---|---|---|
| 1 | `ShadowCSMPass` | `Engine/src/render/passes/ShadowCSMPass.cpp` | Renders depth-only cascade maps from the sun |
| 2 | `ForwardOpaquePass` | `Engine/src/render/passes/ForwardOpaquePass.cpp` | Shades the scene into the HDR framebuffer |
| 3 | `TonemapPass` | `Engine/src/render/passes/TonemapPass.cpp` | ACES tonemap + gamma, fullscreen quad into `targetFBO` |

Passes never talk to each other directly. They read and write a shared
`PassContext` (`Engine/src/render/IRenderPass.h`), which carries the GL targets,
the sun direction, exposure, the shadow-filtering knobs, the IBL texture ids,
and the `CSMSnapshot` that the shadow pass publishes and the forward pass
consumes.

```c++
struct IRenderPass {
    virtual ~IRenderPass() = default;
    virtual const char* name() const = 0;
    virtual void setup(PassContext&) {}
    virtual void resize(PassContext&, int /*w*/, int /*h*/) {}
    virtual bool execute(PassContext&, MyCoreEngine::Scene&, Camera&, const FrameParams&) = 0;
};
```

`FrameParams` is the immutable per-frame view: `view`, `proj`, `deltaTime`,
`frameIndex`, `viewportW`, `viewportH`.

### Lights

The scene has one **directional light** — the sun — held as scene-level state
(`Scene::LightDir()`, `LightColor()`, `LightIntensity()`, edited under
**Settings → Sun / Shadows**). It is scene-level rather than a component
because it is the light that casts the cascaded shadow maps.

Everything else is a **`LightComponent`** on an entity, positioned by that
entity's `Transform`:

| Type | Behaviour |
| --- | --- |
| `LightType::Point` | radiates in all directions from the entity's position |
| `LightType::Spot` | cone aimed down the entity's **-Z** axis (same convention as a camera) |

Punctual lights use a windowed inverse-square falloff, so brightness drops off
physically but reaches exactly zero at `range` — a hard cutoff would show up as
a visible disc edge on the floor. Spot lights fade between `innerAngleDeg` and
`outerAngleDeg`; an outer angle smaller than the inner is clamped, because an
inverted cone would run the falloff backwards and light everything *outside*
the cone.

> **Punctual lights do not cast shadows.** Only the sun does. This is why they
> are a separate, bounded array rather than an extension of the sun path.

**The array is bounded** (`Scene::kMaxPunctualLights`, currently 16, matching
`MAX_PUNCTUAL_LIGHTS` in `frag.glsl`). A scene may contain any number of
lights; each frame `Scene::SelectPunctualLights` resolves them to world space,
drops the disabled and zero-contribution ones, and keeps the most influential
for the current camera — intensity over distance-squared, so a nearby lamp
outranks a distant floodlight. Overflow therefore degrades by dropping the
lights that matter least, not an arbitrary tail. The Rendering Stats panel
shows `Lights (act/cull)`.

Selection is a pure function of the registry — no GL, no renderer state — so it
is unit-tested headlessly in `tests/test_lights.cpp`. If you raise the limit,
change **both** constants.

### HDR target and tonemap

The forward pass renders to an offscreen `GL_RGBA16F` colour texture with a
`GL_DEPTH24_STENCIL8` renderbuffer, created in `Renderer::Setup`. Lighting is
computed in linear HDR; nothing clamps until the tonemap.

`TonemapPass` binds `targetFBO`, disables depth test, and draws a fullscreen
quad sampling the HDR texture. The shader
(`Editor/src/Exported/Shaders/tonemap_frag.glsl`) multiplies by exposure,
applies the ACES filmic curve (Narkowicz 2015), then encodes to sRGB with a
`pow(x, 1/2.2)` approximation.

```c++
float exposure() const;
void  setExposure(float e); // clamped to >= 0.01
```

The editor exposes this as **IBL/HDR → Exposure** (range 0.2–5.0).

The HDR pipeline reallocates automatically whenever the output size changes —
a window resize in the player, a Viewport panel resize in the editor.

> **Gotcha** — `Setup` seeds `lastFbW_`/`lastFbH_` with the size it actually
> allocated. An earlier `lastFbW_ != 0` guard skipped the *first* differing
> frame, which permanently locked any renderer whose render size never changed
> again (the editor's Game view) to the `Setup` size. If you add another size
> path, keep the tracking seeded.

## Environment lighting (skybox + IBL)

Image-based lighting is what stops surfaces facing away from the sun reading as
flat black. The engine bakes four things from an environment:

| Product | What it is | Used for |
| --- | --- | --- |
| environment cube | the sky itself, mipped | drawn by `SkyboxPass` |
| irradiance cube | cosine-convolved, 32³ | the diffuse ambient term |
| prefiltered cube | GGX-importance-sampled, mipped by roughness | glossy reflections |
| BRDF LUT | 512² RG16F, view- and environment-independent | the split-sum scale/bias |

### Choosing an environment

**Settings → IBL/HDR → Environment.**

- **Procedural sky** (default) — an analytic gradient with a sun lobe, driven by
  the sun direction from *Sun / Shadows Controls*. Needs no asset, so IBL works
  in a brand-new scene.
- **HDRi file** — an equirectangular `.hdr`. If the path fails to load, the
  engine falls back to the procedural sky and shows the error next to the field
  rather than going black.

`IBL Intensity` controls how much the environment *lights the scene*.
`Sky brightness` dims only the *drawn* sky. They are deliberately separate: a
dim backdrop with bright lighting is a normal thing to want.

### When it re-bakes

Baking costs milliseconds, so `Renderer::SyncEnvironment` re-bakes only when the
settings actually change — or, for the procedural sky, when the sun moves more
than 1.5°, since that sky has the sun baked into it. It is driven from
`RenderFrame`, so neither the editor nor the player has to remember to call it
and Play cannot end up lit differently from the shipped build.

### Two failure modes worth knowing

**IBL requires both a toggle and resources.** `uUseIBL` is set from
`iblEnabled_ && iblAvailable_`. The second flag is runtime state published by
the forward pass. Before it existed, `iblEnabled_` defaulted to `true` with no
maps bound, so the shader sampled unbound cubemaps — which read as black,
making ambient exactly **zero** rather than the intended `0.03` fallback.

**`uPrefilterMipCount` is a max mip INDEX, not a count.** The shader does
`mip = roughness * uPrefilterMipCount`, so roughness 1.0 must land on the last
mip. Off by one and rough metal samples a sharp mip and sparkles.

### Cost

About 0.3 ms/frame at 1080p (the skybox pass plus real IBL sampling in the
fragment shader). The bake itself is one-off per change, not per frame.

### Not built yet

No cubemap disk cache (every run re-bakes), no reflection probes or parallax-
corrected boxes (one global environment for the whole scene), and the HDRi path
is typed rather than browsed.

## PBR material inputs

A `Material` (`Engine/src/core/Material.h`) is scalar parameters plus optional
GL texture ids. A texture id of `0` means "absent" and the matching scalar is
used instead.

| Field | Type | Default | Notes |
|---|---|---|---|
| `baseColor` | `glm::vec3` | `1,1,1` | |
| `metallic` | `float` | `0.0f` | |
| `roughness` | `float` | `0.5f` | |
| `ao` | `float` | `1.0f` | |
| `emissive` | `glm::vec3` | `0,0,0` | |
| `albedoTex` | `unsigned` | `0` | uploaded as sRGB |
| `normalTex` | `unsigned` | `0` | linear |
| `metallicTex` | `unsigned` | `0` | linear, R or RGB |
| `roughnessTex` | `unsigned` | `0` | linear, R or RGB |
| `aoTex` | `unsigned` | `0` | linear |
| `emissiveTex` | `unsigned` | `0` | usually sRGB |

Presence helpers (`hasAlbedo()`, `hasNormal()`, …) test the ids. Materials are
shared through `using MaterialHandle = std::shared_ptr<Material>;`.

`Mesh::BindForDrawWith` (`Engine/src/core/Model.cpp`) binds a material to fixed
texture units: 0 albedo, 1 normal, 2 metallic, 3 roughness, 4 AO. It also
uploads `uHasNormalMap`, `uHasMetallicMap`, `uHasRoughnessMap`, `uHasAOMap` so
the shader knows which maps exist, then binds the mesh VAO. IBL cubemaps use
units 5 (irradiance), 6 (prefiltered) and 7 (BRDF LUT); shadow cascades start at
unit 8 (`ForwardOpaquePass::kBaseUnit`).

### Per-entity material overrides

Which material a mesh draws with is resolved per entity, per mesh, by
`Scene::chooseMaterial_`: if the entity has a `MaterialOverrides` component
(`Engine/src/core/Components.h`) with an entry for that mesh's
`Mesh::MaterialIndex()`, the override wins; otherwise the mesh's shared
`GetMaterial()` is used; if that is null, the legacy `BindForDraw` path runs.

```c++
struct MaterialOverrides {
    std::unordered_map<size_t, MyCoreEngine::MaterialHandle> byIndex;
};
```

### Scene-level shading settings

`Scene` also carries global shading state uploaded once per frame in
`RenderScene`, and the editor exposes most of it under **Materials**,
**Direct Light** and **IBL/HDR**:

| Setting | Accessors | Default |
|---|---|---|
| PBR on/off | `GetPBREnabled` / `SetPBREnabled` | `true` |
| Metallic / Roughness / AO | `GetMetallic` / `SetMetallic`, … | `0.0` / `0.5` / `1.0` |
| Use metallic / roughness / AO maps | `GetMetallicMapEnabled` / `SetMetallicMapEnabled`, … | `true` |
| Normal mapping | `GetNormalMapEnabled` / `SetNormalMapEnabled` | `true` |
| Light direction / colour / intensity | `LightDir()` / `LightColor()` / `LightIntensity()` (mutable refs) | `normalize(0.3,-1,0.2)` / white / `3.0` |
| IBL on/off, intensity | `GetIBLEnabled` / `SetIBLEnabled`, `GetIBLIntensity` / `SetIBLIntensity` | `true` / `1.0` |

IBL textures are supplied to the renderer separately:

```c++
void SetIBLTextures(unsigned int irradianceCube, unsigned int prefilteredCube,
                    unsigned int brdfLUT2D, float prefilteredMipCount);
```

If any of the three ids is `0`, the forward pass sets `uPrefilterMipCount` to
`0.0f` and skips the IBL binds entirely.

> **Note** — `Scene::LightDir()` (the shading light) and `Renderer::sunDir()`
> (the shadow-casting sun) are two separate values. The editor has a
> **Use Sun Dir for Shading Light** checkbox that copies one into the other;
> nothing keeps them in sync automatically.

## Cascaded shadow maps

`ShadowCSMPass` renders a depth map per cascade from the sun's direction and
publishes a `CSMSnapshot` (light-view-projection matrix, split far distance,
depth texture id and resolution per cascade) for the forward pass to sample.

### Defaults

`Renderer::Setup` constructs the pass as `ShadowCSMPass(4, 2048)` and seeds it:

| Property | Default | Setter (on `Renderer`) |
|---|---|---|
| Enabled | `true` | `setCSMEnabled(bool)` |
| Cascade count | `4` (max 4) | `setCSMNumCascades(int)` |
| Base resolution | `2048` (square, same for every cascade) | `setCSMBaseResolution(int)` |
| Max shadow distance | `200.0f` m | `setCSMMaxShadowDistance(float)` |
| Lambda | `0.7f` | `setCSMLambda(float)` |
| Update policy | `CameraOrSunMoved` | `setCSMUpdatePolicy(...)` |
| Cascade update budget | `0` (= update all stale cascades) | `setCSMCascadeBudget(int)` |
| Stability epsilons | `0.05` m, `0.5` degrees | `setCSMEpsilons(float, float)` |
| Cascade padding | `0.0f` m | `setCSMCascadePadding(float)` |
| Depth margin | `5.0f` m | `setCSMDepthMargin(float)` |
| Slope depth bias (`glPolygonOffset` factor) | `2.0f` | `setCSMSlopeDepthBias(float)` |
| Constant depth bias (`glPolygonOffset` units) | `4.0f` | `setCSMConstantDepthBias(float)` |
| Cull front faces during depth render | `true` | `setCSMCullFrontFaces(bool)` |
| Dynamic-caster interval cap | `4` frames | `setCSMDynamicIntervalCap(int)` |

Receiver-side filtering lives on `Renderer` rather than the pass, and is
uploaded to the fragment shader every frame:

| Property | Default | Setter |
|---|---|---|
| Receiver bias, constant (texels) | `1.5f` | `setShadowBiasConst(float)` |
| Receiver bias, slope (texels, scaled by `1 - N·L`) | `2.0f` | `setShadowBiasSlope(float)` |
| PCF radius per cascade | `{1,1,1,1}`, clamped 0–4 | `setCascadeKernel(int cascade, int radius)` |
| Split cross-fade band (view-space metres) | `4.0f` | private `splitBlend_` |

> **Important** — the split blend band is 4 m, not the 20 m it once was. 20 m
> was tuned for a 1000 m shadow distance; against the current 200 m default it
> spanned entire mid cascades.

### Split modes

`ShadowCSMPass::SplitMode` is `Fixed` or `Lambda`, and the pass defaults to
`Fixed`.

- **Fixed** uses the ratio table `{0.05, 0.15, 0.40, 1.0}` of the shadow range,
  right-aligned for fewer cascades (3 cascades → 15/40/100 %).
- **Lambda** is practical-split PSSM via `MyCoreEngine::ComputeCSMSplits`
  (`Engine/src/render/CSMSplits.h`) — `lambda = 0` is uniform, `1` is
  logarithmic, in between is a blend.

```c++
ENGINE_API inline std::vector<float> ComputeCSMSplits(float nearZ, float farZ,
                                                      int cascades, float lambda);
```

Splits always start at the camera's real near plane and stop at
`min(maxShadowDistance, camera.FarClip)`. Changing the camera's near or far clip
at runtime refits the cascades even if the camera never moved.

> **Gotcha** — `Renderer::setCSMLambda` forwards to `ShadowCSMPass::setLambda`,
> which only clamps and stores the value; it does **not** switch the split mode.
> Since the pass starts in `Fixed`, moving the editor's **Split Lambda** slider
> has no visible effect unless something calls `ShadowCSMPass::setSplitMode(
> SplitMode::Lambda)` or `ShadowCSMPass::setCSMLambda` on the pass directly.

> **Gotcha** — the Fixed table used to be indexed such that the 5 % entry was
> skipped, producing splits of `{15, 40, 100, 100}%`: cascade 0 stretched to
> 15 % and the last cascade collapsed to a degenerate sliver, so 4-cascade
> setups really ran 3. Both split modes now enforce strictly-increasing splits
> with a range-relative epsilon plus a `std::nextafter` guard, because equal
> splits reach `glm::perspective` as `zNear == zFar` and produce NaN cascade
> matrices.

### Stabilisation

Two things keep cascades from shimmering as the camera turns:

1. **Bounding-sphere fit.** Each cascade's ortho extent comes from the bounding
   *sphere* of its view frustum slice. The radius depends only on FOV, aspect
   and split distances — rigid geometry — not on camera position or
   orientation. A box fit to the slice corners changes size as you turn, which
   resizes the texel grid every update and makes snapping useless.
2. **Texel snapping.** The light projection is nudged so the world origin lands
   on a texel corner. With a constant extent the texel size is constant, so
   translating the camera shifts the shadow map by whole texels.

Casters closer to the light than the ortho near plane are pancaked onto it with
`GL_DEPTH_CLAMP` rather than clipped away, so their shadows still land in the
slice.

> **Important** — casters are culled against the **light** frustum only, never
> against the camera's Z slice. An object behind or beside the camera can still
> cast into the slice; dropping it makes shadows pop as the camera moves.
> Receiver-side slice selection happens in the fragment shader. This is why
> the screen-space size cull (below) deliberately does *not* remove casters.

### Update cadence

Re-rendering every cascade every frame is expensive, and re-rendering none of
them leaves stale light matrices that no longer cover the current slice. The
pass takes a middle road with two independent throttles.

**Movement-proportional cadence.** Each cascade bakes a movement margin into
its extent when rendered — `max(1.0f, updateMarginFraction() * radius)`, with
`updateMarginFrac_` defaulting to `0.15f`. The cascade re-renders only once the
slice centre drifts past 95 % of that margin. Near cascades update often but
are cheap (small light frustum, few casters); the far cascade updates rarely.
Coverage is guaranteed inside the margin, so nothing pops.

```c++
void  setUpdateMarginFraction(float f); // clamped to [0.02, 0.5]
float updateMarginFraction() const;
```

**Dynamic-caster throttle.** A caster that moved or rotated invalidates every
cascade its shadow can reach — and far cascades are huge, so a single spinning
object used to double the frame time. `Scene::HasDynamicCasterInViewRange`
approximates each moved caster's shadow footprint as a sphere swept along the
light direction (lengthened as the sun drops) and tests it against the cascade's
view-depth range. Cascade *i* then re-renders for dynamic casters at most every
`min(2^(i-1), cap)` frames:

```c++
void setDynamicIntervalCap(int frames); // clamped to [1, 4]; 1 disables the throttle
int  dynamicIntervalCap() const;
```

Cascades 0 and 1 stay every-frame; far ones amortise. Staleness is bounded — a
pending flag guarantees the final refresh once the caster stops.

`Scene::UpdateTransforms` is what records dirty casters, and it records **both**
the departure and arrival sphere. Recording only the new position leaves a baked
"ghost" shadow behind teleports and fast moves.

> **Gotcha** — a caster only counts if its `ModelComponent` actually has a
> loaded model. An empty `ModelComponent` renders and casts nothing, and every
> render path null-checks it; the dirty-caster predicate must too.

Add the `NoShadow` tag component to an entity to keep it out of every shadow map:

```c++
struct NoShadow {};
```

### Update policy and budget

```c++
enum class UpdatePolicy { Always, CameraOrSunMoved, Manual };
```

`Always` re-renders every cascade every frame. `CameraOrSunMoved` (the default)
applies the cadence rules above. `Manual` refreshes only on global invalidation
— a settings change, or an explicit `Renderer::forceCSMUpdate()`.

`setCSMCascadeBudget(n)` with `n > 0` caps how many cascades update per frame,
nearest first.

> **Gotcha** — round-robin amortisation is **off by default** (`budget = 0`,
> meaning "all stale cascades"). A non-zero budget leaves stale cascades whose
> light matrices no longer cover the current slice, so shadows pop while the
> camera moves. Opt in only if you know you need it.

> **Important** — wholesale changes to the caster set bypass dirty tracking.
> After `Scene::ResetToDefaults`, a scene load, or swapping a model on an
> entity without touching its transform, call `Renderer::forceCSMUpdate()`.
> The editor's `forceAllCSMUpdate_()` does this at every such point, including
> after undo.

### Editor knobs

Under **Sun / Shadows Controls** in the editor
(`Editor/src/EditorApplication.cpp`):

| Control | Range | Maps to |
|---|---|---|
| Rotate Sun (Yaw/Pitch) | checkbox | `setUseSunYawPitch` |
| Yaw / Pitch | −180…180 / −89…89 | `setSunYawPitchDegrees` |
| Sun dir | −1…1 per axis | `setSunDir` |
| CSM Enabled | checkbox | `setCSMEnabled` |
| Cascades | 1–4 | `setCSMNumCascades` |
| Base Resolution | 512–4096 | `setCSMBaseResolution` |
| Split Lambda | 0–1 | `setCSMLambda` (see gotcha above) |
| Max Shadow Distance | 10–2000 | `setCSMMaxShadowDistance` |
| Cascade Padding (m) | 0–50 | `setCSMCascadePadding` |
| Depth Margin (m) | 0–50 | `setCSMDepthMargin` |
| Stability Pos / Ang Epsilon | 0–0.5 m / 0–5° | `setCSMEpsilons` |
| Update Budget (cascades/frame) | 0–cascades | `setCSMCascadeBudget` |
| Far Re-render Interval (frames) | 1–4 | `setCSMDynamicIntervalCap` |
| Slope / Constant Depth Bias | 0–8 / 0–16 | `setCSMSlopeDepthBias`, `setCSMConstantDepthBias` |
| Cull Front Faces | checkbox | `setCSMCullFrontFaces` |
| Receiver Bias Const / Slope (texels) | 0–8 | `setShadowBiasConst`, `setShadowBiasSlope` |
| PCF Radius (cascade 0–3) | 0–4 | `setCascadeKernel` |
| Force Rebuild CSM | button | `forceCSMUpdate` on both renderers |

**CSM Debug** switches the forward shader into a visualisation mode via
`setCSMDebugMode(int)` (clamped 0–5): `0` off, `1` cascade index, `2` shadow
factor, `3` light depth, `4` sampled depth, `5` projected UV.

`getCSMSnapshot()` returns the live `CSMSnapshot` for debug UI.

Sun direction is either set directly or derived every frame from yaw/pitch,
which is on by default with Unity-like values (`yaw -30°`, `pitch 50°`).

> **Note** — `setSunDir` compares against the current direction with an epsilon
> and only marks shadow params dirty if it actually changed, so calling it
> every frame with a constant value is free.

## Frustum culling

The forward pass builds the culling frustum from the same camera and the same
clip planes as the projection matrix it renders with:

```c++
const Frustum camFrustum = createFrustumFromCamera(
    cam, float(fp.viewportW) / float(fp.viewportH), glm::radians(cam.Zoom),
    cam.NearClip, cam.FarClip);
scene.RenderScene(camFrustum, *shader_, cam, fp.viewportH);
```

`createFrustumFromCamera` (`Engine/src/core/Components.h`) produces six `Plane`s
from the camera basis. Each entity's `AABB` is tested with
`AABB::isOnFrustum(frustum, transform)`, which transforms the box centre by the
world matrix and rebuilds axis-aligned extents from the scaled orientation
vectors, then does the standard projection-radius test against all six planes.
Rejected entities increment `RenderStats::culled`.

Culling requires the entity to have `ModelComponent`, `Transform` **and**
`AABB` — `RenderScene` iterates exactly that view. An entity missing `AABB` is
never drawn.

> **Gotcha** — the AABB generator uses `std::numeric_limits<float>::lowest()`,
> not `min()`. `min()` is the smallest *positive* float, which breaks the
> max-reduction for meshes whose vertices are all negative on an axis and
> produces wrong bounds, hence misculling.

## Mesh LOD

Every mesh is loaded with up to three index buffers over **one shared vertex
buffer** (`Mesh::kLodCount == 3`). Level 0 is the full mesh; levels 1 and 2 are
meshoptimizer simplifications targeting roughly 25 % and 8 % of the original
index count.

```c++
struct LodRange {
    unsigned ebo = 0;
    GLsizei  indexCount = 0;
};
const LodRange& Lod(int level) const; // level is clamped to [0, kLodCount-1]
```

Because only indices differ between levels, LODs cost VRAM in indices alone and
share the same VAO, vertex attributes and material state — so switching level
mid-batch never forces a rebind of anything except the element buffer. Each
issue path binds its own element buffer explicitly, since LOD draws mutate the
shared VAO's element-array binding:

```c++
void IssueDraw(int lod = 0) const;
void IssueDrawInstanced(GLsizei instanceCount, int lod = 0) const;
```

A level is only accepted if simplification meaningfully reduced the previous
level (`count >= 3 && count < prevCount * 9 / 10`); otherwise its slot stays
empty and `uploadLods_` aliases the previous level's EBO. Meshes with fewer than
192 indices are not simplified at all. Aliased levels are never double-deleted.

### How a level is chosen

`Scene::RenderScene` picks a level per entity from the projected size proxy
`distance / world radius`:

```c++
int lod = 0;
if (lodEnabled_) {
    const float ratio = dist / (radius * lodDistanceScale_);
    lod = (ratio > 60.f) ? 2 : (ratio > 25.f) ? 1 : 0;
}
```

`radius` is the world-space bounding-sphere radius (half the AABB diagonal
scaled by the largest axis scale, floored at `0.01`), and `dist` is the distance
from the camera to the world-space bounding-sphere centre. A large object stays
at LOD 0 much farther out than a small one, which is the point.

```c++
void  SetLODEnabled(bool v);
bool  GetLODEnabled() const;
void  SetLODDistanceScale(float s); // clamped to [0.1, 8.0]
float GetLODDistanceScale() const;
```

`lodDistanceScale_` defaults to `1.0f`; above 1 keeps high detail farther out,
below 1 switches down sooner. The editor exposes both under **Rendering
Toggles** (slider range 0.25–4).

Shadow maps use a coarser rule — `Scene::RenderShadowsCombined` draws cascades
0 and 1 at LOD 1 and cascades 2+ at LOD 2, since the cascade index is already a
distance proxy and shadow silhouettes tolerate coarse geometry.

> **Gotcha** — LOD was silently inert for OBJ assets from the start. Assimp's
> OBJ importer emitted per-face vertices (disconnected soup) without
> `aiProcess_JoinIdenticalVertices`, so meshoptimizer could not collapse a
> single edge and every level was rejected. The flag is now set in
> `Model::Decode`. If you add importer flags, do not drop it. The asset cooker
> reports per-model LOD acceptance, so you can check a new asset directly.

## Screen-space size cull

Separately from LOD, the forward pass can drop entities whose bounding sphere
projects to fewer than N pixels tall:

```c++
void  SetSmallCullEnabled(bool v);
bool  GetSmallCullEnabled() const;
void  SetSmallCullPixels(float px); // clamped to [0, 64]; 0 disables
float GetSmallCullPixels() const;
```

The test, in `Scene::RenderScene`:

```c++
const float pixelH = (float)viewportHeightPx * radius / (dist * tanHalfFov);
if (pixelH < smallCullPixels_) { stats.culledSmall++; continue; }
```

It needs the viewport pixel height, which `ForwardOpaquePass` passes as
`fp.viewportH`. Passing `0` disables the cull, as does a `Camera::Zoom` that
degenerates `tan(fov/2)`.

**What it trades.** Defaults are `smallCullEnabled_ = false` and
`smallCullPixels_ = 3.0f` — off, because it changes what is visible. The
tradeoffs, all of which only bite at high pixel floors:

- **Distant popping.** There is no hysteresis, so objects hovering at the
  threshold flicker in and out as the camera moves.
- **Orphaned shadows.** The object is dropped from the **forward pass only**.
  Its caster entry is deliberately left alone, so with a low sun you can
  briefly see the shadow of something you cannot see. This is intentional: the
  shadow pass is effectively free (see below), and camera-dependent caster
  culling reintroduces the cascade-ghosting bug.

The radius used is a conservative circumscribing sphere, so the cull
under-culls — it never drops an object that would have appeared larger.

Editor: **Rendering Toggles → Cull tiny objects** plus a **Min on-screen px**
slider (0–48). The `culledSmall` stat reports how many entities it dropped.

> **Gotcha** — `smallCullEnabled`/`smallCullPixels` are **not** written to the
> scene file. `SceneSerializer` persists `lodEnabled`, `lodDistanceScale`,
> `instancingEnabled` and `depthPrepass`, but not the size-cull settings, so
> they reset on scene load.

## GPU instancing and the batching key

`Scene::RenderScene` builds a `DrawItem` per mesh per surviving entity, sorts
them, then walks the sorted list looking for consecutive runs it can collapse
into one instanced draw.

```c++
struct DrawItem {
    uint64_t texKey = 0;
    const Mesh* mesh = nullptr;
    glm::mat4 model{ 1.0f };
    float     depth = 0.0f;
    int       lod = 0;
    entt::entity entity = entt::null;
};
```

**The sort key** is, in order: `texKey`, then mesh pointer, then LOD, then view
depth front-to-back. `texKey` is an FNV-1a hash of the material's five bound
texture ids (albedo, normal, metallic, roughness, AO) via
`Scene::texKeyFromMaterial_` — the material actually chosen for that entity, so
overrides bucket correctly. If an item has no material at all, it falls back to
`Mesh::TextureSignature()`.

**The run key** is the triple `(texKey, mesh, lod)`. A run of 2 or more items
sharing all three becomes a single `glDrawElementsInstanced`; a run of 1 is a
plain `glDrawElements` with a `model` uniform.

Instance matrices for the whole frame go into one buffer:
`uploadInstanceMats_` orphans `instanceVBO_` at its high-water capacity with
`glBufferData(nullptr)` then fills with `glBufferSubData`, and each run points
attributes 8–11 (divisor 1) at its own byte offset. This replaced a per-run
`glBufferData` + `glMapBuffer` cycle, which cost a driver sync per run and was
the top frame cost at high instance counts.

```c++
void SetInstancingEnabled(bool enabled);
bool GetInstancingEnabled() const;
```

> **Gotcha** — the instance buffer's capacity **never shrinks**. Mesh VAOs keep
> instanced-attribute pointers baked at large byte offsets; a smaller store
> would make later fetches out of bounds, which is undefined behaviour in GL.

> **Note** — inside a non-instanced run, the material is re-bound per item.
> Entities can share a `texKey` bucket (same textures) while carrying different
> override instances with different scalars, so the scalars must be re-uploaded.

### Optional depth prepass

```c++
void SetDepthPrepassEnabled(bool v);
bool GetDepthPrepassEnabled() const;
void SetDepthPrepassShader(Shader* s); // non-owning; set per frame by the forward pass
```

When enabled, `RenderScene` first draws the exact same runs, LODs and matrices
with a no-op fragment shader and colour writes masked, then shades with
`GL_EQUAL` and depth writes off, so the PBR/PCF shader runs at most once per
pixel. The prepass program uses the *same* vertex shader as the colour pass, so
`gl_Position` is bit-identical and `GL_EQUAL` is exact.

It is **off by default**: early-Z already rejects most occluded fragments in
roughly front-to-back scenes, so on typical content the extra geometry
submission costs more than the shading it saves. Enable it for scenes with heavy
fragment cost and bad depth ordering.

## Render statistics

`Scene::GetRenderStats()` returns the previous frame's `RenderStats`, which the
editor's Rendering Stats panel displays:

| Field | Meaning |
|---|---|
| `entitiesTotal` | Entities considered |
| `culled` | Rejected by the frustum |
| `culledSmall` | Rejected by the projected-size cull |
| `itemsBuilt` | `DrawItem`s surviving culling |
| `draws` | Non-instanced draw calls |
| `instancedDraws` | Instanced draw calls |
| `instances` | Total instances drawn via instancing |
| `submitted` | Items submitted to the GPU (draws + instances) |
| `vaoBinds`, `textureBinds` | State changes |
| `lodInstances[3]` | Submitted instances per LOD level |

## Performance characteristics

These are measured results from a per-pass A/B harness
(`tests/test_perf_render.cpp`, headless, RTX 3050 @ 1080p, 2026-07-20), run
across static bird's-eye, moving bird's-eye and low oblique wide shots. Every
configuration agreed. The findings are recorded in
`docs/ENGINE_AUDIT_2026-07.md`.

**Wide and bird's-eye frames are vertex/instance-count bound. They are not
shadow-bound, not PCF-bound, and not fill-bound.**

- **Shadows are effectively free.** `setCSMEnabled(false)` — which disables both
  the cascade re-render *and* the forward shader's PCF via `uShadowsOn = 0` —
  changed the median frame time by roughly 0 ms. So did lower shadow resolution
  (1024), fewer cascades (2), a shorter shadow distance (80), and a PCF kernel
  radius of 0. Cascades are already cached by the movement-margin cadence, and
  the depth-only LOD 2 draws are cheap.
- **Not fill or fragment bound.** Rendering at **quarter resolution**
  (480×270, sixteen times fewer pixels) produced the *same* median as 1080p.
  PCF taps are not the cost.
- **Only geometry volume moves the number.** Turning LOD off was the only knob
  that changed anything, at +9 to +11 ms (all LOD 0 versus mostly LOD 2).
  Roughly 37–44k instances cost roughly 14–20 ms regardless of every other
  setting.

**Practical consequence: to speed up a wide view you must draw fewer objects,
or fewer vertices per object. Nothing else helps.** Turning down "shadow
quality" will not improve your frame rate.

That is what the screen-space size cull exists for. Measured on a low oblique
25×25 shot with a 48 px floor: 39.3k → 16.2k instances, 16.6 → 7.1 ms (−57 %).

Two related conclusions worth keeping:

- **Do not build shadow-caster culling.** It measured zero benefit and it risks
  the known ghosting invariant — camera-dependent caster culling leaves stale
  bakes in cascades that are still considered valid.
- **The dynamic-caster throttle was the fix that mattered for moving scenes.**
  A wide, moving shot with a spinning hero object went from 94.3 ms to 51.4 ms
  median (−45 %), and the spin penalty alone from +44.5 ms to +1.4 ms. Earlier
  measurement had shown that case was ~78 ms GPU against only ~16 ms
  main-thread CPU, which is why parallel cull/submission was not built.

If editor frame times are far worse than the equivalent headless run, the
renderer is probably not the culprit. `Application` exposes
`frameSceneRenderMs`, `frameUiMs` and `frameSwapMs`, shown in the Rendering
Stats panel as "3D submit / editor UI / swap+wait", to attribute the frame
before you start tuning render settings.
