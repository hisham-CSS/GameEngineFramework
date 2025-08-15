#version 330 core
layout(location=0) in vec3 aPos;
layout(location=8) in mat4 iModel;  // if you support instanced shadows
uniform mat4 model;
uniform mat4 uLightVP;
uniform int  uUseInstancing;

void main() {
    mat4 M = (uUseInstancing==1) ? iModel : model;
    gl_Position = uLightVP * M * vec4(aPos, 1.0);  // <- required
}
