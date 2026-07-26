# Modern Pixel VFX Redesign

## Goal

Redesign particles, enchantments, portals, and liquids so they share a clean
modern 3D pixel-art style. Fix the procedural meshes that support these effects,
increase particle size and density, strengthen bloom, and prioritize visual
quality and polish over the current software-rendered performance baseline.

General world materials, unrelated model geometry, and strict PS1 hardware
simulation are outside this pass.

## Visual Direction

Effects should use crisp pixel clusters, limited color ramps, nearest-neighbor
texture sampling, deliberately stepped detail, low-poly silhouettes, and stable
motion. Animation may remain smoother than authentic PS1 rendering where that
improves readability. Dithering and alpha transitions should shape an effect
rather than cover it in noise.

Bloom is part of the authored silhouette:

- A small bright core may exceed the bloom threshold.
- A larger colored body must retain visible pixel structure.
- Transparent overlap must not turn the whole effect into a featureless glow.
- Smoke and ordinary water do not bloom.

Visual quality is the first-pass constraint. Draw calls, triangles, particle
counts, and frame time will still be measured and reported so later optimization
has evidence.

## Shared VFX Language

The four effect families will share a small set of conventions rather than one
over-generalized shader:

- Quantized color ramps with effect-specific palettes.
- Saturated high-chroma particle palettes with separated luminance bands so
  hues survive bloom instead of clipping uniformly toward white.
- Pixel-grid masks sampled with nearest filtering.
- Stepped secondary animation layered over smooth world-space motion.
- Hard interior silhouettes with a narrow optional soft outer band.
- Restrained additive passes reserved for emissive cores and highlights.
- Deterministic animation under the engine's fixed timestep and seed.

Dedicated shaders remain separate where their geometry and depth behavior
differs. Particles, enchantment overlays, portal membranes, and horizontal
liquids must not be forced through the generic sprite shader.

## Particles

Particles remain Ogre billboard systems for efficient camera-facing rendering.
The renderer will continue applying per-spawn overrides without mutating
registered presets or leaking state through pooled instances.

### Presentation

- Increase authored base sizes and emission density for fire, poison, smoke,
  lava ash, spell trails, impacts, and showcase effects.
- Improve spawn volumes so dense effects occupy readable shapes instead of one
  bright origin point.
- Use effect-specific pixel masks. Fire, poison, ash, smoke, rain, spell cores,
  trails, and impacts must not all resemble variants of the sparkle texture.
- Add variation through size, rotation, lifetime, velocity, palette, and
  animation phase where the available particle attributes permit stable
  per-particle variation.
- Increase palette saturation for fire, poison, lava ash, spell trails, and
  impacts. Preserve darker body colors beside emissive cores so the increased
  saturation remains visible under bloom.
- Use deliberate size and alpha curves: establish, readable body, then a clean
  breakup or fade.
- Layer selected hero effects into a colored body and a smaller emissive core.
  Bloom comes from the core rather than increasing every particle's intensity.
- Preserve ordinary alpha blending for smoke and use additive blending only for
  energy, flame cores, and sparks.

### Quality

Particle texture masks must remain sharp when particles are enlarged. Atlas
animation should advance at a readable stepped cadence while particle movement
remains smooth. Dense effects must retain gaps and internal contrast instead of
forming opaque rectangular clouds.

## Enchantments

Enchantments remain recursive material overlays so they work across composite
models and preserve each submesh's base material.

- Keep object-space triplanar projection for meshes with missing or poor UVs.
- Replace thin, smoothly anti-aliased procedural symbols with chunky,
  grid-aligned rune marks.
- Quantize rune intensity, pulse, and color into a small number of bands.
- Keep pulse timing smooth enough to read, but step the visible intensity.
- Restrict the Fresnel contribution to a controlled pixel-banded edge accent.
- Separate the readable rune body from the small bloom-driving highlight.
- Configure blend, depth, culling, and shadow behavior explicitly on the
  generated overlay pass.
- Preserve style palettes for arcane, fire, poison, and frost while making each
  palette distinct at low resolution.

## Portals

Portals will use a purpose-built low-poly prop and membrane pipeline.

### Mesh

- Build a stepped arch from shared beveled block primitives: two pillars,
  stepped shoulder blocks, and a lintel or keystone arrangement.
- Avoid smooth ring geometry and avoid a plain rectangular doorway silhouette.
- Validate scale composition so frame thickness and depth are not multiplied
  unexpectedly by parent transforms.
