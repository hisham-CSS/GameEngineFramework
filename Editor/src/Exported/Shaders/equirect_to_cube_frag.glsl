#version 330 core
// Projects an equirectangular (lat/long) HDR image onto a cubemap face.
out vec4 FragColor;
in  vec3 vDir;

uniform sampler2D uEquirect;

const vec2 kInvAtan = vec2(0.1591, 0.3183); // 1/(2pi), 1/pi

vec2 dirToEquirectUV(vec3 d) {
    vec2 uv = vec2(atan(d.z, d.x), asin(clamp(d.y, -1.0, 1.0)));
    return uv * kInvAtan + 0.5;
}

void main() {
    vec3 d = normalize(vDir);
    vec3 c = texture(uEquirect, dirToEquirectUV(d)).rgb;
    // HDR sources routinely contain fireflies (sun disks sampled at low res)
    // that survive into the prefiltered mips as ringing. Clamp to a large but
    // finite value rather than letting a single texel dominate a whole mip.
    c = min(c, vec3(2000.0));
    FragColor = vec4(c, 1.0);
}
