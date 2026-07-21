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
        // PS2: mild horizontal framebuffer resolve, avoiding full blur.
        resolved = center * 0.75 + (left + right) * 0.125;
    } else if (resolveMode < 3.5) {
        // GameCube: clean luma detail with slightly smoother chroma.
        vec3 cross = (left + right + up + down) * 0.25;
        float detail = dot(center - cross, vec3(0.299, 0.587, 0.114));
        resolved = mix(center, cross, 0.12) + vec3(detail * 0.18);
    } else if (resolveMode < 4.5) {
        // N64-style triangular three-point reconstruction.
        resolved = n64ThreePoint(uv);
    } else if (resolveMode < 5.5) {
        // Chunky 3D pixel art: local-contrast crisping at render-pixel scale.
        vec3 cross = (left + right + up + down) * 0.25;
        resolved = center + (center - cross) * 0.45;
    } else {
        // Modern PS1: gentler crisping that retains the low-res silhouette.
        vec3 cross = (left + right + up + down) * 0.25;
        resolved = center + (center - cross) * 0.16;
    }
    fragColour = vec4(clamp(mix(center, resolved, resolveStrength), 0.0, 1.0),
                      1.0);
}
