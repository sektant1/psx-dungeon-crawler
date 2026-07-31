#version 330 core
in vec2  decalUV;
in vec4  decalColour;
in vec3  decalViewNormal;
in float decalViewDepth;

uniform sampler2D albedoTex;
uniform float alphaScissor;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    fragColour = texture(albedoTex, decalUV) * decalColour;
    if (fragColour.a < alphaScissor)
        discard;

    // A decal lies on a surface that already wrote its own normal and depth, so
    // it repeats them rather than zeroing attachment 1 the way a billboard
    // does. Blanking it here would punch a hole in the stylize pass's edge
    // detection and outline every blood splat.
    fragNormalDepth = vec4(normalize(decalViewNormal) * 0.5 + 0.5,
                           decalViewDepth);
}
