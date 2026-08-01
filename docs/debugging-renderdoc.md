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
