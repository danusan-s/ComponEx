#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 Bake;
in vec3 Tint;

out vec4 FragColor;

// The engine's PBR material path binds five texture slots; this shader reuses
// them for its own layers:
uniform sampler2D albedoMap;    // forest floor albedo (sRGB)
uniform sampler2D normalMap;    // forest floor normal
uniform sampler2D metallicMap;  // forest floor ARM: R = occlusion, G = roughness
uniform sampler2D roughnessMap; // rock albedo (sRGB)
uniform sampler2D aoMap;        // rock normal

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
// 1: beyond the grass the ground turns the colour of grass seen from afar, so a
// meadow reads as one where the blades have faded out.
uniform float farGrass;
// The sea's mean surface (metres). Ground under it is seen through the water:
// darker and bluer with depth, with the sun's light bunched into caustics.
uniform float waterLevel;

// The sea's three long swells, as in water.vert (keep the two equal): where the
// surface really is, so ground the water has drawn back from is dry.
const vec4 SWELL[3] = vec4[3](
    vec4(20.0, 90.0, 0.45, 0.0),
    vec4(75.0, 58.0, 0.25, 1.7),
    vec4(-35.0, 44.0, 0.14, 3.1));

float seaHeight(vec2 p) {
    float h = waterLevel;
    for (int i = 0; i < 3; ++i) {
        float a = radians(SWELL[i].x);
        float k = 6.2831853 / SWELL[i].y;
        h += SWELL[i].z * sin(k * dot(vec2(cos(a), sin(a)), p) - sqrt(9.81 * k) * time + SWELL[i].w);
    }
    return h;
}

const float PI = 3.14159265;

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

// Tangent frame from screen-space derivatives (the meshes carry no tangents).
// uv is in the glTF convention (v down), so the bitangent of an OpenGL normal
// map (green = up in the image) is -dP/dv.
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

vec3 decodeNormal(vec4 t) { return normalize(t.xyz * 2.0 - 1.0); }

float distributionGGX(float NdotH, float rough) {
    float a = rough * rough, a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Caustics: the bright net the sun draws on the seabed after the ripples have
// bent it. Light gathers along the edges between the cells of a drifting Voronoi
// pattern (the distance to the second nearest point minus the nearest is ~0
// there), so the net is made of thin bright filaments around dark cells.
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float causticLayer(vec2 p, float t) {
    vec2 i = floor(p), f = fract(p);
    float d1 = 8.0, d2 = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = 0.5 + 0.5 * sin(t + 6.2831853 * hash22(i + g));
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
        }
    }
    float edge = sqrt(d2) - sqrt(d1);
    float line = 1.0 - smoothstep(0.0, 0.30, edge);
    return line * line;
}

