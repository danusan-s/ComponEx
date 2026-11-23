#version 460
#include "frame_data.glsl"

// Distant-tree billboards. One quad per tree, covering the whole tree, that
// stays upright and turns to face the camera. The quad shows a baked view of
// the tree (tools/bake_billboards.py): the atlas holds `views` renders around
// the tree, and this picks the two nearest the camera's direction (in the
// tree's own frame, so every tree's random yaw still shows the right side) and
// hands both to the fragment shader to cross-fade.
//
// The quad is modelled in the tree's model space (x across, y up, 0 at the
// trunk axis) with the exact size the bake covered, so the mesh bounds the
// engine culls with are the tree's. The sway is the foliage shader's.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 3) in mat4 instanceModel;
layout(location = 7) in vec4 instanceColor; // rgb tint, a = baked sun visibility

layout(std140, BIND(1, 0)) uniform Material {
    float windAmp;
    float views;
    float atlasCols;
    float atlasRows;
    float crownCenterY;
    float crownRadius;
    float crownHalfH;
    float crownBottom;
    float crownTop;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec2 UV;
layout(location = 2) out vec2 CrownQ;
layout(location = 3) out vec3 Right;
layout(location = 4) out vec3 Toward;
layout(location = 5) out vec3 Tint;
layout(location = 6) out float Vis;
layout(location = 7) out float HeightT;
layout(location = 8) flat out vec2 Frames;
layout(location = 10) flat out float Blend;

const float TAU = 6.28318530718;

vec3 sway(vec3 base, float h) {
    float k = h * 0.1;
    k *= k;
    float ph = dot(base.xz, vec2(0.0683, 0.0415));
    float gust = 0.6 * sin(time * 0.85 + ph * 6.0) + 0.4 * sin(time * 1.9 + ph * 11.0 + 1.3) +
                 0.25 * sin(time * 0.37 + ph * 2.0);
    return vec3(0.85, 0.0, 0.52) * gust * 0.32 * k * windAmp;
}

// The engine's depth pre-pass reruns this stage; invariance keeps its depth bit-identical
// to the colour pass, which tests for "equal".
invariant gl_Position;

void main()
{
    vec3 base = instanceModel[3].xyz;
    float s = length(instanceModel[1].xyz);

    vec3 toward = vec3(cameraPos.x - base.x, 0.0, cameraPos.z - base.z);
    toward /= max(length(toward), 1e-4);
    vec3 right = vec3(toward.z, 0.0, -toward.x);

    // Camera direction in the tree's frame: the inverse of a rotation (times
    // a uniform scale) is its transpose.
    mat3 rot = mat3(instanceModel) / s;
    vec3 local = transpose(rot) * toward;
    float t = fract(atan(local.x, local.z) / TAU) * views;
    float f0 = floor(t);
    Frames = vec2(f0, mod(f0 + 1.0, views));
    Blend = t - f0;

    vec3 world = base + (right * aPos.x + vec3(0.0, aPos.y, 0.0)) * s;
    world += sway(base, aPos.y * s);

    FragPos = world;
    UV = aUV;
    CrownQ = vec2(aPos.x / crownRadius, (aPos.y - crownCenterY) / crownHalfH);
    Right = right;
    Toward = toward;
    Tint = instanceColor.rgb;
    Vis = instanceColor.a;
    HeightT = clamp((aPos.y - crownBottom) / max(crownTop - crownBottom, 0.001), 0.0, 1.0);
    gl_Position = viewProj * vec4(world, 1.0);
}
