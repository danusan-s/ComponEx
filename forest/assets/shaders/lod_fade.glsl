// LOD cross-fade: the fade is 0 normally. A fading entity's old mesh has it
// rising 0 -> 1 and the new mesh -1 -> 0; each discards its share of a fixed
// screen-space dither (interleaved gradient noise), and the two shares are
// complementary. The pattern must match in the depth pre-pass and colour pass.
float lodDither()
{
    return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

void lodFadeDiscard(float fade)
{
    if (fade != 0.0) {
        float n = lodDither();
        if (fade > 0.0 ? n < fade : n >= 1.0 + fade) discard;
    }
}
