#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uHDRColor;
uniform float uExposure;

// Reinhard (simple, optional)
// vec3 reinhard(vec3 x) { return x / (1.0 + x); }

// ACES (Narkowicz 2015)
vec3 aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uHDRColor, vUV).rgb;   // linear HDR from first pass
    hdr *= uExposure;                         // exposure

    vec3 mapped = aces(hdr);
    // Gamma to sRGB (approx 2.2)
    mapped = pow(mapped, vec3(1.0/2.2));

    FragColor = vec4(mapped, 1.0);
}
