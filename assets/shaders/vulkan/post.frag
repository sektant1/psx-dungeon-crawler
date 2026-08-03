#version 450
// Port of assets/shaders/dither.frag (the Ogre compositor quad) for the RHI
// upscale pass. Grade -> vignette -> ordered dither + colour quantization,
// all on the sRGB-encoded scene colour, exactly as the legacy pass does.
//
// The legacy chain runs this at the low scene resolution and stretches the
// result. Here it is folded into the upscale blit instead, so the dither cell
// is derived from the *source* texel rather than gl_FragCoord: one dither
// value per scene pixel, which is what makes the pattern read as chunky PSX
// banding rather than a fine screen-resolution grain.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColour;
layout(set = 1, binding = 0) uniform sampler2D sourceTexture;
layout(set = 1, binding = 1) uniform sampler2D bloomTexture;

layout(push_constant) uniform PostConstants {
    vec2 uvScale;
    vec2 uvOffset;
    vec4 shadowTint;      // rgb = grade shadow tint
    vec4 midTint;         // rgb = grade mid tint
    vec4 vignetteColour;  // rgb = vignette tint
    vec4 gradeA;          // x desaturate, y contrast, z saturation, w tintStrength
    vec4 gradeB;          // x blackLift, y vignetteStrength, z gradeOn, w ditherOn
    vec4 ditherA;         // x colDepth, y ditherBanding, z ditherDarkFade
    vec4 bloom;           // x intensity, y enabled, z pixel snap
} post;

// The 4x4 Bayer matrix that assets/textures/psxdither.png stores (that PNG is
// this pattern rounded to 8 bit; the residual is <2/255 and is then scaled by
// ditherBanding ~0.02, so generating it here is indistinguishable and saves a
// sampler binding).
const float kBayer[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

void main() {
    vec4 base = texture(sourceTexture, uv);

    // Bloom composite. Ogre gives this its own pass (Engine/Psx/BloomComposite
    // into rt_final); folding it into the top of this shader keeps the legacy
    // ordering exactly -- the glow is added BEFORE the grade and the dither, so
    // it is quantized like everything else rather than laid on top crisp -- and
    // saves a full-screen target and pass.
    //
    // bloom.z snaps the lookup to scene-texel centres. The bright/blur targets
    // are half the scene's resolution, so the bilinear upsample would otherwise
    // lay a smooth ramp *across* each render pixel: the one gradient in the
    // chain finer than the pixel grid, and what makes an otherwise crisp
    // pixel-art frame read as soft around every light source. Snapped, the glow
    // stays built out of render pixels.
    if (post.bloom.y > 0.5) {
        vec2 sceneTexel = 1.0 / vec2(textureSize(sourceTexture, 0));
        vec2 snapped = (floor(uv / sceneTexel) + 0.5) * sceneTexel;
        vec3 glow = texture(bloomTexture, mix(uv, snapped, post.bloom.z)).rgb;
        base.rgb += glow * post.bloom.x;
    }

    if (post.gradeB.z > 0.5) {
        // Moonlit storybook split-tone. Deep values bend toward indigo,
        // readable mids toward dusty blue, highlights keep their warm torch
        // colour.
        float gLuma = dot(base.rgb, vec3(0.2126, 0.7152, 0.0722));
        base.rgb = mix(base.rgb, vec3(gLuma), post.gradeA.x);
        base.rgb = mix(vec3(gLuma), base.rgb, post.gradeA.z);
        float shadowWeight = 1.0 - smoothstep(0.08, 0.48, gLuma);
        float midWeight = smoothstep(0.08, 0.42, gLuma) *
                          (1.0 - smoothstep(0.62, 0.95, gLuma));
        base.rgb = mix(base.rgb, post.shadowTint.rgb,
                       shadowWeight * post.gradeA.w);
        base.rgb = mix(base.rgb, post.midTint.rgb,
                       midWeight * post.gradeA.w * 0.35);
        // Preserve the oppressive dark mass, but keep its shapes legible.
        base.rgb += post.gradeB.x * (1.0 - smoothstep(0.0, 0.32, gLuma));
        base.rgb = clamp((base.rgb - 0.5) * post.gradeA.y + 0.5, 0.0, 1.0);
    }

    float vignette = smoothstep(0.22, 0.72, distance(uv, vec2(0.5)));
    base.rgb = mix(base.rgb, base.rgb * post.vignetteColour.rgb,
                   vignette * post.gradeB.y);

    if (post.gradeB.w < 0.5) {
        outColour = vec4(base.rgb, 1.0);
        return;
    }

    ivec2 cell = ivec2(floor(uv * vec2(textureSize(sourceTexture, 0)))) & 3;
    float dith = kBayer[cell.y * 4 + cell.x] / 16.0 - 0.5;
    // Fade the pattern out in near-black areas: those band boundaries are the
    // highest-contrast ones and shimmer during camera motion.
    float luma = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    float dithAmt = post.ditherA.y *
                    smoothstep(0.0, max(post.ditherA.z, 1e-5), luma);
    float colDepth = max(post.ditherA.x, 1.0);
    outColour = vec4(clamp(round(base.rgb * colDepth + dith * dithAmt) /
                           colDepth, 0.0, 1.0), 1.0);
}
