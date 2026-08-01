// Shared value noise for the prototype VFX shaders.
//
// The authored portal/liquid/lava shaders read their flow field from a texture.
// The prototype variants have no art to read -- they exist so a scene missing
// its VFX textures still shows the effect rather than a flat box -- so they
// synthesise an equivalent field here: same 0..1 range, same broad feature
// size, so every palette threshold and tuning value downstream keeps meaning.

float protoHash(vec2 p)
{
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Bilinear value noise. Smoothstep on the cell fraction keeps the derivative
// continuous, so the pixel-grid quantisation stays the only visible stepping.
float protoValueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = protoHash(cell);
    float b = protoHash(cell + vec2(1.0, 0.0));
    float c = protoHash(cell + vec2(0.0, 1.0));
    float d = protoHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Three octaves is enough structure at the pixel grids these shaders run at,
// and stays cheap enough for a full portal plane.
float protoFbm(vec2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 3; ++i) {
        sum += protoValueNoise(p) * amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    return clamp(sum / 0.875, 0.0, 1.0); // 0.5+0.25+0.125 -> renormalise to 0..1
}
