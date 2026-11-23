#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 ObjectColor;
in vec3 WorldNormal;
flat in float LodFade;

out vec4 FragColor;

// Material textures
uniform sampler2D albedoMap;      // base color
uniform sampler2D normalMap;      // tangent-space normal
uniform sampler2D metallicMap;    // metallic (0=dielectric, 1=metal)
uniform sampler2D roughnessMap;   // roughness (0=smooth, 1=rough)
uniform sampler2D aoMap;          // ambient occlusion

// Lighting, environment and fog (lightDir points towards the light) come from
// the FrameData block.
// Per-frame values, uploaded once per frame by the RenderSystem. Must match
// renderer/frame_data.hpp exactly, and be declared identically in every stage.
layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

// Toggle texture sampling (set to 0 to use solid colors / white textures)
uniform int useTextures;

// Toggle normal mapping
uniform int useNormalMap;

const float PI = 3.141592653589793;

// LOD cross-fade: LodFade is 0 normally. A fading entity's old mesh has it
// rising 0 -> 1 and the new mesh -1 -> 0; each discards its share of a fixed
// screen-space dither (interleaved gradient noise), and the two shares are
// complementary. The pattern must match in the depth pre-pass and colour pass.
float lodDither()
{
    return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

vec3 parseNormal(vec3 normal, sampler2D normalMap, vec2 uv, int useNormalMap) {
    if (useNormalMap == 0) return normalize(normal);
    vec3 mapNormal = texture(normalMap, uv).rgb;
    mapNormal = normalize(mapNormal * 2.0 - 1.0);
    // Build TBN (simplified: assume world-space-ish normal perturbation)
    vec3 N = normalize(normal);
    vec3 T = normalize(cross(N, vec3(0.0, 1.0, 0.0)));
    if (length(T) < 0.1) T = normalize(cross(N, vec3(0.0, 0.0, 1.0)));
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * mapNormal);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

vec3 schlickFresnel(float cosTheta, vec3 F0) {
    return pow(vec3(1.0 - cosTheta), vec3(5.0)) * (vec3(1.0) - F0) + F0;
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float k = a / 2.0; // Smith-Schlick-BGX
    float ggx1 = NdotL / (NdotL * (1.0 - k) + k);
    float ggx2 = NdotV / (NdotV * (1.0 - k) + k);
    return ggx1 * ggx2;
}

void main()
{
    if (LodFade != 0.0) {
        float n = lodDither();
        if (LodFade > 0.0 ? n < LodFade : n >= 1.0 + LodFade) discard;
    }
#ifdef COMPONENG_DEPTH_PREPASS
    // Depth pre-pass of a fading batch: the discard above is all it needed.
    return;
#endif

    vec3 N = normalize(WorldNormal);

    // Sample material properties
    vec3 albedo = ObjectColor;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    vec3 normal = N;

    if (useTextures == 1) {
        albedo = texture(albedoMap, TexCoords).rgb * ObjectColor;
        metallic = texture(metallicMap, TexCoords).r;
        roughness = texture(roughnessMap, TexCoords).r;
        ao = texture(aoMap, TexCoords).r;
    }
    if (useNormalMap == 1) {
        normal = parseNormal(N, normalMap, TexCoords, 1);
    }
    N = normalize(normal);

    // PBR: Cook-Torrance BRDF
    vec3 V = normalize(cameraPos - FragPos);
    vec3 L = normalize(lightDir); // lightDir points from surface to light

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Fresnel
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = schlickFresnel(VdotH, F0);

    // Normal Distribution Function
    float NDF = distributionGGX(N, H, roughness);

    // Geometry
    float G = geometrySmith(NdotV, NdotL, roughness);

    // Cook-Torrance specular
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    // Diffuse (Lambert, scaled by 1 - metallic)
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // Lighting
    float irradiance = max(NdotL, 0.0);
    vec3 radiance = lightColor * irradiance;

    vec3 ambient = ambientColor * ambientStrength * ao;

    vec3 color = (kD * albedo / PI + specular) * radiance + ambient;

    // Energy conservation: scale by AO
    color *= ao;

    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(0.4545)); // gamma 2.2

    // Fog
    if (fogColor.a > 0.0) {
        float distance = length(FragPos - cameraPos);
        float fog = smoothstep(fogStart, fogEnd, distance);
        color = mix(color, fogColor.rgb, fog);
    }

    FragColor = vec4(color, 1.0);
}
