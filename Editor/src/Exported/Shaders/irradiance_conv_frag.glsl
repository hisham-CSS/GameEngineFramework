#version 330 core
// Diffuse irradiance: cosine-weighted convolution of the environment cube.
// Result is the DIFFUSE term the split-sum approximation multiplies by albedo,
// so the 1/PI normalisation and the PI from the Lambert integral cancel into
// the `PI * sum / n` below.
out vec4 FragColor;
in  vec3 vDir;

uniform samplerCube uEnvironment;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vDir);
    // Build a tangent frame around N. The up-vector swap avoids the
    // degenerate cross product when N is (near) vertical, which otherwise
    // shows up as two black texels at the poles of the irradiance cube.
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    const float step = 0.025;

    for (float phi = 0.0; phi < 2.0 * PI; phi += step) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += step) {
            // spherical -> tangent -> world
            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 world = tangent.x * right + tangent.y * up + tangent.z * N;
            // cos(theta) is the Lambert term, sin(theta) the solid-angle
            // measure for this parameterisation.
            irradiance += texture(uEnvironment, world).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }

    irradiance = PI * irradiance / max(samples, 1.0);
    FragColor = vec4(irradiance, 1.0);
}
