# Portals

The portal is two things that are easy to confuse: a **prop** (a membrane slab
inside a kit surround, assembled by `createPortalProp` in
`game/src/SceneFactory.cpp`) and a **look** (the shader the membrane wears).
This document is about the look. The prop's depth ordering is the subject of
`game/tests/PortalGeometryTests.cpp`, and is where to go if the membrane is
showing past the stone rather than looking wrong.

## Where the look lives

The shaders are split in two families, on purpose:

```
surface_common.glsl        the KERNEL: presentation shared by any stylised surface
  portal_pattern.glsl      one PROFILE of it: the swirl that makes a portal
    portal.frag            field = the authored flow texture
    prototype_portal.frag  field = procedural noise
  surface.vert             shared vertex stage

scroll_common.glsl         the OTHER family: sliding tiling art
  liquid.frag / prototype_liquid.frag / lava.frag
```

**The kernel owns everything that is the same whatever the surface is:** stepped
time, the metre-sized pixel grid, Bayer dither, the 4-tone palette, emission,
and what a thick mesh's rims do. It has no `main()` — that is the profile's, and
a test asserts it stays that way, because the moment a pattern lands in the
kernel no other surface type can reuse it.

**A profile owns only what makes it that thing.** `portal_pattern.glsl` is the
log-polar swirl, the depth layers, the parallax, the event horizon and the
containment ring. Writing another surface type — a forcefield, a rune panel,
coloured glass — means copying *that* file, not the kernel.

The two `.frag` files are three lines each. They differ only in answering
`float surfaceField(vec2 uv)`, which the kernel declares and they define, so the
authored and art-free variants cannot drift apart.

**The scrolling family is deliberately not built on the kernel.** Water, slime
and lava get their look from sliding tiling art across the mesh, not from a
field evaluated per pixel. Two shapes of problem, two shaders; forcing them
together would give one that fits neither. `scroll_common.glsl` carries their
shared half — palette, stepped time, pixel grid, and the scroll offset.

That offset is wrapped with `fract()` before it is added, and that is the one
correction over the classic tutorial version: `time` grows without bound, so
`uv + speed * time` loses mantissa bits and a scrolled liquid visibly quantises
into judder after some minutes. Wrapping the *offset* is exactly equivalent
under a repeating sampler; wrapping the sampling *coordinate* instead would put
a hard seam where it wraps.

The vertex stage forwards more than UVs, and each extra output pays for
something specific:

| Output | Buys |
|---|---|
| `surfaceLocal` | the mesh's size in metres, read off the mapping (see below) |
| `surfaceNormal` | telling the two big faces from the rims of a thick mesh |
| `surfaceView` | parallax between the swirl's depth layers |

## The frame, in order

1. **Stepped time** (`surfaceStepFps`) — the stop-motion cadence the rest of the
   game's VFX share. Everything downstream reads this, never `time`.
2. **A pixel grid in metres.** `surfaceLocal` is metres and `surfaceUV` is 0..1
   over the same quad, both linear, so `fwidth(surfaceLocal.xz) /
   fwidth(surfaceUV)` *is* the mesh's size. The grid is then authored in
   centimetres (`surfaceTexelSize`, 5.5 cm) rather than in cells across the UV.
   This is what makes the shader survive a resized membrane: a bigger portal
   gets more pixels instead of bigger ones, and pixels stay square on a slab
   that is wider than it is tall.
3. **Log-polar depth layers.** Three samples of the field, taken in
   (turns around the swirl, log of the radius) space. Equal steps along that
   second axis are equal *ratios* of radius, so a layer scrolling at a constant
   rate reads as an endless fall inward rather than a texture sliding across a
   quad. Deeper layers turn slower, are scaled differently, and count for less.
4. **Parallax.** Each layer is shifted by the eye's direction in the membrane's
   own space, by `portalParallax` metres of depth per layer. Straight on it does
   nothing; walk past the portal and the swirl's interior slides behind its
   opening, which is the whole difference between a hole and a poster.
5. **Shaping.** The analytic spiral (the look the portal has always had) is
   blended with the field by `portalFieldWeight`, the middle burns out into the
   core tone, the field dims as it climbs outward, and it is cut at the
   containment ring — that cut is what makes a rectangular slab read as a round
   maw with dark corners instead of a lit green square.
