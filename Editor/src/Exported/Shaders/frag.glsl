#version 330 core

in VS_OUT {
    vec2  uv;
    mat3  TBN;
    vec3  worldPos;
    float viewDepth;
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

// --- punctual lights (point / spot) ---------------------------------------
// The directional light above is the SUN: it is the one that casts the
// cascaded shadow maps. These extra lights are unshadowed, which is why they
// are a separate, bounded array rather than an extension of the sun path.
// Packed into vec4s to keep the uniform component count down.
#define MAX_PUNCTUAL_LIGHTS 16
uniform int  uNumLights;                              // how many are live
uniform vec4 uLightPosRange[MAX_PUNCTUAL_LIGHTS];     // xyz world pos, w range
uniform vec4 uLightColorInt[MAX_PUNCTUAL_LIGHTS];     // rgb colour,   a intensity
uniform vec4 uLightSpotDir[MAX_PUNCTUAL_LIGHTS];      // xyz spot dir, w cos(outer)
uniform vec4 uLightSpotMisc[MAX_PUNCTUAL_LIGHTS];     // x cos(inner), y type (0 point, 1 spot)
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
uniform int  uShadingModel;   // per-material: 0 PBR, 1 Toon/cel

// Transparency. 0 Opaque, 1 Mask, 2 Blend.
uniform int   uAlphaMode;
uniform float uOpacity;
uniform float uAlphaCutoff;

// -------- CSM uniforms (4 cascades) --------
uniform mat4 uLightVP[4];
uniform sampler2D uShadowCascade[4];
uniform float uCSMSplits[4];   // view-space distances (end of each split)
uniform int   uCascadeCount;

uniform float uShadowBiasConst;   // in light clip depth units (start with ~0.001�0.01)
uniform float uShadowBiasSlope;   // scales with (1 - dot(N,L))

uniform int   uCascadeKernel[4];   // e.g. {1,2,3,4}  => 1x1, 3x3, 5x5, 7x7
uniform float uSplitBlend;         // world-space meters to blend across splits

// -------------------------------------------
// debug mode
uniform int uCSMDebug;   // 0=off, 1=cascade index, 2=shadow factor, 3=light depth

uniform float uCascadeTexel[4];

// choose cascade by comparing *view-space* depth with split FARs
int chooseCascade(float inViewDepth)
{
    int c = 0;
    // strictly increasing splits; clamp at last
    for (int i = 0; i < uCascadeCount - 1; ++i) {
        if (inViewDepth < uCSMSplits[i]) { c = i; break; }
        c = i + 1;
    }
    return c;
}
// optional smooth blend between cascades near the boundary
float blendWeight(float d, float splitFar, float widthMeters)
{
    if (widthMeters <= 0.0) return 0.0;
    return clamp((d - (splitFar - widthMeters)) / max(widthMeters, 1e-6), 0.0, 1.0);
}

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


// --- shadow factor [0..1] for a cascade, or -1.0 when the fragment is
// outside that cascade's coverage (callers must treat -1 as "no opinion",
// NOT as lit — otherwise blending washes shadows out at coverage edges) ---
float pcfShadowCascade(int ci, vec4 lightClip, vec3 N, vec3 L)
{
    // project to [0,1]
    vec3 uvz = lightClip.xyz / max(lightClip.w, 1e-6);
    uvz = uvz * 0.5 + 0.5;

    // outside the cascade rect or past its far plane: no coverage
    if (uvz.z > 1.0 || uvz.x < 0.0 || uvz.x > 1.0 || uvz.y < 0.0 || uvz.y > 1.0)
        return -1.0;

    //float NdL  = max(dot(N, L), 0.0);
    float NdL  = max(dot(normalize(N), normalize(L)), 0.0);

    float texel = uCascadeTexel[ci];

    // fallback if uCascadeTexel wasn't set
    if (texel <= 0.0) texel = 1.0 / float(textureSize(uShadowCascade[ci], 0).x);
    float bias  = (uShadowBiasConst + uShadowBiasSlope * (1.0 - NdL)) * texel;

    int   r   = max(uCascadeKernel[ci], 0);   // radius in texels
    vec2  step = vec2(texel);

    float sum = 0.0;
    float w   = 0.0;
    // Box PCF; (optional) swap to Poisson if you prefer
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            float d = texture(uShadowCascade[ci], uvz.xy + vec2(x,y)*step).r;
            sum += ((uvz.z - bias) <= d) ? 1.0 : 0.0;
            w   += 1.0;
        }
    return (w > 0.0) ? (sum / w) : 1.0;
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
    float NdL  = max(dot(N, L), 0.0);
    //float bias = max(0.002 * (1.0 - NdL), 0.0005);
    float bias = max(1.5 * uCascadeTexel[ci], 0.0005) + (1.0 - NdL) * 0.001;
    vec2 texel = vec2(uCascadeTexel[ci]);

    //float texel = 1.0 / float(textureSize(uShadowCascade[ci], 0).x);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        float closest = texture(uShadowCascade[ci], proj.xy + vec2(x,y)*texel).r;
        sum += (proj.z - bias) <= closest ? 1.0 : 0.0;
    }
    return sum / 9.0;
}

