#version 330 core
// Bloom composite: drawn with additive blend (GL_ONE, GL_ONE) into the HDR
// buffer, so the blurred glow is ADDED to the scene before tonemapping. The
// half-res bloom texture is bilinearly upscaled by the sampler.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uBloom;
uniform float uIntensity;

void main() {
    FragColor = vec4(texture(uBloom, vUV).rgb * uIntensity, 1.0);
}
