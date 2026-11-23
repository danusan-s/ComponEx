#version 460
#include "frame_data.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 BladeNormal;
layout(location = 2) in vec3 Tint;
layout(location = 3) in vec2 Light;
layout(location = 4) in float BladeRand;
layout(location = 5) in float Along;

layout(location = 0) out vec4 FragColor;

layout(std140, BIND(1, 0)) uniform Material {
    float worldHalf;
    float heightCells;
    float tuftSide;
    float period;
    float chunkSize;
    vec3 chunkInfo;
    float fadeInStart;
    float fadeInEnd;
    float fadeStart;
    float fadeEnd;
    vec3 debugTint;
    float debugBands;
    float bladeLod;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

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

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 applyFog(vec3 col, vec3 P) {
    vec3 v = P - cameraPos;
    float d = length(v);
    float f = 1.0 - exp(-pow(d * fogDensity, 2.0));
    float sunAmt = pow(max(dot(v / d, lightDir), 0.0), 5.0);
    vec3 fc = mix(fogColor.rgb, lightColor * vec3(0.55, 0.42, 0.25), sunAmt * 0.45);
    return mix(col, fc, f);
}

void main()
{
    // Colour: dark at the root, bright at the tip, patches of drier grass, and
    // per-blade variation. Albedo values are linear.
    vec3 root = vec3(0.012, 0.035, 0.006);
    vec3 tip = vec3(0.13, 0.27, 0.035);
    vec3 dry = vec3(0.27, 0.22, 0.07);
    float dryness = smoothstep(0.55, 0.85, vnoise(FragPos.xz * 0.045 + 7.0));
    vec3 albedo = mix(root, mix(tip, dry, dryness * 0.8), pow(Along, 0.8));
    albedo *= (0.75 + 0.5 * BladeRand) * Tint;

    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - FragPos);

    // Blades are thin and curved: light them as if their normals lean up, which
    // reads as a soft meadow instead of a field of flat strips.
    vec3 Nb = normalize(BladeNormal);
    if (dot(Nb, V) < 0.0) Nb = -Nb;
    vec3 N = normalize(mix(Nb, vec3(0.0, 1.0, 0.0), 0.65));

    float vis = Light.x;
    float gaps = vnoise(FragPos.xz * 0.85 + vec2(time * 0.12, time * 0.05)) * 0.7 +
                 vnoise(FragPos.xz * 2.1 - vec2(time * 0.09, 0.0)) * 0.3;
    float t = 1.0 - vis;
    vis *= mix(1.0, smoothstep(t - 0.12, t + 0.12, gaps), 4.0 * vis * (1.0 - vis));

    float wrap = clamp((dot(N, L) + 0.3) / 1.3, 0.0, 1.0);
    float back = pow(clamp(dot(V, -L), 0.0, 1.0), 2.0) * Along;
    vec3 direct = lightColor * vis * (albedo * wrap + albedo * vec3(0.9, 1.0, 0.45) * back * 1.1);

    float ao = mix(0.35, 1.0, smoothstep(0.0, 0.7, Along)) * Light.y;
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, N.y * 0.5 + 0.5) * ao;

    vec3 color = applyFog(direct + ambient, FragPos);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
