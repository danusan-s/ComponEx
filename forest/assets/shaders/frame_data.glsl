// Per-frame values, set 0 binding 0, uploaded once per frame by the renderer.
// Must match renderer/frame_data.hpp exactly; every stage declares it the same
// way, which this include guarantees.
layout(std140, BIND(0, 0)) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};
