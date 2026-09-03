#version 330 core
layout(location=0) in vec3 aPos;
layout(location=8) in mat4 iModel;  // if you support instanced shadows
uniform mat4 model;
uniform mat4 uLightVP;
uniform int  uUseInstancing;

#ifdef SKINNED
// The same skin as vertex.glsl (ROADMAP M3.2e), so a posed fighter casts the
// shadow of its pose and not of its rest mesh. Same attributes, same block.
layout(location=5) in ivec4 aJoints;
layout(location=6) in vec4  aWeights;
layout(std140) uniform uBones { mat4 uBoneMat[128]; };
#endif

void main() {
    mat4 M = (uUseInstancing==1) ? iModel : model;
    vec3 P = aPos;
#ifdef SKINNED
    mat4 S = aWeights.x * uBoneMat[aJoints.x] + aWeights.y * uBoneMat[aJoints.y]
           + aWeights.z * uBoneMat[aJoints.z] + aWeights.w * uBoneMat[aJoints.w];
    P = vec3(S * vec4(aPos, 1.0));
#endif
    gl_Position = uLightVP * M * vec4(P, 1.0);  // <- required
}
