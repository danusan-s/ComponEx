// The vertex inputs of an instanced mesh. Locations 0 to 2 come from the mesh;
// the rest are per-instance and written by the material's writeInstanceData().
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 3) in mat4 instanceModel; // per-instance model matrix
layout(location = 7) in vec4 instanceColor; // per-instance colour
layout(location = 8) in float instanceFade; // LOD cross-fade, 0 = none
