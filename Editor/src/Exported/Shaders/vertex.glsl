#version 330 core

// Required for the depth prepass: the same source is linked into a second
// program (empty fragment stage) and the color pass uses GL_EQUAL — without
// this, GLSL does not guarantee identical gl_Position across programs.
invariant gl_Position;

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;

// Instancing: 4..7 for per-instance model matrix
layout (location = 8) in mat4 iModel;  // 8,9,10,11

#ifdef SKINNED
// Skinning (ROADMAP M3.2e). Compiled into the SAME source with "#define
// SKINNED 1" injected after #version, so the skinned program can differ from
// the static one in nothing but the skin. Four joint indices and weights per
// vertex (Mesh::UploadSkin puts them at attributes 5 and 6), and the palette
// in a std140 uniform block: 128 mat4 is 8 KB, under the 16 KB minimum every
// GL 3.3 core driver guarantees, where a plain uniform array could not hold a
// humanoid's joints beside uLightVP[4] inside the 1024-component minimum.
layout (location = 5) in ivec4 aJoints;
layout (location = 6) in vec4  aWeights;
layout (std140) uniform uBones { mat4 uBoneMat[128]; };
#endif

uniform mat4 model;        // used when not instancing
uniform mat4 view;
uniform mat4 projection;
uniform int  uUseInstancing; // 0/1

uniform mat4 uLightVP[4];

out VS_OUT {
    vec2  uv;
    mat3  TBN;              // to fragment
    vec3  worldPos;         // keep if your lighting needs it
    float viewDepth;
} vs_out;

void main()
{
    mat4 M = (uUseInstancing == 1) ? iModel : model;

    vec3 P  = aPos;
    vec3 Nn = aNormal;
    vec3 Tt = aTangent;
#ifdef SKINNED
    // The blended skin matrix moves the position; its upper 3x3 moves the
    // normal and tangent too (toon bands and the outline read the normal, so
    // a posed limb lit by its rest normal would shade wrong).
    mat4 S = aWeights.x * uBoneMat[aJoints.x] + aWeights.y * uBoneMat[aJoints.y]
           + aWeights.z * uBoneMat[aJoints.z] + aWeights.w * uBoneMat[aJoints.w];
    P  = vec3(S * vec4(aPos, 1.0));
    mat3 S3 = mat3(S);
    Nn = S3 * aNormal;
    Tt = S3 * aTangent;
#endif

    vec4 wpos = M * vec4(P, 1.0);
    vec4 viewPos  = view * wpos;
    gl_Position = projection * viewPos;

    // TBN in world space
    mat3 M3 = mat3(M);
    vec3 T = normalize(M3 * Tt);
    vec3 N = normalize(M3 * Nn);
    T = normalize(T - dot(T, N) * N);
    vec3 B = normalize(cross(N, T));
    vs_out.TBN = mat3(T, B, N);

    vs_out.uv = aTex;
    vs_out.worldPos = wpos.xyz;
    vs_out.viewDepth = -viewPos.z;  
}