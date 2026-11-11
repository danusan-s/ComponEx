#version 330 core

// The sea. Only the long swells move the geometry: the grid is 10 m, so
// anything much shorter than ~40 m would alias. The shorter ripples live in the
// fragment shader as normals only (see water.frag). The first three waves here
// must match the first three there.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

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

const float G = 9.81;
const int SWELLS = 3;
//                         direction (deg), wavelength (m), amplitude (m), phase
const vec4 SWELL[SWELLS] = vec4[SWELLS](
    vec4(20.0, 90.0, 0.45, 0.0),
    vec4(75.0, 58.0, 0.25, 1.7),
    vec4(-35.0, 44.0, 0.14, 3.1));

// Deep-water sum of sines: h = sum A sin(k (D . p) - w t + phase), w = sqrt(g k).
float swellHeight(vec2 p) {
    float h = 0.0;
    for (int i = 0; i < SWELLS; ++i) {
        float a = radians(SWELL[i].x);
        float k = 6.2831853 / SWELL[i].y;
        float w = sqrt(G * k);
        h += SWELL[i].z * sin(k * dot(vec2(cos(a), sin(a)), p) - w * time + SWELL[i].w);
    }
    return h;
}

void main()
{
    vec4 world = instanceModel * vec4(aPos, 1.0);
    world.y += swellHeight(world.xz);
    FragPos = world.xyz;
    gl_Position = viewProj * world;
}
