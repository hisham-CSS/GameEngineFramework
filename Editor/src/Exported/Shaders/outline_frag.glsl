#version 330 core
// Ink outline from scene-depth discontinuities. A depth-only edge detect
// (no normal buffer in a forward renderer) catches silhouettes and depth
// steps -- the contour "ink" that completes a cel-shaded look.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;   // tonemapped colour (chain input)
uniform sampler2D uDepth;   // scene depth (DEPTH_COMPONENT24)
uniform vec2  uTexel;       // 1 / viewport
uniform float uThickness;   // neighbour tap offset, in pixels
uniform float uThreshold;   // edge sensitivity on relative depth
uniform float uStrength;    // ink opacity, 0..1
uniform vec3  uColor;       // ink colour
uniform float uNear;
uniform float uFar;

float linearDepth(vec2 uv) {
    float d = texture(uDepth, uv).r;      // [0,1] non-linear
    float z = d * 2.0 - 1.0;              // -> NDC
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main() {
    vec3 col = texture(uScene, vUV).rgb;

    vec2 o = uTexel * max(uThickness, 0.0);
    float c  = linearDepth(vUV);
    float l  = linearDepth(vUV - vec2(o.x, 0.0));
    float r  = linearDepth(vUV + vec2(o.x, 0.0));
    float u  = linearDepth(vUV - vec2(0.0, o.y));
    float dn = linearDepth(vUV + vec2(0.0, o.y));

    // RELATIVE gradient (divide by centre depth): keeps the threshold
    // scale-invariant so distant geometry isn't blanket-outlined.
    float g = (abs(c - l) + abs(c - r) + abs(c - u) + abs(c - dn)) / max(c, 1e-3);
    float edge = smoothstep(uThreshold, uThreshold * 2.0, g);

    col = mix(col, uColor, edge * clamp(uStrength, 0.0, 1.0));
    FragColor = vec4(col, 1.0);
}
