#version 330 core

// Portal, authored variant: the flow field is the stylised texture the material
// binds (nearest-filtered, wrapped).
//
// Three lines of its own. The presentation is surface_common.glsl (the kernel
// every stylised surface shares) and the look is portal_pattern.glsl (the swirl
// that makes it a portal). This file only answers where the field comes from.

uniform sampler2D surfaceTexture;

#include <surface_common.glsl>
#include <portal_pattern.glsl>

// The sampling coordinate is (turns around the swirl, depth into it), so both
// axes wrap: the texture must be tiling and its address mode `wrap`.
float surfaceField(vec2 uv)
{
    return texture(surfaceTexture, uv).r;
}
