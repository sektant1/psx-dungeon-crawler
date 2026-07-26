#version 330 core
in vec2 portalUV;
uniform sampler2D portalTexture;
uniform vec4 portalDark;
uniform vec4 portalMid;
uniform vec4 portalBright;
uniform float portalStepFps;
uniform float portalFlowSpeed;
uniform float portalPixelGrid;
uniform float time;
layout(location = 0) out vec4 fragColour;

vec3 portalPalette(float value)
{
    if (value < 0.34)
        return portalDark.rgb;
    if (value < 0.72)
        return portalMid.rgb;
    return portalBright.rgb;
}

void main()
{
    float steppedTime =
        floor(time * portalStepFps) / max(portalStepFps, 1.0);
    float grid = max(portalPixelGrid, 4.0);
    vec2 pixelUV = (floor(portalUV * grid) + 0.5) / grid;
    vec2 centered = pixelUV - 0.5;
    float radius = length(centered);
    float angle = atan(centered.y, centered.x);
    vec2 flowUV = pixelUV;
    flowUV.y += steppedTime * portalFlowSpeed;
    flowUV.x += sin(angle * 3.0 - steppedTime * 2.0) * 0.055;
    float textureValue = texture(portalTexture, flowUV).r;
    float spiral = 0.5 + 0.5 * sin(angle * 3.0 - radius * 22.0 +
                                   steppedTime * 4.0);
    float value = clamp(textureValue * 0.62 + spiral * 0.38, 0.0, 1.0);
    float rim = step(0.38, radius) * (1.0 - step(0.49, radius));
    value = max(value, rim);
    vec3 colour = portalPalette(value);
    fragColour = vec4(colour, 1.0);
}
