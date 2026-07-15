#version 330 core
// Separable 5-tap Gaussian; blurDir = (1,0) for H, (0,1) for V.
in vec2 uv;
uniform sampler2D sceneTex;
uniform vec2 blurDir;
out vec4 fragColour;
void main()
{
    vec2 texel = blurDir / vec2(textureSize(sceneTex, 0));
    vec3 sum = texture(sceneTex, uv).rgb * 0.375;               // 6/16
    sum += texture(sceneTex, uv + texel).rgb * 0.25;            // 4/16
    sum += texture(sceneTex, uv - texel).rgb * 0.25;
    sum += texture(sceneTex, uv + 2.0 * texel).rgb * 0.0625;    // 1/16
    sum += texture(sceneTex, uv - 2.0 * texel).rgb * 0.0625;
    fragColour = vec4(sum, 1.0);
}
