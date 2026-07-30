#version 330 core

// Prototype liquid: liquid.frag's look with no art behind it.
//
// Identical structure to the authored shader -- two cross-scrolling layers
// through a three-tone palette, stepped and pixel-quantised -- with the flow
// texture swapped for procedural noise. Keep the two in step: if liquid.frag's
// look changes, change this the same way.
//
// One shader, several profiles: the palette and flow uniforms are what make it
// read as water, slime or lava, so a new liquid is a material, not a shader.
// See Engine/PrototypeLiquid and Engine/PrototypeLava.
//
// Tweak from the material, or live in the debug overlay's material panel:
//   liquidDark/Mid/Bright  the three palette tones, darkest first
//   liquidFlowA/B          per-layer drift; opposing directions read as depth
//   liquidStepFps          pose rate; lower = chunkier stop-motion
//   liquidPixelGrid        quantisation; lower = bigger pixels
//   liquidEmission         glow added to the brightest band (lava, not water)
//   liquidNoiseScale       feature size of the procedural field

in vec2 liquidUV;
uniform vec4 liquidDark;
uniform vec4 liquidMid;
uniform vec4 liquidBright;
uniform vec2 liquidFlowA;
uniform vec2 liquidFlowB;
uniform float liquidStepFps;
uniform float liquidPixelGrid;
uniform float liquidEmission;
uniform float liquidNoiseScale;
uniform float time;
layout(location = 0) out vec4 fragColour;

#include <prototype_noise.glsl>

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
    float scale = max(liquidNoiseScale, 0.5);
    // Stand in for the .r and .g channels of the authored flow texture. The
    // second layer is offset as well as rescaled so the two never beat against
    // each other into a visible stationary pattern.
    float layerA = protoFbm((pixelUV + liquidFlowA * steppedTime) * scale);
    float layerB = protoFbm((pixelUV * 1.7 + liquidFlowB * steppedTime) *
                                scale + vec2(17.3, 8.7));
    float value = clamp(layerA * 0.62 + layerB * 0.38, 0.0, 1.0);
    vec3 colour = liquidPalette(value);
    float highlight = step(0.86, value) * liquidEmission;
    fragColour = vec4(colour + liquidBright.rgb * highlight, 1.0);
}