// Cook-Torrance direct lighting for ONE light direction, without the light's
// own colour/intensity/attenuation. Factored out so the sun and every
// punctual light are shaded by exactly the same BRDF -- if this drifts, lights
// of different types stop matching and the scene looks subtly wrong.
vec3 pbrDirect(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness)
{
    vec3  H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    float a  = roughness;
    float k  = (a + 1.0); k = (k * k) / 8.0;

    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, k);
    vec3  F = F_Schlick(F0, VdotH);

    vec3 spec = (F * (D * G)) / max(4.0 * max(NdotV, 1e-4) * max(NdotL, 1e-4), 1e-4);
    vec3 kd   = (1.0 - F) * (1.0 - metallic);
    vec3 diff = kd * albedo / 3.14159265;

    return (diff + spec) * NdotL;
}

// Distance falloff: physically-plausible inverse square, windowed so a light
// reaches exactly zero at its range instead of being clipped mid-gradient
// (a hard cutoff shows up as a visible disc edge on the floor).
float distanceAttenuation(float dist, float range)
{
    float t   = dist / max(range, 1e-4);
    float t2  = t * t;
    float win = clamp(1.0 - t2 * t2, 0.0, 1.0);
    return (win * win) / (dist * dist + 1.0);
}

void main()
{
    vec4 albedoSample = texture(diffuseMap, fs_in.uv);
    vec3 albedo = albedoSample.rgb * uBaseColor;

    // Alpha coverage. uAlphaMode: 0 Opaque, 1 Mask, 2 Blend. The albedo
    // texture's alpha is LINEAR even in an sRGB texture (sRGB affects only
    // RGB), so it is used directly. Mask discards early -- before any lighting
    // -- so a cut-out fragment costs nothing to shade and never writes depth.
    float alpha = 1.0;
    if (uAlphaMode == 2) {          // Blend
        alpha = albedoSample.a * uOpacity;
    } else if (uAlphaMode == 1) {   // Mask
        if (albedoSample.a < uAlphaCutoff) discard;
    }

    vec3 N = getNormal();
    vec3 V = normalize(uCamPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    // pick cascade index using *view-space* depth (meters)
    // OpenGL convention: camera looks down -Z, so eye distance is -view.z
    float viewZ = -(view * vec4(fs_in.worldPos, 1.0)).z;
    
    int ci = 0;
    for (int i = 0; i < uCascadeCount - 1; ++i) {
        if (viewZ < uCSMSplits[i]) { ci = i; break; }
        ci = i + 1;
    }

    // light-space position for the chosen cascade
    vec4 lightClip = uLightVP[ci] * vec4(fs_in.worldPos, 1.0);

    vec3 proj = lightClip.xyz / max(lightClip.w, 1e-6);
    proj = proj * 0.5 + 0.5; // [0,1] in XY and Z

    // --- CSM debug views (return early) ---
    if (uCSMDebug == 1) {
        // color by cascade index
        vec3 cc = (ci == 0) ? vec3(1.0, 0.2, 0.2)
                : (ci == 1) ? vec3(0.2, 1.0, 0.2)
                             : vec3(0.2, 0.5, 1.0);
        FragColor = vec4(cc, 1.0);
        return;
    }
    if (uCSMDebug == 2) {
        // show shadow factor (white=lit, dark=shadowed)
        float shadow = (uShadowsOn == 1) ? pcfShadowCascade(ci, lightClip, N, normalize(-uLightDir)) : 1.0;
        if (shadow < 0.0) shadow = 1.0; // out of coverage
        FragColor = vec4(vec3(shadow), 1.0);
        return;
    }
    if (uCSMDebug == 3) {
        // visualize light-space depth 0..1
        float ndcZ = clamp(lightClip.z / lightClip.w * 0.5 + 0.5, 0.0, 1.0);
        FragColor = vec4(vec3(ndcZ), 1.0);
        return;
    }
    // 4 = sampled depth, 5 = projected UV (red if outside)
    if (uCSMDebug == 4) {
        float dtex = texture(uShadowCascade[ci], proj.xy).r;
        FragColor = vec4(vec3(dtex), 1.0);
        return;
    }
    if (uCSMDebug == 5) {
        bool o = (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0);
        FragColor = o ? vec4(1,0,0,1) : vec4(proj.xy, 0.0, 1.0); // UV heatmap; red=outside
        return;
    }

    // --- Toon / cel shading (per-material), independent of the global PBR
    // toggle. Banded diffuse (sun + punctual, shadowed sun) + a hard specular
    // glint + a rim light, over a flat environment-tinted ambient. Pairs with
    // the depth-edge ink outline for a full cartoon look.
    if (uShadingModel == 1) {
        float sh = (uShadowsOn == 1) ? pcfShadowCascade(ci, lightClip, N, L) : 1.0;
        if (sh < 0.0) sh = 1.0;                     // out of cascade coverage
        const float bands = 4.0;                    // hard diffuse steps

        float sunNL   = max(dot(N, L), 0.0) * sh;
        float sunStep = floor(sunNL * bands + 0.5) / bands;
        vec3  sun     = albedo * uLightColor * uLightIntensity * sunStep;

        vec3 pl = vec3(0.0);
        for (int i = 0; i < uNumLights && i < MAX_PUNCTUAL_LIGHTS; ++i) {
            vec3  toL  = uLightPosRange[i].xyz - fs_in.worldPos;
            float dist = length(toL);
            float rng  = uLightPosRange[i].w;
            if (dist > rng) continue;
            vec3  Li = toL / max(dist, 1e-4);
            float at = distanceAttenuation(dist, rng);
            if (uLightSpotMisc[i].y > 0.5) {
                float cd = dot(normalize(uLightSpotDir[i].xyz), -Li);
                at *= smoothstep(uLightSpotDir[i].w, uLightSpotMisc[i].x, cd);
            }
            if (at <= 0.0) continue;
            float nl = max(dot(N, Li), 0.0) * at;
            pl += albedo * uLightColorInt[i].rgb * uLightColorInt[i].a
                * (floor(nl * bands + 0.5) / bands);
        }

        vec3  H  = normalize(V + L);
        float sp = pow(max(dot(N, H), 0.0), 48.0);
        float specMask = smoothstep(0.5, 0.52, sp) * step(1e-3, sunNL);
        float rim = smoothstep(0.65, 0.85, 1.0 - max(dot(N, V), 0.0));

        vec3 amb = (uUseIBL == 1)
                 ? texture(irradianceMap, N).rgb * albedo * uIBLIntensity
                 : 0.12 * albedo;

        vec3 color = amb + sun + pl + vec3(specMask) * 0.5
                   + rim * 0.25 * uLightColor + uEmissive;
        FragColor = vec4(color, alpha);
        return;
    }

    if (uUsePBR == 0) {
        float ndl = max(dot(N, L), 0.0);
        float sh  = pcfShadowCascade(ci, lightClip, N, L);
        if (sh < 0.0) sh = 1.0; // out of coverage
        vec3 color = albedo * (0.05 + sh * uLightColor * uLightIntensity * ndl);
        FragColor = vec4(color, alpha);
        return;
    }

    float metallic  = (uUseMetallicMap==1 && uHasMetallicMap==1) ? texture(metallicMap,  fs_in.uv).r : uMetallic;
    float roughness = (uUseRoughnessMap==1 && uHasRoughnessMap==1) ? texture(roughnessMap, fs_in.uv).r : uRoughness;
    roughness = clamp(roughness, 0.045, 1.0);
    float ao       = (uUseAOMap==1 && uHasAOMap==1) ? texture(aoMap, fs_in.uv).r : uAO;
    ao = saturate(ao);

    float NdotV = max(dot(N, V), 0.0);   // still needed by the IBL term below

    // shadow only modulates the direct term
    float shadow = 1.0;
    if (uShadowsOn == 1) {
        shadow = pcfShadowCascade(ci, lightClip, N, L);
        if (shadow < 0.0 && ci + 1 < uCascadeCount) {
            // outside this cascade's coverage: the next (larger) one may cover
            vec4 lcFallback = uLightVP[ci+1] * vec4(fs_in.worldPos, 1.0);
            shadow = pcfShadowCascade(ci+1, lcFallback, N, L);
        }
        if (shadow < 0.0) shadow = 1.0; // nothing covers this fragment
    }

    // Blend across split planes using a view-space band uSplitBlend (meters).
    // A neighbor sample of -1 (fragment outside that cascade's coverage) is
    // skipped — blending toward "lit" there is what washed mid-range shadows
    // out entirely.
    if (uShadowsOn == 1 && uSplitBlend > 0.0) {
        // Far edge: blend with next cascade
        if (ci < (uCascadeCount - 1)) {
            float s = uCSMSplits[ci]; // far plane of current slice (view-space)
            float t = smoothstep(s - uSplitBlend, s + uSplitBlend, viewZ);
            if (t > 0.0) {
                vec4 lcNext = uLightVP[ci+1] * vec4(fs_in.worldPos, 1.0);
                float shadowNext = pcfShadowCascade(ci+1, lcNext, N, L);
                if (shadowNext >= 0.0) shadow = mix(shadow, shadowNext, t);
            }
        }
        // Near edge: blend with previous cascade. Weight wPrev is 0.5 at the
        // boundary and falls to 0 deeper into this cascade. (This mix used to
        // be inverted — deep fragments took 100% of the PREVIOUS cascade's
        // sample, which mostly lands outside its coverage: missing shadows.)
        if (ci > 0) {
            float sPrev = uCSMSplits[ci-1];
            float wPrev = 1.0 - smoothstep(sPrev - uSplitBlend, sPrev + uSplitBlend, viewZ);
            if (wPrev > 0.0) {
                vec4 lcPrev = uLightVP[ci-1] * vec4(fs_in.worldPos, 1.0);
                float shadowPrev = pcfShadowCascade(ci-1, lcPrev, N, L);
                if (shadowPrev >= 0.0) shadow = mix(shadow, shadowPrev, wPrev);
            }
        }
    }

    // --- sun (shadowed) ---
    vec3 Lo = shadow * pbrDirect(N, V, L, albedo, metallic, roughness)
            * uLightColor * uLightIntensity;

    // --- punctual lights (unshadowed) ---
    // Bounded loop: the CPU already sorted by influence and uploaded at most
    // MAX_PUNCTUAL_LIGHTS, so this is the strongest set, not an arbitrary one.
    for (int i = 0; i < uNumLights && i < MAX_PUNCTUAL_LIGHTS; ++i) {
        vec3  toLight = uLightPosRange[i].xyz - fs_in.worldPos;
        float dist    = length(toLight);
        float range   = uLightPosRange[i].w;
        if (dist > range) continue;

        vec3  Li    = toLight / max(dist, 1e-4);
        float atten = distanceAttenuation(dist, range);

        // spot cone: angle between the light's aim and the direction from the
        // light TO this fragment (-Li), softened between the inner and outer
        // cone so the edge is not a hard stencil.
        if (uLightSpotMisc[i].y > 0.5) {
            float cd = dot(normalize(uLightSpotDir[i].xyz), -Li);
            atten *= smoothstep(uLightSpotDir[i].w, uLightSpotMisc[i].x, cd);
        }
        if (atten <= 0.0) continue;

        Lo += pbrDirect(N, V, Li, albedo, metallic, roughness)
            * uLightColorInt[i].rgb * uLightColorInt[i].a * atten;
    }

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
    // alpha is 1.0 for Opaque/Mask; for Blend it is albedoAlpha*uOpacity, which
    // the transparent pass composites with SRC_ALPHA/ONE_MINUS_SRC_ALPHA.
    FragColor = vec4(color, alpha);
}

// old helper: linearize depth from [0,1] back to view-space meters
//uniform float uCamNear;   // 0.1
//uniform float uCamFar;    // matches ShadowCSMPass::maxShadowDistance()
//float linearizeDepth(float z01) {
//    // z01 is gl_FragCoord.z in [0,1]
//    float n = uCamNear, f = uCamFar;
//    return (2.0 * n * f) / (f + n - (2.0 * z01 - 1.0) * (f - n));
//}