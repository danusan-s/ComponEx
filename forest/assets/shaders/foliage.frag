#version 460
#include "frame_data.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 CrownNormal;
layout(location = 3) in vec2 UV;
layout(location = 4) in vec3 Tint;
layout(location = 5) in float Vis;
layout(location = 6) in float CrownT;
layout(location = 7) in float HeightT;

layout(location = 0) out vec4 FragColor;

layout(BIND(1, 1)) uniform sampler2D albedoMap;

layout(std140, BIND(1, 0)) uniform Material {
    float windAmp;
    vec3 crownCenter;
    float crownRadius;
    float crownBottom;
    float crownTop;
    float crownBlend;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

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
    vec4 tex = texture(albedoMap, UV);

    // Mip-mapping averages coverage with the empty texels, so distant cards
    // thin out; lower the cut-off as the card minifies to keep the density.
    vec2 dx = dFdx(UV * 1024.0), dy = dFdy(UV * 1024.0);
    float lod = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    float cut = mix(0.5, 0.26, clamp(lod / 3.5, 0.0, 1.0));
    if (tex.a < cut) discard;
#ifdef COMPONENG_DEPTH_PREPASS
    return; // the engine's depth pre-pass only needs the cut-out shape
#endif

    vec3 albedo = tex.rgb * Tint;

    vec3 Ng = normalize(Normal);
    if (!gl_FrontFacing) Ng = -Ng;
    vec3 Ns = normalize(mix(Ng, normalize(CrownNormal), crownBlend));

    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - FragPos);

    // Wrapped diffuse: leaves scatter light, so the terminator is soft.
    float wrap = clamp((dot(Ns, L) + 0.4) / 1.4, 0.0, 1.0);
    // Light reaching the inside of a crown is filtered by the leaves above it.
    float depth = mix(0.22, 1.0, smoothstep(0.15, 0.95, CrownT)) *
                  mix(0.45, 1.0, sqrt(HeightT));
    float vis = Vis * depth;

    // Backlit leaves glow.
    float back = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * (1.0 - wrap * 0.5);
    vec3 direct = lightColor * vis * (albedo * wrap + albedo * vec3(1.0, 0.95, 0.6) * back * 0.9);

    float ao = mix(0.3, 1.0, smoothstep(0.0, 1.0, CrownT)) * mix(0.5, 1.0, HeightT);
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, Ns.y * 0.5 + 0.5) * ao;

    vec3 color = applyFog(direct + ambient, FragPos);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
