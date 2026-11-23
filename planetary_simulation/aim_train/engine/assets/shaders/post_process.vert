#version 330 core

// Full-screen triangle with no vertex data, like the sky's: gl_VertexID 0, 1, 2
// -> uv (0,0), (2,0), (0,2). The uv is the position in the scene texture, with
// v = 0 at the bottom.

out vec2 vUV;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
