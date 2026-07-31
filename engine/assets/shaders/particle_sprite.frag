#version 330 core
in vec2  particleUV;
in vec4  particleColour;
in float particleViewDepth;

uniform sampler2D albedoTex;
uniform float alphaScissor;

#if defined(SOFT_FADE)
// Soft fade needs the scene's linear depth in the same encoding the PSX
// compositor writes to MRT attachment 1: alpha = viewDepth / farClip. A target
// cannot be sampled while it is being written, so this variant only works once
// a depth copy exists; nothing binds it today.
uniform sampler2D sceneDepthTex;
uniform vec4 targetSize;        // xy = pixels, zw = 1/pixels
uniform float farClip;
uniform float softFadeDistance;
#endif

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    fragColour = texture(albedoTex, particleUV) * particleColour;

#if defined(SOFT_FADE)
    float sceneDepth =
        texture(sceneDepthTex, gl_FragCoord.xy * targetSize.zw).a * farClip;
    fragColour.a *= clamp((sceneDepth - particleViewDepth) /
                              max(softFadeDistance, 0.0001),
                          0.0, 1.0);
#endif

    if (fragColour.a < alphaScissor)
        discard;

    // Billboards contribute no edge metadata: the stylize pass would outline
    // every sprite silhouette and turn a smoke plume into a sticker.
    fragNormalDepth = vec4(0.0);
}
