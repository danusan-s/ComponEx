#version 330 core

// Full-screen triangle with no vertex data: three vertices cover the viewport
// (positions come from gl_VertexID), and each fragment is given the world
// direction it looks along, so the sky needs no mesh.

out vec3 fragDir;

uniform mat4 invSkyViewProj; // inverse(projection * rotation-only view)

void main()
{
    // gl_VertexID 0, 1, 2 -> (0,0), (2,0), (0,2) -> clip (-1,-1), (3,-1), (-1,3)
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
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
