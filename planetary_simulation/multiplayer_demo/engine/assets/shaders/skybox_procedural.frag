#version 330 core

in vec3 fragDir;

out vec4 FragColor;

// Per-frame values, from the shared uniform buffer. Must match
// renderer/frame_data.hpp exactly.
layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform vec3 skyColor;     // colour straight overhead
uniform vec3 horizonColor; // colour at the horizon

void main()
{
    vec3 dir = normalize(fragDir);

    // Gradient from horizon (dir.y = 0) up to the zenith; anything below the
    // horizon stays at the horizon colour so it meets the fogged ground.
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 color = mix(horizonColor, skyColor, pow(h, 0.6));

    // Sun disc plus a soft glow around it, from the scene's directional light.
    vec3 sun = normalize(lightDir);
    float s = max(dot(dir, sun), 0.0);
    color += lightColor * (pow(s, 1500.0) * 2.0 + pow(s, 24.0) * 0.25);

    FragColor = vec4(color, 1.0);
}
