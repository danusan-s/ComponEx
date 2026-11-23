#version 330 core

// Lens flare, drawn as screen-space quads from ONE procedural entity (nine
// instances, no per-instance data). Instance i is one element of the flare:
//
//   0  star glare at the sun          3..8  ghosts along the line from the sun
//   1  broad warm veil                      through the screen centre and out
//   2  faint halo ring                      the far side, alternately discs
//                                           and hexagonal-aperture shapes
//
// The sun's screen position comes from the same light direction the sky and the
// terrain use, projected through viewProj. The engine offers no depth read-back,
// so whether the sun is actually visible (behind trees, hills) is decided on
// the CPU and handed in as the `sunVisible` uniform.
//
// This is drawn with depth test off and additive blending, after the scene: the
// RenderSystem sorts draws by shader, and this shader is loaded last.

layout(location = 0) in vec3 aPos; // quad corner, xy in [-1, 1]

layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};

uniform float aspect;     // viewport width / height (the engine letterboxes to 16:9)
uniform float sunVisible; // 0..1, smoothed on the CPU

flat out int Kind;
out vec2 Local;
out vec3 Color;
out float Strength;

// Per element: kind, position along the sun-to-mirror axis (0 = on the sun,
// 1 = screen centre, 2 = the sun mirrored), half-height in NDC, tint, strength.
const int   KIND[9] = int[9](0, 1, 2, 3, 4, 3, 4, 3, 3);
const float AXIS[9] = float[9](0.0, 0.0, 0.0, 0.55, 0.85, 1.25, 1.55, 1.90, 2.35);
const float SIZE[9] = float[9](0.85, 2.0, 0.55, 0.07, 0.11, 0.05, 0.22, 0.14, 0.35);
const float STR[9]  = float[9](1.0, 0.35, 0.45, 0.55, 0.45, 0.50, 0.30, 0.30, 0.18);
const vec3  TINT[9] = vec3[9](
    vec3(1.00, 0.95, 0.80), vec3(1.00, 0.80, 0.55), vec3(1.00, 0.97, 0.90),
    vec3(0.50, 0.80, 1.00), vec3(1.00, 0.70, 0.40), vec3(0.60, 1.00, 0.70),
    vec3(0.90, 0.60, 1.00), vec3(1.00, 0.85, 0.50), vec3(0.50, 0.70, 1.00));

void collapse() {
    Kind = 0;
    Local = vec2(0.0);
    Color = vec3(0.0);
    Strength = 0.0;
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0); // outside the clip volume: dropped
}

void main()
{
    int i = gl_InstanceID;

    // Where the sun is on screen. A point far along the light direction stands
    // in for the sun; behind the camera (w <= 0) there is no flare.
    vec3 L = normalize(lightDir);
    vec4 c = viewProj * vec4(cameraPos + L * 1000.0, 1.0);
    if (c.w <= 0.001) {
        collapse();
        return;
    }
    vec2 sun = c.xy / c.w;

    // A flare persists a little after the sun leaves the screen, then is gone.
    float edge = max(abs(sun.x), abs(sun.y));
    float onScreen = 1.0 - smoothstep(0.9, 1.5, edge);
    float vis = sunVisible * onScreen;
    if (vis <= 0.002) {
        collapse();
        return;
    }

    vec2 centre = sun * (1.0 - AXIS[i]);
    vec2 corner = aPos.xy;
    vec2 ndc = centre + vec2(corner.x / aspect, corner.y) * SIZE[i];

    // Ghosts weaken as the sun moves away from the middle of the screen.
    float ghost = (AXIS[i] > 0.0) ? mix(1.0, 0.45, smoothstep(0.2, 1.3, length(sun))) : 1.0;

    Kind = KIND[i];
    Local = corner;
    Color = TINT[i] * (lightColor / max(max(lightColor.r, lightColor.g), lightColor.b));
    Strength = STR[i] * vis * ghost;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
