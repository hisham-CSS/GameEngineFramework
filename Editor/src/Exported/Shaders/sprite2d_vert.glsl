#version 330 core
// Batched 2D sprite/quad vertex stage (Renderer2D).
// Positions arrive already transformed into the mode's 2D space (screen pixels
// or world units) — the batcher bakes per-quad rotation on the CPU so quads
// with different transforms still share one draw call.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

uniform mat4 uViewProj;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
}
