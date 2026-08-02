#version 450
// Port of assets/shaders/particle_sprite.frag. Ramp colours arrive in the same
// encoding the scene target already holds, so there is no transfer here --
// texture * colour, exactly like the legacy pass.

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 colour;

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec4 outNormalDepth;

void main() {
    vec4 c = texture(albedoTexture, uv) * colour;
    if (c.a <= 0.0)
        discard;
    outColour = c;
    // Billboards contribute no edge metadata: the stylize pass would outline
    // every sprite silhouette and turn a smoke plume into a sticker. Zero is
    // exactly what its early-out tests for.
    outNormalDepth = vec4(0.0);
}
