#version 450
// The stylised-surface family: water, slime and lava. Ports
// assets/shaders/liquid.frag + scroll_common.glsl + lava.frag.
//
// Ogre compiles one fragment_program per profile; here they are a runtime mode,
// for the same reason the particle variants are -- the RHI keys pipelines on
// blend/depth state, and a program per profile would multiply the cache for
// shaders that share a vertex layout and differ only in ALU.
//
// The scrolling half of the surface split: the look comes from sliding tiling
// art across the mesh rather than from a field evaluated per pixel. The classic
// technique -- offset the UV by time and let the sampler's REPEAT mode tile --
// with one thing the tutorials gloss over: `time` grows without bound, so
// `uv + speed * time` loses mantissa bits as a session runs and a scrolled
// liquid visibly quantises into judder after some minutes. The offset is
// wrapped to one tile before it is added, which is exactly equivalent under a
// repeating sampler and numerically stable forever.
//
// The portal is the other half of the split: a field evaluated per pixel rather
// than art slid across the mesh (surface_common.glsl + portal_pattern.glsl).
// Both live here because they share the palette, dither and emission kernel.
//
// Keep in sync with SurfaceMode in Renderer.cpp: 1 Liquid, 2 Lava, 3 Portal.

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 surfaceLocal;  // object space, metres
layout(location = 2) in vec3 surfaceNormal; // object space
layout(location = 3) in vec3 surfaceView;   // object space, surface -> eye
layout(location = 4) in vec3 viewNormal;
layout(location = 5) in float viewDepth;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec4 outNormalDepth;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 cameraPositionAndLightCount;
    vec4 ambient;
    vec4 fogColourDensity;
    vec4 clipParams;
    mat4 lightViewProjection;
    vec4 shadowParams;        // enabled, bias, strength, texel
    vec4 lightPositionRange[16];
    vec4 lightColourType[16];
} scene;

layout(set = 1, binding = 0) uniform sampler2D surfaceTexture;

// Shared verbatim with the mesh path: this profile uses the same vertex stage,
// so uvTransform in particular must keep its meaning.
layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 tintOpacity;
    vec4 rimColourStrength;
    vec4 surfaceParams;
    vec4 uvTransform;
} drawData;

// Everything a surface profile needs beyond that. Per material rather than per
// draw, and far past what is left of the 128-byte push range once the model
// matrix is in it, so it rides in its own block.
layout(set = 0, binding = 1, std140) uniform SurfaceUniforms {
    vec4 paletteA;   // liquidDark   / lavaDark   / surfaceDark
    vec4 paletteB;   // liquidMid    / lavaCrust  / surfaceMid
    vec4 paletteC;   // liquidBright / lavaHot    / surfaceBright
    vec4 paletteD;   // lavaCore                  / surfaceCore
    vec4 glowColour; // surfaceGlowColour
    vec4 flowA;      // xy = liquidFlowA, zw = liquidFlowB
    vec4 tuning;     // x stepFps, y pixelGrid, z emission, w flowSpeed
    vec4 modeTime;   // x = mode, y = time
    vec4 present;    // texelSize, dither, brightness, glowStrength
    vec4 rims;       // edgeGlow, edgeFlow, edgeMode, glowThreshold
    vec4 motion;     // portal: flowSpeed, swirlSpeed, twist, arms
    vec4 shapeA;     // portal: armWidth, depthScale, parallax, fieldWeight
    vec4 shapeB;     // portal: coreRadius, coreBoost, rimRadius, rimWidth
    vec4 shapeC;     // portal: rimIntensity, edgeFade
} surface;

const float kTau = 6.2831853;

// --- shared presentation ---------------------------------------------------

float steppedTime()
{
    // Stop-motion cadence, shared with the rest of the game's VFX.
    float fps = surface.modeTime.x > 0.0 ? surface.tuning.x : 1.0;
    return floor(surface.modeTime.y * fps) / max(fps, 1.0);
}

vec2 pixelate(vec2 coord)
{
    float grid = max(surface.tuning.y, 4.0);
    return (floor(coord * grid) + 0.5) / grid;
}

