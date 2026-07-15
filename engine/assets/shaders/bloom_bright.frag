#version 330 core
// Bloom bright-pass: keep only what exceeds the threshold, renormalised so
// intensity scaling behaves predictably. LDR input (sRGB-encoded scene) --
// fine for the stylized look; runs at half the pixelated resolution.
in vec2 uv;
uniform sampler2D sceneTex;
uniform float bloomThreshold;     // 0..1, default 0.7
out vec4 fragColour;
void main()
{
    vec3 c = texture(sceneTex, uv).rgb;
    vec3 bright = max(c - vec3(bloomThreshold), vec3(0.0))
                  / max(1.0 - bloomThreshold, 1e-4);
    fragColour = vec4(bright, 1.0);
}
