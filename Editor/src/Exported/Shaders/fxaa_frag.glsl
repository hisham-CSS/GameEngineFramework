#version 330 core
// FXAA 3.11-style edge antialiasing (Timothy Lottes' algorithm, rewritten
// for clarity rather than transcribed).
//
// Runs on GAMMA-SPACE LDR, after tonemapping. That ordering is not optional:
// every threshold below is tuned against PERCEPTUAL luma. Applied to linear
// HDR instead, the same numbers mean completely different things -- bright
// regions blow past the edge threshold and get smeared while dark detail is
// ignored entirely.
out vec4 FragColor;
in  vec2 vUV;

uniform sampler2D uScene;
uniform vec2  uTexel;        // 1.0 / resolution
uniform float uEdgeThreshold;    // relative contrast needed to treat as an edge
uniform float uEdgeThresholdMin; // absolute floor: ignore near-black noise
uniform float uSubpixel;         // 0 = off, 1 = full subpixel softening

// Luma from gamma-space RGB. The green-only shortcut FXAA often uses is
// cheaper but loses red/blue edges, which shows up on saturated UI and
// emissive geometry.
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

const int   kSearchSteps = 12;
const float kSearchScale[12] = float[12](
    1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

void main() {
    vec3  rgbM = texture(uScene, vUV).rgb;
    float lumaM = luma(rgbM);

    // 4-neighbourhood contrast
    float lumaN = luma(textureLod(uScene, vUV + vec2(0.0, -uTexel.y), 0.0).rgb);
    float lumaS = luma(textureLod(uScene, vUV + vec2(0.0,  uTexel.y), 0.0).rgb);
    float lumaW = luma(textureLod(uScene, vUV + vec2(-uTexel.x, 0.0), 0.0).rgb);
    float lumaE = luma(textureLod(uScene, vUV + vec2( uTexel.x, 0.0), 0.0).rgb);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = lumaMax - lumaMin;

    // Flat enough to leave alone. Skipping here is most of why FXAA is cheap:
    // the expensive search below runs only along actual edges.
    if (range < max(uEdgeThresholdMin, lumaMax * uEdgeThreshold)) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    // Corners, needed to tell a horizontal edge from a vertical one.
    float lumaNW = luma(textureLod(uScene, vUV + vec2(-uTexel.x, -uTexel.y), 0.0).rgb);
    float lumaNE = luma(textureLod(uScene, vUV + vec2( uTexel.x, -uTexel.y), 0.0).rgb);
    float lumaSW = luma(textureLod(uScene, vUV + vec2(-uTexel.x,  uTexel.y), 0.0).rgb);
    float lumaSE = luma(textureLod(uScene, vUV + vec2( uTexel.x,  uTexel.y), 0.0).rgb);

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float edgeHorz = abs(-2.0 * lumaW + lumaNW + lumaSW)
                   + abs(-2.0 * lumaM + lumaNS) * 2.0
                   + abs(-2.0 * lumaE + lumaNE + lumaSE);
    float edgeVert = abs(-2.0 * lumaN + lumaNW + lumaNE)
                   + abs(-2.0 * lumaM + lumaWE) * 2.0
                   + abs(-2.0 * lumaS + lumaSW + lumaSE);
    bool horizontal = edgeHorz >= edgeVert;

    // Step ACROSS the edge, toward whichever side has the steeper gradient.
    float luma1 = horizontal ? lumaN : lumaW;
    float luma2 = horizontal ? lumaS : lumaE;
    float grad1 = abs(luma1 - lumaM);
    float grad2 = abs(luma2 - lumaM);
    bool  steepest1 = grad1 >= grad2;

    float stepLength = horizontal ? uTexel.y : uTexel.x;
    if (steepest1) stepLength = -stepLength;

    // Average luma on the edge, used as the comparison value while walking.
    float lumaLocalAvg = 0.5 * ((steepest1 ? luma1 : luma2) + lumaM);
    float gradScaled = 0.25 * max(grad1, grad2);

    // Sample point half a texel across the edge.
    vec2 currentUV = vUV;
    if (horizontal) currentUV.y += stepLength * 0.5;
    else            currentUV.x += stepLength * 0.5;

    // Walk ALONG the edge in both directions until the contrast breaks, to
    // find how long the edge is. Blend strength comes from where this pixel
    // sits along that span, which is what turns a staircase into a line.
    vec2 offset = horizontal ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);
    vec2 uv1 = currentUV - offset;
    vec2 uv2 = currentUV + offset;

    float lumaEnd1 = luma(textureLod(uScene, uv1, 0.0).rgb) - lumaLocalAvg;
    float lumaEnd2 = luma(textureLod(uScene, uv2, 0.0).rgb) - lumaLocalAvg;
    bool reached1 = abs(lumaEnd1) >= gradScaled;
    bool reached2 = abs(lumaEnd2) >= gradScaled;
    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reached1 || !reached2) {
        for (int i = 2; i < kSearchSteps; ++i) {
            if (!reached1) {
                lumaEnd1 = luma(textureLod(uScene, uv1, 0.0).rgb) - lumaLocalAvg;
                reached1 = abs(lumaEnd1) >= gradScaled;
            }
            if (!reached2) {
                lumaEnd2 = luma(textureLod(uScene, uv2, 0.0).rgb) - lumaLocalAvg;
                reached2 = abs(lumaEnd2) >= gradScaled;
            }
            if (reached1 && reached2) break;
            // Growing steps: long edges resolve without paying 30 taps.
            if (!reached1) uv1 -= offset * kSearchScale[i];
            if (!reached2) uv2 += offset * kSearchScale[i];
        }
    }

    float dist1 = horizontal ? (vUV.x - uv1.x) : (vUV.y - uv1.y);
    float dist2 = horizontal ? (uv2.x - vUV.x) : (uv2.y - vUV.y);
    bool  nearer1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / max(edgeLength, 1e-6) + 0.5;

    // Reject the case where this pixel is on the wrong side of the edge:
    // blending there would smear the edge outward instead of softening it.
    bool isLumaMCentre = lumaM < lumaLocalAvg;
    bool correctVariation =
        ((nearer1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaMCentre;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Subpixel term: handles single-pixel features (wires, foliage, specular
    // sparkle) that the edge walk above cannot see because they have no run.
    float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaNS + lumaWE)
                                    + lumaNW + lumaNE + lumaSW + lumaSE);
    float subpixOffset1 = clamp(abs(lumaAvg - lumaM) / max(range, 1e-6), 0.0, 1.0);
    float subpixOffset2 = (-2.0 * subpixOffset1 + 3.0) * subpixOffset1 * subpixOffset1;
    float subpixFinal = subpixOffset2 * subpixOffset2 * uSubpixel;
    finalOffset = max(finalOffset, subpixFinal);

    vec2 finalUV = vUV;
    if (horizontal) finalUV.y += finalOffset * stepLength;
    else            finalUV.x += finalOffset * stepLength;

    FragColor = vec4(textureLod(uScene, finalUV, 0.0).rgb, 1.0);
}
