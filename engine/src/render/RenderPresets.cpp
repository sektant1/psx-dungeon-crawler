#include "RenderPresets.h"
#include "eng/RenderPresetInfo.h"
#include "eng/Log.h"
#include "eng/Renderer.h"
#include <cstring>

namespace eng {

// The single place the name -> id mapping is written down. Both the lookup
// below and every UI that lists presets read this, so neither can drift.
const std::vector<RenderPresetInfo>& renderPresets()
{
    static const std::vector<RenderPresetInfo> kPresets = {
        // Era emulations, then the stylised profiles, then the game's own look,
        // then the mood profiles -- which are era profiles wearing a palette.
        {"ps1", 1},      {"ps2", 2},        {"gamecube", 3},     {"n64", 4},
        {"pixel-3d", 5}, {"modern-ps1", 6}, {"dungeon", 7},
        {"psx-horror", 8}, {"fire-dimension", 9}, {"poison-swamp", 10},
    };
    return kPresets;
}

int renderPresetFromName(const char* name)
{
    if (!name) return -1;
    if (!std::strcmp(name, "default")) return kDefaultRenderPreset;
    for (const RenderPresetInfo& p : renderPresets())
        if (!std::strcmp(name, p.name)) return p.id;
    return -1;
}

const char* renderPresetName(int id)
{
    for (const RenderPresetInfo& p : renderPresets())
        if (p.id == id) return p.name;
    return "unknown";
}

RenderPresetBloom renderPresetBloom(int id)
{
    const RenderPresetValues v = renderPresetValues(id);
    return {v.bloom, v.bloomThreshold, v.bloomIntensity};
}

int renderPresetFromArgs(int argc, const char* const* argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (!argv[i] || std::strcmp(argv[i], "--render-preset"))
            continue;
        const int id = renderPresetFromName(argv[i + 1]);
        if (id > 0)
            return id;
        log::warn("Unknown --render-preset '%s'; using the default profile "
                  "instead", argv[i + 1] ? argv[i + 1] : "");
        return 0;
    }
    return 0;
}