- Build the membrane as a correctly oriented vertical disc or faceted opening
  whose winding, normals, UVs, and inset prevent clipping and back-face errors.
- Keep portal labels, interaction anchors, lights, and particles attached to
  the portal root.

### Shader

- Replace the generic continuously scrolling sprite material with a dedicated
  portal shader.
- Combine a stable central motif with low-resolution directional flow.
- Advance fine texture details at a stepped cadence without stepping the whole
  portal transform.
- Quantize the descent and ascent palettes independently.
- Use pixel-edged distortion and a narrow emissive rim, not smooth fluid noise.
- Write depth consistently so the portal reads as a solid magical threshold,
  while preventing overlapping faces from accumulating excessive bloom.

## Liquids

Water, lava, and toxic slime will use a dedicated horizontal-surface shader.

- Blend two low-resolution scrolling layers with distinct directions and
  stepped UV offsets.
- Quantize the result into authored palette bands.
- Add sparse blocky highlights; lava may emit and bloom, water may use subdued
  highlights, and toxic slime may use a small emissive accent.
- Remain opaque and stylized. Realistic refraction, reflection, and transparency
  are out of scope.
- Avoid grazing-angle shimmer with stable sampling and mip behavior that does
  not blur the magnified pixel pattern.

Pool surfaces will use a correctly oriented plane or disc with verified winding,
normals, UVs, and a small recess inside the plinth. The surface must not
intersect the basin or inherit an unintended nonuniform scale.

## Diagnostics and RenderDoc

The engine-owned visual tools will produce deterministic screenshots,
benchmarks, and exact-frame `.rdc` captures. The RenderDoc MCP must inspect the
resulting replay when the capture environment supports it.

The current Xvfb capture succeeds but MCP replay fails while requesting an
OpenGL ES 3.x replay context. Investigation will identify whether the capture
metadata, replay device selection, or MCP launch environment is responsible.
This tooling issue may be fixed if the change is narrowly required for a
replayable capture; it must not be hidden by substituting screenshot-only
inspection.

For representative particle, enchantment, portal, and liquid draws, inspection
will cover:

- Vertex and index counts plus post-vertex positions.
- Bound vertex/fragment shaders and uniforms.
- Blend, depth, culling, and render-target state.
- Bound textures and sampling state.
- Mesh topology, UVs, normals, winding, and transformed orientation.
- Pixel history for emissive cores and overlapping transparent regions where
  supported.

## Testing

Automated tests will be added before production changes where a stable contract
can express the defect.

- Shader-contract tests for dedicated portal/liquid programs, pixel
  quantization, explicit overlay state, and required uniforms.
- Particle preset and spawn-option tests for increased size/density, sanitized
  overrides, pool reset behavior, and distinct material assignments.
- Pure procedural-geometry tests for vertex/index counts, finite attributes,
  index bounds, triangle winding, normal direction, UV ranges, portal
  composition, and liquid-surface orientation.
- GLSL compilation or validation for every changed shader.
- Existing particle, enchantment, primitive, model, lobby, and level tests.
- Deterministic before/after screenshots at the same frame, seed, camera, map,
  and render preset.
- Before/after benchmark and RenderDoc evidence, with performance reported but
  not used as the acceptance gate for this quality-first pass.

## Acceptance Criteria

- Particles are visibly larger, denser, more varied, and more polished while
  retaining crisp internal pixel structure and distinctly saturated palettes.
- Hero particles and emissive surfaces produce stronger shaped bloom without
  becoming featureless additive blobs.
- Enchantment runes read clearly on UV-less and composite low-poly meshes.
- Portal frames have an intentional stepped-arch silhouette, and membranes have
  stable pixel flow with correct orientation and depth behavior.
- Water, lava, and slime read as distinct pixel-art liquids rather than generic
  scrolling sprites.
- Supporting procedural meshes pass topology, winding, normal, UV, and
  orientation tests.
- Changed shaders validate, focused and integration tests pass, and a
  deterministic visual comparison is recorded.
- A replayable RenderDoc capture is inspected through MCP, or the exact external
  replay limitation is documented with all in-scope remediation exhausted.

## Non-Goals

- Reworking the general PSX world shader or post-processing pipeline.
- Replacing the Ogre particle system with a GPU simulation framework.
- Physically based water, volumetric fluids, or realistic transparency.
- Reintroducing smooth torus/ring portal geometry.
- Making current llvmpipe frame time an acceptance gate.
