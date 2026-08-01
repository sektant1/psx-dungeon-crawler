#version 330 core
// Editor-only: the infinite checkerboard the material staging scene stands on.
//
// Generated from world position rather than sampled from a texture, so it stays
// sharp at any camera distance and needs no UV layout on the floor mesh.
//
// It is FILTERED, not point-sampled. A point-sampled checkerboard is the
// textbook aliasing case: past a few metres each pixel covers more than one
// square and the pattern collapses into moire crawl -- precisely where a
// preview floor spends most of its screen area, and precisely the noise that
// makes it impossible to judge a material against it. So instead of asking
// "which square is this pixel centre in", this integrates the pattern across
// the pixel's actual footprint (Inigo Quilez's analytic box filter), which
// fades honestly to flat grey in the distance instead of sparkling.

smooth in vec3 vWorldPos;
smooth in vec3 vNormalVS;
smooth in float vViewDepth;

uniform vec4 checkerColourA; // light square
uniform vec4 checkerColourB; // dark square
uniform float checkerScale;  // metres per square
uniform float farClip;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

// Integral of the 1D square wave from 0 to x, used to box-filter the pattern.
vec2 triangleWave(vec2 x)
{
    return abs(fract(x * 0.5) - 0.5) * 2.0;
}

float filteredChecker(vec2 uv)
{
    // Pixel footprint in pattern space, straight from the screen-space
    // derivatives: this is what makes the filter widen automatically as the
    // floor recedes and as it tilts away at grazing angles.
    vec2 footprint = fwidth(uv) + 1e-5;
    // Average of the square wave over [uv - w/2, uv + w/2] per axis.
    vec2 integrated =
        (triangleWave(uv + 0.5 * footprint) - triangleWave(uv - 0.5 * footprint)) /
        footprint * 0.5;
    // XOR the two axes: that is what makes a checkerboard rather than stripes.
    return 0.5 - integrated.x * integrated.y * 0.5;
}

void main()
{
    vec2 uv = vWorldPos.xz / max(checkerScale, 1e-4);
    float pattern = filteredChecker(uv);
    vec3 colour = mix(checkerColourB.rgb, checkerColourA.rgb, pattern);

    // Fade to the mean of the two squares with distance. Once the filter has
    // averaged the pattern away there is nothing left to show, and dissolving
    // into flat grey reads as depth rather than as the floor ending.
    float horizon = clamp(vViewDepth / max(farClip, 1e-3) * 3.0, 0.0, 1.0);
    vec3 mean = mix(checkerColourB.rgb, checkerColourA.rgb, 0.5);
    colour = mix(colour, mean, horizon);

    fragColour = vec4(colour, 1.0);
    // The floor still owes the post chain its surface metadata, or the stylize
    // pass reads garbage where the floor is and outlines it against nothing.
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5, vViewDepth / farClip);
}
