#version 460
#include "frame_data.glsl"

// The sea. The surface normal is built from three layers:
//   - the three long swells, which also move the geometry (water.vert);
//   - five shorter waves with peaked crests and flat troughs (exp(sin - 1)),
//     each sampled at a position dragged by the ones before it, which sharpens
//     the crests and breaks up the regular stripes a plain sum of sines makes;
//   - noise ripples, the fine texture of the surface.
// A layer fades out once its wavelength is under a few pixels, and the slope it
// takes with it is handed to the specular lobe as roughness, so far water
// settles into a broad sun path instead of shimmering.
//
// Shading:
//   - body colour from the water's depth (read from the terrain's height map:
//     the engine has no depth read-back), turquoise in the shallows, dark blue
//     in the deep, and see-through where it is shallow;
//   - Fresnel: grazing views reflect the sky (the same sky as sky.frag, clouds
//     and all), looking down sees into the water;
//   - the sun's glint, a GGX lobe, and a faint glow where a crest is lit from
//     behind;
//   - foam: lacy and broken where the water meets the shore, a little on the
//     steepest crests out at sea.
// Drawn blended, after the terrain and the sky.

layout(location = 0) in vec3 FragPos;

layout(location = 0) out vec4 FragColor;

layout(BIND(1, 1)) uniform sampler2D heightMap;

layout(std140, BIND(1, 0)) uniform Material {
    float worldHalf;
    float heightCells;
    float waterLevel;
    vec3 skyColor;
    vec3 skyAmbient;
    vec3 shallowColor;
    vec3 deepColor;
    vec3 airHorizon;
    vec3 airSun;
};

const float PI = 3.14159265;
const float G = 9.81;

const int SWELLS = 3;  // these move the geometry too: keep equal to water.vert
//                          direction (deg), wavelength (m), amplitude (m), phase
const vec4 SWELL[SWELLS] = vec4[SWELLS](
    vec4(20.0, 90.0, 0.45, 0.0),
    vec4(75.0, 58.0, 0.25, 1.7),
    vec4(-35.0, 44.0, 0.14, 3.1));

const int WAVES = 5;
const vec4 WAVE[WAVES] = vec4[WAVES](
    vec4(110.0, 27.0, 0.30, 0.9),
    vec4(-70.0, 16.0, 0.17, 2.4),
    vec4(160.0, 9.5, 0.12, 4.0),
    vec4(40.0, 5.5, 0.07, 5.2),
    vec4(-130.0, 3.2, 0.04, 0.4));

// Bilinear, not the terrain mesh's own triangle split: the foam is driven by
// this depth, and the split shows as creases along the 1.25 m cells.
float terrainHeight(vec2 xz) {
    vec2 g = (xz + worldHalf) * (heightCells / (2.0 * worldHalf));
    g = clamp(g, vec2(0.0), vec2(heightCells - 1e-3));
    ivec2 i = ivec2(g);
    vec2 t = g - vec2(i);
    float h00 = texelFetch(heightMap, i, 0).r;
    float h10 = texelFetch(heightMap, i + ivec2(1, 0), 0).r;
    float h01 = texelFetch(heightMap, i + ivec2(0, 1), 0).r;
    float h11 = texelFetch(heightMap, i + ivec2(1, 1), 0).r;
    return mix(mix(h00, h10, t.x), mix(h01, h11, t.x), t.y);
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

float fbm(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; ++i) {
        s += a * vnoise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        a *= 0.5;
    }
    return s;
}

