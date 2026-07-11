#version 330 core
// Depth-prepass fragment shader: depth writes only, no color output.
// Paired with the SAME vertex.glsl as the color pass so gl_Position is
// bit-identical and the color pass can use glDepthFunc(GL_EQUAL).
void main() {}
