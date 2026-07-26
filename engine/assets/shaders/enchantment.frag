#version 330 core
in vec2 enchantUV;
in float enchantPulse;
uniform vec4 enchantColour;
uniform float enchantStrength;
layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;
void main()
{
    // A moving field of varied low-resolution runes. Each UV cell selects one
    // of three signed-distance motifs, avoiding the repetitive diagonal bars
    // of the old placeholder shader.
    vec2 cell = floor(enchantUV);
    vec2 p = fract(enchantUV) - 0.5;
    float hash = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);

    float diamond = abs(abs(p.x) + abs(p.y) - 0.27);
    float crossRune = min(max(abs(p.x) - 0.045, abs(p.y) - 0.30),
                          max(abs(p.y) - 0.045, abs(p.x) - 0.30));
    float ring = abs(length(p) - 0.25);
    float dist = hash < 0.34 ? diamond : (hash < 0.67 ? crossRune : ring);

    float rune = 1.0 - smoothstep(0.035, 0.085, dist);
    float core = 1.0 - smoothstep(0.0, 0.025, dist);
    float scan = 0.72 + 0.28 * sin((enchantUV.x + enchantUV.y) * 2.4);
    float alpha = enchantColour.a * enchantStrength * rune *
                  enchantPulse * scan;
    vec3 colour = enchantColour.rgb * (0.75 + rune * 0.65) +
                  vec3(core * 0.35);
    fragColour = vec4(colour * alpha, alpha);
    fragNormalDepth = vec4(0.0);
}
