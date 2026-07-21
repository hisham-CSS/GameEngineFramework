#version 330 core
out vec4 FragColor;
in  vec3 vDir;

uniform samplerCube uEnvironment;
uniform float uIntensity;

void main() {
    // Writes LINEAR HDR: this runs into the HDR target before the tonemap
    // pass, so the sky goes through exactly the same ACES curve and exposure
    // as the lit geometry. Tonemapping it separately is what makes a skybox
    // look pasted on.
    FragColor = vec4(texture(uEnvironment, normalize(vDir)).rgb * uIntensity, 1.0);
}
