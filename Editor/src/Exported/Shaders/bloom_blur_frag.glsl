#version 330 core
// Separable Gaussian blur (9-tap). Run horizontally then vertically; repeat for
// a wider glow. uDirection is the per-tap texel step along one axis.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uImage;
uniform vec2 uDirection; // (1/w, 0) horizontal, or (0, 1/h) vertical

// 9-tap Gaussian weights (sigma ~2), symmetric.
const float W[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

void main() {
    vec3 sum = texture(uImage, vUV).rgb * W[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = uDirection * float(i);
        sum += texture(uImage, vUV + off).rgb * W[i];
        sum += texture(uImage, vUV - off).rgb * W[i];
    }
    FragColor = vec4(sum, 1.0);
}
