#version 330 core

// One grass tuft = a small mesh of tapered blades (built in C++), drawn once per
// instance by a ProceduralInstancesComponent entity: there is no per-instance
// data at all. Everything about a tuft is derived here from gl_InstanceID.
//
//  - Position: either a fixed chunk of the world (chunkSize > 0: a jittered grid
//    inside the chunk's own square; the engine culls the chunks that are out of
//    view and GrassChunkSystem the ones out of range), or, for the original
//    grass, a jittered grid of `period`-metre squares wrapped so the copy nearest
//    the camera is used (walking moves the pool with you).
//  - Height: the terrain's height map (R32F), interpolated with the same
//    triangle split as the terrain mesh, so tufts sit exactly on the surface.
//  - Whether it exists: a baked mask (R = grass density, G = sun, B = sky), at
//    0.625 m resolution, with rocks, trunks and steep ground already cut out.
//    A tuft whose density is below its own random threshold is not drawn.
//
// A tuft that is not drawn has its vertices collapsed outside the clip volume.
// The GPU still runs the vertex shader for them (nothing can compact the
// instance list without compute/indirect draws), so the rejection tests come
// first and are cheap: distance, world edge, then whether the tuft is inside
// the camera's view frustum at all (about 70% of the pool is behind or beside
// the camera at any moment), and only then the density mask and the rest.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV; // x = blade index + 0.9 * (0..1 across the blade), y = 0 root .. 1 tip

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform sampler2D heightMap; // R32F, (cells + 1)^2, row = z
uniform sampler2D grassMask; // RGBA8 over the whole world

uniform float worldHalf;  // metres from the origin to a world edge
uniform float heightCells; // quads per edge of the height map
uniform float tuftSide;   // tufts per side of the pool: side * side instances
uniform float period;     // metres covered by the repeating pool
uniform float chunkSize;  // 0: a wrapped pool (above). Otherwise this draw is one fixed chunk of the
uniform vec3 chunkInfo;   // world, chunkSize metres square: xy = its corner's world xz, z = seed offset
uniform float fadeInStart; // tufts nearer than this are not drawn by this pool, and
uniform float fadeInEnd;   // grow from nothing to full size by this (equal: no growing)
uniform float fadeStart;
uniform float fadeEnd;
uniform vec3 debugTint;   // (1, 1, 1) normally; FOREST_GRASS_DEBUG colours each pool
uniform float debugBands; // 1: colour by distance band (FOREST_GRASS_DEBUG=2): green < 24 m,
                          // yellow < 37, orange < 58, red < 93, purple beyond
uniform float bladeLod;    // 1: blades drop out with distance, the rest grow to make up

out vec3 FragPos;
out vec3 BladeNormal;
out vec3 Tint;
out vec2 Light;
out float BladeRand;
out float Along; // 0 root .. 1 tip

// --- integer hash: every per-tuft random comes from the instance id ---------
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
// k-th random in [0, 1) for instance `id` (up to 16 per instance).
float rnd(uint id, uint k) {
    return float(pcg(id * 16u + k) & 0xFFFFFFu) / 16777216.0;
}

// A random for blade `blade` of instance `id`.
float rndBlade(uint id, uint blade) {
    return float(pcg(pcg(id) + blade * 2654435761u) & 0xFFFFFFu) / 16777216.0;
}

float terrainHeight(vec2 xz) {
    vec2 g = (xz + worldHalf) * (heightCells / (2.0 * worldHalf));
    g = clamp(g, vec2(0.0), vec2(heightCells - 1e-3));
    ivec2 i = ivec2(g);
    vec2 t = g - vec2(i);
    float h00 = texelFetch(heightMap, i, 0).r;
    float h10 = texelFetch(heightMap, i + ivec2(1, 0), 0).r;
    float h01 = texelFetch(heightMap, i + ivec2(0, 1), 0).r;
    float h11 = texelFetch(heightMap, i + ivec2(1, 1), 0).r;
    // The terrain mesh splits each quad along the (1,0)-(0,1) diagonal.
    if (t.x + t.y <= 1.0) return h00 + t.x * (h10 - h00) + t.y * (h01 - h00);
    return h11 + (1.0 - t.x) * (h01 - h11) + (1.0 - t.y) * (h10 - h11);
}

