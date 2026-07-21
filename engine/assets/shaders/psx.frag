#version 330 core
// Port of shaders/psx_base.gdshaderinc fragment() for Ogre GL3Plus.
// Variants via preprocessor_defines:
//   LIT, METAL, NO_TEXTURE, ALPHA_BLEND, ALPHA_SCISSOR, LIGHT_VOLUME, BLEND_ADD,
//   RIM (requires LIT: fresnel edge sheen for glassy materials)

noperspective in vec2 vUV;
noperspective in vec4 vColour;
noperspective in vec3 vLight;
noperspective in vec3 vNormalVS;
noperspective in float vViewDepth;

#ifdef LIT
// Perspective-correct inputs for the per-pixel lighting path.
smooth in vec3 vVsPos;
smooth in vec3 vNormalSmooth;
#endif

uniform vec4 modulateColor;
#ifndef NO_TEXTURE
uniform sampler2D albedoTex;
#endif
#ifdef ALPHA_SCISSOR
uniform float alphaScissor;
#endif
#ifdef RIM
uniform vec4 rimColour;  // rgb = sRGB sheen tint, a = strength
uniform float rimPower;
#endif

// world_env.tres: fog_enabled, exponential density fog
uniform vec4 fogColour;   // scene fog colour
uniform vec4 fogParams;   // x = density (Ogre FOG_EXP)
uniform float farClip;
uniform float fogDesatBoost; // 0 = off: sink distant colour toward grey
                             // before the fog mix so props drown, not lerp

#ifdef LIT
#include <psx_lighting.glsl>
uniform float perPixelLighting; // >= 0.5: fragment lighting, else vertex vLight
#endif

layout(location = 0) out vec4 fragColour;
// MRT surface 1 (PSX/Stylized compositor): view-space normal encoded
// *0.5+0.5, alpha = linear view depth / far clip. When the scene renders
// straight to the window (no compositor) GL discards this output.
layout(location = 1) out vec4 fragNormalDepth;

// Godot shades in LINEAR space and encodes to sRGB for display. Reproduce
// that: sRGB inputs (albedo texture, source_color uniforms) are linearised,
// lighting/fog happen in linear, the result is re-encoded. Light/ambient/fog
// colours arrive already linear (converted CPU-side, where energy multiplies
// the linear colour like Godot does). Gamma 2.2 approximation.
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

void main()
{
#ifdef METAL
    // UV from view-space normal (psx_base: "Special thanks to Adam McLaughlan")
#if defined(LIT)
    // Per-pixel mode: perspective-correct normal keeps the matcap glued to
    // the surface; vertex-lit mode keeps the authentic affine swim.
    vec3 n = perPixelLighting >= 0.5 ? normalize(vNormalSmooth)
                                     : normalize(vNormalVS);
#else
    vec3 n = normalize(vNormalVS);
#endif
    vec2 texture_uv = vec2(n.x / 2.0 + 0.5, (-n.y) / 2.0 + 0.5);
#else
    vec2 texture_uv = vUV;
#endif

    // vColour (glTF COLOR_0) is already linear, like in Godot;
    // modulate_color is a `source_color` uniform -> linearise.
    vec4 color_base = vColour * vec4(toLinear(modulateColor.rgb), modulateColor.a);
    float alpha = 1.0;

#ifdef NO_TEXTURE
    vec3 albedo = color_base.rgb;
#if defined(ALPHA_BLEND) || defined(ALPHA_SCISSOR)
    alpha = color_base.a;
#endif
#else
    vec4 texture_color = texture(albedoTex, texture_uv);
    texture_color.rgb = toLinear(texture_color.rgb); // source_color sampler
    vec3 albedo = (color_base * texture_color).rgb;
#if defined(ALPHA_BLEND) || defined(ALPHA_SCISSOR)
    alpha = texture_color.a * color_base.a;
#endif
#endif

#ifdef LIGHT_VOLUME
    alpha = 1.0 - vUV.y;
#endif

#ifdef ALPHA_SCISSOR
    if (alpha < alphaScissor)
        discard;
#endif

#ifdef LIT
    vec3 lightAmt = vLight;       // vertex-lit: ambient + lambert from VS
    if (perPixelLighting >= 0.5)
    {
        // renormalize after interpolation
        lightAmt = psxComputeLight(vVsPos, normalize(vNormalSmooth));
    }
    vec3 rgb = albedo * lightAmt;
#ifdef RIM
    // Glassy fresnel edge: brightest where the surface grazes the view.
    // Perspective-correct normal/position so the sheen hugs the silhouette
    // instead of swimming with the affine interpolation. Added after
    // lighting so it reads as a specular sheen, before fog so distance
    // still swallows it.
    vec3 rimN = normalize(vNormalSmooth);
    vec3 rimV = normalize(-vVsPos);
    float rimF = pow(1.0 - clamp(dot(rimN, rimV), 0.0, 1.0), rimPower);
    // Gate by the local light level: the sheen is reflected light, so it
    // must die on shadowed/unlit faces instead of glowing through them.
    float rimLit = clamp(dot(lightAmt, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    rgb += toLinear(rimColour.rgb) * (rimF * rimColour.a * rimLit);
#endif
#else
    vec3 rgb = albedo;            // unshaded
#endif

    // Godot exponential fog: amount = 1 - exp(-dist * density)
    float fog_amount = clamp(1.0 - exp(-vViewDepth * fogParams.x), 0.0, 1.0);
#ifdef BLEND_ADD
    rgb *= (1.0 - fog_amount);    // additive blend: fade to black, never brighten
#else
    // Verdigris murk: desaturate + darken with distance so geometry sinks
    // into the fog instead of flat-lerping toward its colour.
    float sink = clamp(fog_amount * fogDesatBoost, 0.0, 1.0);
    rgb = mix(rgb, vec3(dot(rgb, vec3(0.2126, 0.7152, 0.0722))) * 0.6, sink);
    rgb = mix(rgb, fogColour.rgb, fog_amount);   // fogColour already linear
#endif

    fragColour = vec4(toSrgb(rgb), alpha);
#ifdef BLEND_ADD
    // Additive light volumes must not disturb edge data: adding zero is a no-op.
    fragNormalDepth = vec4(0.0);
#else
    // Edge-detection buffer wants geometric truth: the perspective-correct
    // normal where the lit path provides one. The noperspective vNormalVS
    // swims across big low-poly triangles (crystal shards), which punches
    // holes in the stylizer's normal-edge outlines mid-face.
#ifdef LIT
    fragNormalDepth = vec4(normalize(vNormalSmooth) * 0.5 + 0.5,
                           vViewDepth / farClip);
#else
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5,
                           vViewDepth / farClip);
#endif
#endif
}
