#version 450
// Colour to MRT target 0, zero to the normal/depth metadata on target 1.
//
// That zero is load-bearing: the stylize pass passes any pixel whose metadata
// is near-zero straight through (see stylize.frag), so a debug line keeps the
// exact colour the caller asked for instead of being darkened by the outline
// and shadow ink laid over the world. The legacy GL debug_lines.frag did the
// same thing for the same reason.

layout(location = 0) in vec4 colour;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec4 outNormalDepth;

void main() {
    outColour = vec4(colour.rgb, 1.0);
    outNormalDepth = vec4(0.0);
}
