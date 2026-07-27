#version 330 core

// Prototype portal: portal.frag's look with no art behind it.
//
// Identical structure to the authored shader -- stepped time, quantised pixel
// grid, three-tone palette, spiral + rim -- with the flow texture swapped for
// procedural noise. Keep the two in step: if portal.frag's look changes, change
// this the same way, or missing-texture scenes stop matching the real thing.
//
// Tweak from the material (Engine/PrototypePortal), or live in the debug
// overlay's material panel:
//   portalDark/Mid/Bright  the three palette tones, darkest first
//   portalStepFps          pose rate; lower = chunkier stop-motion
//   portalFlowSpeed        drift rate; negative reverses the flow
//   portalPixelGrid        quantisation; lower = bigger pixels
//   portalNoiseScale       feature size of the procedural field

in vec2 portalUV;
uniform vec4 portalDark;
uniform vec4 portalMid;
uniform vec4 portalBright;
uniform float portalStepFps;
uniform float portalFlowSpeed;
uniform float portalPixelGrid;
uniform float portalNoiseScale;
uniform float time;
layout(location = 0) out vec4 fragColour;

#include <prototype_noise.glsl>

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
    // Stands in for texture(portalTexture, flowUV).r.
    float fieldValue = protoFbm(flowUV * max(portalNoiseScale, 0.5));
    float spiral = 0.5 + 0.5 * sin(angle * 3.0 - radius * 22.0 +
                                   steppedTime * 4.0);
    float value = clamp(fieldValue * 0.62 + spiral * 0.38, 0.0, 1.0);
    float rim = step(0.38, radius) * (1.0 - step(0.49, radius));
    value = max(value, rim);
    vec3 colour = portalPalette(value);
    fragColour = vec4(colour, 1.0);
}
