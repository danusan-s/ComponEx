#version 330 core

in vec3 fragDir;

out vec4 FragColor;

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform vec3 skyColor;     // colour straight overhead
uniform vec3 horizonColor; // fog colour, so fogged ground meets the sky

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

float fbm(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; ++i) {
        s += a * vnoise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        a *= 0.5;
    }
    return s;
}

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 dir = normalize(fragDir);
    vec3 L = normalize(lightDir);
    float h = max(dir.y, 0.0);
    float s = max(dot(dir, L), 0.0);

    // Gradient: haze at the horizon up to a deeper blue overhead.
    vec3 col = mix(horizonColor, skyColor, pow(h, 0.42));
    // Warm scattering towards the sun.
    col += lightColor * vec3(1.0, 0.62, 0.32) * (pow(s, 5.0) * 0.045 + pow(s, 40.0) * 0.12);

    // Clouds on a flat layer, thinning towards the horizon.
    if (dir.y > 0.01) {
        vec2 p = dir.xz / (dir.y + 0.12) * 0.75 + vec2(time * 0.01, time * 0.004);
        float c = fbm(p * 1.4);
        c = smoothstep(0.50, 0.82, c);
        float fade = smoothstep(0.01, 0.22, dir.y);
        vec3 cloud = mix(vec3(0.75, 0.80, 0.90), lightColor * vec3(1.0, 0.92, 0.80) * 0.5, pow(s, 3.0) + 0.25);
        col = mix(col, cloud, c * fade * 0.8);
    }

    // Sun disc.
    col += lightColor * smoothstep(0.99935, 0.99975, s) * 3.0;

    // Underwater (the game turns the fog up to ~0.18, against 0.007 above) there
    // is no sky to see, only the murk: show the fog colour.
    col = mix(col, horizonColor, smoothstep(0.01, 0.12, fogDensity));

    FragColor = vec4(pow(aces(col), vec3(1.0 / 2.2)), 1.0);
}
