#version 330 core

in VS_OUT {
    vec2 uv;
    mat3 TBN;
    vec3 worldPos;
} fs_in;

out vec4 FragColor;

// Existing
uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform int uHasNormalMap;
uniform int uNormalMapEnabled;

// NEW: PBR toggle + params
uniform int  uUsePBR;                // 0 = old lambert, 1 = PBR
uniform vec3 uCamPos;                // camera world position
uniform vec3 uLightDir;              // directional light (world, pointing FROM light) e.g. normalize(vec3(0.3,-1,0.2))
uniform vec3 uLightColor;            // light color (linear)
uniform float uLightIntensity;       // scalar intensity
uniform float uMetallic;             // 0..1
uniform float uRoughness;            // 0..1
uniform float uAO;                   // 0..1

// --- helpers (GGX/Smith/Schlick) ---
float saturate(float x) { return clamp(x, 0.0, 1.0); }

float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d + 1e-7);
}

float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k + 1e-7);
}

float G_Smith(float NdotV, float NdotL, float k)
{
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

vec3 F_Schlick(vec3 F0, float VdotH)
{
    float f = pow(1.0 - VdotH, 5.0);
    return F0 + (1.0 - F0) * f;
}

vec3 getNormal()
{
    // geometric normal from VS:
    vec3 N = normalize(fs_in.TBN[2]);
    if (uNormalMapEnabled == 1 && uHasNormalMap == 1) {
        vec3 n_tan = texture(normalMap, fs_in.uv).xyz * 2.0 - 1.0;
        N = normalize(fs_in.TBN * n_tan);
    }
    return N;
}

void main()
{
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb; // already linear due to sRGB texture upload

    vec3 N = getNormal();
    vec3 V = normalize(uCamPos - fs_in.worldPos);

    // simple legacy lambert fallback (kept for A/B)
    if (uUsePBR == 0) {
        vec3 L = normalize(-uLightDir);
        float ndl = max(dot(N, L), 0.0);
        vec3 color = albedo * (0.05 + uLightColor * uLightIntensity * ndl);
        FragColor = vec4(color, 1.0);
        return;
    }

    // --- Minimal Cook–Torrance (directional light only) ---
    float metallic  = saturate(uMetallic);
    float roughness = clamp(uRoughness, 0.045, 1.0); // avoid 0
    float ao        = saturate(uAO);

    vec3  L  = normalize(-uLightDir);          // light direction towards surface
    vec3  H  = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Fresnel reflectance at normal incidence
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float a = roughness;
    float k = (a + 1.0);
    k = (k * k) / 8.0; // Schlick-GGX geometry term factor

    float  D = D_GGX(NdotH, a);
    float  G = G_Smith(NdotV, NdotL, k);
    vec3   F = F_Schlick(F0, VdotH);

    vec3 numerator   = F * (D * G);
    float denom      = 4.0 * max(NdotV, 1e-4) * max(NdotL, 1e-4);
    vec3 specular    = numerator / max(denom, 1e-4);

    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / 3.14159265;

    vec3 Lo = (diffuse + specular) * uLightColor * uLightIntensity * NdotL;

    // ambient (AO only for now; IBL later)
    vec3 ambient = 0.03 * albedo * ao;

    vec3 color = ambient + Lo;
    FragColor = vec4(color, 1.0);
}
