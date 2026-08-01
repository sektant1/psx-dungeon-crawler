#version 330 core

smooth in vec2 spriteUV;
flat in vec2 spriteCell;
flat in vec2 spriteInvGrid;
smooth in vec4 spriteColour;

uniform sampler2D spriteTexture;
uniform vec4 spriteTint;
uniform float spriteAlphaCutoff;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    // Per-fragment wrapping keeps scrolling inside the selected atlas frame
    // without destroying the billboard's interpolated UV gradient.
    vec2 atlasUV = (fract(spriteUV) + spriteCell) * spriteInvGrid;
    vec4 sampleColour = texture(spriteTexture, atlasUV);
    // Texture and author tint are sRGB assets; the compositor expects linear
    // scene colour. Alpha and vertex colour remain linear.
    sampleColour.rgb = pow(max(sampleColour.rgb, vec3(0.0)), vec3(2.2));
    vec3 linearTint = pow(max(spriteTint.rgb, vec3(0.0)), vec3(2.2));
    vec4 outColour = vec4(sampleColour.rgb * linearTint,
                          sampleColour.a * spriteTint.a) * spriteColour;
    if (outColour.a < spriteAlphaCutoff)
        discard;
    fragColour = outColour;
    fragNormalDepth = vec4(0.5, 0.5, 1.0, 0.0);
}
