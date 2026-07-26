#version 330 core
// Adds the blurred bright-pass onto the pixelated scene, before the dither
// pass so the glow is quantized/dithered like everything else.
in vec2 uv;
uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform float bloomIntensity;     // default 0.8
uniform float bloomEnabled;       // 0 = pass-through
// 1 = sample the glow at scene-texel centres. The bright/blur targets are half
// the scene's resolution, so the bilinear upsample this pass would otherwise do
// lays a smooth ramp *across* each render pixel -- the one gradient in the
// chain finer than the pixel grid, and the thing that makes an otherwise crisp
// pixel-art frame read as soft around every light source. Snapping the lookup
// keeps the glow on the grid: still a glow, but built out of render pixels.
// Leave at 0 for the PS2/GameCube profiles, which want the smooth bloom.
uniform float bloomPixelSnap;
out vec4 fragColour;
void main()
{
    vec3 scene = texture(sceneTex, uv).rgb;
    vec2 sceneTexel = 1.0 / vec2(textureSize(sceneTex, 0));
    vec2 snapped = (floor(uv / sceneTexel) + 0.5) * sceneTexel;
    vec3 bloom = texture(bloomTex, mix(uv, snapped, bloomPixelSnap)).rgb;
    fragColour = vec4(scene + bloom * bloomIntensity * bloomEnabled, 1.0);
}
