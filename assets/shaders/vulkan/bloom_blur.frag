#version 450
// Port of assets/shaders/bloom_blur.frag. Separable 5-tap Gaussian;
// blurDir = (1,0) for the horizontal pass, (0,1) for the vertical.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColour;

layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

layout(push_constant) uniform BloomConstants {
    vec2 uvScale;
    vec2 uvOffset;
    vec4 params;   // xy = blur direction, in texels
} bloom;

void main() {
    vec2 texel = bloom.params.xy / vec2(textureSize(sourceTexture, 0));
    vec3 sum = texture(sourceTexture, uv).rgb * 0.375;              // 6/16
    sum += texture(sourceTexture, uv + texel).rgb * 0.25;           // 4/16
    sum += texture(sourceTexture, uv - texel).rgb * 0.25;
    sum += texture(sourceTexture, uv + 2.0 * texel).rgb * 0.0625;   // 1/16
    sum += texture(sourceTexture, uv - 2.0 * texel).rgb * 0.0625;
    outColour = vec4(sum, 1.0);
}
