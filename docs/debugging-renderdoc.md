# Debugging this renderer with RenderDoc

How to take a GPU capture of this engine and read it. Written for someone — or
some agent — who has never seen this frame graph before, because a capture of an
unfamiliar renderer is a wall of unnamed draw calls until you know which pass is
which.

## Taking a capture

```sh
make renderdoc APP=game                  # launch under the RenderDoc UI, capture with F12
make renderdoc APP=scene_editor          # the editor's viewport instead
make renderdoc APP=game FRAME=200        # headless: capture exactly frame 200, then exit
make renderdoc-capture APP=game FRAME=90 # same, via tools/visual_test.py, with a JSON report
```

Prefer `FRAME=` for anything you intend to compare against a previous capture.
It runs at a fixed timestep with a pinned RNG seed, so frame 200 is the *same*
frame every run — which is what makes "this draw regressed" a statement about
the code rather than about when you happened to hit F12.

`APP=` matters more than it looks. The game, the editor and the demo all drive
the same renderer, so a shader bug reproduces in all three. The editor is
usually fastest: it opens a scene directly, has no gameplay to walk through, and
its material staging mode (`make material`) isolates one material on one sphere.

## What you are looking at

Every frame goes through `PSX/Stylized`
(`assets/compositors/psx.compositor`). The render targets, in order:

| Target | Size | What it holds |
|---|---|---|
| `mrt` | ⅓ × ⅓ | **Where the world is drawn.** MRT: attachment 0 is scene colour, attachment 1 is `vec4(normal * 0.5 + 0.5, depth / farClip)`. Everything except world-space UI. |
| `rt_post` | ⅓ | `Engine/Psx/PixelStylize` — outlines, ink, highlights, read off the normal/depth attachment |
| `rt_bright` | ⅙ | `Engine/Psx/BloomBright` — the threshold cut |
| `rt_blur` | ⅙ | `Engine/Psx/BloomBlurH`, then `BloomBlurV` back into `rt_bright` |
| `rt_final` | ⅓ | `Engine/Psx/BloomComposite` — post + bloom |
| `rt_resolve` | ⅓ | `Engine/Psx/HardwareResolve` — the era-specific resolve filter |
| `target_output` | full | the dither/grade pass upscales here, then world-space text plaques (render queue 100) are drawn at full resolution over it |

Two things surprise people:

- **The world renders at a third of the window.** The pixelation is a real
  low-resolution framebuffer, not a shader effect. Draw calls in `mrt` are
  supposed to look tiny.
- **Text plaques are absent from `mrt` and appear at the end.** They carry only
  visibility bit 30 (`eng::kFullResUiVisibilityFlag`), which `mrt`'s
  `visibility_mask 0xBFFFFFFF` masks off, so they stay sharp instead of being
  pixelated with the world.

## Reading the world pass

Geometry is drawn by the `PSX_VS_*` / `PSX_FS_*` program family
(`assets/programs/psx.program`), compiled into variants by preprocessor
define: `LIT`, `METAL`, `NO_TEXTURE`, `ALPHA_BLEND`, `ALPHA_SCISSOR`,
`LIGHT_VOLUME`, `BLEND_ADD`, `RIM`. In a capture, the variant is the fastest way
to tell what a draw *is*: a `NO_TEXTURE` draw with `ALPHA_BLEND` is VFX, a plain
`LIT` draw is level geometry.

Uniforms worth checking when something looks wrong:

| Uniform | Symptom when wrong |
|---|---|
| `precisionMultiplier` | vertex wobble too strong or entirely absent (it is a *snap* grid; a value finer than a render pixel is mathematically invisible) |
| `affineAmount` | textures swimming on large triangles (1 = full PSX warp, 0 = perspective-correct) |
| `lightCount`, `lightPos`, `lightDiffuse` | geometry black or lit by the wrong lights; the shaders bind 16 slots, and OGRE's per-pass default of 8 truncates the list |
| `uvScale`, `uvOffset` | textures mirrored or offset — the kit is authored top-down and needs a V flip |
| `modulateColor` | a whole material the wrong tint |
| `farClip` | the normal/depth attachment's depth channel out of range, so outlines vanish or appear everywhere |

## Common failures and where they show

**Everything at the origin.** Look at the world matrices in `mrt`'s draws. If
they are all identity, the registry never marked entities dirty and `SceneSync`
never pushed a transform (`engine/src/ecs/SceneSync.cpp`).

**An editor viewport showing the font atlas.** `ImTextureID` is a raw GL texture
name for the SDL2/OpenGL3 backend, not an OGRE `ResourceHandle`
(`RenderCore::viewportTextureId`). Passing the wrong one lands on whatever
texture owns that id — usually ImGui's atlas.

**The world upside down in the editor.** The offscreen RTT already hands back a
top-down image; flipping V in the `ImGui::Image` UVs flips it again.

**Outlines missing or wrong.** Check attachment 1 of `mrt` in the texture
viewer: it should read as a pastel normal map with a depth ramp in alpha. A
material that forgets to write `fragNormalDepth` leaves garbage there, and the
stylize pass then outlines noise.