// A scroll offset wrapped to a single tile. fract() on the OFFSET, never on the
// sampling coordinate: wrapping the coordinate itself would put a hard seam
// wherever it wraps, while wrapping the offset is invisible because the texture
// repeats -- and it keeps the number small enough to stay precise.
vec2 scrollOffset(vec2 flow, float t) { return fract(flow * t); }

// --- lava field ------------------------------------------------------------

float lavaHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float lavaNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    float a = lavaHash(cell);
    float b = lavaHash(cell + vec2(1.0, 0.0));
    float c = lavaHash(cell + vec2(0.0, 1.0));
    float d = lavaHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, blend.x), mix(c, d, blend.x), blend.y);
}

float lavaFbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    mat2 octaveTransform = mat2(1.6, 1.2, -1.2, 1.6);
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * lavaNoise(p);
        p = octaveTransform * p;
        amplitude *= 0.5;
    }
    return value;
}

vec2 domainWarp(vec2 p, float t)
{
    vec2 flow = vec2(lavaFbm(p + vec2(t * 0.31, 1.7)),
                     lavaFbm(p + vec2(-2.4, t * -0.27)));
    return p + (flow - 0.5) * 1.35;
}

// --- surface kernel (surface_common.glsl) ----------------------------------
// Everything between a profile's 0..1 pattern value and the pixel it writes.

vec3 surfacePalette(float value)
{
    if (value < 0.30) return surface.paletteA.rgb;
    if (value < 0.58) return surface.paletteB.rgb;
    if (value < 0.84) return surface.paletteC.rgb;
    return surface.paletteD.rgb;
}

// Emission, kept separate from what colour the surface IS. Bloom has no colour
// of its own -- it blurs whatever exceeds its threshold, in that pixel's own
// colour -- so with the palette alone the tones would be picking the colour and
// deciding the glow at once. Added on top, this is the thing that crosses 1.0
// and therefore the thing that blooms: a green surface can have a white core.
vec3 surfaceGlow(vec3 colour, float value)
{
    float hot = smoothstep(clamp(surface.rims.w, 0.0, 0.999), 1.0, value);
    return colour + surface.glowColour.rgb * (hot * max(surface.present.w, 0.0));
}

// Ordered 4x4 Bayer, 0..1. Stable under motion and stipples the palette's band
// edges the way an indexed-colour renderer would; noise dither crawls.
float surfaceBayer(vec2 cell)
{
    const int pattern[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6,
                                    3, 11, 1, 9, 15, 7, 13, 5);
    int x = int(mod(cell.x, 4.0));
    int y = int(mod(cell.y, 4.0));
    return float(pattern[y * 4 + x]) / 16.0;
}

vec2 surfaceQuantize(vec2 p, float cells)
{
    return (floor(p * cells) + 0.5) / cells;
}

// The mesh's size in metres, read off the mapping rather than passed in:
// surfaceLocal is metres and uv is 0..1 over the same quad, both linear, so the
// ratio of their derivatives IS the size. Call before any branching -- two
// faces of a slab can share a 2x2 quad at a silhouette, and derivatives inside
// divergent control flow are undefined.
vec2 surfaceMeshSize()
{
    return fwidth(surfaceLocal.xz) / max(fwidth(uv), vec2(1e-6));
}

// Pixel cells across the mesh's height: authored texel size when there is one,
// the fixed grid otherwise. Quantisation in METRES, so a bigger mesh gets more
// pixels rather than bigger ones.
float surfaceCells(float height)
{
    return surface.present.x > 0.0
               ? clamp(height / surface.present.x, 8.0, 512.0)
               : max(surface.tuning.y, 4.0);
}

bool surfaceIsRim() { return abs(surfaceNormal.y) < 0.5; }

vec4 surfaceResolve(float value, vec2 ditherCell)
{
    value += (surfaceBayer(ditherCell) - 0.5) * surface.present.y * 0.28;
    value = clamp(value, 0.0, 1.0);
    return vec4(surfaceGlow(surfacePalette(value), value) * surface.present.z,
                1.0);
}

