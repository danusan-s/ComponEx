#version 330 core

// Distant-tree billboards (see billboard.vert). The atlas holds flat albedo, so
// the lighting is the foliage shader's, applied to a normal the quad does not
// have: each pixel is treated as a point on the crown's ellipsoid, facing out
// from its middle, so the tree is lit as a soft volume like the 3D ones.

in vec3 FragPos;
in vec2 UV;
in vec2 CrownQ;
in vec3 Right;
in vec3 Toward;
in vec3 Tint;
in float Vis;
in float HeightT;
flat in vec2 Frames;
flat in float Blend;

out vec4 FragColor;

uniform sampler2D albedoMap; // RGBA: the views side by side, sRGB colour + coverage
uniform float atlasCols;
uniform float atlasRows;

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform vec3 skyAmbient;
uniform vec3 groundAmbient;

vec3 aces(vec3 x) {
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 applyFog(vec3 col, vec3 P) {
    vec3 v = P - cameraPos;
    float d = length(v);
    float f = 1.0 - exp(-pow(d * fogDensity, 2.0));
    float sunAmt = pow(max(dot(v / d, lightDir), 0.0), 5.0);
    vec3 fc = mix(fogColor.rgb, lightColor * vec3(0.55, 0.42, 0.25), sunAmt * 0.45);
    return mix(col, fc, f);
}

vec4 view(float index) {
    float row = floor(index / atlasCols);
    float col = index - row * atlasCols;
    return texture(albedoMap, (vec2(col, row) + UV) / vec2(atlasCols, atlasRows));
}

void main()
{
    vec4 a = view(Frames.x);
    vec4 b = view(Frames.y);
    float cover = mix(a.a, b.a, Blend);

    // Mip-mapping averages coverage with the empty texels, so a far tree would
    // thin out; lower the cut-off as the quad minifies to keep its mass.
    vec2 size = vec2(textureSize(albedoMap, 0)) / vec2(atlasCols, atlasRows);
    vec2 dx = dFdx(UV * size), dy = dFdy(UV * size);
    float lod = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));
    float cut = mix(0.5, 0.22, clamp(lod / 3.0, 0.0, 1.0));
    if (cover < cut) discard;

    vec3 albedo = mix(a.rgb, b.rgb, Blend) * Tint;

    // A point on the crown's ellipsoid, seen from the camera: x across, y up,
    // z towards the viewer; flattened vertically like the foliage's crown normal.
    vec2 q = clamp(CrownQ, -1.0, 1.0);
    float z = sqrt(max(1.0 - dot(q, q), 0.04));
    vec3 n = normalize(vec3(q.x, q.y * 0.55, z));
    vec3 Ns = normalize(Right * n.x + vec3(0.0, n.y, 0.0) + Toward * n.z);

    vec3 L = normalize(lightDir);
    vec3 V = normalize(cameraPos - FragPos);

    float wrap = clamp((dot(Ns, L) + 0.4) / 1.4, 0.0, 1.0);
    // What is visible of a crown is mostly its lit outside, with the shaded
    // inside showing through gaps.
    float depth = 0.8;
    float vis = Vis * depth;

    float back = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * (1.0 - wrap * 0.5);
    vec3 direct = lightColor * vis * (albedo * wrap + albedo * vec3(1.0, 0.95, 0.6) * back * 0.9);

    float ao = 0.85 * mix(0.5, 1.0, HeightT);
    vec3 ambient = albedo * mix(groundAmbient, skyAmbient, Ns.y * 0.5 + 0.5) * ao;

    vec3 color = applyFog(direct + ambient, FragPos);
    FragColor = vec4(pow(aces(color), vec3(1.0 / 2.2)), 1.0);
}
