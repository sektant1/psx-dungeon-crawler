#version 330 core
// Port of shaders/pp_band-dither.gdshader (canvas_item) as an Ogre compositor
// quad shader. Scene is rendered into a 320x240 target first (matching the
// DitherBanding SubViewport in post_process/pp_stack.tscn), dithered, then
// stretched to the window with nearest filtering.
in vec2 uv;
uniform sampler2D sceneTex;
uniform sampler2D ditherTex;      // shaders/psxdither.png, 4x4, nearest+repeat
uniform float colDepth;           // 15.0
uniform float ditherBanding;      // bool -> 1.0 / 0.0
out vec4 fragColour;
void main()
{
    vec4 base_color = texture(sceneTex, uv);
    vec2 dith_size = vec2(textureSize(ditherTex, 0));
    vec2 buf_size = vec2(textureSize(sceneTex, 0));
    vec3 dith = texture(ditherTex, uv * (buf_size / dith_size)).rgb - 0.5;
    fragColour = vec4(round(base_color.rgb * colDepth + dith * ditherBanding) / colDepth, 1.0);
}
