#version 450
// Port of assets/shaders/bloom_bright.frag. Bright-pass: keep only what exceeds
// the threshold, renormalised so intensity scaling behaves predictably. LDR
// input (the sRGB-encoded scene) -- fine for the stylized look; runs at half the
// pixelated resolution.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColour;

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;

layout(push_constant) uniform BloomConstants {
    vec2 uvScale;
    vec2 uvOffset;
    vec4 params;   // x = threshold
} bloom;

void main() {
    vec3 c = texture(sceneTexture, uv).rgb;
    float threshold = bloom.params.x;
    vec3 bright = max(c - vec3(threshold), vec3(0.0)) /
                  max(1.0 - threshold, 1e-4);
    outColour = vec4(bright, 1.0);
}
