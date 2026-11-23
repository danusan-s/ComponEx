#version 460
#include "frame_data.glsl"

// Full-screen triangle with no vertex data: three vertices cover the viewport
// (positions come from VERTEX_INDEX), and each fragment is given the world
// direction it looks along, so the sky needs no mesh.

layout(location = 0) out vec3 fragDir;

layout(std140, BIND(1, 0)) uniform Material {
    mat4 invSkyViewProj;
    vec3 skyColor;
    vec3 horizonColor;
};

void main()
{
    // VERTEX_INDEX 0, 1, 2 -> (0,0), (2,0), (0,2) -> clip (-1,-1), (3,-1), (-1,3)
    vec2 uv = vec2((VERTEX_INDEX << 1) & 2, VERTEX_INDEX & 2);
    vec2 ndc = uv * 2.0 - 1.0;

    // Unproject a point on the pixel's ray. The view has no translation, so
    // the result is already a direction from the camera. Any depth gives the
    // same direction; the middle of the range keeps the arithmetic well
    // conditioned (the far plane is 1e4 away).
    vec4 world = invSkyViewProj * vec4(ndc, 0.0, 1.0);
    fragDir = world.xyz / world.w;

    // z = w pins the sky to exactly the far plane (depth 1.0).
    gl_Position = vec4(ndc, 1.0, 1.0);
}
