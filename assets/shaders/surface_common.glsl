// PSX surface kernel: the presentation half of any stylised surface shader.
//
// This is the part that is the same whatever the surface *is* -- a portal, a
// forcefield, a rune panel, a pane of coloured glass. It owns:
//
//   * stepped time            stop-motion cadence, shared with the rest of the
//                             game's VFX (surfaceStepFps)
//   * a pixel grid in METRES  quantisation authored in centimetres, not in UVs,
//                             so a bigger mesh gets more pixels rather than
//                             bigger ones, and they stay square on a mesh that
//                             is not
//   * ordered dither          4x4 Bayer, applied to the value before the
//                             palette quantises it: band edges break up into
//                             pixel-art stipple rather than hard contours
//   * a 4-tone palette        dark / mid / bright / core
//   * emission                what the surface BLOOMS, separate from what
//                             colour it is (see below)
//   * slab rims               what the faces closing a thick mesh do
//
// What it does NOT own is the pattern: there is no main() here. A profile
// includes this, adds its own uniforms and its own main(), and calls these
// helpers. `engine/assets/shaders/portal_pattern.glsl` is the worked example --
// the portal is one profile of this kernel, not a shader of its own.
//
// The other half of the split, deliberately NOT built on this: liquid.frag and
// lava.frag are the scrolling family, where the look comes from sliding a
// texture rather than from a field evaluated per pixel. Two shapes of problem,
// two shaders; forcing them together would give one that fits neither.
//
// Every knob is a uniform with a material default, and the game's debug console
// pushes them live -- nothing here needs a rebuild to tune.

in vec2 surfaceUV;
in vec3 surfaceLocal;  // object space, metres
in vec3 surfaceNormal; // object space
in vec3 surfaceView;   // object space vector to the eye

// --- palette ---------------------------------------------------------------
uniform vec4 surfaceDark;   // lowest band
uniform vec4 surfaceMid;    // body
uniform vec4 surfaceBright; // lit
uniform vec4 surfaceCore;   // hottest band

// --- presentation ----------------------------------------------------------
uniform float surfaceStepFps;   // pose rate; lower = chunkier stop-motion
uniform float surfaceTexelSize; // metres per pixel; <= 0 uses the grid below
uniform float surfacePixelGrid; // fallback: cells across the mesh's height
uniform float surfaceDither;    // ordered-dither strength, 0..1
uniform float surfaceBrightness; // master gain, straight into the bloom pass

// --- the mesh's rims -------------------------------------------------------
uniform float surfaceEdgeGlow; // brightness of the thickness faces
uniform float surfaceEdgeFlow; // how fast the band travels along them
uniform float surfaceEdgeMode; // 0 continue the face, 1 own band, 2 hidden

// --- emission --------------------------------------------------------------
// What the surface BLOOMS, kept separate from what colour it IS.
//
// Bloom has no colour of its own: the post pass blurs whatever exceeds its
// threshold, in that pixel's own colour. So with the palette alone, the tones
// are doing two jobs at once -- picking the colour and deciding how hard it
// glows -- and the two cannot be tuned apart. Raising the brightness to get more
// glow also washes the palette out, and a palette authored below 1.0 cannot glow
// at all no matter what the bloom pass is set to.
//
// These three separate them. The glow is ADDED on top of the palette, so it is
// the thing that crosses 1.0 and therefore the thing that blooms, in its own
// colour: a green surface can have a white-hot core, and emission becomes one
// slider instead of four colour swatches.
uniform vec4 surfaceGlowColour;    // the colour that blooms
uniform float surfaceGlowStrength; // how far past 1.0; 0 = palette only
uniform float surfaceGlowThreshold;// which part glows (1 = the hottest only)

uniform float time;

layout(location = 0) out vec4 fragColour;

const float kTau = 6.2831853;

// The field the profile reads its pattern from, 0..1. Declared here and defined
// by the including shader, which is what lets one profile have an authored
// (textured) and an art-free (procedural) variant that cannot drift apart.
float surfaceField(vec2 uv);

vec3 surfacePalette(float value)
{
    if (value < 0.30)
        return surfaceDark.rgb;
    if (value < 0.58)
        return surfaceMid.rgb;
    if (value < 0.84)
        return surfaceBright.rgb;
    return surfaceCore.rgb;
}

