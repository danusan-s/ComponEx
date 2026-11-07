#version 330 core

flat in int Kind;
in vec2 Local;  // [-1, 1] across the quad
in vec3 Color;
in float Strength;

out vec4 FragColor;

// Hexagon "distance": 1.0 on the edge, so a lens-aperture ghost can be shaped
// with the same smoothsteps as a disc.
float hexDist(vec2 p) {
    p = abs(p);
    return max(dot(p, vec2(0.8660254, 0.5)), p.y);
}

float ring(float x, float radius, float width) {
    float d = (x - radius) / width;
    return exp(-d * d);
}

void main()
{
    float r = length(Local);
    vec3 col = vec3(0.0);

    if (Kind == 0) {
        // Star glare: bright core, soft glow, six spikes and a horizontal streak.
        float a = atan(Local.y, Local.x);
        float core = exp(-r * r * 40.0);
        float glow = exp(-r * 5.0) * 0.45;
        float spikes = pow(abs(cos(a * 3.0)), 48.0) * exp(-r * 3.2) * 0.55;
        float streak = exp(-abs(Local.y) * 45.0) * exp(-abs(Local.x) * 3.0) * 0.28;
        col = Color * (core + glow + spikes + streak);
    } else if (Kind == 1) {
        // Veil: a very broad, faint warm haze.
        col = Color * exp(-r * r * 3.0);
    } else if (Kind == 2) {
        // Halo: a thin ring with the colours slightly split, like dispersion.
        col = vec3(ring(r, 0.605, 0.05), ring(r, 0.62, 0.05), ring(r, 0.635, 0.05)) * Color;
        col += Color * exp(-r * r * 6.0) * 0.06;
    } else if (Kind == 3) {
        // Disc ghost: faint body, bright rim.
        float body = smoothstep(1.0, 0.8, r);
        float rim = smoothstep(0.55, 0.95, r) * body;
        col = Color * (0.25 * body + 0.9 * rim) + vec3(0.20, 0.0, 0.30) * rim * 0.3;
    } else {
        // Aperture ghost: the same, but hexagonal.
        float h = hexDist(Local * 1.2);  // scaled so the corners stay inside the quad
        float body = smoothstep(1.0, 0.85, h);
        float rim = smoothstep(0.6, 0.95, h) * body;
        col = Color * (0.22 * body + 0.9 * rim);
    }

    // Quads are square; every shape must reach zero before the edge.
    col *= 1.0 - smoothstep(0.92, 1.0, r);

    // Additive blending uses alpha as the multiplier, so keep it at 1.
    FragColor = vec4(col * Strength, 1.0);
}
