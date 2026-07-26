#version 330 core
in vec2 liquidUV;
uniform sampler2D liquidTexture;
uniform vec4 liquidDark;
uniform vec4 liquidMid;
uniform vec4 liquidBright;
uniform vec2 liquidFlowA;
uniform vec2 liquidFlowB;
uniform float liquidStepFps;
uniform float liquidPixelGrid;
uniform float liquidEmission;
uniform float time;
layout(location = 0) out vec4 fragColour;

vec3 liquidPalette(float value)
{
    if (value < 0.38)
        return liquidDark.rgb;
    if (value < 0.76)
        return liquidMid.rgb;
    return liquidBright.rgb;
}

void main()
{
    float steppedTime =
        floor(time * liquidStepFps) / max(liquidStepFps, 1.0);
    float grid = max(liquidPixelGrid, 4.0);
    vec2 pixelUV = (floor(liquidUV * grid) + 0.5) / grid;
    float layerA = texture(
        liquidTexture, pixelUV + liquidFlowA * steppedTime).r;
    float layerB = texture(
        liquidTexture, pixelUV * 1.7 + liquidFlowB * steppedTime).g;
    float value = clamp(layerA * 0.62 + layerB * 0.38, 0.0, 1.0);
    vec3 colour = liquidPalette(value);
    float highlight = step(0.86, value) * liquidEmission;
    fragColour = vec4(colour + liquidBright.rgb * highlight, 1.0);
}