// The field a profile reads its pattern from, 0..1. The portal's authored
// variant samples the flow texture; both axes wrap, so the texture must tile.
float surfaceField(vec2 coord) { return texture(surfaceTexture, coord).r; }

// The faces closing a thick mesh, shaded as a band of their own: brightest
// mid-thickness, quantised to the same texel size as the face. Without the edge
// length the rim renders at screen resolution and the mesh is fringed with a
// dashed line of loose pixels.
vec3 surfaceRimShade(float steppedTime, float edgeMetres)
{
    // Three pixels across is fixed rather than derived: a slab is centimetres
    // thick, and rounding that to its own texel count gives one or two.
    const float kAcross = 3.0;
    float cells = surface.present.x > 0.0
                      ? clamp(edgeMetres / surface.present.x, 8.0, 512.0)
                      : max(surface.tuning.y, 4.0);
    vec2 grid = vec2(cells, kAcross);
    vec2 cell = floor(uv * grid);
    vec2 rimUV = (cell + 0.5) / grid;

    float across = abs(rimUV.y - 0.5) * 2.0;
    float travel = rimUV.x * 2.0 + steppedTime * surface.rims.y;
    float pulse = surfaceField(vec2(travel, steppedTime * 0.08));
    float value = (1.0 - across * 0.40) * (0.68 + 0.32 * pulse);
    value *= max(surface.rims.x, 0.0);
    value += (surfaceBayer(cell) - 0.5) * surface.present.y * 0.20;
    return surfaceGlow(surfacePalette(clamp(value, 0.0, 1.0)),
                       clamp(value, 0.0, 1.0));
}

