#version 330 core
// Batched 2D sprite/quad fragment stage (Renderer2D).
//
// Untextured draws bind the renderer's built-in 1x1 white texture rather than
// branching, so colour-only quads batch together with textured ones.
//
// Output is written straight to the final LDR target: the UI pass runs AFTER
// tonemapping and all post-processing, so these colours are already in gamma
// space and must NOT be tonemapped or graded again. Text and thin UI edges are
// the first things to visibly suffer if they are.
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTex;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uTex, vUV);
    vec4 c = texel * vColor;
    // Fully transparent fragments still cost blending and can leave depth/stencil
    // artefacts in some drivers; drop them early.
    if (c.a <= 0.0) discard;
    FragColor = c;
}