// Emission. Applied to the face and the rims alike, so a thick mesh's lit edge
// blooms with the rest of it rather than staying the one dull part.
vec3 surfaceGlow(vec3 colour, float value)
{
    float hot = smoothstep(clamp(surfaceGlowThreshold, 0.0, 0.999), 1.0, value);
    return colour + surfaceGlowColour.rgb * (hot * max(surfaceGlowStrength, 0.0));
}

// Ordered 4x4 Bayer threshold, 0..1. Cheap, stable under motion, and stipples
// the palette's band edges the way an indexed-colour renderer would -- unlike
// noise dither, which crawls.
float surfaceBayer(vec2 cell)
{
    const int pattern[16] = int[16](0, 8, 2, 10,
                                    12, 4, 14, 6,
                                    3, 11, 1, 9,
                                    15, 7, 13, 5);
    int x = int(mod(cell.x, 4.0));
    int y = int(mod(cell.y, 4.0));
    return float(pattern[y * 4 + x]) / 16.0;
}

vec2 surfaceQuantize(vec2 p, float cells)
{
    return (floor(p * cells) + 0.5) / cells;
}

float surfaceSteppedTime()
{
    return floor(time * surfaceStepFps) / max(surfaceStepFps, 1.0);
}

// The mesh's size in metres, read off the mapping rather than passed in.
// surfaceLocal is metres and surfaceUV is 0..1 over the same quad, both linear,
// so the ratio of their derivatives IS the size. That is what lets a profile
// adapt to a resized mesh with nothing to update on the CPU side.
//
// Call this before any branching: two faces of the same slab can share a 2x2
// quad at a silhouette, and derivatives inside divergent control flow are
// undefined.
vec2 surfaceMeshSize()
{
    return fwidth(surfaceLocal.xz) / max(fwidth(surfaceUV), vec2(1e-6));
}

// Pixel cells across the mesh's height, from the authored texel size when there
// is one and the fixed grid otherwise.
float surfaceCells(float height)
{
    return surfaceTexelSize > 0.0 ? clamp(height / surfaceTexelSize, 8.0, 512.0)
                                  : max(surfacePixelGrid, 4.0);
}

bool surfaceIsRim()
{
    return abs(surfaceNormal.y) < 0.5;
}

// The faces closing a thick mesh, shaded as a band of their own: brightest
// mid-thickness, quantised to the same texel size as the face. `edgeMetres` is
// the rim's length -- without it the rim renders at screen resolution and the
// mesh is fringed with a dashed line of loose pixels.
vec3 surfaceRimShade(float steppedTime, float edgeMetres)
{
    // Rim UVs run along the edge (u) and across the thickness (v). Three pixels
    // across is fixed rather than derived: a slab is centimetres thick, and
    // rounding that to its own texel count gives one or two.
    const float kAcross = 3.0;
    float cells = surfaceTexelSize > 0.0
                      ? clamp(edgeMetres / surfaceTexelSize, 8.0, 512.0)
                      : max(surfacePixelGrid, 4.0);
    vec2 grid = vec2(cells, kAcross);
    vec2 cell = floor(surfaceUV * grid);
    vec2 rimUV = (cell + 0.5) / grid;

    float across = abs(rimUV.y - 0.5) * 2.0;
    float travel = rimUV.x * 2.0 + steppedTime * surfaceEdgeFlow;
    float pulse = surfaceField(vec2(travel, steppedTime * 0.08));
    float value = (1.0 - across * 0.40) * (0.68 + 0.32 * pulse);
    value *= max(surfaceEdgeGlow, 0.0);
    value += (surfaceBayer(cell) - 0.5) * surfaceDither * 0.20;
    value = clamp(value, 0.0, 1.0);
    return surfaceGlow(surfacePalette(value), value);
}

// Palette + dither + emission + master gain, i.e. everything between a
// profile's 0..1 pattern value and the pixel it writes.
vec4 surfaceResolve(float value, vec2 ditherCell)
{
    value += (surfaceBayer(ditherCell) - 0.5) * surfaceDither * 0.28;
    value = clamp(value, 0.0, 1.0);
    return vec4(surfaceGlow(surfacePalette(value), value) * surfaceBrightness,
                1.0);
}
