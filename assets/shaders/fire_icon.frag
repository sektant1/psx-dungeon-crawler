#version 330 core
// Procedural animated fire icon: the demonstration subject for the material
// stage's quad preview mode. Nothing in the game references it, so it cannot
// move the frozen rendered image.
//
// It is written the way a pixel-art icon is authored rather than the way a
// realistic flame is simulated: the UV is snapped to a coarse cell grid and the
// clock is snapped to a low frame rate, so the result reads as a hand-drawn
// looping sprite instead of a smooth analytic gradient. Colours come from a
// four-entry ramp with hard thresholds, which is what gives it a poster-like
// silhouette at any zoom.
//
// GLSL 330 note: `const` here requires a compile-time-constant initialiser,
// unlike C++. Anything derived from a uniform must be a plain local.

in vec2 uv;
uniform float time;
uniform vec4 fireDark;
uniform vec4 fireMid;
uniform vec4 fireHot;
uniform vec4 fireCore;
uniform float firePixelGrid; // cells across the icon
uniform float fireStepFps;   // animation quantisation, in frames per second
uniform float fireRiseSpeed;
layout(location = 0) out vec4 fragColour;

float fireHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float fireNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    float a = fireHash(cell);
    float b = fireHash(cell + vec2(1.0, 0.0));
    float c = fireHash(cell + vec2(0.0, 1.0));
    float d = fireHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, blend.x), mix(c, d, blend.x), blend.y);
}

float fireFbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 3; ++octave) {
        value += amplitude * fireNoise(p);
        p *= 2.03;
        amplitude *= 0.5;
    }
    return value;
}

void main()
{
    // Snap to the icon's cell grid first: every later computation then happens
    // once per visible pixel of the sprite, which is what keeps the edges hard.
    float grid = max(firePixelGrid, 1.0);
    vec2 cell = (floor(uv * grid) + 0.5) / grid;

    // Snap the clock too. A flame animating at the display refresh rate looks
    // like fog; at ten frames per second it looks drawn.
    float fps = max(fireStepFps, 1.0);
    float t = floor(time * fps) / fps;

    // Fuel rises, so the noise field scrolls downward in UV space (v = 0 is the
    // bottom of the quad as the plane primitive lays out its UVs).
    vec2 flow = vec2(0.0, -t * fireRiseSpeed);
    float n = fireFbm(cell * vec2(4.0, 6.0) + flow * 6.0);

    // Height falloff plus a horizontal taper gives the teardrop silhouette; the
    // noise then eats into it, which is what makes the tongues flicker.
    float height = 1.0 - cell.y;
    float taper = 1.0 - smoothstep(0.0, 0.55, abs(cell.x - 0.5));
    float heat = height * taper * (0.55 + 0.9 * n);

    // Hard thresholds rather than a smooth mix: four bands, like an indexed
    // palette. Below the lowest band the icon is transparent, so it composites
    // as a sprite against whatever backdrop the stage is using.
    if (heat < 0.28)
        discard;
    vec4 colour = fireDark;
    if (heat > 0.42)
        colour = fireMid;
    if (heat > 0.58)
        colour = fireHot;
    if (heat > 0.76)
        colour = fireCore;
    fragColour = vec4(colour.rgb, 1.0);
}