6. **Ordered dither**, 4x4 Bayer, applied to the value *before* the palette
   quantises it, so the band edges stipple like indexed colour instead of
   contouring. Noise dither would crawl; this does not.
7. **A four-tone palette** — void / body / arms / core. Core and arms are
   deliberately above 1.0: that is what feeds them to the bloom pass.

## The rims

The membrane is a slab (`PortalPropStyle::membraneThickness`, 14 cm), so four
faces close its thickness. `surfaceEdgeMode` decides what they do:

| Mode | What the four thickness faces do |
|---|---|
| `0` (default) | continue the front pattern through the thickness |
| `1` | a band of their own — `surfaceEdgeGlow` / `surfaceEdgeFlow` |
| `2` | discarded |

**Mode 0 works because the field position comes from object space, not the UVs.**
The rim faces share the face's XZ footprint and differ only in Y, so taking the
pattern from `surfaceLocal.xz` alone extrudes it through the thickness and the
sides become the edge of the same swirl. `surfaceUV` cannot do this: on a rim
those UVs run along the edge and across the thickness, a different mapping
entirely. For the face the two are the same number — the plane spans ±half in X
and Z with linear UVs across it — so only the rims changed.

Mode 1 is the older look: a band running along the edge, brightest
mid-thickness, quantised to the same texel size as the face so the slab is not
fringed with screen-resolution pixels.

Mode 2 uses `discard` rather than an early return with alpha 0, so the rims stop
writing depth as well. Dimming cannot substitute for it: the palette's first
tone is a colour and not black, so a "dark" rim is still a visible band — and
once a profile pushes `surfaceEdgeGlow` past saturation the rim clamps to the
core tone, emission adds on top, and the edge blooms out to a white strip.

## Tuning

Every knob is a uniform with a material default, and all of them are live.

Open the tuning panel (**F1**, or `tune` in the dev console) → **Portal** tab,
next to Combat / Feel / Enemies. Pick the descent or ascent
profile, drag; each control writes straight through to the material with
`Renderer::setMaterialParam`, so the swirl changes under the cursor. Nothing is
cached engine-side and no rebuild is involved.

- **Push all** re-sends the whole panel, for when a material was reloaded under
  it.
- **Copy as material params** puts the block on the clipboard. Paste it into
  `game/assets/materials/vfx.material` — *that* file is where a tuned portal is
  kept; the panel is only where it is found.

The defaults live in `engine/assets/programs/vfx.program` (and its art-free twin
`prototype_vfx.program`). A material profile overrides only what makes it a
profile: `Game/PortalDown` is fel-green and flows inward, `Game/PortalUp` is
arcane-blue and runs outward and counter-rotating, so the pair reads as
opposites from across the room.

| Group | Knobs |
|---|---|
| Palette | `surfaceDark` `surfaceMid` `surfaceBright` `surfaceCore` `surfaceBrightness` `surfaceDither` |
| Motion | `surfaceStepFps` `portalFlowSpeed` `portalSwirlSpeed` `portalTwist` `portalArms` `portalArmWidth` |
| Depth / resolution | `surfaceTexelSize` `surfacePixelGrid` `portalDepthScale` `portalParallax` `portalFieldWeight` |
| Shape | `portalCoreRadius` `portalCoreBoost` `portalRimRadius` `portalRimWidth` `portalRimIntensity` `portalEdgeFade` |
| Emission (what blooms) | `surfaceGlowColour` `surfaceGlowStrength` `surfaceGlowThreshold` |
| Slab rims | `surfaceEdgeMode` `surfaceEdgeGlow` `surfaceEdgeFlow` |
| Dressing | glow colour, glow range, wisp effect (not shader params — see below) |
| Bloom (global post) | enabled, threshold, intensity — the whole frame, not just the portal |

### Emission vs. palette

These are the two knobs to reach for when the portal should glow more, or glow a
different colour, and they are deliberately not the palette.

