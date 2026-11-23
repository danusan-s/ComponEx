#version 460
#include "frame_data.glsl"

// Bark, logs, stumps and rocks. Trees sway with the wind (windAmp > 0); the
// foliage shader applies the identical sway so leaves stay attached to the
// branches they were modelled on.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 3) in mat4 instanceModel;
layout(location = 7) in vec4 instanceColor; // rgb tint, a = baked sun visibility
layout(location = 8) in float instanceFade;  // LOD cross-fade, 0 = none

layout(std140, BIND(1, 0)) uniform Material {
    float windAmp;
    float canopyShade;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 UV;
layout(location = 3) out vec3 Tint;
layout(location = 4) out float Vis;
layout(location = 5) flat out float LodFade;
layout(location = 6) out float Height;

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
    float s = length(instanceModel[1].xyz);
    float h = aPos.y * s;
    vec3 world = (instanceModel * vec4(aPos, 1.0)).xyz;
    world += sway(instanceModel[3].xyz, h);

    FragPos = world;
    Normal = normalize(mat3(instanceModel) * aNormal);
    UV = aUV;
    Tint = instanceColor.rgb;
    Vis = instanceColor.a;
    LodFade = instanceFade;
    Height = h;
    gl_Position = viewProj * vec4(world, 1.0);
}