// Value noise and its derivative: (value, d/dx, d/dy). Quintic fade, so the
// derivative is continuous and the ripples have no visible grid.
vec3 noised(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float a = hash12(i), b = hash12(i + vec2(1, 0));
    float c = hash12(i + vec2(0, 1)), d = hash12(i + vec2(1, 1));
    float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y,
                du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

// The sky as sky.frag draws it (minus the sun's disc, which the glint covers),
// for the reflection.
vec3 skyRadiance(vec3 dir, vec3 horizon, vec3 sunCol) {
    vec3 L = normalize(lightDir);
    float h = max(dir.y, 0.0);
    float s = max(dot(dir, L), 0.0);
    vec3 col = mix(horizon, skyColor, pow(h, 0.42));
    col += sunCol * vec3(1.0, 0.62, 0.32) * (pow(s, 5.0) * 0.045 + pow(s, 40.0) * 0.12);
    if (dir.y > 0.01) {
        vec2 p = dir.xz / (dir.y + 0.12) * 0.75 + vec2(time * 0.01, time * 0.004);
        float c = smoothstep(0.50, 0.82, fbm(p * 1.4));
        float fade = smoothstep(0.01, 0.22, dir.y);
        vec3 cloud = mix(vec3(0.75, 0.80, 0.90), sunCol * vec3(1.0, 0.92, 0.80) * 0.5, pow(s, 3.0) + 0.25);
        col = mix(col, cloud, c * fade * 0.8);
    }
    return col;
}

// The foam's texture: a few octaves of smooth noise, each turned against the
// last and the whole field warped, so nothing lines up with a grid. Octaves
// finer than a pixel give way to their mean (0.5) instead of crawling.
float foamField(vec2 p, float fp) {
    const mat2 R = mat2(0.80, -0.60, 0.60, 0.80);
    p += noised(p * 0.23 + vec2(3.1, time * 0.015)).yz * 2.5;
    vec2 drift = vec2(0.06, -0.04) * time;
    float f = 0.9, a = 0.5, sum = 0.0, wsum = 0.0;
    for (int i = 0; i < 4; ++i) {
        float fade = 1.0 - smoothstep(0.15, 0.5, fp * f);
        sum += a * mix(0.5, noised(p * f + drift).x, fade);
        wsum += a;
        p = R * p;
        drift = R * drift * -2.0;
        f *= 2.1;
        a *= 0.55;
    }
    return 0.5 + (sum / wsum - 0.5) * 1.7;
}

// Fine ripples: noise octaves scrolling in different directions. Adds slope to
// `slope` and the slope variance it dropped to `lost`. fp is the footprint of a
// pixel on the water, in metres.
void ripples(vec2 p, float fp, inout vec2 slope, inout float lost) {
    const mat2 R = mat2(0.80, -0.60, 0.60, 0.80);
    float freq = 0.8;
    float amp = 0.055;
    vec2 q = p;
    vec2 drift = vec2(0.30, 0.18) * time;
    for (int i = 0; i < 5; ++i) {
        vec3 n = noised(q * freq + drift);
        float fade = 1.0 - smoothstep(0.10, 0.30, fp * freq);
        float s = amp * freq;
        slope += n.yz * (s * fade);
        lost += 0.35 * s * s * (1.0 - fade);
        // Next octave: finer, turned, and carried off in another direction.
        q = R * q;
        drift = R * drift * -1.3;
        freq *= 2.1;
        amp *= 0.46;
    }
}

// Surface slope (d h / dx, d h / dz), the height of the swells alone, how close
// to a crest this is (0..1), and the slope variance that was too fine to show.
void surface(vec2 p, float fp, out vec2 slope, out float swell, out float crest, out float lost) {
    slope = vec2(0.0);
    swell = 0.0;
    crest = 0.0;
    lost = 0.0;

    for (int i = 0; i < SWELLS; ++i) {
        float a = radians(SWELL[i].x);
        vec2 d = vec2(cos(a), sin(a));
        float k = 6.2831853 / SWELL[i].y;
        float w = sqrt(G * k);
        float ph = k * dot(d, p) - w * time + SWELL[i].w;
        slope += d * (SWELL[i].z * k * cos(ph));
        swell += SWELL[i].z * sin(ph);
    }

    vec2 q = p;
    float ampSum = 0.0;
    for (int i = 0; i < WAVES; ++i) {
        float a = radians(WAVE[i].x);
        vec2 d = vec2(cos(a), sin(a));
        float k = 6.2831853 / WAVE[i].y;
        float w = sqrt(G * k);
        float ph = k * dot(d, q) - w * time + WAVE[i].w;
        float e = exp(sin(ph) - 1.0);        // 1 on the crest, ~0.14 in the trough
        float fade = 1.0 - smoothstep(0.10, 0.30, fp / WAVE[i].y);
        float s = WAVE[i].z * k;
        slope += d * (s * cos(ph) * e * fade);
        lost += 0.5 * s * s * 0.3 * (1.0 - fade);
        crest += e * WAVE[i].z * fade;
        ampSum += WAVE[i].z;
        // The next wave is sampled where this one has pushed the water.
        q -= d * (cos(ph) * e * WAVE[i].y * 0.05);
    }
    crest /= ampSum;

    ripples(p, fp, slope, lost);
}

// The swells' height alone, as water.vert has it.
float swellHeight(vec2 p) {
    float h = 0.0;
    for (int i = 0; i < SWELLS; ++i) {
        float a = radians(SWELL[i].x);
        float k = 6.2831853 / SWELL[i].y;
        h += SWELL[i].z * sin(k * dot(vec2(cos(a), sin(a)), p) - sqrt(G * k) * time + SWELL[i].w);
    }
    return h;
}

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// GGX highlight with Smith visibility; alpha is the lobe's width.
float specularGGX(float NH, float NL, float NV, float alpha) {
    float a2 = alpha * alpha;
    float d = NH * NH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float vis = 0.5 / (NL * sqrt(NV * NV * (1.0 - a2) + a2) + NV * sqrt(NL * NL * (1.0 - a2) + a2) + 1e-4);
    return D * vis;
}

void main()
{
    vec3 P = FragPos;
    vec3 toEye = cameraPos - P;
    float dist = length(toEye);
    vec3 V = toEye / dist;
    vec3 L = normalize(lightDir);

    float fp = max(length(dFdx(P.xz)), length(dFdy(P.xz)));

    vec2 slope;
    float swell, crest, lost;
    surface(P.xz, fp, slope, swell, crest, lost);
    // Is the eye under the water? (Not whether this crest is above it: out at
    // the horizon every wave top is level with the eye.)
    bool below = cameraPos.y < waterLevel + swellHeight(cameraPos.xz);
    vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));
    // The same without the ripples' fine detail, for what should not sparkle.
    vec3 Nsoft = normalize(vec3(-slope.x * 0.35, 1.0, -slope.y * 0.35));
    if (below) { N = -N; Nsoft = -Nsoft; }

    // --- Depth below this point of the surface --------------------------------
    // The surface here is the still level plus the swells, evaluated exactly: the
    // interpolated vertex height would carry the mesh's cells into the foam.
    float depth = max(waterLevel + swell - terrainHeight(P.xz), 0.0);

    // --- What is under the surface (the body) ---------------------------------
    float absorb = 1.0 - exp(-depth * 0.28);
    vec3 body = mix(shallowColor, deepColor, absorb);
    body *= skyAmbient * 0.9 + lightColor * (0.12 * max(dot(Nsoft, L), 0.0));
    // Light shining through a crest towards the eye.
    float lit = smoothstep(-0.2, 0.7, swell + crest * 0.25);
    float through = pow(max(dot(V, -normalize(L + Nsoft * 0.35)), 0.0), 4.0);
    body += vec3(0.05, 0.42, 0.34) * lightColor * (0.12 * through * lit);
    // Shallows are see-through; the deep is opaque.
    float bodyAlpha = mix(0.10, 1.0, 1.0 - exp(-depth * 0.75));

    // --- What is reflected -----------------------------------------------------
    float NV = max(dot(N, V), 1e-3);
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(Nsoft, V), 0.0), 5.0);
    // Blur the sky a little where the surface is rough, and never reflect the
    // ground: a ray that would go below the horizon is mirrored back up.
    vec3 R = reflect(-V, normalize(mix(N, Nsoft, 0.5)));
    R.y = abs(R.y);
    vec3 sky = skyRadiance(normalize(R), fogColor.rgb, lightColor);

    // The sun's glint: GGX, as narrow as the ripples are sharp and widened by
    // the detail too fine to resolve.
    vec3 H = normalize(L + V);
    float NL = max(dot(N, L), 0.0);
    float lobe = clamp(sqrt(0.0016 + 2.0 * lost), 0.04, 0.7);
    float VH = max(dot(V, H), 0.0);
    float Fh = 0.02 + 0.98 * pow(1.0 - VH, 5.0);
    float horizon = smoothstep(0.0, 0.15, L.y);
    vec3 glint = vec3(0.0);
    if (!below)
        glint = lightColor * horizon * min(specularGGX(max(dot(N, H), 0.0), NL, NV, lobe) * Fh * NL, 40.0);

    // --- Foam -------------------------------------------------------------------
    float foamNoise = foamField(P.xz, fp);
    float n1 = foamNoise;
    float n2 = foamField(P.xz * 2.7 + 11.0, fp * 2.7);   // a second, unrelated pattern
    float n3 = foamField(P.xz * 6.1 - 5.0, fp * 6.1);    // for the bubbles
    // Shore: the wash reaches further out and falls back over a few seconds, and
    // thins into lace as it goes. `depth` already follows the swells.
    float reach = 0.9 + 0.5 * sin(time * 0.8 + n1 * 5.0);
    float k = 1.0 - clamp(depth / reach, 0.0, 1.0);
    float shore = smoothstep(0.50, 0.60, foamNoise + k * k * 0.95 - 0.38);
    shore *= smoothstep(0.0, 0.03, depth);          // wet sand, then foam
    shore *= 0.55 + 0.45 * n3;                       // bubbly
    // A hard thin line right at the waterline.
    shore = max(shore, (1.0 - smoothstep(0.03, 0.09 + 0.05 * n2, depth)) * smoothstep(0.0, 0.015, depth) * 0.8);
    // Whitecaps on the steepest crests, nearby only.
    float caps = smoothstep(0.66, 0.98, crest) * smoothstep(0.50, 0.80, foamNoise) *
                 (1.0 - smoothstep(40.0, 160.0, dist)) * 0.20;
    float foam = clamp(max(shore, caps), 0.0, 1.0);
    if (below) foam = 0.0;
    vec3 foamColor = (skyAmbient * 0.9 + lightColor * 0.35 * max(L.y, 0.0)) * vec3(0.95, 0.97, 1.0);

    // --- Combine. The result is blended over the scene with `alpha`, so each ---
    // colour term is weighted by how much of the final pixel it is.
    if (below) fresnel = 0.0;
    float seeThrough = bodyAlpha * (1.0 - fresnel);
    float alpha = seeThrough + fresnel;
    vec3 color = body * seeThrough + sky * fresnel + glint;
    alpha = clamp(alpha + dot(glint, vec3(0.33)), 0.0, 1.0);
    color = color / max(alpha, 1e-3);
    color = mix(color, foamColor, foam);
    alpha = max(alpha, foam * 0.92);

    // --- From below: Snell's window ---------------------------------------------
    // Looking up through the surface you see the whole sky squeezed into a cone
    // about 48 degrees across, bent and broken by the ripples; past its edge the
    // surface is a mirror, and what it mirrors is the murk. The surface is
    // opaque from here, so it hides what is above it.
    if (below) {
        const float eta = 1.33;
        vec3 I = -V;
        vec3 T = refract(I, N, eta);          // N faces down, into the water
        float cosi = clamp(dot(-I, N), 0.0, 1.0);
        float sint = eta * sqrt(max(1.0 - cosi * cosi, 0.0));
        float F = 1.0;                        // total internal reflection
        if (sint < 1.0) {
            float cost = sqrt(1.0 - sint * sint);
            float rs = (eta * cosi - cost) / (eta * cosi + cost);
            float rp = (eta * cost - cosi) / (eta * cost + cosi);
            F = 0.5 * (rs * rs + rp * rp);
        }
        vec3 win = vec3(0.0);
        if (dot(T, T) > 0.0) {
            T = normalize(T);
            win = skyRadiance(T, airHorizon, airSun);
            float sd = max(dot(T, L), 0.0);
            win += airSun * (pow(sd, 500.0) * 6.0 + pow(sd, 40.0) * 0.12);
        }
        // The water takes the red first, over the way up to the surface.
        win *= exp(-dist * vec3(0.30, 0.085, 0.045)) * 0.6;
        vec3 murk = fogColor.rgb * 0.75;
        color = mix(win, murk, F);
        alpha = 1.0;
    }

    // --- Fog, then the same tone map as everything else -------------------------
    vec3 v = P - cameraPos;
    float f = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    float sunFog = pow(max(dot(v / dist, L), 0.0), 5.0);
    vec3 fc = mix(fogColor.rgb, lightColor * vec3(0.55, 0.42, 0.25), sunFog * 0.45);
    color = mix(color, fc, f);
    alpha = mix(alpha, 1.0, f);

    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), alpha);
}