**Bloom has no colour of its own.** The post pass blurs whatever exceeds its
threshold, in that pixel's own colour. So with the palette alone the four tones
are doing two jobs at once — picking what colour the portal *is* and deciding
how hard it glows — and the two cannot be tuned apart: raising `surfaceBrightness`
for more glow also washes the palette out, and a palette authored entirely below
1.0 cannot glow at all no matter how the bloom pass is set. That is not
hypothetical; it is what a violet retune of `Game/PortalDown` ran into, with
every tone under 1.0 and only the mid channel accidentally crossing.

`surfaceGlowColour` is **added** on top of the palette, so it is the thing that
crosses 1.0 and therefore the thing that blooms — in its own colour, whatever
the palette is. A green portal can have a white-hot core. `surfaceGlowStrength`
is then one slider for emission, and `surfaceGlowThreshold` picks how much of the
swirl participates (1 = the core only, 0 = the whole thing). `surfaceBrightness`
scales palette and glow together, so it stays a master gain rather than a second
competing emission control. The glow reaches the slab's rims too, so a thick
membrane's lit edge is not the one dull part of it.

**Bloom is global**, and the tab says so. It sits here anyway because the core
and arms are authored above 1.0 *specifically* to feed it: tuning those tones
without the pass that blooms them is tuning half the effect. It drives the same
`Renderer::setBloomEnabled` / `setBloomParams` as the engine's Render tab, is
shared by both portal profiles (there is only one bloom), and is seeded from the
`dungeon` render preset the game starts in. It is not written by the Copy
button — bloom belongs to the render profile, not to a material.

The **Dressing** section edits the live prop rather than the material: the light
the portal throws into the room (`Renderer::setLightColour` / `setLightRange`)
and the particle effect drifting in front of it, picked from every effect in
`game/assets/particles.toml`. Switching the effect despawns the portal's
instance and spawns one of the new definition on the same node, because an
effect is a definition and the portal holds one instance of it. That selection
lives only for the session; the prop's shipped values are `PortalPropStyle`
(`lightColour`, `lightRange`, `particles`) in `game/src/SceneFactory.h`, set per
portal in `LiveLevel.cpp`.

`portalArmWidth` is how much of each turn is lit arm rather than dark gap. The
raw sine is an even 50/50 split and the palette thresholds are fixed constants,
so without it an arm has exactly one thickness and the only way to fatten it is
to recolour the palette — changing the portal's colour to fix its shape. This
skews the duty cycle with a power curve instead, leaving colour alone. `0.5` is
the raw sine, so it is a no-op default; below thins the arms toward threads,
above fattens them until the gaps are what read as the pattern.

`portalArms` wants whole numbers: the angular sampling axis is scaled by the arm
count so it wraps at a texture repeat, and a fractional count puts a seam down
one arm.

## Adding a portal profile

No shader work. Copy a `material` block in `game/assets/materials/vfx.material`,
rename it, change the palette and the flow, and point a `PortalPropStyle` at the
new name:

```
material Game/PortalVoid
{
    technique { pass {
        cull_hardware none
        depth_write on
        vertex_program_ref PixelVfx/PortalVS { }
        fragment_program_ref PixelVfx/PortalFS
        {
            param_named surfaceDark float4 0.02 0.01 0.05 1.0
            param_named surfaceMid float4 0.35 0.10 0.75 1.0
            param_named surfaceBright float4 0.90 0.35 1.60 1.0
            param_named surfaceCore float4 2.10 1.30 2.60 1.0
            param_named portalFlowSpeed float 0.45
        }
        texture_unit { texture slime_stylized.png filtering none
                       tex_address_mode wrap }
    } }
}
```

Two rules the material has to keep: `filtering none` (the flow texture is
pixel art) and `tex_address_mode wrap` (both sampling axes wrap). Add the
matching art-free profile to `engine/assets/materials/prototype_vfx.material` if
the portal should still read when the textures are missing.

## Verifying

```sh
./game --scene portal    # spawns facing the descent portal, sim frozen
make screenshot SHOT=/tmp/portal.png FRAME=200
```

`engine/tests/VfxShaderAssetTests.cpp` holds the structural invariants: the
animation is frame-stepped, the output is palette-quantised and dithered, the
grid is sized in metres, the rims shade separately, and both fragment shaders
stay a thin lookup over the shared body.
