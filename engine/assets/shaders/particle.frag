#version 330 core
in vec2 particleUV;
in vec4 particleColour;
uniform sampler2D albedoTex;
uniform float alphaScissor;
layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;
void main()
{
#if defined(PROCEDURAL_FLAME)
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
#else
    vec4 texel = texture(albedoTex, particleUV);
    fragColour = texel * particleColour;
#endif
    if (fragColour.a < alphaScissor)
        discard;
    // Billboards do not participate in screen-space ink/highlight metadata.
    fragNormalDepth = vec4(0.0);
}
