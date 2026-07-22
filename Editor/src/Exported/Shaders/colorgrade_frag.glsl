#version 330 core
// Procedural colour grade on the tonemapped (gamma-space) image: white
// balance -> lift/gain -> contrast -> saturation. A self-contained stand-in
// for a LUT workflow that needs no external asset.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uContrast;    // 1 = neutral
uniform float uSaturation;  // 1 = neutral, 0 = greyscale
uniform float uTemperature; // -1 cool .. +1 warm
uniform float uTint;        // -1 green .. +1 magenta
uniform float uLift;        // black-point offset
uniform float uGain;        // white-point scale

void main() {
    vec3 c = texture(uScene, vUV).rgb;

    // White balance: warm pushes red up / blue down; tint trades green/magenta.
    c.r *= 1.0 + uTemperature * 0.2;
    c.b *= 1.0 - uTemperature * 0.2;
    c.g *= 1.0 + uTint * 0.2;

    // Lift (shadows) + gain (overall scale) about mid-grey.
    c = (c - 0.5) * uGain + 0.5 + uLift;

    // Contrast about mid-grey.
    c = (c - 0.5) * uContrast + 0.5;

    // Saturation toward Rec.709 luma.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, uSaturation);

    FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
