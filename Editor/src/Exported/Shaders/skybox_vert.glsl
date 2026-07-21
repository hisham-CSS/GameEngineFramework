#version 330 core
layout(location = 0) in vec3 aPos;

out vec3 vDir;

uniform mat4 uView;       // camera view with TRANSLATION STRIPPED
uniform mat4 uProjection;

void main() {
    vDir = aPos;
    vec4 clip = uProjection * uView * vec4(aPos, 1.0);
    // Force w == z so the cube lands exactly on the far plane. Combined with
    // glDepthFunc(GL_LEQUAL) this makes the sky fill only pixels no geometry
    // claimed, without needing a huge cube or a depth-range hack.
    gl_Position = clip.xyww;
}
