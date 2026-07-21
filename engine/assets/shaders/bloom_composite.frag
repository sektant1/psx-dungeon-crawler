#version 330 core
// Adds the blurred bright-pass onto the pixelated scene, before the dither
// pass so the glow is quantized/dithered like everything else.
in vec2 uv;
uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform float bloomIntensity;     // default 0.8
uniform float bloomEnabled;       // 0 = pass-through
out vec4 fragColour;
void main()
{
    vec3 scene = texture(sceneTex, uv).rgb;
    vec3 bloom = texture(bloomTex, uv).rgb;
    fragColour = vec4(scene + bloom * bloomIntensity * bloomEnabled, 1.0);
}
