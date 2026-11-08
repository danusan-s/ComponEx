#version 330 core

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

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform float windAmp;
uniform float views;        // views around the tree
uniform float crownCenterY; // model space; the shading's crown ellipsoid
uniform float crownRadius;
uniform float crownHalfH;
uniform float crownBottom;
uniform float crownTop;

out vec3 FragPos;
out vec2 UV;
out vec2 CrownQ;           // position across the crown, -1..1 on both axes
out vec3 Right;
out vec3 Toward;           // horizontal, from the tree to the camera
out vec3 Tint;
out float Vis;
out float HeightT;
flat out vec2 Frames;      // the two views, as indices
flat out float Blend;      // weight of the second

const float TAU = 6.28318530718;

vec3 sway(vec3 base, float h) {
    float k = h * 0.1;
    k *= k;
    float ph = dot(base.xz, vec2(0.0683, 0.0415));
    float gust = 0.6 * sin(time * 0.85 + ph * 6.0) + 0.4 * sin(time * 1.9 + ph * 11.0 + 1.3) +
                 0.25 * sin(time * 0.37 + ph * 2.0);
    return vec3(0.85, 0.0, 0.52) * gust * 0.32 * k * windAmp;
}

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