float caustics(vec2 xz) {
    // Two layers at unrelated scales and headings, so the cells never repeat.
    float a = causticLayer(xz * 0.55 + vec2(time * 0.10, 0.0), time * 0.9);
    float b = causticLayer(mat2(0.8, -0.6, 0.6, 0.8) * xz * 0.83 + 7.3, time * 0.7 + 3.0);
    return 0.6 * (a + b) + 0.4 * a * b;
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
    vec3 P = FragPos;
    vec3 Ng = normalize(Normal);

    // --- Layer weights -----------------------------------------------------
    float slope = 1.0 - Ng.y;
    float rockW = smoothstep(0.17, 0.36, slope + (vnoise(P.xz * 0.21) - 0.5) * 0.10);

    // --- Forest floor, sampled twice at different scales/rotations so the
    // repeat is not visible, blended by low-frequency noise.
    vec2 uv1 = P.xz / 2.4;
    vec2 uv2 = mat2(0.8, -0.6, 0.6, 0.8) * (P.xz / 1.75) + 0.37;
    float mixW = smoothstep(0.3, 0.7, vnoise(P.xz * 0.05));
    vec3 floorAlbedo = mix(texture(albedoMap, uv1).rgb, texture(albedoMap, uv2).rgb, mixW);
    vec3 floorArm    = mix(texture(metallicMap, uv1).rgb, texture(metallicMap, uv2).rgb, mixW);
    vec3 floorN = normalize(mix(
        perturb(Ng, P, uv1, decodeNormal(texture(normalMap, uv1))),
        perturb(Ng, P, uv2, decodeNormal(texture(normalMap, uv2))), mixW));

    // Sunlit flat ground is meadow; shaded ground stays brown leaf litter.
    float meadow = smoothstep(0.45, 0.95, Bake.x) * (1.0 - rockW);
    floorAlbedo *= mix(vec3(0.95, 0.9, 0.8), vec3(1.15, 1.0, 0.85), vnoise(P.xz * 0.031));
    floorAlbedo = mix(floorAlbedo, floorAlbedo * vec3(0.55, 0.9, 0.32) + vec3(0.01, 0.025, 0.0), meadow * 0.7);

    // --- Rock on steep ground, triplanar (a planar projection would smear).
    vec3 w = pow(abs(Ng), vec3(5.0));
    w /= w.x + w.y + w.z;
    vec2 ux = P.zy / 3.2, uy = P.xz / 3.2, uz = P.xy / 3.2;
    vec3 rockAlbedo = texture(roughnessMap, ux).rgb * w.x + texture(roughnessMap, uy).rgb * w.y +
                      texture(roughnessMap, uz).rgb * w.z;
    vec3 rockN = normalize(
        perturb(Ng, P, ux, decodeNormal(texture(aoMap, ux))) * w.x +
        perturb(Ng, P, uy, decodeNormal(texture(aoMap, uy))) * w.y +
        perturb(Ng, P, uz, decodeNormal(texture(aoMap, uz))) * w.z);

    vec3 albedo = mix(floorAlbedo, rockAlbedo, rockW) * Tint;
    // Ground the waves keep wet is darker and richer.
    float sea = seaHeight(P.xz);
    float aboveSea = P.y - sea;
    albedo *= mix(0.62, 1.0, smoothstep(0.0, 0.45, aboveSea));
    if (farGrass > 0.5) {
        float far = smoothstep(25.0, 60.0, length(P - cameraPos));
        vec3 grassAlbedo = vec3(0.07, 0.15, 0.025) * (0.85 + 0.3 * vnoise(P.xz * 0.04)) * Tint;
        albedo = mix(albedo, grassAlbedo, far * (1.0 - rockW));
    }
    vec3 N = normalize(mix(floorN, rockN, rockW));
    float rough = mix(floorArm.g, 0.8, rockW);
    float texAO = mix(floorArm.r, 1.0, rockW);

    // --- Lighting ------------------------------------------------------------
    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - P);

    // Baked canopy shadow is an average; open it up into dappled sun spots.
    // The threshold t is chosen so the lit fraction still matches Bake.x.
    float vis = Bake.x;
    float gaps = vnoise(P.xz * 0.85 + vec2(time * 0.12, time * 0.05)) * 0.7 +
                 vnoise(P.xz * 2.1 - vec2(time * 0.09, 0.0)) * 0.3;
    float t = 1.0 - vis;
    vis *= mix(1.0, smoothstep(t - 0.12, t + 0.12, gaps), 4.0 * vis * (1.0 - vis));

    float NL = max(dot(N, L), 0.0);
    // The geometric normal gates the light so normal-mapped bumps do not leak
    // sun onto faces that point away from it.
    float lit = NL * smoothstep(0.0, 0.1, dot(Ng, L)) * vis;

    vec3 H = normalize(L + V);
    float spec = distributionGGX(max(dot(N, H), 0.0), rough) * 0.04 * PI * 0.25;
    // Under the sea: the light passes down and back up through the water, which
    // takes the red first. When the camera is under it too, the fog is the way
    // back up to the eye, so only the way down is counted here.
    float depthSea = max(sea - P.y, 0.0);
    float seen = 1.0 - smoothstep(0.01, 0.12, fogDensity);   // 1: camera above the water
    float sunSea = 1.0;
    if (depthSea > 0.05) {
        // Fades in with depth (none on the wet edge), out with depth (the sun's
        // light thins), and off steep ground, where the flat projection stretches.
        float k = smoothstep(0.05, 0.45, depthSea) * exp(-depthSea * 0.30) * smoothstep(0.55, 0.9, Ng.y);
        sunSea = 1.0 + 2.0 * k * caustics(P.xz);
    }
    vec3 direct = lightColor * lit * (albedo * sunSea + vec3(spec));

    float skyVis = Bake.y * texAO;
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, N.y * 0.5 + 0.5) * skyVis;

    vec3 lightLeft = exp(-depthSea * vec3(0.34, 0.095, 0.05) * mix(1.0, 1.5, seen));
    vec3 color = applyFog((direct + ambient) * lightLeft, P);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
