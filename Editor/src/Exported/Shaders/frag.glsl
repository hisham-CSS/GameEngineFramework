#version 330 core

in VS_OUT {
    vec2 uv;
    mat3 TBN;
    vec3 worldPos;
} fs_in;

out vec4 FragColor;

// camera / matrices (needed for view-space depth)
uniform mat4 view;

// base material inputs (same as before)
uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;
uniform int uHasNormalMap;
uniform int uNormalMapEnabled;

uniform int uShadowsOn;   // 0 = skip shadowing, 1 = use CSM
uniform int  uUsePBR;
uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;

uniform int uHasMetallicMap;
uniform int uHasRoughnessMap;
uniform int uHasAOMap;

uniform int uUseMetallicMap;
uniform int uUseRoughnessMap;
uniform int uUseAOMap;

// IBL
uniform samplerCube irradianceMap;
uniform samplerCube prefilteredMap;
uniform sampler2D  brdfLUT;
uniform int   uUseIBL;
uniform float uIBLIntensity;
uniform float uPrefilterMipCount;

uniform vec3 uBaseColor;
uniform vec3 uEmissive;

// -------- CSM uniforms (3 cascades) --------
uniform mat4 uLightVP[3];
uniform sampler2D uShadowCascade[3];
uniform float uCSMSplits[3];   // view-space distances (end of each split)
// -------------------------------------------

// GGX helpers
float saturate(float x) { return clamp(x, 0.0, 1.0); }
float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = (NdotH*NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d + 1e-7);
}
float G_SchlickGGX(float NdotV, float k) { return NdotV / (NdotV*(1.0-k)+k+1e-7); }
float G_Smith(float NdotV, float NdotL, float k) { return G_SchlickGGX(NdotV,k)*G_SchlickGGX(NdotL,k); }
vec3  F_Schlick(vec3 F0, float VdotH) { float f = pow(1.0 - VdotH, 5.0); return F0 + (1.0 - F0)*f; }

vec3 getNormal()
{
    vec3 N = normalize(fs_in.TBN[2]);
    if (uNormalMapEnabled == 1 && uHasNormalMap == 1) {
        vec3 n_tan = texture(normalMap, fs_in.uv).xyz * 2.0 - 1.0;
        N = normalize(fs_in.TBN * n_tan);
    }
    return N;
}

// --- 3x3 PCF sampling against the addressed cascade texture ---
float sampleShadow(int ci, vec4 lightClip, vec3 N, vec3 L)
{
    // project to [0,1]
    vec3 proj = lightClip.xyz / max(lightClip.w, 1e-5);
    proj = proj * 0.5 + 0.5;

    // outside light frustum => lit
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // slope-scale bias
    float NdL  = max(dot(N, -L), 0.0);
    float bias = max(0.002 * (1.0 - NdL), 0.0005);

    float texel = 1.0 / float(textureSize(uShadowCascade[ci], 0).x);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        float closest = texture(uShadowCascade[ci], proj.xy + vec2(x,y)*texel).r;
        sum += (proj.z - bias) <= closest ? 1.0 : 0.0;
    }
    return sum / 9.0;
}

void main()
{
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb * uBaseColor;

    vec3 N = getNormal();
    vec3 V = normalize(uCamPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    // choose cascade using *view-space* depth
    float viewZ = -(view * vec4(fs_in.worldPos, 1.0)).z;   // OpenGL view space: forward is -Z
    int ci = (viewZ <= uCSMSplits[0]) ? 0
           : (viewZ <= uCSMSplits[1]) ? 1
           : 2;

    // light-space position for the chosen cascade
    vec4 lightClip = uLightVP[ci] * vec4(fs_in.worldPos, 1.0);

    if (uUsePBR == 0) {
        float ndl = max(dot(N, L), 0.0);
        float sh  = sampleShadow(ci, lightClip, N, L);
        vec3 color = albedo * (0.05 + sh * uLightColor * uLightIntensity * ndl);
        FragColor = vec4(color, 1.0);
        return;
    }

    float metallic  = (uUseMetallicMap==1 && uHasMetallicMap==1) ? texture(metallicMap,  fs_in.uv).r : uMetallic;
    float roughness = (uUseRoughnessMap==1 && uHasRoughnessMap==1) ? texture(roughnessMap, fs_in.uv).r : uRoughness;
    roughness = clamp(roughness, 0.045, 1.0);
    float ao       = (uUseAOMap==1 && uHasAOMap==1) ? texture(aoMap, fs_in.uv).r : uAO;
    ao = saturate(ao);

    vec3  H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float a = roughness;
    float k = (a + 1.0); k = (k*k) / 8.0;

    float  D = D_GGX(NdotH, a);
    float  G = G_Smith(NdotV, NdotL, k);
    vec3   F = F_Schlick(F0, VdotH);
    vec3 specular = (F * (D * G)) / max(4.0 * max(NdotV,1e-4) * max(NdotL,1e-4), 1e-4);

    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / 3.14159265;

    // shadow only modulates the direct term
    float shadow = (uShadowsOn == 1) ? sampleShadow(ci, lightClip, N, L) : 1.0;
    vec3 Lo = shadow * (diffuse + specular) * uLightColor * uLightIntensity * NdotL;

    // IBL
    vec3 ambient;
    if (uUseIBL == 1) {
        vec3 F0a = mix(vec3(0.04), albedo, metallic);
        vec3 Fa  = F_Schlick(F0a, NdotV);
        vec3 kS  = Fa;
        vec3 kD  = (1.0 - kS) * (1.0 - metallic);
        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;
        vec3 R = reflect(-V, N);
        float mip = roughness * uPrefilterMipCount;
        vec3 prefiltered = textureLod(prefilteredMap, R, mip).rgb;
        vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
        vec3 specularIBL = prefiltered * (Fa * brdf.x + brdf.y);
        ambient = (kD * diffuseIBL + specularIBL) * ao * uIBLIntensity;
    } else {
        ambient = 0.03 * albedo * ao;
    }

    vec3 color = ambient + Lo + uEmissive;
    FragColor = vec4(color, 1.0);
}
