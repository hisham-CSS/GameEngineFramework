#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;

// Instancing: 4..7 for per-instance model matrix
layout (location = 8) in mat4 iModel;  // 8,9,10,11

uniform mat4 model;        // used when not instancing
uniform mat4 view;
uniform mat4 projection;
uniform int  uUseInstancing; // 0/1

uniform mat4 uLightVP;
out vec4 vShadowPos;

out VS_OUT {
    vec2 uv;
    mat3 TBN;              // to fragment
    vec3 worldPos;         // keep if your lighting needs it
} vs_out;

void main()
{
    mat4 M = (uUseInstancing == 1) ? iModel : model;

    // Basic transform
    vec4 wpos = M * vec4(aPos, 1.0);
    gl_Position = projection * view * wpos;
    vs_out.worldPos = wpos.xyz;

    // Transform to light clip space for shadow mapping
    vShadowPos = uLightVP * M * vec4(aPos, 1.0);

    // Build TBN in world space (assumes uniform scales; good enough for now)
    mat3 M3 = mat3(M);
    vec3 T = normalize(M3 * aTangent);
    vec3 N = normalize(M3 * aNormal);
    // Orthonormalize (gram-schmidt) to be robust:
    T = normalize(T - dot(T, N) * N);
    vec3 B = normalize(cross(N, T));
    vs_out.TBN = mat3(T, B, N);

    vs_out.uv = aTex;
}
