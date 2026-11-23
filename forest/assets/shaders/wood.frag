#version 460
#include "frame_data.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 UV;
layout(location = 3) in vec3 Tint;
layout(location = 4) in float Vis;
layout(location = 6) in float Height;
layout(location = 5) flat in float LodFade;

layout(location = 0) out vec4 FragColor;

layout(BIND(1, 1)) uniform sampler2D albedoMap;
layout(BIND(1, 2)) uniform sampler2D normalMap;
layout(BIND(1, 3)) uniform sampler2D metallicMap;

layout(std140, BIND(1, 0)) uniform Material {
    float windAmp;
    float canopyShade;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

const float PI = 3.14159265;

vec3 perturb(vec3 N, vec3 p, vec2 uv, vec3 tangentNormal) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    tangentNormal.y = -tangentNormal.y;
    return normalize(mat3(T * invmax, B * invmax, N) * tangentNormal);
}

float distributionGGX(float NdotH, float rough) {
    float a = rough * rough, a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
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

// LOD cross-fade (RenderSettings::lodDither); the same as the engine's diffuse.frag.
// LodFade is 0 for an ordinary instance. While an entity changes LOD level its
// old mesh has LodFade rising 0 -> 1 and the new one -1 -> 0; each discards its
// share of a screen-space dither pattern and the two shares are complementary.
// The pattern depends only on the pixel, so the depth pre-pass and the colour
// pass discard identically.
float lodDither() {
    // 8x8 Bayer matrix, by bit tricks: 64 evenly spread levels, and an exact
    // checkerboard at one half. (Interleaved gradient noise showed diagonal lines.)
    ivec2 p = ivec2(gl_FragCoord.xy);
    int x = p.x & 7, y = (p.x ^ p.y) & 7;
    int m = ((y & 1) << 5) | ((x & 1) << 4) | ((y & 2) << 2) | ((x & 2) << 1) |
            ((y & 4) >> 1) | ((x & 4) >> 2);
    return (float(m) + 0.5) / 64.0;
}

bool lodFadeDiscard() {
    if (LodFade == 0.0) return false;
    float n = lodDither();
    return LodFade > 0.0 ? n < LodFade : n >= 1.0 + LodFade;
}

void main()
{
    if (lodFadeDiscard()) discard;
#ifdef COMPONENG_DEPTH_PREPASS
    return; // a fading batch's depth pre-pass only needs the dither above
#endif

    vec3 Ng = normalize(Normal);
    if (!gl_FrontFacing) Ng = -Ng;

    vec3 albedo = texture(albedoMap, UV).rgb * Tint;
    vec3 arm = texture(metallicMap, UV).rgb;
    vec3 tn = texture(normalMap, UV).xyz * 2.0 - 1.0;
    vec3 N = perturb(Ng, FragPos, UV, normalize(tn));

    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - FragPos);

    float vis = Vis * mix(1.0, mix(0.45, 1.0, smoothstep(1.0, 9.0, Height)), canopyShade);
    float ao = arm.r * mix(1.0, mix(0.55, 1.0, smoothstep(0.0, 5.0, Height)), canopyShade);

    float lit = max(dot(N, L), 0.0) * smoothstep(0.0, 0.1, dot(Ng, L)) * vis;
    vec3 H = normalize(L + V);
    float spec = distributionGGX(max(dot(N, H), 0.0), arm.g) * 0.04 * PI * 0.25;
    vec3 direct = lightColor * lit * (albedo + vec3(spec));
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, N.y * 0.5 + 0.5) * ao;

    vec3 color = applyFog(direct + ambient, FragPos);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
