#version 460
#include "frame_data.glsl"

// Terrain chunk. The mesh's UV channel is reused to carry the lighting the
// game baked at start-up (the engine has no shadow pass):
//   aBake.x = sun visibility (terrain self-shadowing + tree canopies)
//   aBake.y = sky/ambient visibility (canopy + trunk + concavity occlusion)
// Texture coordinates are derived from world position in the fragment shader,
// so the mesh UVs are free for this.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 3) in mat4 instanceModel;
layout(location = 7) in vec4 instanceColor;

layout(std140, BIND(1, 0)) uniform Material {
    float farGrass;
    float waterLevel;
    vec3 skyAmbient;
    vec3 groundAmbient;
};

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 Bake;
layout(location = 3) out vec3 Tint;

// The engine's depth pre-pass reruns this stage; invariance keeps its depth bit-identical
// to the colour pass, which tests for "equal".
invariant gl_Position;

void main()
{
    vec4 world = instanceModel * vec4(aPos, 1.0);
    FragPos = world.xyz;
    Normal = aNormal;
    Bake = aUV;
    Tint = instanceColor.rgb;
    gl_Position = viewProj * world;
}
