#version 330 core
// Radial edge darkening on the tonemapped (gamma-space) image.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uIntensity;   // 0 = off .. 1 = corners fully darkened
uniform float uRoundness;   // 0 = follows the frame rectangle .. 1 = circular
uniform float uSmoothness;  // width of the falloff band (0 hard .. 1 soft)
uniform float uAspect;      // viewport width / height

void main() {
    vec3 col = texture(uScene, vUV).rgb;

    // Distance from centre. Correcting x by aspect (scaled in by roundness)
    // makes the darkened region a circle rather than a stretched ellipse.
    vec2 d = vUV - vec2(0.5);
    d.x *= mix(1.0, uAspect, clamp(uRoundness, 0.0, 1.0));
    float dist = length(d) * 1.41421356; // ~1.0 at the corners

    // 1 in the clear centre, ramping to 0 at the corners.
    float band = clamp(uSmoothness, 0.001, 1.0);
    float vig = 1.0 - smoothstep(1.0 - band, 1.0, dist);

    // intensity 0 => untouched; 1 => corners multiplied by `vig` (down to 0).
    float factor = mix(1.0, vig, clamp(uIntensity, 0.0, 1.0));
    FragColor = vec4(col * factor, 1.0);
}