RenderPresetValues renderPresetValues(int preset)
{
    RenderPresetValues v;
    switch (preset) {
    case 1: // PS1 -- tuned against TDM's "PSX rendering" (shadertoy Mt3Gz2),
            // which fakes the console's GTE exactly: integer screen-space
            // vertices, screen-space (affine) UV interpolation, point-sampled
            // texels, no colour grade.
        v.pixelSize = 3; v.perPixel = false; v.bloom = false;
        v.hardwareResolveMode = resolve::kPs1Chroma;
        // psx.vert snaps NDC to floor(512,448 * p), and NDC spans [-1,1], so
        // the grid is 2*floor(512*p) steps across the screen. The old 0.50
        // gave 512x448 steps over a 320x240 target -- FINER than a render
        // pixel, i.e. the wobble was mathematically invisible. TDM quantizes
        // to a grid ~1.5-2 render pixels coarse; 0.156 -> 158x138 steps over
        // 320x240 reproduces that.
        v.precisionMultiplier = 0.156f;
        // The PS1 had no posterized lighting term. Its light was Gouraud, and
        // what banded the result was the 15-bit framebuffer -- which this
        // profile already models, one pass later, with a real ordered dither.
        // Banding the light as well quantized the same signal twice and put
        // rings on curved surfaces that no PS1 game had.
        v.bandedLightingEnabled = false; v.stepSoftness = 0.07f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        // TDM applies no grade at all; keep only enough to seat the palette in
        // this game's dungeon, not the moonlit wash the profile used to carry.
        v.gradeDesaturate = 0.02f; v.gradeContrast = 1.02f;
        v.gradeShadow = {0.13f, 0.13f, 0.16f}; v.gradeMid = {0.64f, 0.62f, 0.58f};
        v.gradeSaturation = 1.0f; v.gradeTintStrength = 0.015f;
        v.gradeBlackLift = 0.02f; v.vignetteStrength = 0.03f;
        v.vignetteColor = {0.24f, 0.23f, 0.28f};
        v.ditherBanding = 0.040f; // 5-bit colDepth 31 + ordered dither = real
                                  // PS1 output; TDM skips both.
        // The GPU dithered every pixel it wrote, dark ones included -- the
        // pattern in a dim room is one of the most recognisable things about
        // the console. The 0.20 default faded it out of exactly that range,
        // which is a modern anti-shimmer measure, not a PS1 one.
        v.ditherDarkFade = 0.05f;
        // Mode 1 truncates chroma, but dither.frag already quantizes to 15-bit
        // one pass later, so most of this was doing the same job twice.
        v.hardwareResolveStrength = 0.30f;
        v.affineAmount = 1.0f; // TDM interpolates UV purely in screen space
        // Hardware fog was a straight lerp toward the fog colour, with no
        // saturation term anywhere in the pipeline to do anything else.
        v.fogDesatBoost = 0.0f;
        break;
    case 2: // PS2
        // The Graphics Synthesizer topped out at 640x448 on NTSC. Rendering at
        // the full window was both wrong and the most expensive profile here --
        // at 1080p that is 7x the fragments of the real framebuffer, for every
        // pass in the chain.
        v.targetWidth = 640; v.targetHeight = 448;
        v.pixelSize = 2; // divisor fallback if the absolute target is turned off
        v.perPixel = true;
        v.hardwareResolveMode = resolve::kPs2Flicker;
        v.bandedLightingEnabled = false; v.stepSoftness = 0.35f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.bloomThreshold = 0.84f; v.bloomIntensity = 0.25f;
        // The one modern liberty this profile takes, and it is one the era took
        // too: the GS blended in 32-bit with no colour cost, so late PS2 titles
        // (Silent Hill 3, God of War) leaned on additive glow. Bilinear
        // upsample rather than pixel-snapped, because nothing on this console
        // was on a pixel grid.
        v.bloomPixelSnap = 0.0f;
        v.gradeDesaturate = 0.015f; v.gradeContrast = 1.0f;
        v.gradeSaturation = 1.05f; // GS output ran hot, not neutral
        v.gradeTintStrength = 0.01f; v.gradeBlackLift = 0.025f;
        v.vignetteStrength = 0.025f;
        v.fogDesatBoost = 0.0f; // fixed-function fog: a lerp, nothing more
        // The GS ran 16-, 24- and 32-bit modes, and most 3D titles took 24 or
        // 32. colDepth 63 was 6-bit, which is no mode the hardware had; the era
        // character comes from the resolution and the interlace filter instead.
        v.colDepth = 255.0f; v.ditherBanding = 0.0f;
        v.hardwareResolveStrength = 0.45f; // vertical flicker filter, mode 2
        break;
    case 3: // GameCube
        // Flipper output 640x480 at 24-bit. Same story as PS2: this was the
        // other full-window profile, and the heaviest.
        v.targetWidth = 640; v.targetHeight = 480;
        v.pixelSize = 2; // divisor fallback if the absolute target is turned off
        v.perPixel = true; v.bandedLightingEnabled = false;
        v.hardwareResolveMode = resolve::kGamecubeCopy;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        // Flipper's TEV could fold several passes into one, and the console's
        // signature titles spent that on glow (Metroid Prime's scan visor and
        // lava, Wind Waker's sky). Bloom is the least anachronistic thing in
        // this profile, so it is the one that carries the era's character.
        v.bloomThreshold = 0.72f; v.bloomIntensity = 0.44f;
        v.bloomPixelSnap = 0.0f;
        v.gradeDesaturate = 0.0f; v.gradeContrast = 1.01f;
        v.gradeSaturation = 1.12f; v.gradeTintStrength = 0.0f;
        v.gradeBlackLift = 0.020f; v.vignetteStrength = 0.015f;
        v.fogDesatBoost = 0.0f;
        // True 24-bit: 255 makes the quantize an exact 8-bit no-op. The old 127
        // was 7-bit, banding an output that never banded.
        v.colDepth = 255.0f; v.ditherBanding = 0.0f;
        // Mode 3 is now the copy-out AA + deflicker smooth it always should
        // have been, so this is a blur weight, not a sharpen weight -- and the
        // GameCube was the crispest console of the three.
        v.hardwareResolveStrength = 0.30f;
        break;
    case 4: // N64
        // The VI could do 640x480, but games "typically used lower resolutions
        // to conserve resources" -- 320x240 was the norm. pixelSize 3 rather
        // than an absolute target: 320x240 divides a 960x720 or 1920x1080
        // window cleanly, and the divisor keeps that true if either changes.
        v.pixelSize = 3; v.perPixel = false; v.bloom = false;
        v.hardwareResolveMode = resolve::kN64ThreePoint;
        // Deliberately above the render-pixel grid: the N64's rasterizer had
        // subpixel-accurate vertices, so it never wobbled the way a PS1 did.
        // The era look comes from the three-point filter and the blur, below.
        v.precisionMultiplier = 0.72f; v.bandedLightingEnabled = false;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.gradeDesaturate = 0.035f; v.gradeContrast = 0.98f;
        v.gradeSaturation = 0.96f; v.gradeTintStrength = 0.02f;
        v.gradeBlackLift = 0.040f; v.vignetteStrength = 0.025f;
        // 5-bit + a real dither: RGBA5551 was the common framebuffer choice and
        // the RDP's blender dithered it. The old 0.004 had the depth right and
        // the dither all but switched off, so it banded instead of dithering.
        v.colDepth = 31.0f; v.ditherBanding = 0.030f;
        v.hardwareResolveStrength = 0.92f; // N64: soft bilinear/AA blur
        // The RDP mapped textures with perspective correction -- that was one of
        // its headline advantages over the PS1, so warping them was backwards.
        // Its softness comes from the three-point filter (mode 4) and the VI.
        v.affineAmount = 0.0f;
        v.gradeShadow = {0.14f, 0.12f, 0.10f}; // warm murk vs PS1's cool
        v.gradeMid = {0.70f, 0.64f, 0.56f};
        // The RDP dithered its 5-bit output the way the PS1 GPU did, and for
        // the same reason: it is what stops long shallow gradients banding.
        v.ditherDarkFade = 0.08f;
        // Fog on this console was not atmosphere, it was the draw distance --
        // a hard lerp to the fog colour a few metres short of the far plane.
        v.fogDesatBoost = 0.0f;
        break;
    case 5: // pixel-3d -- David Holland's 3D pixel art method
            // (davidhol.land/articles/3d-pixel-art-rendering): 1px cross-kernel
            // outlines off the depth buffer, edge highlights on convex folds
            // only, flat toon lighting, and a clean nearest upscale.
        // 480x360 at 960x720, not 320x240. This is a *modern* 3D-pixel-art
        // technique, not a 1994 console: its whole point is crisp readable
        // silhouettes on a disciplined pixel grid, and at a third of the
        // window the 1px depth-outline kernel and the object shapes it traces
        // were coarse enough that props became unidentifiable blobs. The era
        // emulations (ps1, n64) keep their authentic 320x240; the stylised
        // presets buy back readability, which is the thing they exist for.
        v.pixelSize = 2; v.perPixel = true;
        // "clean, flat, and minimal": a 0.20 seam still spans most of a band,
        // so the posterization reads as a gradient instead of steps.
        v.bandedLightSteps = 5.0f; v.stepSoftness = 0.10f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.edgeConvexity = 1.0f; v.edgeConvexBias = 0.05f;
        v.inkStrength = 0.24f; v.inkThreshold = 0.20f;
        v.inkColor = {0.025f, 0.045f, 0.10f};
        // Convex-only highlights fire on roughly half as many edges as the old
        // undivided test, so each one carries more weight and can start lower.
        v.highlightStrength = 0.26f; v.highlightThreshold = 0.28f;
        v.highlightDarkFade = 0.12f;
        v.highlightColorOverride = false;
        v.outlineOpacity = 0.52f; v.outlineDepthSens = 10.0f;
        // outlineNormalSens now feeds concave folds alone; raised so creases
        // still read once the convex half of the signal moved to highlights.
        v.outlineNormalSens = 0.52f; v.outlineSharpness = 0.82f;
        v.outlineDistFade = 0.045f; v.outlineDarkFade = 0.09f;
        v.outlineColor = {0.018f, 0.035f, 0.09f};
        v.bloomThreshold = 0.82f; v.bloomIntensity = 0.35f;
        v.bloomPixelSnap = 1.0f; // glow stays built out of render pixels
        v.gradeDesaturate = 0.025f; v.gradeContrast = 1.01f;
        v.gradeSaturation = 1.04f; v.gradeTintStrength = 0.025f;
        v.gradeBlackLift = 0.045f; v.vignetteStrength = 0.07f;
        // Flat colour fields are the point of the style, so no dither: at
        // 6-bit the banding it would break up is already below the eye.
        v.colDepth = 63.0f; v.ditherBanding = 0.0f;
        // Mode 5's local-contrast crisping lays a bright halo one texel outside
        // every dark outline -- exactly the soft edge this style exists to
        // avoid. Holland's chain sharpens nothing; the pixels are already hard.
        v.hardwareResolveMode = resolve::kPixelCrisp;
        v.hardwareResolveStrength = 0.0f;
        break;
    case 6: // modern-ps1 -- the "both worlds" profile, and the only one where
            // that is the stated goal rather than a compromise. Every value
            // here is either a PS1 artefact kept because it *is* the look
            // (vertex snap, affine warp, dither, low resolution) or a modern
            // affordance the console could not run and the eye reads as
            // quality rather than as anachronism (per-fragment light, bloom,
            // a faint depth outline). Nothing in between.
        // "modern" is the operative word: same readability budget as pixel-3d
        // and dungeon (480x360 at 960x720). The authentic 320x240 belongs to
        // the ps1 preset, which is right next to it for the comparison.
        v.pixelSize = 2;
        // The single biggest departure, and the one the whole preset is for.
        // Vertex lighting is not a PS1 *look*, it is a PS1 *limit*: it puts
        // the light on the triangle corners, so a torch on a wall lights the
        // wall's corners. Per-fragment falloff is what every modern PSX-style
        // game (Signalis, the demake scene) runs, and it costs the era nothing
        // -- the pixels, the warp and the dither are all still here.
        v.perPixel = true;
        // ~1 render pixel of snap grid: present, but half the amplitude of the
        // PS1 profile, to match this preset's "retro hint" affine setting.
        // (The old 0.65 snapped below the pixel grid, so it did nothing.)
        v.precisionMultiplier = 0.30f;
        // Posterized light on top of per-fragment falloff would put the rings
        // back that per-fragment falloff exists to remove. The dither below is
        // what carries the quantized feel.
        v.bandedLightingEnabled = false; v.stepSoftness = 0.22f;
        v.inkStrength = 0.13f; v.highlightStrength = 0.055f;
        v.outlineOpacity = 0.18f; v.outlineNormalSens = 0.14f;
        v.bloomThreshold = 0.80f; v.bloomIntensity = 0.40f;
        v.bloomPixelSnap = 1.0f; // one render pixel: keep the glow on the grid
        // 6-bit rather than the PS1's 5: enough quantization to read as era,
        // little enough that the modern lighting's gradients survive it.
        v.colDepth = 63.0f; v.ditherBanding = 0.018f;
        v.gradeSaturation = 1.06f; v.gradeBlackLift = 0.035f;
        v.hardwareResolveMode = resolve::kSoftCrisp;
        v.hardwareResolveStrength = 0.45f;
        v.affineAmount = 0.30f; // subtle warp: retro hint, not full swim
        break;
    case 7: // dungeon -- the default. Not an era emulation like 1-6: this is the
            // game's own look. Dark-fantasy roguelike, torchlit stone, near
            // black between the lights.
            //
            // The governing constraint is that this is a *survival* look, not a
            // screenshot look. Every darkening decision below is paired with
            // something that keeps a silhouette legible, because a player who
            // cannot tell a doorway from a wall at 8 metres will not care how
            // atmospheric the frame is.
        v.pixelSize = 2;   // 480x360 at 960x720: retro texel, readable shapes
        v.perPixel = true; // torch pools need per-fragment falloff
        // Naturalistic firelight rather than posterized rings: banding torch
        // falloff draws contour lines on the floor that read as terrain.
        v.bandedLightingEnabled = false; v.stepSoftness = 0.30f;
        v.precisionMultiplier = 0.50f; // below the pixel grid: no vertex wobble
        v.affineAmount = 0.12f;        // a whisper of PSX warp for identity

        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.edgeConvexity = 1.0f; v.edgeConvexBias = 0.05f;
        // Contact darkening where geometry meets geometry -- what stops props
        // from looking like decals lying on the floor.
        v.inkStrength = 0.20f; v.inkThreshold = 0.22f;
        v.inkColor = {0.020f, 0.020f, 0.035f};
        // Convex edges catch the torch. Warm, faint, and gated hard by
        // highlightDarkFade so nothing rim-lights itself out of the dark.
        v.highlightStrength = 0.10f; v.highlightThreshold = 0.34f;
        v.highlightDarkFade = 0.22f;
        v.highlightColorOverride = true;
        v.highlightColor = {1.0f, 0.70f, 0.40f};
        // Readability, not style: a low-opacity depth outline is the thing that
        // separates a player-shaped silhouette from the black behind it. Faded
        // hard with distance so the far end of a corridor does not turn into
        // line art, and by luminance so it never draws on unlit geometry the
        // player is not meant to see yet.
        v.outlineOpacity = 0.30f; v.outlineThickness = 1.0f;
        v.outlineDepthSens = 9.0f; v.outlineNormalSens = 0.22f;
        v.outlineSharpness = 0.86f; v.outlineDistFade = 0.085f;
        v.outlineDarkFade = 0.14f;
        v.outlineColor = {0.015f, 0.015f, 0.028f};

        // Torches are the only real light source, so they are allowed to bloom
        // properly -- a low threshold is what sells "this flame is the only
        // reason you can see". Snapped to the pixel grid to match pixelSize 2.
        v.bloom = true; v.bloomThreshold = 0.56f; v.bloomIntensity = 0.72f;
        v.bloomPixelSnap = 1.0f;

        // Cold desaturated stone against warm fire: the split-tone does the
        // heavy lifting, so tint strength runs well above the era profiles.
        v.gradeDesaturate = 0.12f; v.gradeSaturation = 0.88f;
        v.gradeContrast = 1.12f;
        v.gradeShadow = {0.060f, 0.070f, 0.130f}; // cold blue-black
        v.gradeMid = {0.58f, 0.55f, 0.50f};       // damp stone
        v.gradeTintStrength = 0.09f;
        // The one anti-darkness term in the grade. Contrast 1.12 crushes the
        // blacks; this lifts them just enough that shapes survive down there.
        v.gradeBlackLift = 0.028f;

        // Claustrophobia. Strong enough to close the frame in, tinted to the
        // same cold black as the shadows so it reads as unlit depth rather
        // than as a lens effect.
        v.vignetteEnabled = true; v.vignetteStrength = 0.28f;
        v.vignetteColor = {0.090f, 0.090f, 0.150f};

        // 6-bit with a real dither: torchlight is all long shallow gradients,
        // which is exactly what bands. ditherDarkFade runs high so the pattern
        // dies before the near-black, where it would shimmer as the player
        // walks and be the most visible thing on screen.
        v.colDepth = 63.0f; v.ditherBanding = 0.022f;
        v.ditherDarkFade = 0.28f;
        v.hardwareResolveMode = resolve::kShadowCrisp;
        v.hardwareResolveStrength = 0.35f; // crisping in shadow only
        // Distance sinks toward the fog colour instead of lerping to it, so
        // corridors read as going dark rather than going grey.
        v.fogDesatBoost = 0.65f;
        break;

    // ---- mood profiles ----------------------------------------------------
    // 8-10 are not new rendering techniques. Each one picks an era profile as
    // its chassis -- resolution, snap, dither, resolve filter, lighting model --
    // and spends the grade, the bloom and the vignette on a place. That is the
    // whole reason resolveMode stopped being the preset id: a fantasy look
    // built on the PS1 pipeline should run the PS1 composite, not a filter that
    // exists because it happened to be numbered eight.

    case 8: // psx-horror -- the 1999 fog-town survival-horror look. PS1
            // chassis, unaltered: 320x240, full affine swim, integer vertex
            // snap, 15-bit dither, Gouraud light, chroma-truncating composite.
            //
            // The era's horror games did not have a horror renderer. They had
            // the same hardware as everything else and one idea: pull the draw
            // distance in until the fog is the level, then drain the colour out
            // of what is left. Both of those are grade and fog values, which is
            // exactly why this is a profile and not a shader.
        v.pixelSize = 3; v.perPixel = false; v.bloom = false;
        v.precisionMultiplier = 0.156f;   // the ps1 wobble, unchanged
        v.affineAmount = 1.0f;            // the ps1 warp, unchanged
        v.bandedLightingEnabled = false;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.hardwareResolveMode = resolve::kPs1Chroma;
        v.hardwareResolveStrength = 0.40f;
        // The palette is the effect. Not "grey": a sick warm grey, because the
        // reference is a fog-lit street and rusted metal, and a neutral
        // desaturate reads as a black-and-white filter instead.
        v.gradeDesaturate = 0.52f; v.gradeSaturation = 0.72f;
        v.gradeContrast = 1.06f;
        v.gradeShadow = {0.11f, 0.10f, 0.085f}; // brown-black, not blue-black
        v.gradeMid = {0.64f, 0.61f, 0.55f};
        v.gradeTintStrength = 0.16f;
        // Fog light does not let anything go truly black -- it scatters into
        // the shadows, which is why a foggy street at night is legible and a
        // dark room is not. The lift is the difference between the two.
        v.gradeBlackLift = 0.055f;
        v.vignetteStrength = 0.24f;
        v.vignetteColor = {0.16f, 0.15f, 0.14f};
        v.colDepth = 31.0f; v.ditherBanding = 0.045f; v.ditherDarkFade = 0.05f;
        // Distance drains colour before it drains light: the far end of a
        // street goes grey, then goes away. Highest of any profile here, and
        // the single value that makes the fog read as this era's fog.
        v.fogDesatBoost = 0.90f;
        break;

    case 9: // fire-dimension -- an ember-lit hell plane. modern-ps1 chassis,
            // because this one has to stay readable while everything in frame
            // glows: per-fragment falloff, 480x360, a hint of warp.
            //
            // The trap with a fire look is that it becomes an orange screen.
            // The defence here is that the split-tone pushes the *shadows* to
            // ember and leaves the mids near their own hue, so lit surfaces
            // keep their material and only the dark half of the frame burns.
        v.pixelSize = 2; v.perPixel = true;
        v.precisionMultiplier = 0.30f; v.affineAmount = 0.15f;
        v.bandedLightingEnabled = false; v.stepSoftness = 0.24f;
        v.hardwareResolveMode = resolve::kShadowCrisp;
        v.hardwareResolveStrength = 0.30f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.edgeConvexity = 1.0f; v.edgeConvexBias = 0.05f;
        v.inkStrength = 0.22f; v.inkThreshold = 0.24f;
        v.inkColor = {0.045f, 0.010f, 0.008f}; // charcoal, not blue-black
        v.highlightStrength = 0.16f; v.highlightThreshold = 0.30f;
        v.highlightDarkFade = 0.18f;
        v.highlightColorOverride = true;
        v.highlightColor = {1.0f, 0.52f, 0.16f};
        v.outlineOpacity = 0.26f; v.outlineDepthSens = 9.0f;
        v.outlineNormalSens = 0.20f; v.outlineSharpness = 0.86f;
        v.outlineDistFade = 0.075f; v.outlineDarkFade = 0.12f;
        v.outlineColor = {0.040f, 0.008f, 0.006f};
        // Low threshold, high intensity: in a place lit by fire, the fire is
        // meant to be the brightest thing by a margin the eye cannot miss.
        v.bloom = true; v.bloomThreshold = 0.46f; v.bloomIntensity = 0.95f;
        v.bloomPixelSnap = 1.0f;
        // Desaturate first, then tint. Without the pull toward grey the scene's
        // own light keeps its hue and the ember tint lands *on top of* it -- a
        // blue-lit room stays blue and merely warms, which reads as a filter
        // rather than as a place. Draining it first is what lets the split-tone
        // decide the colour; the saturation push afterwards is what stops the
        // result being flat.
        v.gradeDesaturate = 0.34f; v.gradeSaturation = 1.20f;
        v.gradeContrast = 1.10f;
        v.gradeShadow = {0.24f, 0.055f, 0.022f}; // banked coals
        v.gradeMid = {0.92f, 0.48f, 0.22f};      // scorched stone
        v.gradeTintStrength = 0.38f;
        v.gradeBlackLift = 0.030f;
        v.vignetteStrength = 0.26f;
        v.vignetteColor = {0.34f, 0.08f, 0.03f};
        v.colDepth = 63.0f; v.ditherBanding = 0.020f; v.ditherDarkFade = 0.24f;
        // Heat haze is hot, so distance must not drain it: a low boost keeps
        // the far end of the room glowing rather than fading to grey.
        v.fogDesatBoost = 0.15f;
        break;

    case 10: // poison-swamp -- standing water, spore light, everything damp.
             // modern-ps1 chassis again, for the same reason: a miasma is a
             // long shallow gradient, and long shallow gradients are what
             // vertex lighting destroys and dither rescues.
        v.pixelSize = 2; v.perPixel = true;
        v.precisionMultiplier = 0.30f; v.affineAmount = 0.12f;
        v.bandedLightingEnabled = false; v.stepSoftness = 0.28f;
        v.hardwareResolveMode = resolve::kSoftCrisp;
        v.hardwareResolveStrength = 0.38f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.edgeConvexity = 1.0f; v.edgeConvexBias = 0.05f;
        // Heavier contact ink than the other profiles: in a swamp the thing
        // you need to read is where the water meets the root, and that is a
        // contact, not a silhouette.
        v.inkStrength = 0.24f; v.inkThreshold = 0.20f;
        v.inkColor = {0.012f, 0.030f, 0.016f};
        v.highlightStrength = 0.12f; v.highlightThreshold = 0.32f;
        v.highlightDarkFade = 0.20f;
        v.highlightColorOverride = true;
        v.highlightColor = {0.72f, 1.0f, 0.48f}; // spore glow
        v.outlineOpacity = 0.28f; v.outlineDepthSens = 9.0f;
        v.outlineNormalSens = 0.22f; v.outlineSharpness = 0.84f;
        v.outlineDistFade = 0.070f; v.outlineDarkFade = 0.13f;
        v.outlineColor = {0.010f, 0.026f, 0.014f};
        // Enough bloom for the glowing things to bleed into the haze, not
        // enough to make the whole swamp luminous.
        v.bloom = true; v.bloomThreshold = 0.64f; v.bloomIntensity = 0.50f;
        v.bloomPixelSnap = 1.0f;
        // Same order as fire-dimension and for the same reason, one notch
        // gentler: a swamp is damp, not lit by anything, so the drained scene
        // showing through the green is part of the read.
        v.gradeDesaturate = 0.30f; v.gradeSaturation = 1.02f;
        v.gradeContrast = 1.04f;
        v.gradeShadow = {0.055f, 0.115f, 0.060f}; // algae black
        v.gradeMid = {0.52f, 0.70f, 0.38f};       // wet moss
        v.gradeTintStrength = 0.34f;
        // Suspended spores scatter light the way fog does, so the blacks lift
        // here for the same reason they do in psx-horror.
        v.gradeBlackLift = 0.050f;
        v.vignetteStrength = 0.25f;
        v.vignetteColor = {0.10f, 0.17f, 0.09f};
        v.colDepth = 63.0f; v.ditherBanding = 0.026f; v.ditherDarkFade = 0.30f;
        // The haze is the colour: distance should sink into green, not grey,
        // so this stays far below the dungeon's 0.65.
        v.fogDesatBoost = 0.12f;
        break;

    default: break;
    }
    return v;
}