void main() {
    const int mode = int(surface.modeTime.x + 0.5);
    const float t = steppedTime();

    if (mode == 3) {
        // --- portal (portal_pattern.glsl) ---------------------------------
        // Derivatives first, before any branching, for the reason documented
        // on surfaceMeshSize().
        vec2 meshSize = surfaceMeshSize();
        float height = max(meshSize.y, 1e-3);
        // The rims run along X or along Z depending on which of the four it
        // is, so their length is the distance travelled in XZ per unit u
        // rather than one axis of the mapping.
        float edgeMetres =
            length(vec2(fwidth(surfaceLocal.x), fwidth(surfaceLocal.z))) /
            max(fwidth(uv.x), 1e-6);

        bool rim = surfaceIsRim();
        if (rim && surface.rims.z >= 1.5)
            // discard, not alpha 0: the pass writes depth, so an
            // invisible-but-written rim would punch a hole in what is behind.
            discard;
        if (rim && surface.rims.z >= 0.5) {
            outColour = vec4(surfaceRimShade(t, edgeMetres) *
                                 surface.present.z, 1.0);
            outNormalDepth = vec4(0.0);
            return;
        }

        // Field position from OBJECT space, which is what lets the rims simply
        // continue the front pattern: the four rim faces share the face's XZ
        // footprint and differ only in Y.
        vec2 p = surfaceLocal.xz / height;
        float cells = surfaceCells(height);

        // Parallax: metres of sideways shift per metre of depth. The clamp
        // stops a grazing view shearing the layers off the mesh entirely.
        vec3 viewDir = normalize(surfaceView);
        vec2 parallax = -vec2(viewDir.x, viewDir.z) /
                        max(abs(viewDir.y), 0.25) *
                        (surface.shapeA.z / height);

        vec2 q = surfaceQuantize(p, cells);
        float radius = max(length(q), 0.004);
        float angle = atan(q.y, q.x);
        float arms = max(floor(surface.motion.w + 0.5), 1.0);

        // Three depth layers, each further in, slower to turn and further
        // sheared by the eye, weighted 1, 1/2, 1/3 so the nearest carries the
        // read. Log-polar: equal steps along the sampling axis are equal
        // *ratios* of radius, so a layer scrolling at a constant rate reads as
        // an endless fall inward rather than a texture sliding across a quad.
        float layers = 0.0;
        float weights = 0.0;
        for (int i = 0; i < 3; ++i) {
            float li = float(i);
            vec2 lp = surfaceQuantize(p + parallax * li, cells);
            float lr = max(length(lp), 0.004);
            float la = atan(lp.y, lp.x);
            // Angular coordinate scaled by a whole number of arms, so the wrap
            // at +-pi lands on a texture repeat instead of a seam.
            vec2 coord = vec2(
                la * (arms + li) / kTau + surface.motion.z / max(lr, 0.05) +
                    t * surface.motion.y * (1.0 + li * 0.25),
                log(lr) * surface.shapeA.y * (1.0 + li * 0.35) +
                    t * surface.motion.x * (1.0 + li * 0.40));
            float w = 1.0 / (1.0 + li);
            layers += surfaceField(coord) * w;
            weights += w;
        }
        float field = layers / max(weights, 1e-4);

        // The analytic spiral, kept as a blend target so the shape stays
        // readable at low resolution even when the flow art is noisy.
        float spiral = 0.5 + 0.5 * sin(angle * arms -
                                       log(radius) * surface.shapeA.y * kTau +
                                       t * surface.motion.y * kTau);
        float value = mix(spiral, field, clamp(surface.shapeA.w, 0.0, 1.0));

        // How much of each turn is lit arm rather than dark gap. The raw sine
        // is an even split and the palette thresholds are fixed, so without
        // this the only way to fatten the arms is to recolour the palette --
        // changing the portal's colour to fix its shape. 0.5 is a no-op.
        float armWidth = clamp(surface.shapeA.x, 0.02, 0.98);
        value = pow(clamp(value, 0.0, 1.0), (1.0 - armWidth) / armWidth);

        // Event horizon: the middle burns out into the core tone.
        float core = 1.0 - smoothstep(0.0, max(surface.shapeB.x, 0.001),
                                      radius);
        value = mix(value, 1.0, core * clamp(surface.shapeB.y, 0.0, 1.0));

        // The light comes from deep inside, so the field dims as it climbs out
        // and is cut at the containment ring. That cut is what makes a
        // rectangular slab read as a round maw.
        float fade = max(surface.shapeC.y, 0.001);
        float inside = smoothstep(surface.shapeB.z + fade,
                                  surface.shapeB.z - fade, radius);
        // Gently: the palette's first band is the void between the arms, so a
        // steep gradient would drop the whole outer disc into it and the portal
        // would read as an empty ring instead of a swirl.
        float depthGain = mix(0.78, 1.10,
                              smoothstep(surface.shapeB.z, 0.0, radius));
        // Not to zero: outside the ring the swirl keeps a faint spill, which
        // stops the rest of the slab reading as a flat painted panel.
        float mask = mix(0.55, 1.0, inside);
        // The slab is wider than it is tall, so the round cut alone can leave
        // the side corners lit; the quad's own border closes them.
        vec2 border = abs(surfaceLocal.xz) / max(meshSize * 0.5, vec2(1e-4));
        mask *= 1.0 - smoothstep(1.0 - fade, 1.0, max(border.x, border.y));
        value *= depthGain * mask;

        // Containment ring: added after the cut, since it IS the cut's edge.
        float ring = 1.0 - smoothstep(0.0, max(surface.shapeB.w, 0.001),
                                      abs(radius - surface.shapeB.z));
        value = max(value, ring * surface.shapeC.x);

        outColour = surfaceResolve(value, floor(q * cells));
        // A portal is a hole, not a surface: no edge metadata, or the stylizer
        // would ink the quad's outline across the maw.
        outNormalDepth = vec4(0.0);
        return;
    }

    const vec2 pixelUV = pixelate(uv);
    vec3 rgb;

    if (mode == 2) {
        // Lava: a domain-warped fbm, banded into four tones. The texture is
        // only a fine detail term on top of the field.
        vec2 flowing = pixelUV * 5.0;
        flowing += vec2(t * surface.tuning.w, -t * surface.tuning.w * 0.63);
        vec2 warped = domainWarp(flowing, t);

        float broadFlow = lavaFbm(warped);
        float veins = abs(lavaFbm(warped * 1.85 + broadFlow) - 0.5) * 2.0;
        float detail =
            texture(surfaceTexture, pixelUV * 1.4 + vec2(t * 0.018, 0.0)).r;
        float heat = clamp((1.0 - veins) * 0.72 + broadFlow * 0.20 +
                           detail * 0.08, 0.0, 1.0);
        heat = floor(heat * 4.0) / 3.0;
        rgb = heat < 0.34   ? surface.paletteA.rgb
              : heat < 0.58 ? surface.paletteB.rgb
              : heat < 0.82 ? surface.paletteC.rgb
                            : surface.paletteD.rgb;
    } else {
        // --- water / slime -------------------------------------------------
        // Rebuilt as a wave FIELD rather than two sliding texture layers.
        //
        // The old version scrolled one tiling image twice and cross-faded it.
        // That has three problems a liquid cannot hide: the texture's own
        // features travel bodily across the surface (real water moves up and
        // down, not sideways), the tile repeat is visible on anything larger
        // than the tile, and the whole look depends on art that has to be
        // authored per liquid. This evaluates height directly, so there is no
        // tile, no bodily drift, and a new liquid is a palette.
        //
        // Four directional waves at incommensurable angles and frequencies:
        // enough to never visibly repeat, few enough to stay cheap. Summing
        // sines is the classic cheap water height field, and it is the right
        // one here because the palette quantises the result to a handful of
        // bands anyway -- spending more on the height buys nothing visible.
        vec2 w = pixelUV * 6.2831853;
        // flowA.xy is the current: it biases the wave phase so a river reads
        // as flowing one way without translating the pattern.
        vec2 drift = surface.flowA.xy * t * 6.2831853;
        float height = 0.0;
        height += sin(dot(w, vec2( 1.00,  0.17)) + drift.x + t * 1.7) * 0.50;
        height += sin(dot(w, vec2(-0.31,  1.00)) + drift.y + t * 1.3) * 0.32;
        height += sin(dot(w, vec2( 1.61, -1.13)) * 1.9 + t * 2.6) * 0.13;
        height += sin(dot(w, vec2(-1.27, -0.74)) * 3.1 + t * 3.7) * 0.05;
        // 0..1, with the midpoint at the still level.
        float value = clamp(height * 0.5 + 0.5, 0.0, 1.0);

        // Crest slope, from the analytic derivative of the dominant wave. A
        // crest is where the surface turns over, and that -- not the height --
        // is where light catches, so the sparkle rides the slope.
        float slope = abs(cos(dot(w, vec2(1.00, 0.17)) + drift.x + t * 1.7));

        rgb = surfacePalette(value);
        // Quantised glints: the top of a crest picks up a hard specular chip.
        // Stepped rather than smooth so it reads as a pixel-art sparkle and
        // survives the palette banding instead of being averaged away.
        float glint = step(0.82, value) * step(0.55, slope);
        rgb = mix(rgb, surface.paletteC.rgb, glint * 0.65);
        // Emission rides the crests, so a slime glows where it heaves and the
        // glow is what crosses 1.0 into the bloom threshold.
        rgb += surface.paletteC.rgb * (glint * surface.tuning.z);
        // Dither before the caller writes it: same 4x4 Bayer the rest of the
        // family uses, so the band edges stipple instead of drawing contours.
        rgb += (surfaceBayer(floor(uv * max(surface.tuning.y, 4.0))) - 0.5) *
               surface.present.y * 0.06;
    }

    outColour = vec4(rgb * drawData.tintOpacity.rgb, drawData.tintOpacity.a);
    // These surfaces are their own light. They write real normal/depth so the
    // stylizer still outlines a lava pool against the floor, but the depth sign
    // is negative -- the DUNGEON_NO_HIGHLIGHT marker -- because an edge
    // highlight wash over a self-lit surface reads as a smear.
    float encodedDepth = viewDepth / max(scene.clipParams.y, 1e-4);
    outNormalDepth = vec4(normalize(viewNormal) * 0.5 + 0.5, -encodedDepth);
}
