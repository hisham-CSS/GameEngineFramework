#version 330 core
// Bloom bright-pass: keep the HDR energy above a threshold, with a soft knee so
// the transition into bloom is gradual (no hard cut-in as a highlight brightens).
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uThreshold; // luminance where bloom starts
uniform float uKnee;      // soft-knee width (fraction of threshold)

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float l = max(max(c.r, c.g), c.b);      // luminance ~ max channel (HDR-safe)

    // Soft-knee curve (Karis / Unity style): 0 below (threshold-knee), ramps
    // quadratically through the knee, linear above.
    float knee = max(uThreshold * uKnee, 1e-4);
    float soft = clamp(l - uThreshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 1e-4);
    float contrib = max(soft, l - uThreshold) / max(l, 1e-4);

    FragColor = vec4(c * contrib, 1.0);
}
