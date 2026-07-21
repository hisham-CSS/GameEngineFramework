#version 330 core
// Specular prefilter: GGX importance-sampled environment, one mip per
// roughness step. Mip 0 is mirror-sharp, the last mip is fully rough.
out vec4 FragColor;
in  vec3 vDir;

uniform samplerCube uEnvironment;
uniform float uRoughness;
uniform float uEnvResolution; // face size of mip 0 of the SOURCE cube

const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

void main() {
    vec3 N = normalize(vDir);
    // The split-sum approximation assumes view == normal == reflection.
    vec3 V = N;

    const uint SAMPLES = 1024u;
    vec3 color = vec3(0.0);
    float weight = 0.0;

    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H = ImportanceSampleGGX(Xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Sample a MIP of the source chosen from the sample's solid angle.
        // Sampling mip 0 everywhere is the classic mistake here: sparse
        // samples over a high-res environment alias into bright speckles that
        // look like fireflies baked into every rough reflection.
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);
        float D = DistributionGGX(NdotH, uRoughness);
        float pdf = (D * NdotH / (4.0 * max(HdotV, 1e-4))) + 1e-4;

        float saTexel = 4.0 * PI / (6.0 * uEnvResolution * uEnvResolution);
        float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);
        float mip = uRoughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

        color += textureLod(uEnvironment, L, max(mip, 0.0)).rgb * NdotL;
        weight += NdotL;
    }

    FragColor = vec4(color / max(weight, 1e-4), 1.0);
}
