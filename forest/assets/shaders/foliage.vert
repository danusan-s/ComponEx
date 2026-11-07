#version 330 core

// Alpha-cut leaf / twig cards (trees, ferns, shrubs). Besides the shared wind
// sway it adds per-card flutter, and hands the fragment shader a "crown
// normal" (pointing out from the middle of the crown) and how deep inside the
// crown the vertex is: foliage cards are flat and chaotic, so shading them with
// their own normals looks noisy, while shading the crown as a soft volume does
// not.

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
uniform vec3 crownCenter;   // model space
uniform float crownRadius;  // model space
uniform float crownBottom;  // model-space y range of the foliage
uniform float crownTop;

out vec3 FragPos;
out vec3 Normal;
out vec3 CrownNormal;
out vec2 UV;
out vec3 Tint;
out float Vis;
out float CrownT;   // 0 at the crown's centre, 1 at its surface
out float HeightT;  // 0 at the lowest foliage, 1 at the top

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
    float s = length(instanceModel[1].xyz);
    float h = aPos.y * s;
    vec3 world = (instanceModel * vec4(aPos, 1.0)).xyz;
    vec3 nrm = normalize(mat3(instanceModel) * aNormal);

    world += sway(instanceModel[3].xyz, h);
    float flutter = sin(time * 4.6 + dot(world, vec3(1.3, 2.1, 1.7)) * 1.9) *
                    sin(time * 2.3 + world.x * 0.9);
    world += nrm * flutter * 0.03 * min(h * 0.2, 1.0) * windAmp;

    vec3 d = aPos - crownCenter;
    CrownT = length(d) / crownRadius;
    d.y *= 0.55;
    CrownNormal = normalize(mat3(instanceModel) * d);

    FragPos = world;
    Normal = nrm;
    UV = aUV;
    Tint = instanceColor.rgb;
    Vis = instanceColor.a;
    HeightT = clamp((aPos.y - crownBottom) / max(crownTop - crownBottom, 0.001), 0.0, 1.0);
    gl_Position = viewProj * vec4(world, 1.0);
}
