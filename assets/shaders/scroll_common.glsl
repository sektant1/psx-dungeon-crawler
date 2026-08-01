// PSX scrolling-surface kernel: the other half of the surface split.
//
// surface_common.glsl evaluates a field per pixel (a swirl, a fractal, a
// distance function) and quantises it. This one does the older, cheaper thing:
// it slides tiling art across the mesh and reads it. Water, slime and lava are
// that shape of problem, and forcing them through a field kernel would give one
// shader that fits neither.
//
// The technique is the classic one (cf. the Defold texture-scrolling tutorial):
// offset the UV by time and let the sampler's REPEAT address mode do the
// tiling, rather than wrapping the coordinate by hand. Two layers at different
// scales and speeds, combined, is what stops a single sliding texture reading
// as a sliding texture.
//
// One thing that tutorial glosses over and this does not: `time` grows without
// bound. `uv + speed * time` loses mantissa bits as the session runs, and a
// scrolled liquid visibly quantises into judder after some minutes. The offset
// is wrapped to one tile here before it is added, which is EXACTLY equivalent
// under a repeating sampler and numerically stable forever.

in vec2 liquidUV;

// --- palette ---------------------------------------------------------------
uniform vec4 liquidDark;
uniform vec4 liquidMid;
uniform vec4 liquidBright;

// --- presentation ----------------------------------------------------------
uniform float liquidStepFps;   // pose rate; the shared stop-motion cadence
uniform float liquidPixelGrid; // cells across the surface
uniform float liquidEmission;  // highlight boost on the brightest band

uniform float time;

layout(location = 0) out vec4 fragColour;

vec3 liquidPalette(float value)
{
    if (value < 0.38)
        return liquidDark.rgb;
    if (value < 0.76)
        return liquidMid.rgb;
    return liquidBright.rgb;
}

float liquidSteppedTime()
{
    return floor(time * liquidStepFps) / max(liquidStepFps, 1.0);
}

vec2 liquidPixelate(vec2 uv)
{
    float grid = max(liquidPixelGrid, 4.0);
    return (floor(uv * grid) + 0.5) / grid;
}

// A scroll offset wrapped to a single tile. fract() on the OFFSET, never on the
// sampling coordinate: wrapping the coordinate itself would put a hard seam
// wherever it seam-wraps, while wrapping the offset is invisible because the
// texture repeats -- and it keeps the number small enough to stay precise.
vec2 liquidScroll(vec2 flow, float steppedTime)
{
    return fract(flow * steppedTime);
}
