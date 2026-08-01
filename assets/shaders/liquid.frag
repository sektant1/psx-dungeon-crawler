#version 330 core

// Water / slime: two tiling layers scrolling at different rates and scales,
// palette-quantised. The scrolling half of the surface split -- see
// scroll_common.glsl for why it is not built on surface_common.glsl.

uniform sampler2D liquidTexture;
uniform vec2 liquidFlowA;
uniform vec2 liquidFlowB;

#include <scroll_common.glsl>

void main()
{
    float steppedTime = liquidSteppedTime();
    vec2 pixelUV = liquidPixelate(liquidUV);

    // Two layers, different scale and direction: one sliding texture reads as a
    // sliding texture, two crossing ones read as a surface with a current. The
    // offsets are wrapped to a tile (liquidScroll) so a long session cannot
    // grind them into judder.
    float layerA = texture(
        liquidTexture, pixelUV + liquidScroll(liquidFlowA, steppedTime)).r;
    float layerB = texture(
        liquidTexture,
        pixelUV * 1.7 + liquidScroll(liquidFlowB, steppedTime)).g;

    float value = clamp(layerA * 0.62 + layerB * 0.38, 0.0, 1.0);
    vec3 colour = liquidPalette(value);
    float highlight = step(0.86, value) * liquidEmission;
    fragColour = vec4(colour + liquidBright.rgb * highlight, 1.0);
}
