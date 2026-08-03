#version 450
// Port of assets/shaders/particle.frag. That file selects one of ten looks with
// preprocessor defines and gets one Ogre fragment_program per variant; here they
// become a runtime mode, because this backend keys pipelines on blend state and
// a define-per-variant would multiply the pipeline cache for shaders that differ
// only in a handful of ALU. The bodies are otherwise unchanged.
//
// Keep in sync with ParticleMode in Renderer.cpp.
//   0 Textured  1 Atlas  2 Flame  3 Smoke  4 Rain
//   5 Block     6 Mote   7 Shard  8 Bubble 9 Wisp

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 colour;

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(push_constant) uniform ParticleConstants {
    vec4 modeScissor;  // x = mode, y = alpha scissor
} params;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec4 outNormalDepth;

void main() {
    const int mode = int(params.modeScissor.x + 0.5);
    vec4 result;

    if (mode == 1) {
        // The generated atlas is black-keyed. Luminance becomes alpha,
        // preserving soft smoke while fire/embers retain their authored colour.
        vec3 texel = texture(albedoTexture, uv).rgb;
        float keyAlpha = max(texel.r, max(texel.g, texel.b));
        vec3 tint = mix(vec3(1.0), colour.rgb, 0.55);
        result = vec4(texel * tint, keyAlpha * colour.a);
    } else if (mode == 2) {
        // Quantized tapered card: readable blocks, animated by the particle
        // system's motion/scale/rotation rather than a fragile sprite.
        vec2 p = (floor(uv * vec2(7.0, 8.0)) + 0.5) / vec2(7.0, 8.0);
        float y = clamp(p.y, 0.0, 1.0);
        float taper = mix(0.46, 0.10, y);
        float lick = sin(y * 15.0 + colour.r * 3.0) * 0.035 * y;
        float body = 1.0 - step(taper, abs(p.x - 0.5 + lick));
        // Cut a couple of grid cells from the taper. Random card rotation and
        // emitter colour ranges turn this asymmetric cluster into varied square
        // flame chunks without temporal noise or star-shaped sprites.
        float notch = step(0.72, y) * step(0.5, p.x);
        float alpha = body * (1.0 - notch) * (1.0 - smoothstep(0.90, 1.0, y));
        vec3 hot = vec3(1.0, 1.0, 0.75);
        vec3 cool = vec3(1.0, 0.16, 0.015);
        vec3 flame = mix(hot, cool, smoothstep(0.05, 0.95, y));
        result = vec4(flame * mix(vec3(1.0), colour.rgb, 0.45), alpha * colour.a);
    } else if (mode == 3) {
        // Small blocky puffs: an irregular stepped disc that expands through
        // the particle scaler. Deliberately dim so it never enters bloom.
        vec2 p = (floor(uv * 8.0) + 0.5) / 8.0 - 0.5;
        float wobble = sin((p.y + colour.r) * 13.0) * 0.045;
        float radius = length(vec2(p.x + wobble, p.y * 0.88));
        float alpha = 1.0 - smoothstep(0.29, 0.47, radius);
        vec3 smoke = colour.rgb * mix(0.72, 1.0, smoothstep(0.46, 0.0, radius));
        result = vec4(smoke, alpha * colour.a);
    } else if (mode == 4) {
        // A continuous, slightly slanted streak with tapered ends.
        float slantedX = uv.x + (uv.y - 0.5) * 0.10;
        float side = 1.0 - smoothstep(0.32, 0.49, abs(slantedX - 0.5));
        float head = smoothstep(0.0, 0.16, uv.y);
        float tail = 1.0 - smoothstep(0.72, 1.0, uv.y);
        float highlight = 1.0 - smoothstep(0.0, 0.13, abs(slantedX - 0.46));
        vec3 rain = mix(colour.rgb, vec3(0.82, 0.90, 1.0),
                        0.25 + highlight * 0.35);
        result = vec4(rain, side * head * tail * colour.a * 0.72);
    } else if (mode == 5) {
        // Embers and ash are compact pixel clusters, never sampled sprites.
        vec2 p = floor(uv * 5.0);
        float centre = step(1.0, p.x) * step(p.x, 3.0) *
                       step(1.0, p.y) * step(p.y, 3.0);
        float cornerCut = step(3.0, p.x + p.y) * step(p.x + p.y, 5.0);
        float alpha = max(centre * cornerCut,
                          step(2.0, p.x) * step(p.x, 2.0) *
                          step(0.0, p.y) * step(p.y, 1.0));
        result = vec4(colour.rgb, alpha * colour.a);
    } else if (mode == 6) {
        vec2 cell = floor(uv * 5.0);
        vec2 d = abs(cell - vec2(2.0));
        float body = 1.0 - step(2.1, d.x + d.y);
        float core = 1.0 - step(0.6, max(d.x, d.y));
        result = vec4(colour.rgb * (0.72 + core * 0.72), body * colour.a);
    } else if (mode == 7) {
        vec2 cell = floor(uv * vec2(5.0, 7.0));
        float y = cell.y;
        float halfWidth = y < 2.0 ? 0.5 : (y < 5.0 ? 1.5 : 0.5);
        float body = 1.0 - step(halfWidth + 0.1, abs(cell.x - 2.0));
        float facet = step(2.0, y) * (1.0 - step(4.1, y)) * step(2.0, cell.x);
        result = vec4(colour.rgb * (0.70 + facet * 0.62), body * colour.a);
    } else if (mode == 8) {
        vec2 cell = floor(uv * 7.0) - vec2(3.0);
        float radius2 = dot(cell, cell);
        float outer = 1.0 - step(10.5, radius2);
        float inner = 1.0 - step(4.5, radius2);
        float ring = outer * (1.0 - inner);
        float glint = step(1.5, -cell.x) * step(1.5, cell.y);
        result = vec4(colour.rgb * (0.78 + glint * 0.58),
                      max(ring, glint) * colour.a);
    } else if (mode == 9) {
        vec2 cell = floor(uv * vec2(7.0, 9.0));
        float curve = 3.0 + sin((cell.y + 1.0) * 0.72) * 1.35;
        float width = mix(1.8, 0.55, cell.y / 8.0);
        float body = 1.0 - step(width, abs(cell.x - curve));
        float head = 1.0 - step(1.25, length(cell - vec2(3.0, 6.5)));
        result = vec4(colour.rgb * (0.68 + head * 0.78),
                      max(body * 0.82, head) * colour.a);
    } else {
        result = texture(albedoTexture, uv) * colour;
    }

    if (result.a < params.modeScissor.y)
        discard;
    outColour = result;
    // Billboards contribute no edge metadata: the stylize pass would outline
    // every sprite silhouette and turn a smoke plume into a sticker. Zero is
    // exactly what its early-out tests for.
    outNormalDepth = vec4(0.0);
}