void applyRenderPreset(Renderer& r, const RenderPresetValues& v)
{
    r.setPixelSize(v.pixelSize);
    // After setPixelSize, which clears any absolute target of its own.
    if (v.targetWidth > 0 && v.targetHeight > 0)
        r.setRenderResolution(v.targetWidth, v.targetHeight);
    r.setGlobalMaterialParam("precisionMultiplier", v.precisionMultiplier);
    r.setGlobalMaterialParam("affineAmount", v.affineAmount);
    r.setPerPixelLightingEnabled(v.perPixel);
    r.setLightSteps(v.bandedLightingEnabled ? v.bandedLightSteps : 0.0f);
    r.setLightStepSoftness(v.stepSoftness);
    // Negative means the profile has no opinion: the game's per-level render
    // palette owns this one, and an era profile must not stomp it.
    if (v.fogDesatBoost >= 0.0f)
        r.setFogDesatBoost(v.fogDesatBoost);
    r.setDitherEnabled(true);
    r.setBloomEnabled(v.bloom);
    r.setBloomParams(v.bloomThreshold, v.bloomIntensity);
    r.setMaterialParam("Engine/Psx/BloomComposite", "bloomPixelSnap", v.bloomPixelSnap);

    r.setMaterialParam("Engine/Psx/PixelStylize", "stylizeEnabled", 1.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowsEnabled", v.inkEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightsEnabled", v.highlightsEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineEnabled", v.outlinesEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowStrength", v.inkStrength);
    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowThreshold", v.inkThreshold);
    r.setMaterialParam("Engine/Psx/PixelStylize", "shadowColor", v.inkColor);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightStrength", v.highlightStrength);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightThreshold", v.highlightThreshold);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightDarkFade", v.highlightDarkFade);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightColorOverride",
                       v.highlightColorOverride ? 1.0f : 0.0f);
    r.setMaterialParam("Engine/Psx/PixelStylize", "highlightColor", v.highlightColor);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineOpacity", v.outlineOpacity);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineThickness", v.outlineThickness);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineDepthSens", v.outlineDepthSens);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineNormalSens", v.outlineNormalSens);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineSharpness", v.outlineSharpness);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineDistFade", v.outlineDistFade);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineDarkFade", v.outlineDarkFade);
    r.setMaterialParam("Engine/Psx/PixelStylize", "outlineColor", v.outlineColor);
    r.setMaterialParam("Engine/Psx/PixelStylize", "edgeConvexity", v.edgeConvexity);
    r.setMaterialParam("Engine/Psx/PixelStylize", "edgeConvexBias", v.edgeConvexBias);

    r.setGradeEnabled(true);
    r.setGradeParams(v.gradeDesaturate, v.gradeContrast, v.gradeShadow, v.gradeMid);
    r.setMaterialParam("Engine/Psx/DitherPost", "gradeSaturation", v.gradeSaturation);
    r.setMaterialParam("Engine/Psx/DitherPost", "gradeTintStrength", v.gradeTintStrength);
    r.setMaterialParam("Engine/Psx/DitherPost", "gradeBlackLift", v.gradeBlackLift);
    r.setMaterialParam("Engine/Psx/DitherPost", "vignetteStrength",
                       v.vignetteEnabled ? v.vignetteStrength : 0.0f);
    r.setMaterialParam("Engine/Psx/DitherPost", "vignetteColor", v.vignetteColor);
    r.setMaterialParam("Engine/Psx/DitherPost", "colDepth", v.colDepth);
    r.setMaterialParam("Engine/Psx/DitherPost", "ditherBanding", v.ditherBanding);
    r.setMaterialParam("Engine/Psx/DitherPost", "ditherDarkFade", v.ditherDarkFade);

    r.setMaterialParam("Engine/Psx/HardwareResolve", "resolveMode", v.hardwareResolveMode);
    r.setMaterialParam("Engine/Psx/HardwareResolve", "resolveStrength", v.hardwareResolveStrength);
}

// Public by-id entry point: this is all a game needs, and it keeps
// RenderPresetValues (and its ~40 fields) an engine-private detail.
void applyRenderPreset(Renderer& r, int id)
{
    applyRenderPreset(r, renderPresetValues(id));
}

} // namespace eng
