#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec3 CrownNormal;
in vec2 UV;
in vec3 Tint;
in float Vis;
in float CrownT;
in float HeightT;

out vec4 FragColor;

uniform sampler2D albedoMap; // RGBA, sRGB colour + coverage

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform vec3 skyAmbient;
uniform vec3 groundAmbient;
// How much the card normal is replaced by the crown normal (1 = pure volume).
uniform float crownBlend;
// Ferns and shrubs sit on the ground and take its shade; trees do not.
uniform float groundCover;

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

    vec3 albedo = tex.rgb * Tint;

    vec3 Ng = normalize(Normal);
    if (!gl_FrontFacing) Ng = -Ng;
    vec3 Ns = normalize(mix(Ng, normalize(CrownNormal), crownBlend));

    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - FragPos);

    // Wrapped diffuse: leaves scatter light, so the terminator is soft.
    float wrap = clamp((dot(Ns, L) + 0.4) / 1.4, 0.0, 1.0);
    // Light reaching the inside of a crown is filtered by the leaves above it.
    float depth = mix(1.0, mix(0.22, 1.0, smoothstep(0.15, 0.95, CrownT)) *
                           mix(0.45, 1.0, sqrt(HeightT)), 1.0 - groundCover);
    float vis = Vis * depth;

    // Backlit leaves glow.
    float back = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * (1.0 - wrap * 0.5);
    vec3 direct = lightColor * vis * (albedo * wrap + albedo * vec3(1.0, 0.95, 0.6) * back * 0.9);

    float ao = mix(mix(0.3, 1.0, smoothstep(0.0, 1.0, CrownT)) * mix(0.5, 1.0, HeightT), 0.6, groundCover);
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, Ns.y * 0.5 + 0.5) * ao;

    vec3 color = applyFog(direct + ambient, FragPos);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
