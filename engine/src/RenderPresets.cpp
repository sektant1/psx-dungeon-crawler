#include "RenderPresets.h"
#include "eng/Renderer.h"
#include <cstring>

namespace eng {

int renderPresetFromName(const char* name)
{
    if (!name) return -1;
    if (!std::strcmp(name, "ps1")) return 1;
    if (!std::strcmp(name, "ps2")) return 2;
    if (!std::strcmp(name, "gamecube")) return 3;
    if (!std::strcmp(name, "n64")) return 4;
    if (!std::strcmp(name, "pixel-3d")) return 5;
    if (!std::strcmp(name, "modern-ps1")) return 6;
    if (!std::strcmp(name, "dungeon")) return 7;
    if (!std::strcmp(name, "default")) return kDefaultRenderPreset;
    return -1;
}

RenderPresetValues renderPresetValues(int preset)
{
    RenderPresetValues v;
    v.hardwareResolveMode = float(preset);
    switch (preset) {
    case 1: // PS1 -- tuned against TDM's "PSX rendering" (shadertoy Mt3Gz2),
            // which fakes the console's GTE exactly: integer screen-space
            // vertices, screen-space (affine) UV interpolation, point-sampled
            // texels, no colour grade.
        v.pixelSize = 3; v.perPixel = false; v.bloom = false;
        // psx.vert snaps NDC to floor(512,448 * p), and NDC spans [-1,1], so
        // the grid is 2*floor(512*p) steps across the screen. The old 0.50
        // gave 512x448 steps over a 320x240 target -- FINER than a render
        // pixel, i.e. the wobble was mathematically invisible. TDM quantizes
        // to a grid ~1.5-2 render pixels coarse; 0.156 -> 158x138 steps over
        // 320x240 reproduces that.
        v.precisionMultiplier = 0.156f;
        v.bandedLightSteps = 4.0f; v.stepSoftness = 0.07f;
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
        // Mode 1 truncates chroma, but dither.frag already quantizes to 15-bit
        // one pass later, so most of this was doing the same job twice.
        v.hardwareResolveStrength = 0.30f;
        v.affineAmount = 1.0f; // TDM interpolates UV purely in screen space
        break;
    case 2: // PS2
        // The Graphics Synthesizer topped out at 640x448 on NTSC. Rendering at
        // the full window was both wrong and the most expensive profile here --
        // at 1080p that is 7x the fragments of the real framebuffer, for every
        // pass in the chain.
        v.targetWidth = 640; v.targetHeight = 448;
        v.pixelSize = 2; // divisor fallback if the absolute target is turned off
        v.perPixel = true;
        v.bandedLightingEnabled = false; v.stepSoftness = 0.35f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.bloomThreshold = 0.84f; v.bloomIntensity = 0.25f;
        v.gradeDesaturate = 0.015f; v.gradeContrast = 1.0f;
        v.gradeTintStrength = 0.01f; v.gradeBlackLift = 0.025f;
        v.vignetteStrength = 0.025f;
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
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = false;
        v.bloomThreshold = 0.76f; v.bloomIntensity = 0.38f;
        v.gradeDesaturate = 0.0f; v.gradeContrast = 1.01f;
        v.gradeSaturation = 1.12f; v.gradeTintStrength = 0.0f;
        v.gradeBlackLift = 0.020f; v.vignetteStrength = 0.015f;
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
        break;
    case 5: // pixel-3d -- David Holland's 3D pixel art method
            // (davidhol.land/articles/3d-pixel-art-rendering): 1px cross-kernel
            // outlines off the depth buffer, edge highlights on convex folds
            // only, flat toon lighting, and a clean nearest upscale.
        v.pixelSize = 3; v.perPixel = true;
        // "clean, flat, and minimal": at 320x240 a 0.20 seam spans most of a
        // band, so the posterization reads as a gradient instead of steps.
        v.bandedLightSteps = 5.0f; v.stepSoftness = 0.10f;
        v.inkEnabled = v.highlightsEnabled = v.outlinesEnabled = true;
        v.edgeConvexity = 1.0f; v.edgeConvexBias = 0.05f;
        v.inkStrength = 0.24f; v.inkThreshold = 0.20f;
        v.inkColor = {0.025f, 0.045f, 0.10f};
        // Convex-only highlights fire on roughly half as many edges as the old
        // undivided test, so each one carries more weight and can start lower.
        v.highlightStrength = 0.26f; v.highlightThreshold = 0.28f;
        v.highlightDarkFade = 0.12f;
        v.highlightColor = {0.48f, 0.78f, 1.0f};
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
        v.hardwareResolveStrength = 0.0f;
        break;
    case 6: // modern-ps1
        v.pixelSize = 3; v.perPixel = false;
        // ~1 render pixel of snap grid: present, but half the amplitude of the
        // PS1 profile, to match this preset's "retro hint" affine setting.
        // (The old 0.65 snapped below the pixel grid, so it did nothing.)
        v.precisionMultiplier = 0.30f;
        v.bandedLightSteps = 4.0f; v.stepSoftness = 0.18f;
        v.inkStrength = 0.13f; v.highlightStrength = 0.055f;
        v.outlineOpacity = 0.18f; v.outlineNormalSens = 0.14f;
        v.bloomThreshold = 0.80f; v.bloomIntensity = 0.40f;
        v.bloomPixelSnap = 1.0f; // pixelSize 3: keep the glow on the grid too
        v.ditherBanding = 0.018f;
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
        v.bloom = true; v.bloomThreshold = 0.62f; v.bloomIntensity = 0.55f;
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
        v.hardwareResolveStrength = 0.35f; // mode 7: crisping in shadow only
        // Distance sinks toward the fog colour instead of lerping to it, so
        // corridors read as going dark rather than going grey.
        v.fogDesatBoost = 0.65f;
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
    r.setMaterialParam("PSX/BloomComposite", "bloomPixelSnap", v.bloomPixelSnap);

    r.setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 1.0f);
    r.setMaterialParam("PSX/PixelStylize", "shadowsEnabled", v.inkEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "highlightsEnabled", v.highlightsEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "outlineEnabled", v.outlinesEnabled ? 1.0f : 0.0f);
    r.setMaterialParam("PSX/PixelStylize", "shadowStrength", v.inkStrength);
    r.setMaterialParam("PSX/PixelStylize", "shadowThreshold", v.inkThreshold);
    r.setMaterialParam("PSX/PixelStylize", "shadowColor", v.inkColor);
    r.setMaterialParam("PSX/PixelStylize", "highlightStrength", v.highlightStrength);
    r.setMaterialParam("PSX/PixelStylize", "highlightThreshold", v.highlightThreshold);
    r.setMaterialParam("PSX/PixelStylize", "highlightDarkFade", v.highlightDarkFade);
    r.setMaterialParam("PSX/PixelStylize", "highlightColor", v.highlightColor);
    r.setMaterialParam("PSX/PixelStylize", "outlineOpacity", v.outlineOpacity);
    r.setMaterialParam("PSX/PixelStylize", "outlineThickness", v.outlineThickness);
    r.setMaterialParam("PSX/PixelStylize", "outlineDepthSens", v.outlineDepthSens);
    r.setMaterialParam("PSX/PixelStylize", "outlineNormalSens", v.outlineNormalSens);
    r.setMaterialParam("PSX/PixelStylize", "outlineSharpness", v.outlineSharpness);
    r.setMaterialParam("PSX/PixelStylize", "outlineDistFade", v.outlineDistFade);
    r.setMaterialParam("PSX/PixelStylize", "outlineDarkFade", v.outlineDarkFade);
    r.setMaterialParam("PSX/PixelStylize", "outlineColor", v.outlineColor);
    r.setMaterialParam("PSX/PixelStylize", "edgeConvexity", v.edgeConvexity);
    r.setMaterialParam("PSX/PixelStylize", "edgeConvexBias", v.edgeConvexBias);

    r.setGradeEnabled(true);
    r.setGradeParams(v.gradeDesaturate, v.gradeContrast, v.gradeShadow, v.gradeMid);
    r.setMaterialParam("PSX/DitherPost", "gradeSaturation", v.gradeSaturation);
    r.setMaterialParam("PSX/DitherPost", "gradeTintStrength", v.gradeTintStrength);
    r.setMaterialParam("PSX/DitherPost", "gradeBlackLift", v.gradeBlackLift);
    r.setMaterialParam("PSX/DitherPost", "vignetteStrength",
                       v.vignetteEnabled ? v.vignetteStrength : 0.0f);
    r.setMaterialParam("PSX/DitherPost", "vignetteColor", v.vignetteColor);
    r.setMaterialParam("PSX/DitherPost", "colDepth", v.colDepth);
    r.setMaterialParam("PSX/DitherPost", "ditherBanding", v.ditherBanding);
    r.setMaterialParam("PSX/DitherPost", "ditherDarkFade", v.ditherDarkFade);

    r.setMaterialParam("PSX/HardwareResolve", "resolveMode", v.hardwareResolveMode);
    r.setMaterialParam("PSX/HardwareResolve", "resolveStrength", v.hardwareResolveStrength);
}

} // namespace eng
