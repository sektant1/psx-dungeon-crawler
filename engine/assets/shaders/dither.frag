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
uniform float ditherDarkFade;     // luma below which dither fades out; 0 = off
uniform float ditherEnabled;      // 0 = pass-through (no quantization)
out vec4 fragColour;
void main()
{
    vec4 base_color = texture(sceneTex, uv);
    if (ditherEnabled < 0.5) {
        fragColour = vec4(base_color.rgb, 1.0);
        return;
    }
    vec2 dith_size = vec2(textureSize(ditherTex, 0));
    vec2 buf_size = vec2(textureSize(sceneTex, 0));
    vec3 dith = texture(ditherTex, uv * (buf_size / dith_size)).rgb - 0.5;
    // Fade the dither pattern out in near-black areas: band boundaries there
    // are the highest-contrast ones and shimmer during camera motion.
    float luma = dot(base_color.rgb, vec3(0.299, 0.587, 0.114));
    float dithAmt = ditherBanding * smoothstep(0.0, max(ditherDarkFade, 1e-5), luma);
    fragColour = vec4(round(base_color.rgb * colDepth + dith * dithAmt) / colDepth, 1.0);
}
