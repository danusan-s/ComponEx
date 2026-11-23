#version 330 core

// The last pass: copies the scene texture onto the window. With fxaaOn it smooths
// edges on the way, otherwise it only copies (and, when the scene was drawn at
// a different size, scales: the sampler is linear).
//
// FXAA (Timothy Lottes' algorithm, the compact form): find the direction of the
// edge from the luma of the four diagonal neighbours, then average a few
// samples along it. The result is kept only if it stays inside the local luma
// range, which stops it blurring fine detail that is not an edge.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D sceneColor;
uniform vec2 invResolution; // 1 / scene size in pixels
uniform float fxaaOn;       // 0: only copy (and scale) the scene

const float REDUCE_MIN = 1.0 / 128.0;
const float REDUCE_MUL = 1.0 / 8.0;
const float SPAN_MAX = 8.0;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec3 rgbM = texture(sceneColor, vUV).rgb;
    if (fxaaOn < 0.5) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    vec3 rgbNW = texture(sceneColor, vUV + vec2(-1.0, -1.0) * invResolution).rgb;
    vec3 rgbNE = texture(sceneColor, vUV + vec2(1.0, -1.0) * invResolution).rgb;
    vec3 rgbSW = texture(sceneColor, vUV + vec2(-1.0, 1.0) * invResolution).rgb;
    vec3 rgbSE = texture(sceneColor, vUV + vec2(1.0, 1.0) * invResolution).rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM = luma(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * REDUCE_MUL),
                          REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * invResolution;

    vec3 rgbA = 0.5 * (texture(sceneColor, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(sceneColor, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneColor, vUV + dir * -0.5).rgb +
                                      texture(sceneColor, vUV + dir * 0.5).rgb);
    float lumaB = luma(rgbB);

    FragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