// Is a sphere around `center` inside (or touching) the view frustum's four side
// planes and near plane? The planes come straight out of viewProj's rows, so
// this needs no extra uniforms. Conservative: the radius covers a whole tuft,
// blades, lean and wind sway included, so nothing pops at the screen edge.
bool inView(vec3 center, float radius) {
    vec4 p = vec4(center, 1.0);
    vec4 r0 = vec4(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
    vec4 r1 = vec4(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
    vec4 r2 = vec4(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    vec4 r3 = vec4(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);
    vec4 left = r3 + r0, right = r3 - r0, bottom = r3 + r1, top = r3 - r1, near = r3 + r2;
    return dot(left, p) >= -radius * length(left.xyz) &&
           dot(right, p) >= -radius * length(right.xyz) &&
           dot(bottom, p) >= -radius * length(bottom.xyz) &&
           dot(top, p) >= -radius * length(top.xyz) &&
           dot(near, p) >= -radius * length(near.xyz);
}

void cull() {
    FragPos = vec3(0.0);
    BladeNormal = vec3(0.0, 1.0, 0.0);
    Tint = vec3(1.0);
    Light = vec2(1.0);
    BladeRand = 0.0;
    Along = 0.0;
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0); // outside the clip volume: dropped
}

void main()
{
    uint localId = uint(gl_InstanceID);
    uint id = localId;  // what every random below comes from
    float side = tuftSide;
    vec2 cell = vec2(float(localId % uint(side)), float(localId / uint(side)));

    vec2 xz;
    if (chunkSize > 0.5) {
        // A chunk of the world: a jittered grid inside its own square, where it
        // always is (the engine culls whole chunks that are out of view).
        id = localId + uint(chunkInfo.z);
        xz = chunkInfo.xy + (cell + vec2(rnd(id, 0u), rnd(id, 1u))) * (chunkSize / side);
    } else {
        // Jittered grid inside one period, then the copy nearest the camera.
        float cellSize = period / side;
        vec2 base = (cell + vec2(rnd(id, 0u), rnd(id, 1u))) * cellSize;
        // The copy of `base` nearest the camera, as base + a whole number of
        // periods. Computed from `base` rather than as camera + (base - camera) so
        // that, while the same copy stays nearest, xz is bit-identical every frame:
        // the camera's own rounding error must not leak into the tuft's position.
        xz = base + period * floor((cameraPos.xz - base) / period + 0.5);
    }
    vec2 d = xz - cameraPos.xz;

    // Cheapest rejections first: too far, or off the edge of the world.
    float dist2 = dot(d, d);
    if (dist2 > fadeEnd * fadeEnd || dist2 < fadeInStart * fadeInStart ||
        any(greaterThan(abs(xz), vec2(worldHalf - 6.0)))) {
        cull();
        return;
    }

    // Geometry LOD: far tufts keep fewer of their blades. Dropping the others
    // here, before anything else is worked out, makes their vertices nearly free
    // (the blades are scattered through the tuft, so any prefix is a thinner tuft).
    float bladeId = floor(aUV.x);
    float keepBlades = 12.0;
    if (bladeLod > 0.5) {
        keepBlades = mix(12.0, 3.0, smoothstep(22.0, 56.0, sqrt(dist2)));
        if (bladeId + 0.5 > keepBlades) {
            cull();
            return;
        }
    }

    // Frustum: most of the pool is behind or beside the camera. The terrain
    // height is needed for a true position (the side planes tilt with pitch),
    // and is needed afterwards anyway for every tuft that stays.
    vec3 root = vec3(xz.x, terrainHeight(xz) - 0.03, xz.y);
    if (!inView(root + vec3(0.0, 0.8, 0.0), 2.2)) {
        cull();
        return;
    }

    // Density: the mask already has rocks, trunks and steep ground removed.
    vec3 mask = texture(grassMask, (xz + worldHalf) / (2.0 * worldHalf)).rgb;
    if (mask.r <= rnd(id, 2u)) {
        cull();
        return;
    }

    float baseScale = 0.9 + 0.65 * rnd(id, 3u) * rnd(id, 4u);
    float wind = 0.3 + 0.7 * rnd(id, 5u);
    float v = 0.82 + 0.3 * rnd(id, 6u);
    vec3 tint = vec3(v * (0.9 + 0.2 * rnd(id, 7u)), v, v * (0.85 + 0.2 * rnd(id, 8u)));

    float scale = baseScale * (0.7 + 0.3 * mask.r);

    float t = aUV.y;

    float dist = sqrt(dist2);
    float size = scale * (1.0 - smoothstep(fadeStart, fadeEnd, dist));
    if (fadeInEnd > fadeInStart) size *= smoothstep(fadeInStart, fadeInEnd, dist);
    // Fewer blades, a little bigger, keeps the tuft's mass.
    size *= pow(12.0 / keepBlades, 0.3);

    // Every tuft is turned randomly so the mesh's blade pattern never repeats.
    float yaw = rnd(id, 9u) * 6.2831853;
    float cy = cos(yaw), sy = sin(yaw);
    mat2 rot = mat2(cy, sy, -sy, cy);

    vec3 local = aPos;
    local.xz = rot * local.xz;
    local *= size;

    // Wind: a travelling gust plus faster per-blade flutter; the root stays put.
    float ph = dot(root.xz, vec2(0.11, 0.07));
    float gust = 0.55 * sin(time * 1.5 + ph * 4.0) + 0.35 * sin(time * 2.7 + ph * 9.0 + 2.0) +
                 0.3 * sin(time * 0.6 + ph * 1.5);
    float flutter = sin(time * 5.0 + bladeId * 6.7 + root.x * 1.3);
    float bend = t * t * size;
    local.xz += vec2(0.85, 0.52) * gust * (0.12 + 0.28 * wind) * bend;
    local.xz += vec2(0.52, -0.85) * flutter * 0.04 * wind * bend;
    local.y -= abs(gust) * 0.05 * bend; // a bent blade is a little shorter

    vec3 world = root + local;
    FragPos = world;

    vec2 nxz = rot * aNormal.xz;
    BladeNormal = vec3(nxz.x, aNormal.y, nxz.y);
    Tint = tint * debugTint;
    if (debugBands > 0.5) {
        Tint = dist < 24.0 ? vec3(0.4, 1.0, 0.4) : dist < 37.0 ? vec3(1.0, 1.0, 0.2)
             : dist < 58.0 ? vec3(1.0, 0.5, 0.1) : dist < 93.0 ? vec3(1.0, 0.2, 0.2) : vec3(0.5, 0.2, 1.0);
    }
    Light = mask.gb;
    BladeRand = rndBlade(id, uint(bladeId));
    Along = t;
    gl_Position = viewProj * vec4(world, 1.0);
}
