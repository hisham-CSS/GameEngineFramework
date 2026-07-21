#version 330 core
// Analytic gradient sky, used when no HDRi is assigned.
//
// This exists so image-based lighting works with NO asset: an engine whose
// flagship lighting feature requires the user to first find a 20MB .hdr file
// is a feature nobody switches on. It is a cheap Preetham-flavoured gradient,
// not a physical sky model, but it produces a plausible horizon/zenith ramp
// and a sun lobe, which is all IBL needs to stop looking flat.
out vec4 FragColor;
in  vec3 vDir;

uniform vec3  uSunDir;      // direction the light TRAVELS (points away from sun)
uniform vec3  uZenithColor;
uniform vec3  uHorizonColor;
uniform vec3  uGroundColor;
uniform float uSunIntensity;

void main() {
    vec3 d = normalize(vDir);
    vec3 toSun = normalize(-uSunDir);

    // Sky gradient: horizon -> zenith above, fading to ground below. pow()
    // tightens the band so the horizon is a band rather than a linear ramp.
    float up = d.y;
    vec3 sky = mix(uHorizonColor, uZenithColor, pow(clamp(up, 0.0, 1.0), 0.45));
    vec3 col = mix(uGroundColor, sky, smoothstep(-0.1, 0.05, up));

    // Sun: a bright core plus a wide glow. The core is deliberately finite --
    // an infinitely bright disk becomes a firefly in the prefiltered mips.
    float cosA = max(dot(d, toSun), 0.0);
    float disk = smoothstep(0.9995, 0.9999, cosA);
    float glow = pow(cosA, 350.0) * 0.35 + pow(cosA, 8.0) * 0.06;
    col += uSunIntensity * (disk * 8.0 + glow);

    // Horizon haze keeps the ground from reading as a hard seam in the
    // irradiance convolution.
    col += uHorizonColor * 0.15 * (1.0 - smoothstep(0.0, 0.25, abs(up)));

    FragColor = vec4(max(col, vec3(0.0)), 1.0);
}
