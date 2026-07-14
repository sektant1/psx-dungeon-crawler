# 3D Pixelation + Outline Post-Process Design

Date: 2026-07-14
Branch: feat/engine-scaffold

## Goal

Keep the existing PSX look (vertex snap, affine textures, vertex lighting, fog,
dither/colour-depth compositor) and add the "3D pixel art" stylizer from
`~/gamedev/shaders/Godot-3d-pixelart-demo-master/pixelart_stylizer.gdshader`
(itself a port of the three.js `webgl_postprocessing_pixel` example):

- **Adjustable pixelation**: a live pixel-size slider (like the three.js demo)
  that changes the low-res render-target resolution at runtime.
- **Depth-based shadow outlines**: darken pixels where a depth discontinuity
  says a farther surface lies behind (exterior silhouette edges).
- **Normal-based highlight edges**: brighten pixels on convex creases where
  neighbouring normals diverge (interior edges), gated by depth so concave
  edges don't highlight.
- **Debug UI sliders** mirroring both references: pixel size, shadow/highlight
  enable, strengths, colours.

## Reference technique (what the Godot shader does)

Runs as a full-screen pass reading three inputs per pixel and its 4 neighbours
(up/down/left/right, 1 texel apart):

1. **Depth** (linearized view-space): `depth_diff = Σ clamp(neighbour - centre)`
   → `smoothstep(0.2, 0.3, depth_diff)` = shadow-outline mask. A negative
   accumulator (`neg_depth_diff`) detects "centre is farther" cases and is used
   to suppress highlights on concave edges.
2. **Normals** (view-space): `normalIndicator()` per neighbour — dot of the
   normal difference against bias `(1,1,1)`, gated by a depth indicator —
   summed, `smoothstep(0.2, 0.8, ...)`, minus `neg_depth_diff` = highlight mask.
3. **Scene colour**: `final = mix(mix(scene, highlight_color, highlight_strength),
   shadow_color, shadow_strength)` per mask.

Pixelation itself is just rendering the scene at low resolution and upscaling
with nearest filtering — which our PSX/Dither compositor already does at fixed
512×448.

## Constraints and decisions (user-confirmed)

- **Pixel size**: live slider. RT size = window size / pixelSize (integer 1–16,
  default keeping current look). Changing it rebuilds the compositor chain.
- **Normals/depth source**: **MRT from the PSX shaders** — one scene render
  writes colour to RT0 and view-space normal + linear depth to RT1. Exact match
  to the reference inputs, no second scene pass. `psx.frag` already receives
  `vNormalVS` and `vViewDepth`, so the data is free.
- PSX look unchanged: stylizer inserts *between* the low-res scene render and
  the existing dither pass, operating at the low resolution (like the Godot
  demo, where the stylizer quad lives inside the low-res viewport).

## Architecture

Ogre 14, GL3Plus, existing compositor `PSX/Dither` grows into `PSX/Stylized`:

```
scene ──MRT──► rt_scene (RGBA) + rt_normal (RGBA: xyz = view normal *0.5+0.5,
   (low res)                                       a = linear view depth / far)
        │
        ▼
target rt_post: render_quad  material PSX/PixelStylize
        inputs: rt_scene, rt_normal          (outline+highlight, low res)
        │
        ▼
target_output: render_quad  material PSX/DitherPost   (existing dither/banding)
        input: rt_post  →  nearest-upscaled to window
```

### Components

1. **`psx.frag` MRT output** (`engine/assets/shaders/psx.frag`)
   - Add `layout(location=1) out vec4 fragNormal;` writing
     `vec4(normalize(vNormalVS) * 0.5 + 0.5, vViewDepth / farClip)`.
     `farClip` via `param_named_auto far_clip_distance`.
   - Unlit/light-volume/transparent variants still write sane values (light
     volumes and additive blends write `fragNormal = vec4(0)` with colour-only
     blending so they don't corrupt edges — guarded by `#ifdef BLEND_ADD` etc.;
     Ogre MRT blending applies to all targets, acceptable: soft volumes
     shouldn't produce outlines anyway).
   - Depth in alpha avoids a separate depth-texture read-back path in Ogre 1.x
     compositors (no `hint_depth_texture` equivalent); linear view depth is
     what the Godot shader reconstructs anyway — we skip the reconstruction.

2. **New `pixel_stylize.frag`** (`engine/assets/shaders/`)
   - Port of `pixelart_stylizer.gdshader` fragment logic. Inputs: `sceneTex`
     (unit 0), `normalDepthTex` (unit 1). Depth read = `.a * farClip`
     (linear view depth — same quantity `getDepth()` produces in Godot).
   - Uniforms: `shadowsEnabled`, `highlightsEnabled`, `shadowStrength` (0.4),
     `highlightStrength` (0.1), `shadowColor` (black), `highlightColor`
     (white), `texelSize` auto via `textureSize()`.
   - Skips the Godot `getPos`/ALPHA parts (that shader ran as a transparent
     quad in-scene; ours is a real post pass, output is opaque).

3. **Compositor** (`psx.compositor`): rename technique to chain
   `rt_scene`+`rt_normal` (MRT target with `input previous`), stylize pass into
   `rt_post`, dither pass to output. RT sizes declared **relative** is not
   enough (need window/pixelSize) → RenderCore creates the compositor from C++
   or recreates the chain with `texture rt ... <w> <h>` templated: implementer
   choice, simplest is `CompositorManager::removeCompositorChain` + re-add
   after patching texture defs via `CompositorTechnique` API on pixel-size
   change (matches existing `setDitherEnabled` plumbing in RenderCore.cpp).

4. **RenderCore/Renderer API** (`engine/src/RenderCore.*`, `Renderer.cpp`,
   `include/eng/Renderer.h`)
   - `setPixelSize(int)` — clamps 1..16, rebuilds chain, keeps enable states.
   - `setStylizeEnabled(bool)`; existing `setMaterialParam` covers the rest.
   - Handle window resize: recompute RT from new window size.
   - Cache state in Renderer's env struct for DebugUi read-back (existing
     pattern).

5. **DebugUi** (`engine/src/DebugUi.cpp`) — new "pixel art" section, mirroring
   three.js demo controls:
   - `pixel size` int slider 1–16 → `setPixelSize`
   - `stylize` checkbox → `setStylizeEnabled`
   - `shadows` / `highlights` checkboxes, `shadow strength`, `highlight
     strength` sliders 0–1, `shadow color` / `highlight color` pickers →
     `setMaterialParam("PSX/PixelStylize", ...)`
   - Existing dither controls stay.

## Error handling

- MRT requires GL3Plus FBO support — already the only target; assert RT1
  creation succeeded, log via `ogre.log` otherwise, fall back to stylize
  disabled.
- Pixel-size rebuild while chain active: disable → remove → re-add → restore
  enables; guard against 0-size on minimized window.

## Testing

- Build + run `samples/psx-demo`; verify: outlines on wall/prop silhouettes,
  highlights on convex edges, no highlights on concave corners (neg-depth
  suppression working), fog/dither unchanged when stylize off.
- Slider sweep 1→16 without crash or leak (watch `ogre.log` for texture churn).
- Toggle each checkbox; `stylize off` must be pixel-identical to current look.
- Resize window; RT follows.

## Out of scope

- Pixel-grid-snapped camera movement (three.js demo has it; separate feature).
- Per-material outline exclusion.
- Ogre RTSS interaction (all materials are hand-written GLSL already).
