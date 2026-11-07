#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
in vec3 Tint;
in float Vis;
in float Height;

out vec4 FragColor;

uniform sampler2D albedoMap;   // sRGB
uniform sampler2D normalMap;   // OpenGL (+Y) tangent-space
uniform sampler2D metallicMap; // ARM: R = occlusion, G = roughness (slot reused)

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
// 1 for trees: the lower trunk sits under its own crown and its neighbours'.
uniform float canopyShade;

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

void main()
{
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
