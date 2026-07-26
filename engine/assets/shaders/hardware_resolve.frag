#version 330 core
// Optional era-specific resolve. Mode is uniform for the whole draw, so the
// mode branches do not diverge across pixels. Mode 0 is an exact pass-through.
in vec2 uv;
uniform sampler2D sceneTex;
uniform float resolveMode;
uniform float resolveStrength;
out vec4 fragColour;

vec3 sampleAt(vec2 pixel)
{
    vec2 size = vec2(textureSize(sceneTex, 0));
    return texture(sceneTex, (pixel + 0.5) / size).rgb;
}

vec3 n64ThreePoint(vec2 p)
{
    vec2 size = vec2(textureSize(sceneTex, 0));
    vec2 texel = p * size - 0.5;
    vec2 base = floor(texel);
    vec2 f = fract(texel);
    vec3 c00 = sampleAt(base);
    vec3 c10 = sampleAt(base + vec2(1.0, 0.0));
    vec3 c01 = sampleAt(base + vec2(0.0, 1.0));
    vec3 c11 = sampleAt(base + vec2(1.0, 1.0));
    vec3 lower = c00 + (c10 - c00) * f.x + (c01 - c00) * f.y;
    vec3 upper = c11 + (c01 - c11) * (1.0 - f.x)
                      + (c10 - c11) * (1.0 - f.y);
    return mix(lower, upper, step(1.0, f.x + f.y));
}

void main()
{
    vec3 center = texture(sceneTex, uv).rgb;
    if (resolveMode < 0.5) {
        fragColour = vec4(center, 1.0);
        return;
    }

    vec2 texel = 1.0 / vec2(textureSize(sceneTex, 0));
    vec3 left = texture(sceneTex, uv - vec2(texel.x, 0.0)).rgb;
    vec3 right = texture(sceneTex, uv + vec2(texel.x, 0.0)).rgb;
    vec3 up = texture(sceneTex, uv - vec2(0.0, texel.y)).rgb;
    vec3 down = texture(sceneTex, uv + vec2(0.0, texel.y)).rgb;
    vec3 resolved = center;

    if (resolveMode < 1.5) {
        // PS1: preserve hard pixels; subtly truncate chroma independently.
        float y = dot(center, vec3(0.299, 0.587, 0.114));
        vec2 chroma = vec2(center.r - y, center.b - y);
        chroma = round(chroma * 31.0) / 31.0;
        resolved = vec3(y + chroma.x, y, y + chroma.y);
    } else if (resolveMode < 2.5) {
        // PS2: the GS drove interlaced NTSC (640x448 max), so its output
        // artifact is the flicker filter blending adjacent *scanlines* to stop
        // field-to-field shimmer. Vertical, not horizontal: interlacing splits
        // the image by row, so neighbouring rows are what alias against each
        // other. Canonical [1/4, 1/2, 1/4] tap.
        resolved = center * 0.5 + (up + down) * 0.25;
    } else if (resolveMode < 3.5) {
        // GameCube: Flipper's copy-out from the embedded framebuffer applies
        // antialiasing and deflicker -- both *smoothing* passes, deflicker
        // being a vertical filter. This used to add a luma sharpen, which is
        // the opposite of what the hardware did. Weighted vertically for the
        // deflicker, with a lighter horizontal term for the copy AA. Keep it
        // gentle: at 640x480 24-bit the GameCube was the crispest of the era.
        resolved = center * 0.6 + (up + down) * 0.15 + (left + right) * 0.05;
    } else if (resolveMode < 4.5) {
        // N64-style triangular three-point reconstruction.
        resolved = n64ThreePoint(uv);
    } else if (resolveMode < 5.5) {
        // Chunky 3D pixel art: local-contrast crisping at render-pixel scale.
        vec3 cross = (left + right + up + down) * 0.25;
        resolved = center + (center - cross) * 0.45;
    } else if (resolveMode < 6.5) {
        // Modern PS1: gentler crisping that retains the low-res silhouette.
        vec3 cross = (left + right + up + down) * 0.25;
        resolved = center + (center - cross) * 0.16;
    } else {
        // Dungeon: local contrast that only acts in the dark. A dark-fantasy
        // frame is mostly near-black, and a flat unsharp mask over that would
        // ring around every torch -- the brightest, most looked-at thing on
        // screen. Weighting by (1 - luma) puts the crisping where silhouettes
        // actually need separating from the black and leaves lit stone alone.
        vec3 cross = (left + right + up + down) * 0.25;
        float y = dot(center, vec3(0.299, 0.587, 0.114));
        float shadowWeight = 1.0 - smoothstep(0.05, 0.55, y);
        resolved = center + (center - cross) * (0.34 * shadowWeight);
    }
    fragColour = vec4(clamp(mix(center, resolved, resolveStrength), 0.0, 1.0),
                      1.0);
}
