# Render presets

A render preset is one complete look: framebuffer resolution, lighting model,
stylize pass, colour grade, dither, and the era filter that resolves the frame.
An app picks one by name and never assembles one by hand.

```sh
./game --render-preset ps1
PSX_RENDER_PRESET=poison-swamp ./build/psx_demo
```

In the debug UI: **Render → Render Profile**. In the console: `r.preset`
lists them, `r.preset <name>` switches. In the demo: `Tab` cycles.

Source of truth is `engine/src/render/RenderPresets.cpp`; the name→id table is
`renderPresets()` in `engine/include/eng/RenderPresetInfo.h`, and every UI that
lists profiles reads that table rather than carrying its own array.

## The three families

| Family | Presets | What decides the values |
|---|---|---|
| Era | `ps1` `ps2` `gamecube` `n64` | What the hardware actually did |
| Style | `pixel-3d` `modern-ps1` `dungeon` | A deliberate art direction |
| Mood | `psx-horror` `fire-dimension` `poison-swamp` | A place, on an era chassis |

`dungeon` (id 7) is the default and is the game's own look. It is frozen:
`make visual-test` proves the shipped image does not move, so a preset review
retunes 1-6 and adds 8-10 without touching it.

### Era — canonical first

Each era preset models one console, and the rule is that a value has to be
something the hardware *did*, not something that reads as "old":

- **ps1** — 320×240 (pixelSize 3), integer vertex snap on a grid ~1.5 render
  pixels coarse, screen-space (affine) UVs, Gouraud light, 15-bit output with a
  real ordered dither that does **not** fade in the dark, and a chroma-truncating
  composite. Posterized lighting was removed in review: the PS1 had no such
  term, and its banding already comes from the 15-bit quantize one pass later.
- **ps2** — 640×448 absolute (the GS's NTSC maximum), 24/32-bit so no dither at
  all, per-fragment light, and the interlace deflicker `[1/4, 1/2, 1/4]`
  vertical tap. The one modern-looking value, bloom, is era-correct: late PS2
  titles leaned on additive glow because the GS blended in 32-bit for free.
- **gamecube** — 640×480 24-bit, Flipper's copy-out AA plus deflicker (both
  *smoothing* filters), saturated palette, and the strongest bloom of the four,
  because that is what the console's signature titles spent their TEV passes on.
- **n64** — 320×240, perspective-correct UVs (the RDP's headline advantage over
  the PS1, so warping them would be backwards), RGBA5551 with the RDP's dither,
  and the triangular three-point reconstruction filter plus the VI blur that
  give the console its softness.

Fog: every era preset sets `fogDesatBoost = 0`. Fixed-function hardware fog was
a straight lerp toward the fog colour; the desaturating fog is the dungeon
profile's own idea.

### Style — the modern half

- **pixel-3d** — David Holland's 3D-pixel-art method: depth outlines, convex-only
  edge highlights, flat toon bands, clean nearest upscale, no sharpen.
- **modern-ps1** — the explicit "both worlds" profile. Every value is either a
  PS1 artefact kept because it *is* the look (vertex snap, affine warp, dither,
  low resolution) or a modern affordance the console could not run and the eye
  reads as quality rather than as anachronism (per-fragment light, bloom, a
  faint depth outline). Nothing in between. Per-fragment lighting is the biggest
  departure and the reason the preset exists: vertex lighting is a PS1 *limit*,
  not a PS1 *look*.
- **dungeon** — the game: torchlit stone, near black between the lights,
  readability bought back with a low-opacity depth outline and a black lift.

### Mood — a place on an era chassis

Mood presets are not new rendering techniques. Each picks an era profile whole
— resolution, snap, dither, resolve filter, lighting model — and spends the
grade, bloom and vignette on a location.

- **psx-horror** — the 1999 fog-town survival-horror look. PS1 chassis
  unaltered; the effect is the palette (a sick *warm* grey, not a neutral
  desaturate), a black lift because fog light scatters into the shadows, and
  `fogDesatBoost = 0.90`, the highest here: distance drains colour before it
  drains light.
- **fire-dimension** — ember-lit hell plane on the modern-ps1 chassis. Drains
  the scene toward grey *first*, then split-tones the shadows to coals, so a
  blue-lit room becomes a hot room instead of a blue room with a warm filter.
  Low bloom threshold, warm convex highlights, `fogDesatBoost = 0.15` so the far
  end of the room stays hot.
- **poison-swamp** — same chassis and same order, one notch gentler: algae-black
  shadows, wet-moss mids, spore-green edge highlights, heavier contact ink
  because what you need to read in a swamp is where the water meets the root.

## Resolve modes

`hardware_resolve.frag` runs one filter over the finished frame. The mode used
to be assigned `float(preset)`, which quietly meant every new preset needed a
new branch in that shader. It is now an explicit field with named constants
(`eng::resolve::` in `engine/src/render/RenderPresets.h`):

| Mode | Constant | Filter |
|---|---|---|
| 0 | `kNone` | exact pass-through |
| 1 | `kPs1Chroma` | chroma truncation, hard pixels |
| 2 | `kPs2Flicker` | interlace deflicker, vertical |
| 3 | `kGamecubeCopy` | copy-out AA + deflicker |
| 4 | `kN64ThreePoint` | triangular three-point reconstruction |
| 5 | `kPixelCrisp` | local-contrast crisping |
| 6 | `kSoftCrisp` | the same, gentler |
| 7 | `kShadowCrisp` | crisping weighted into shadow only |

A mood preset picks the filter of the era it is built on: `psx-horror` runs
mode 1, not "mode 8".

## Adding a preset

1. Add `{"name", id}` to `renderPresets()` in `RenderPresets.cpp`. Ids need not
   be contiguous, and nothing indexes them positionally.
2. Add a `case id:` to `renderPresetValues()`. Start from the era or style
   profile you are dressing, and set `hardwareResolveMode` explicitly.
3. Nothing else. Every combo, the console command, the demo's `Tab` cycle and
   the `--render-preset` parser all read the table.

Verify it the way the rest of this repo verifies rendering — capture and look:

```sh
PSX_RENDER_PRESET=<name> PSX_SCREENSHOT=/tmp/x.png PSX_SCREENSHOT_FRAME=160 \
  xvfb-run -a ./build/psx_demo
```

The demo is the better subject than the game: it puts stone, liquids, a portal
and particles in one frame under a fixed camera, so two profiles are comparable
pixel for pixel.

## Tuning one live

The whole profile is editable in the debug UI (`F1` / `tune`): **Render** covers
framebuffer, lighting, bloom and grade, **Shaders** covers the stylize pass and
the resolve mode. "Copy profile as TOML" dumps every field so a tuned look can
be pasted back into a `case` in `RenderPresets.cpp` — that file, not the panel,
is where a profile is kept.

The full field list is `RenderPresetValues` in
`engine/src/render/RenderPresets.h`. It is engine-private on purpose: a game
picks a profile, it does not hand-assemble one. The one public reader is
`renderPresetBloom()`, because bloom is global post that other panels mirror.
