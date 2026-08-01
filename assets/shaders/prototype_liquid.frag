#version 330 core

// Water / slime, art-free variant: liquid.frag's look with no texture behind
// it, for a tree that ships no VFX art. Same kernel, same structure -- the only
// difference is that the two layers are synthesised instead of sampled.
//
// liquidNoiseScale sets the feature size of that procedural field; every other
// knob is documented in scroll_common.glsl.

uniform vec2 liquidFlowA;
uniform vec2 liquidFlowB;
uniform float liquidNoiseScale;

#include <prototype_noise.glsl>
#include <scroll_common.glsl>

void main()
{
    float steppedTime = liquidSteppedTime();
    vec2 pixelUV = liquidPixelate(liquidUV);
    float scale = max(liquidNoiseScale, 0.5);

    // Stand in for the .r and .g channels of the authored flow texture. The
    // second layer is offset as well as rescaled so the two never beat against
    // each other into a visible stationary pattern.
    //
    // The scroll offsets are wrapped to a tile for the same reason as the
    // authored shader: value noise is periodic under the hash, so a wrapped
    // offset is equivalent, and an unwrapped one loses precision over a long
    // session.
    float layerA =
        protoFbm((pixelUV + liquidScroll(liquidFlowA, steppedTime)) * scale);
    float layerB =
        protoFbm((pixelUV * 1.7 + liquidScroll(liquidFlowB, steppedTime)) *
                     scale + vec2(17.3, 8.7));

    float value = clamp(layerA * 0.62 + layerB * 0.38, 0.0, 1.0);
    vec3 colour = liquidPalette(value);
    float highlight = step(0.86, value) * liquidEmission;
    fragColour = vec4(colour + liquidBright.rgb * highlight, 1.0);
}
