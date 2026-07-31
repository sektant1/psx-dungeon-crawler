#version 330 core

// Portal, art-free variant: portal.frag's look with no texture behind it, for a
// tree that ships no VFX art. Shares the same kernel and the same pattern, so
// the only difference is this file's `surfaceField`.
//
// portalNoiseScale sets the feature size of that procedural field; every other
// knob is documented in surface_common.glsl or portal_pattern.glsl.

uniform float portalNoiseScale;

#include <prototype_noise.glsl>
#include <surface_common.glsl>
#include <portal_pattern.glsl>

// The angular axis wraps at whole turns, and value noise does not, so the two
// sides of the seam are cross-faded: at u -> 1 this returns exactly what it
// returned at u = 0. Without it a still bright line runs out of the portal's
// centre, which no amount of palette tuning hides.
float surfaceField(vec2 uv)
{
    float scale = max(portalNoiseScale, 0.5);
    float turn = fract(uv.x);
    float near = protoFbm(vec2(turn, uv.y) * scale);
    float wrapped = protoFbm(vec2(turn - 1.0, uv.y) * scale);
    return mix(near, wrapped, turn);
}
