#version 330 core

// Vertex attributes
layout(location = 0) in vec3 aPos;    // x, y, z
layout(location = 1) in vec3 aNormal; // nx, ny, nz
layout(location = 2) in vec2 aUV;     // u, v

layout(location = 3) in mat4 instanceModel; // Per-instance model matrix
layout(location = 7) in vec4 instanceColor; // Per-instance color
layout(location = 8) in float instanceFade;  // LOD cross-fade, 0 = none

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

out vec3 FragPos;
out vec4 ObjectColor;
out vec3 Normal;
out vec2 TexCoords;
flat out float LodFade;

// Both the depth pre-pass and the colour pass run this stage; invariance makes
// them compute exactly the same depth, which the "equal" depth test relies on.
invariant gl_Position;

void main()
{
    ObjectColor = instanceColor;
    LodFade = instanceFade;
    FragPos = vec3(instanceModel * vec4(aPos, 1.0f));
    Normal = mat3(transpose(inverse(instanceModel))) * aNormal;
    TexCoords = aUV;
    gl_Position = viewProj * instanceModel * vec4(aPos, 1.0f);
}

