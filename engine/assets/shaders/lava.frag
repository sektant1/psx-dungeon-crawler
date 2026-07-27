#version 330 core
in vec2 liquidUV;
uniform sampler2D lavaTexture;
uniform vec4 lavaDark;
uniform vec4 lavaCrust;
uniform vec4 lavaHot;
uniform vec4 lavaCore;
uniform float lavaStepFps;
uniform float lavaPixelGrid;
uniform float lavaFlowSpeed;
uniform float time;
layout(location = 0) out vec4 fragColour;

float lavaHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float lavaNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    float a = lavaHash(cell);
    float b = lavaHash(cell + vec2(1.0, 0.0));
    float c = lavaHash(cell + vec2(0.0, 1.0));
    float d = lavaHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, blend.x), mix(c, d, blend.x), blend.y);
}

float lavaFbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    mat2 octaveTransform = mat2(1.6, 1.2, -1.2, 1.6);
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * lavaNoise(p);
        p = octaveTransform * p;
        amplitude *= 0.5;
    }
    return value;
}

vec2 domainWarp(vec2 p, float steppedTime)
{
    vec2 flow = vec2(
        lavaFbm(p + vec2(steppedTime * 0.31, 1.7)),
        lavaFbm(p + vec2(-2.4, steppedTime * -0.27)));
    return p + (flow - 0.5) * 1.35;
}

vec3 lavaPalette(float value)
{
    if (value < 0.34)
        return lavaDark.rgb;
    if (value < 0.58)
        return lavaCrust.rgb;
    if (value < 0.82)
        return lavaHot.rgb;
    return lavaCore.rgb;
}

void main()
{
    float steppedTime =
        floor(time * lavaStepFps) / max(lavaStepFps, 1.0);
    float grid = max(lavaPixelGrid, 4.0);
    vec2 pixelUV = (floor(liquidUV * grid) + 0.5) / grid;
    vec2 flowing = pixelUV * 5.0;
    flowing += vec2(steppedTime * lavaFlowSpeed,
                    -steppedTime * lavaFlowSpeed * 0.63);
    vec2 warped = domainWarp(flowing, steppedTime);

    float broadFlow = lavaFbm(warped);
    float veins = abs(lavaFbm(warped * 1.85 + broadFlow) - 0.5) * 2.0;
    float textureDetail = texture(
        lavaTexture, pixelUV * 1.4 + vec2(steppedTime * 0.018, 0.0)).r;
    float heat = clamp((1.0 - veins) * 0.72 +
                       broadFlow * 0.20 + textureDetail * 0.08, 0.0, 1.0);
    heat = floor(heat * 4.0) / 3.0;
    fragColour = vec4(lavaPalette(heat), 1.0);
}
