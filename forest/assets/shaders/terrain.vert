#version 330 core

// Terrain chunk. The mesh's UV channel is reused to carry the lighting the
// game baked at start-up (the engine has no shadow pass):
//   aBake.x = sun visibility (terrain self-shadowing + tree canopies)
//   aBake.y = sky/ambient visibility (canopy + trunk + concavity occlusion)
// Texture coordinates are derived from world position in the fragment shader,
// so the mesh UVs are free for this.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aBake;

layout(location = 3) in mat4 instanceModel;
layout(location = 7) in vec4 instanceColor;

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

out vec3 FragPos;
out vec3 Normal;
out vec2 Bake;
out vec3 Tint;

void main()
{
    vec4 world = instanceModel * vec4(aPos, 1.0);
    FragPos = world.xyz;
    Normal = aNormal;
    Bake = aBake;
    Tint = instanceColor.rgb;
    gl_Position = viewProj * world;
}
