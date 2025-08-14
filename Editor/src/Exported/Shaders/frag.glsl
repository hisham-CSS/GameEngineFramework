#version 330 core

in VS_OUT {
    vec2 uv;
    mat3 TBN;
    vec3 worldPos;
} fs_in;

out vec4 FragColor;

// Existing textures/uniforms you already bind:
uniform sampler2D diffuseMap;

// NEW: normal map plumbing
uniform sampler2D normalMap;
uniform int uHasNormalMap;       // set per mesh (0/1)
uniform int uNormalMapEnabled;   // toggle from UI (0/1)

// If you keep your existing lambert/phong, make sure it reads `N` below
// (This stub uses a very simple lambert to illustrate.)
uniform vec3 uLightDir = normalize(vec3(0.3, -1.0, 0.2));
uniform vec3 uLightColor = vec3(1.0);

void main()
{
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb;

    // Base normal = geometric normal from VS
    vec3 N = fs_in.TBN[2]; // N column

    // Normal map path (tangent -> world):
    if (uNormalMapEnabled == 1 && uHasNormalMap == 1) {
        vec3 n_tan = texture(normalMap, fs_in.uv).xyz * 2.0 - 1.0;
        N = normalize(fs_in.TBN * n_tan);
    } else {
        N = normalize(N);
    }

    // Tiny lambert (replace with your current lighting if different)
    float ndl = max(dot(N, -normalize(uLightDir)), 0.0);
    vec3 color = albedo * (0.05 + uLightColor * ndl);

    FragColor = vec4(color, 1.0);
}
