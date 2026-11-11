#version 330 core

// The sea. A sum of sines gives the surface normal; the shading is
//   - body colour from the water's depth (read from the terrain's height map:
//     the engine has no depth read-back), turquoise in the shallows, dark blue
//     in the deep, and see-through where it is shallow;
//   - Fresnel: grazing views reflect the sky, looking down sees into the water;
//   - the sun's glint, and a faint glow where a wave crest is lit from behind;
//   - foam where the water meets the shore.
// Drawn blended, after the terrain and the sky.

in vec3 FragPos;

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

uniform sampler2D heightMap; // R32F terrain heights, (cells + 1)^2, row = z
uniform float worldHalf;
uniform float heightCells;
uniform vec3 skyColor;       // the sky's zenith colour, reflected
uniform vec3 skyAmbient;
uniform vec3 shallowColor;   // linear
uniform vec3 deepColor;

const float G = 9.81;
const int SWELLS = 3;  // the first three move the geometry too (water.vert)
const int WAVES = 8;
//                       direction (deg), wavelength (m), amplitude (m), phase
const vec4 WAVE[WAVES] = vec4[WAVES](
    vec4(20.0, 90.0, 0.45, 0.0),
    vec4(75.0, 58.0, 0.25, 1.7),
    vec4(-35.0, 44.0, 0.14, 3.1),
    vec4(110.0, 27.0, 0.30, 0.9),
    vec4(-70.0, 16.0, 0.17, 2.4),
    vec4(160.0, 9.5, 0.10, 4.0),
    vec4(40.0, 5.5, 0.055, 5.2),
    vec4(-130.0, 3.2, 0.03, 0.4));

float terrainHeight(vec2 xz) {
    vec2 g = (xz + worldHalf) * (heightCells / (2.0 * worldHalf));
    g = clamp(g, vec2(0.0), vec2(heightCells - 1e-3));
    ivec2 i = ivec2(g);
    vec2 t = g - vec2(i);
    float h00 = texelFetch(heightMap, i, 0).r;
    float h10 = texelFetch(heightMap, i + ivec2(1, 0), 0).r;
    float h01 = texelFetch(heightMap, i + ivec2(0, 1), 0).r;
    float h11 = texelFetch(heightMap, i + ivec2(1, 1), 0).r;
    if (t.x + t.y <= 1.0) return h00 + t.x * (h10 - h00) + t.y * (h01 - h00);
    return h11 + (1.0 - t.x) * (h01 - h11) + (1.0 - t.y) * (h10 - h11);
}

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

// Surface slope (d h / dx, d h / dz) and the height of the swells alone.
// Short waves fade out with distance, so far water settles to a smooth sheet
// instead of shimmering.
void waves(vec2 p, float dist, out vec2 slope, out float swell) {
    slope = vec2(0.0);
    swell = 0.0;
    for (int i = 0; i < WAVES; ++i) {
        float a = radians(WAVE[i].x);
        vec2 d = vec2(cos(a), sin(a));
        float k = 6.2831853 / WAVE[i].y;
        float w = sqrt(G * k);
        float ph = k * dot(d, p) - w * time + WAVE[i].w;
        float fade = 1.0 - smoothstep(WAVE[i].y * 8.0, WAVE[i].y * 25.0, dist);
        slope += d * (WAVE[i].z * k * cos(ph) * fade);
        if (i < SWELLS) swell += WAVE[i].z * sin(ph);
    }
}

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 P = FragPos;
    vec3 toEye = cameraPos - P;
    float dist = length(toEye);
    vec3 V = toEye / dist;
    vec3 L = normalize(lightDir);

    vec2 slope;
    float swell;
    waves(P.xz, dist, slope, swell);
    vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));

    // --- Depth below this point of the surface --------------------------------
    float depth = max(P.y - terrainHeight(P.xz), 0.0);

    // --- What is under the surface (the body) ---------------------------------
    float absorb = 1.0 - exp(-depth * 0.28);
    vec3 body = mix(shallowColor, deepColor, absorb);
    body *= skyAmbient * 0.9 + lightColor * (0.12 * max(dot(N, L), 0.0));
    // Light shining through a crest towards the eye.
    float crest = smoothstep(-0.2, 0.7, swell);
    float through = pow(max(dot(V, -normalize(L + N * 0.35)), 0.0), 4.0);
    body += vec3(0.05, 0.42, 0.34) * lightColor * (0.07 * through * crest);
    // Shallows are see-through; the deep is opaque.
    float bodyAlpha = mix(0.12, 1.0, 1.0 - exp(-depth * 0.75));

    // --- What is reflected -----------------------------------------------------
    float NV = max(dot(N, V), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - NV, 5.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    float sunAmt = max(dot(R, L), 0.0);
    vec3 sky = mix(fogColor.rgb, skyColor, pow(R.y, 0.42));
    sky += lightColor * vec3(1.0, 0.62, 0.32) * (pow(sunAmt, 5.0) * 0.045 + pow(sunAmt, 40.0) * 0.12);

    // The sun's glint: a tight lobe that the ripples break into sparkles, and a
    // wide one for the glow around it.
    vec3 H = normalize(L + V);
    float NH = max(dot(N, H), 0.0);
    float horizon = smoothstep(0.0, 0.15, L.y);
    vec3 glint = lightColor * horizon * (pow(NH, 1200.0) * 6.0 + pow(NH, 90.0) * 0.12);

    // --- Foam where the water meets the shore -----------------------------------
    float jitter = vnoise(P.xz * 0.6) * 0.5 + vnoise(P.xz * 2.3 + time * 0.2) * 0.5;
    float lap = 0.5 + 0.5 * sin(depth * 9.0 - time * 1.6 + jitter * 6.0);
    float edge = 1.0 - smoothstep(0.02, 0.45 + 0.35 * jitter, depth);
    float foam = clamp(edge * (0.55 + 0.6 * lap), 0.0, 1.0);
    vec3 foamColor = (skyAmbient * 0.9 + lightColor * 0.35 * max(L.y, 0.0)) * vec3(0.95, 0.97, 1.0);

    // --- Combine. The result is blended over the scene with `alpha`, so each ---
    // colour term is weighted by how much of the final pixel it is.
    float seeThrough = bodyAlpha * (1.0 - fresnel);
    float alpha = seeThrough + fresnel;
    vec3 color = body * seeThrough + sky * fresnel + glint;
    alpha = clamp(alpha + dot(glint, vec3(0.33)), 0.0, 1.0);
    color = color / max(alpha, 1e-3);
    color = mix(color, foamColor, foam);
    alpha = max(alpha, foam * 0.92);

    // --- Fog, then the same tone map as everything else -------------------------
    vec3 v = P - cameraPos;
    float f = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    float sunFog = pow(max(dot(v / dist, L), 0.0), 5.0);
    vec3 fc = mix(fogColor.rgb, lightColor * vec3(0.55, 0.42, 0.25), sunFog * 0.45);
    color = mix(color, fc, f);
    alpha = mix(alpha, 1.0, f);

    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), alpha);
}
