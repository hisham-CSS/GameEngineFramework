#version 330 core
// Shared vertex stage for every cubemap bake pass (equirect projection,
// procedural sky, irradiance convolution, specular prefilter).
//
// Renders a unit cube from the INSIDE, once per face, with uView set to the
// canonical look direction for that face. The interpolated local position is
// the world direction the fragment is looking along, which is all any of the
// bake shaders need.
layout(location = 0) in vec3 aPos;

out vec3 vDir;

uniform mat4 uProjection;
uniform mat4 uView;

void main() {
    vDir = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
