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
uniform float gradeEnabled;     // 0 = pass-through (pixel-identical)
uniform float gradeDesaturate;  // 0..1 pull toward luma grey
uniform float gradeContrast;    // contrast about 0.5 pivot (1 = neutral)
uniform vec3  gradeShadowTint;  // multiplied into dark lumas
uniform vec3  gradeMidTint;     // multiplied into mid/high lumas
out vec4 fragColour;
void main()
{
    vec4 base_color = texture(sceneTex, uv);
    // Grade runs before the dither bypass so "dither off" still keeps the
    // palette-unifying grade (independent toggles, like stylizeEnabled).
    if (gradeEnabled > 0.5) {
        // Unify mismatched pack textures: desaturate a touch, split-tone
        // (shadows toward the tint colour, mids/highs toward warm), then a
        // mild contrast about mid-grey. Runs on sRGB-encoded scene colour.
        float gLuma = dot(base_color.rgb, vec3(0.2126, 0.7152, 0.0722));
        base_color.rgb = mix(base_color.rgb, vec3(gLuma), gradeDesaturate);
        vec3 tint = mix(gradeShadowTint, gradeMidTint,
                        smoothstep(0.0, 0.6, gLuma));
        base_color.rgb *= tint;
        base_color.rgb = clamp((base_color.rgb - 0.5) * gradeContrast + 0.5,
                               0.0, 1.0);
    }
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
