#version 330 core
in vec3  voxelNormal;
in vec3  voxelNormalVS;
in vec4  voxelColour;
in float voxelViewDepth;

uniform float farClip;
uniform float alphaScissor;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    // Six fixed tones keyed off the face normal. Dynamic lighting on thousands
    // of gib cubes would cost real time and would not survive the low-res
    // framebuffer anyway; a fixed key/fill/bounce ratio reads as solid volume
    // at a third resolution.
    vec3 n = normalize(voxelNormal);
    vec3 a = abs(n);
    float tone;
    if (a.y >= a.x && a.y >= a.z)
        tone = n.y > 0.0 ? 1.00 : 0.42;    // top lit, underside in shadow
    else if (a.x >= a.z)
        tone = n.x > 0.0 ? 0.82 : 0.66;
    else
        tone = n.z > 0.0 ? 0.90 : 0.58;

    fragColour = vec4(voxelColour.rgb * tone, voxelColour.a);
    if (fragColour.a < alphaScissor)
        discard;

    // Voxels are solid geometry, so unlike sprites they do belong in the edge
    // buffer: the outline is what sells them as blocks. Encoding matches
    // psx.frag, alpha = viewDepth / farClip.
    fragNormalDepth = vec4(normalize(voxelNormalVS) * 0.5 + 0.5,
                           voxelViewDepth / farClip);
}