**A material renders blank.** OGRE logs `has no supportable Techniques` at
startup with the shader compile error. Check the console output before reaching
for a capture — a GLSL error is not a GPU problem. (In GLSL 330, `const`
requires a compile-time-constant initialiser, unlike C++; that one bites often.)

## Comparing two captures

The point of a fixed frame is diffing. Capture before and after a change with
identical `FRAME=`, `SEED=` and `PRESET=`, then compare draw counts first and
individual draws second. `make visual-test` does the coarse version of this
automatically and writes a JSON report; the capture is for when you already know
*that* something changed and need to know *what*.

For pixel-level regressions, `make screenshot SHOT=… FRAME=…` is faster than a
capture and is what the visual-freeze rule is enforced with.

## Debug lines and vertex lighting

Both were stubs in the RHI backend that warned once and discarded the request,
which is why they are worth naming here: the symptoms looked like content bugs.

**Debug lines** (`Renderer::setDebugLines`) now draw through a LineList pipeline
(`debug_line.vert/.frag`) into a per-frame dynamic vertex buffer, after the world
and particles and before the viewmodel pass -- where the legacy queue put them.
The fragment writes zero to the normal/depth MRT target so the stylize pass
leaves the pixels alone and a line keeps the exact colour the caller asked for.
`setDebugLinesXray` selects the depth-tested or draw-over-everything variant.

Until this landed **the editor's grid did not render at all**. If the viewport
looks like a void, check this pass before suspecting the grid maths.

**Vertex lighting** (`setPerPixelLightingEnabled(false)`, which the PS1 and N64
presets ask for) evaluates the diffuse accumulation once per vertex and lets the
rasterizer interpolate it. `clipParams.z` carries the switch -- an existing
unused lane, because a new binding is a pipeline-layout change -- and both
stages call the same `accumulateLighting`, which is what stops the two modes
drifting into two looks. Shadows stay per-pixel: they are a depth-map addition
the console never had, and Gouraud-interpolating them looks like a bug.

The game runs the `dungeon` profile, which is per-pixel, so this changes nothing
about the shipped image; the editor runs `ps1` and does take the vertex-lit path.

## The three GTE artefacts

The PS1 look is not one effect. Three separate things produced it, all authored
per profile in `RenderPresets.cpp`, all pushed at the renderer already, and all
ignored by the Vulkan backend until they were implemented. They share one
uniform lane, `SceneUniforms.psxParams`, appended at the end of the block so
shaders that never look at it (shadow, particle, debug line) stay correct
without being touched.

| Lane | Knob | What it is |
|---|---|---|
| `psxParams.x` | `precisionMultiplier` | vertex snap |
| `psxParams.y` | `lightSteps` | posterized diffuse |
| `psxParams.z` | `lightStepSoftness` | band seam width |
| `psxParams.w` | `affineAmount` | affine texture mapping |

**Affine texture mapping** is the recognisable one, and it is *softened* rather
than applied raw -- `psxParams2.x`, `affine_softness`, in UV units. The
divergence between the perspective and affine interpolations grows without
bound on a polygon seen close and oblique, and the console got away with that
because it drew a room out of many small quads. A modern kit draws a floor as
two triangles, where raw affine stops swimming and starts *shearing* along the
diagonal the two triangles share. A per-component soft knee
(`d * s / (s + |d|)`) leaves small divergence untouched, so the swim reads
exactly as before, and saturates the tail at `s` UV units so nothing tears.
0.10 is the shipped default; raise it toward 1 for raw affine, drop it for a
flatter warp. Live on the debug panel's Render tab beside the amount.
 The console interpolated UVs
linearly in screen space with no perspective divide, so textures swim and buckle
across large polygons and a quad's two triangles crease along their shared
diagonal. The vertex stage emits the UVs twice -- once `smooth`, once
`noperspective` -- and the fragment blends between them, so it is a dial rather
than a compile-time variant. `psx.frag` does the same thing; this matches it.

**Vertex snap** quantises clip position onto a fixed screen grid, which is why
PS1 edges crawl. NDC spans [-1,1], so the grid is `2 * floor(512 * p)` steps
across the screen -- p = 0.156 (the PS1 profile) gives ~158 steps over a ~533px
target and wobbles hard, p = 1.0 is finer than any target here and reads as off.
Vertices behind the eye are left to the clipper: the NDC divide sends them to
infinity and `floor()` folds them back across the screen as a stray triangle.

**Posterized lighting** quantises the *diffuse* term only, so ambient never
bands the whole scene toward black, and stays unclamped so overbright torch
cores survive for the bloom bright pass to threshold on.

All three are tweakable live from the debug panel's Render tab, which already
had the sliders -- `setGlobalMaterialParam` intercepts the two material-shaped
ones so the existing UI drives this backend without learning a new call.

### Still not implemented in this backend

`Renderer` warns once for each and carries on: **world sprites**
(`attachSprite`, `attachTextSprite`), **decal batches**, and the **enchantment
rune overlay** (its rim tint is applied; the scrolling runes are not). These are
subsystems rather than shader knobs -- each needs its own pipeline and pass, not
a uniform lane.
