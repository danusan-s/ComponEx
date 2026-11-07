#version 330 core

// Bark, logs, stumps and rocks. Trees sway with the wind (windAmp > 0); the
// foliage shader applies the identical sway so leaves stay attached to the
// branches they were modelled on.

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

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;
out vec3 Tint;
out float Vis;
out float Height;

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
    world += sway(instanceModel[3].xyz, h);

    FragPos = world;
    Normal = normalize(mat3(instanceModel) * aNormal);
    UV = aUV;
    Tint = instanceColor.rgb;
    Vis = instanceColor.a;
    Height = h;
    gl_Position = viewProj * vec4(world, 1.0);
}
