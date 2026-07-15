#version 330 core
// Port of shaders/psx_base.gdshaderinc fragment() for Ogre GL3Plus.
// Variants via preprocessor_defines:
//   LIT, METAL, NO_TEXTURE, ALPHA_BLEND, ALPHA_SCISSOR, LIGHT_VOLUME, BLEND_ADD

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

// world_env.tres: fog_enabled, exponential density fog
uniform vec4 fogColour;   // scene fog colour
uniform vec4 fogParams;   // x = density (Ogre FOG_EXP)
uniform float farClip;

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
#else
    vec3 rgb = albedo;            // unshaded
#endif

    // Godot exponential fog: amount = 1 - exp(-dist * density)
    float fog_amount = clamp(1.0 - exp(-vViewDepth * fogParams.x), 0.0, 1.0);
#ifdef BLEND_ADD
    rgb *= (1.0 - fog_amount);    // additive blend: fade to black, never brighten
#else
    rgb = mix(rgb, fogColour.rgb, fog_amount);   // fogColour already linear
#endif

    fragColour = vec4(toSrgb(rgb), alpha);
#ifdef BLEND_ADD
    // Additive light volumes must not disturb edge data: adding zero is a no-op.
    fragNormalDepth = vec4(0.0);
#else
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5,
                           vViewDepth / farClip);
#endif
}
