#version 330 core
in vec2 particleUV;
in vec4 particleColour;
uniform sampler2D albedoTex;
uniform float alphaScissor;
layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;
void main()
{
#if defined(ATLAS_PARTICLE)
    vec3 texel = texture(albedoTex, particleUV).rgb;
    // The generated atlas is black-keyed. Luminance becomes alpha, preserving
    // soft smoke while fire/poison/embers retain their authored colour.
    float keyAlpha = max(texel.r, max(texel.g, texel.b));
    vec3 tint = mix(vec3(1.0), particleColour.rgb, 0.55);
    fragColour = vec4(texel * tint, keyAlpha * particleColour.a);
#elif defined(PROCEDURAL_FLAME)
    // Quantized tapered card: Minecraft-like readable blocks, animated by the
    // particle system's motion/scale/rotation rather than a fragile sprite.
    vec2 p = (floor(particleUV * vec2(7.0, 8.0)) + 0.5) / vec2(7.0, 8.0);
    float y = clamp(p.y, 0.0, 1.0);
    float taper = mix(0.46, 0.10, y);
    float lick = sin(y * 15.0 + particleColour.r * 3.0) * 0.035 * y;
    float body = 1.0 - step(taper, abs(p.x - 0.5 + lick));
    // Cut a couple of grid cells from the taper. Random card rotation and
    // emitter colour ranges turn this asymmetric cluster into varied square
    // flame chunks without temporal noise or star-shaped sprites.
    float notch = step(0.72, y) * step(0.5, p.x);
    float alpha = body * (1.0 - notch) * (1.0 - smoothstep(0.90, 1.0, y));
    vec3 hot = vec3(1.0, 1.0, 0.75);
    vec3 cool = vec3(1.0, 0.16, 0.015);
    vec3 flame = mix(hot, cool, smoothstep(0.05, 0.95, y));
    vec3 tint = mix(vec3(1.0), particleColour.rgb, 0.45);
    fragColour = vec4(flame * tint,
                      alpha * particleColour.a);
#elif defined(PROCEDURAL_SMOKE)
    // Small blocky puffs: an irregular stepped disc that expands through the
    // particle scaler. Kept deliberately dim so it never enters bloom.
    vec2 p = (floor(particleUV * 8.0) + 0.5) / 8.0 - 0.5;
    float wobble = sin((p.y + particleColour.r) * 13.0) * 0.045;
    float radius = length(vec2(p.x + wobble, p.y * 0.88));
    float alpha = 1.0 - smoothstep(0.29, 0.47, radius);
    vec3 smoke = particleColour.rgb * mix(0.72, 1.0,
                                           smoothstep(0.46, 0.0, radius));
    fragColour = vec4(smoke, alpha * particleColour.a);
#elif defined(PROCEDURAL_RAIN)
    // A continuous, slightly slanted streak with tapered ends. Rain used to
    // select PROCEDURAL_BLOCK, stretching the ember/ash pixel mask into
    // conspicuous dashed bars.
    vec2 p = particleUV;
    float slantedX = p.x + (p.y - 0.5) * 0.10;
    float side = 1.0 - smoothstep(0.32, 0.49, abs(slantedX - 0.5));
    float head = smoothstep(0.0, 0.16, p.y);
    float tail = 1.0 - smoothstep(0.72, 1.0, p.y);
    float alpha = side * head * tail;
    float highlight = 1.0 - smoothstep(
        0.0, 0.13, abs(slantedX - 0.46));
    vec3 rain = mix(particleColour.rgb, vec3(0.82, 0.90, 1.0),
                    0.25 + highlight * 0.35);
    fragColour = vec4(rain, alpha * particleColour.a * 0.72);
#elif defined(PROCEDURAL_BLOCK)
    // Embers and ash are compact pixel clusters, never sampled star sprites.
    vec2 p = floor(particleUV * 5.0);
    float centre = step(1.0, p.x) * step(p.x, 3.0) *
                   step(1.0, p.y) * step(p.y, 3.0);
    float cornerCut = step(3.0, p.x + p.y) * step(p.x + p.y, 5.0);
    float alpha = max(centre * cornerCut,
                      step(2.0, p.x) * step(p.x, 2.0) *
                      step(0.0, p.y) * step(p.y, 1.0));
    fragColour = vec4(particleColour.rgb, alpha * particleColour.a);
#elif defined(PROCEDURAL_MOTE)
    vec2 cell = floor(particleUV * 5.0);
    vec2 d = abs(cell - vec2(2.0));
    float body = 1.0 - step(2.1, d.x + d.y);
    float core = 1.0 - step(0.6, max(d.x, d.y));
    vec3 colour = particleColour.rgb * (0.72 + core * 0.72);
    fragColour = vec4(colour, body * particleColour.a);
#elif defined(PROCEDURAL_SHARD)
    vec2 cell = floor(particleUV * vec2(5.0, 7.0));
    float y = cell.y;
    float halfWidth = y < 2.0 ? 0.5 : (y < 5.0 ? 1.5 : 0.5);
    float body = 1.0 - step(halfWidth + 0.1, abs(cell.x - 2.0));
    float facet = step(2.0, y) * (1.0 - step(4.1, y)) *
                  step(2.0, cell.x);
    vec3 colour = particleColour.rgb * (0.70 + facet * 0.62);
    fragColour = vec4(colour, body * particleColour.a);
#elif defined(PROCEDURAL_BUBBLE)
    vec2 cell = floor(particleUV * 7.0) - vec2(3.0);
    float radius2 = dot(cell, cell);
    float outer = 1.0 - step(10.5, radius2);
    float inner = 1.0 - step(4.5, radius2);
    float ring = outer * (1.0 - inner);
    float glint = step(1.5, -cell.x) * step(1.5, cell.y);
    vec3 colour = particleColour.rgb * (0.78 + glint * 0.58);
    fragColour = vec4(colour, max(ring, glint) * particleColour.a);
#elif defined(PROCEDURAL_WISP)
    vec2 cell = floor(particleUV * vec2(7.0, 9.0));
    float curve = 3.0 + sin((cell.y + 1.0) * 0.72) * 1.35;
    float width = mix(1.8, 0.55, cell.y / 8.0);
    float body = 1.0 - step(width, abs(cell.x - curve));
    float head = (1.0 - step(1.25, length(cell - vec2(3.0, 6.5))));
    vec3 colour = particleColour.rgb * (0.68 + head * 0.78);
    fragColour = vec4(colour, max(body * 0.82, head) * particleColour.a);
#else
    vec4 texel = texture(albedoTex, particleUV);
    fragColour = texel * particleColour;
#endif
    if (fragColour.a < alphaScissor)
        discard;
    // Billboards do not participate in screen-space ink/highlight metadata.
    fragNormalDepth = vec4(0.0);
}
