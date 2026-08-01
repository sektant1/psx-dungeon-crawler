# Particles, decals and blood

The particle runtime is a custom CPU simulation drawn through GPU instancing.
Ogre's `ParticleSystem` is no longer involved: it billboarded, it approximated
ramps with its stock affectors, and it could not collide, leave decals, or draw
voxels. What replaced it is four small pieces that do not know about each other.

```
Renderer  (unchanged public facade: spawnParticles / stopParticles / ...)
   |
   +-- Particles                  orchestrator, owns handles
         |
         +-- ParticleSim          SoA pool, integration, ramps, collision
         |     ^
         |     +-- IParticleCollider   <- the game's Jolt adapter
         |
         +-- ParticleBatch        instanced sprite / voxel draw
         +-- ParticleMaterials    texture stem -> generated material
         +-- DecalSystem          projected marks, pooling, LRU budget
```

`ParticleSim` and `ParticleEmitters` include no Ogre and no Jolt, so the whole
simulation is testable headlessly with a stub collider. `tools/check_layering.py`
enforces that.

## Lifecycle

**Effect.** `ParticleLibrary` parses `particles.toml` into `ParticleEffectDesc`
and registers each with the renderer, which forwards to `Particles`. Registration
resolves the material (texture stem first, explicit material second, prototype
fallback last), picks or creates the batch for that (material, render mode), and
registers the description with the simulation. Re-registering an existing name
keeps the id and the sim slot, which is what makes hot-reload safe for live
effects.

**Spawn.** `spawnParticles` resolves the effect, applies quality scaling and the
per-spawn `ParticleSpawnOptions`, applies scale and hue jitter, and adds an
instance to the sim carrying the parent node's world transform. The returned
handle is retired automatically once the instance stops emitting and drains.

**Frame.** `Particles::update` pushes parent transforms into the sim, steps it,
drains any decal requests into `DecalSystem`, then fills every batch from the
pool and commits. Alpha batches sort back-to-front; additive ones skip sorting.

**Particle.** Integrate, evaluate the colour and size ramps at `t = age / life`,
advance rotation, retire the expired by swap-remove, then emit. Collision runs
inside integration because it needs the swept segment.

## Collision

Opt-in per effect and globally budgeted. Only particles whose effect sets a
collide response are traced, at most `max_collision_rays_per_frame` (default 64)
per frame, served round-robin — a burst degrades in accuracy, never in frame
time.

The simulation never links physics. It traces through `IParticleCollider`
(`eng/particles/ParticleCollider.h`), and the game installs
`game::JoltParticleCollider`:

```cpp
mParticleCollider.emplace(physics(), eng::kAllLayers);
renderer.setParticleCollider(&*mParticleCollider);
```

**Without a collider installed, `collide` does nothing and particles fall through
the world.** The adapter must outlive the renderer.

## Render modes

`render_mode = "sprite"` is a camera-facing quad with optional flipbook.
`render_mode = "voxel"` is an instanced cube with flat per-face shading — six
fixed tones from the face normal, no dynamic lighting. Voxels are solids: they
write depth and cull backfaces, so they sit correctly among level geometry.

Sprites write `vec4(0.0)` to MRT attachment 1 so the stylize pass does not
outline every billboard. Voxels and decals write real normal and depth so they
do get the ink outline.

Soft depth fade is written and compiles but is **not usable**: the compositor
writes depth into the same target particles draw into, and a target cannot be
sampled while it is written. It needs a depth copy target first.

## Adding a texture

1. Drop `ember.png` into `assets/particles/textures/`.
2. Optionally add overrides to `assets/particles/textures.toml`:

```toml
[texture.ember]
blend    = "additive"
flipbook = { rows = 4, cols = 4, fps = 18, loop = true }
nearest  = true
```

3. Reference the stem from an effect: `texture = "ember"`.

No `.material` file is edited. The material `Particles/Auto/ember` is generated
**on first use**, not at boot: the sprite-sheet import below declares a few
hundred textures, and paying an Ogre material for every one of them at startup
would be a cost for content the level never spawns. `ParticleMaterials::scan`
therefore only parses; `materialFor()` builds. A hot-reload rebuilds whatever
had already been built and leaves the rest unbuilt.

Every `*.toml` **directly in `assets/particles/`** contributes texture
entries, not just `textures.toml`. That is what lets a generated import land in
its own file (`sprite_sheets.toml`) without being pasted into the hand-authored
one and lost the next time the importer runs. Files are parsed in name order, so
a later file wins a duplicate stem.

## Sheet-backed textures

A texture does not need a PNG of its own. An entry that names a `sheet` carves
one animation out of a shared image:

```toml
[texture.shade_crescent_a1]
sheet = "shade32_00.png"
blend = "alpha"
flipbook = { sheet_cols = 8, sheet_rows = 16, origin_col = 0, origin_row = 5,
             frames = 8, per_row = 8, fps = 12, loop = true }
```

The sheet is cut into a `sheet_cols` x `sheet_rows` grid of cells. The animation
is the run of `frames` cells starting at (`origin_col`, `origin_row`), walking
along the row and wrapping down after `per_row` of them. `per_row = 0` means
"never wrap", which is the single-strip case every imported entry uses.

`rows`/`cols` remain the short spelling of the degenerate case — origin (0,0),
`frames = rows*cols`, `per_row = cols` — for a PNG that is nothing but a
flipbook. Both spellings produce the same window, so the shader and the runtime
only ever see one form.

Three uniforms carry that window to `particle_sprite.vert`, and
`ParticleMaterials` sets them per generated material:

| uniform | value |
|---|---|
| `flipbookCell` | UV size of one cell, `1/(sheet_cols, sheet_rows)` |
| `flipbookOrigin` | UV of frame 0's corner |
| `flipbookPerRow` | frames before the strip wraps down |

The frame index itself is per-instance (`uv3.y`), driven from the particle's age
in `Particles::fillBatches`, so a flipbook keeps its authored cadence no matter
how long a particle happens to live.

`engine/tests/ParticleFlipbookTests.cpp` locks the arithmetic and asserts that
the shader, the material script and the material builder still agree on those
three names. They drifted once already: the metadata was parsed and never
reached the GPU, so every flipbook texture drew its **entire sheet** on one
quad — which looks like art, not like a bug.

### Importing an effect pack

`tools/import_sprite_sheets.py` turns the packs under `assets/sprites/shaders`
into 304 named animations across 71 sheets (~6 MB), writing
`assets/particles/textures/sheets/*.png` and the generated
`assets/particles/sprite_sheets.toml`. Re-run it rather than editing
either; per-entry overrides belong in `textures.toml`.

It drops the colour variants on purpose. These packs repeat every animation
horizontally once per hue, and particles are tinted at runtime by their colour
ramp, so the tool keeps the least saturated block and discards the rest — which
is most of why the import is a fraction of the source art rather than a copy of
it. It also zeroes RGB under fully transparent texels, because one pack stores a
background tint there and both bilinear filtering and additive blending read it.

`engine.sprite_burst`, `engine.sprite_bolt` and `engine.sprite_coil` in
`ParticlePresets.cpp` are the worked examples: copy one and change `texture`.

That generated material is a **copy of the scripted `Engine/Particles/SpriteAlpha`**
(or `/Additive`) with only the texture swapped — `copyDetailsTo`, so a batch
already holding the `MaterialPtr` survives a hot-reload. It is a copy and not a
pass built from scratch because the instanced batch draws through
`Particles/SpriteVS`, which reads the per-instance stream the batch binds. A
hand-built fixed-function pass has no vertex program, and GL3Plus rejects it —
at the first *draw*, not at load, so the effect has to be spawned before
anything notices. The boot warmup will not catch it: these materials do not
exist yet when it runs.

A texture stem is meaningless on `render_mode = "voxel"`: `particle_voxel.frag`
has no sampler at all and shades six fixed face tones off the instance colour,
so a voxel effect's art comes from its `colour_ramp` alone.

The boot warmup now catches this class of mistake for every material that
exists by then (`engine/src/render/Warmup.cpp`). `Material::getBestTechnique()`
alone does not: `Technique::checkHardwareSupport` only rejects a shaderless pass
when `Pass::isProgrammable()` is true, and a pass with *no* programs at all is
not programmable, so it is reported as supported and throws later at
`SceneManager::_setPass`. The warmup therefore checks each pass for a vertex and
fragment program directly and logs an error, which turns a crash-on-first-spawn
into a line in the boot log. Ogre's own shaderless built-ins (`BaseWhite`,
`DefaultSettings`, `Ogre/*`) are skipped — nothing draws them.

## Adding an effect

Append to `assets/config/particles.toml`:

```toml
[[effect]]
name = "stone_debris"
visual_role = "feedback"
texture = "debris"          # or material = "Engine/Psx/Fire"
render_mode = "voxel"       # "sprite" | "voxel"
base_width = 0.09
base_height = 0.09
quota = 40
loop = false
burst_count = 12
acceleration = [0.0, -14.0, 0.0]
drag = 0.25
collide = "bounce"          # "none" | "die" | "bounce" | "decal"
restitution = 0.22
friction = 0.6
[[effect.emitter]]
direction = [0, 1, 0]
angle = 60
ttl_min = 1.2
ttl_max = 2.4
velocity_min = 1.5
velocity_max = 4.0
[[effect.colour_ramp]]
t = 0.0
rgba = [0.5, 0.48, 0.45, 1.0]
```

Then `renderer.spawnParticles("stone_debris", point)`. No C++ was added.

`collide = "decal"` additionally needs `decal_profile = "<id>"`; without it the
particle just dies, so a missing profile cannot silently spam marks.

## Decals

Generic projected quads on the hit surface, offset along the normal and spun
randomly so repeated hits are not stamped identically. This is deliberately not
a clipping projector: a mark can leak past a sharp corner, which is the accepted
trade for not building a mesh clipper.

Three properties keep them from eating a frame budget:

- **Budget.** A hard `maxDecals` cap with LRU eviction.
- **Fade.** Alpha ramps down over `fade_time`; `lifetime = 0` means permanent
  until evicted.
- **Pooling.** A profile with `pool = true` grows toward `max_size` and merges
  with a neighbour within `merge_radius` on the same plane, so a bleeding body
  grows one pool instead of stacking hundreds of quads.

`DecalSystem` never learns what blood is. Profiles are registered by string id
by whoever owns the TOML.

## Blood

`game/src/BloodSystem.cpp` reads `assets/config/blood.toml` and is the only thing
that maps "something was hurt" onto effects and marks. Adding a creature type is
a `[[profile]]` block; adding a kind of mark is a `[[decal]]` block.

```toml
[[decal]]
id = "blood_splat"
texture = ""                # empty: flat quad tinted by `colour`
size_min = 0.18
size_max = 0.34
colour = [0.40, 0.03, 0.025, 0.92]
colour_jitter = [0.06, 0.015, 0.015]
lifetime = 0.0              # permanent until the budget evicts it
fade_time = 1.5

[[profile]]
id = "human"
spray_effect = "blood_spray"   # sprite spatter, leaves decals where it lands
gib_effect = "blood_gibs"      # voxel chunks, Heavy severity only
decal = "blood_splat"          # instant mark, stamped on the floor below
pool = "blood_pool"
drip_effect = "blood_drip"
drip_hp_fraction = 0.35
amount_scale = 1.0
damage_bias = 0.55             # 0 = along the normal, 1 = along the blow
```

`damage_bias` exists because spraying purely along the surface normal makes every
wound look hit head-on, while spraying purely along the blow buries the spray in
the wall behind the target.

### Who calls it

Both damage seams in `game/src/main.cpp`, through one `bleed()` helper, so the
player and an enemy bleed by the same rules:

| Seam | Event |
|---|---|
| `playerHit` | a player projectile landed on a registered combatant |
| the enemy hit-player callback | an enemy attack got through i-frames and deflect |
| the death callback | `spawnPool` where the body fell, before the corpse goes dynamic |

```cpp
// severity is a fraction of the victim's health, not an absolute: the same
// fifteen points is a scratch on a boss and a maiming on a rat
blood.spawnHit(renderer, profile, point, -dir, dir,
               bloodSeverityFor(result.dealt, health->max, result.killed));
```

Which profile a creature bleeds is data: `visual.blood` in `enemies.toml`
(`"human"` by default, `"undead_ichor"` for the hollows, `""` for the stone
brute, which chips rather than bleeds), and `player.blood` in `game.toml`.

**The wound and the mark are separate calls.** `spawnHit` throws spray, gibs and
mist at the wound; `spawnSplat` stamps the profile's decal on a surface the
*caller* found -- `bleed()` casts a ray straight down against static geometry
only. A decal is a world-space quad, so painting it at the wound leaves one
hanging in the air where a walking enemy's chest used to be, permanently for the
profiles that never fade. Spray droplets still mark whatever they land on
through `collide = "decal"`.

## Editor preview

The material staging scene has two modes, chosen by
`assets/config/material_preview.toml`:

```toml
quad = ["Engine/Particles/*", "Particles/Auto/*", "Editor/FireIcon"]
```

Anything unlisted previews as the lit sphere, unchanged. A listed material
previews as a front-facing unlit quad with a time uniform driving the shader —
an animated inventory icon — because a shader icon is judged on its animation,
not on how it takes light. The RTT thumbnail swatch follows the same mode.

## The Particles tuning panel

`eng::ParticlePanel` (`engine/include/eng/debug/ParticlePanel.h`) is a tab on the
debug console, docked in the `Content` group beside the shader panels — an
effect is authored against the bloom and grade settings that live there. It is
engine tooling, like the surface panels: everything it touches is engine
machinery, so the game and the demo both install it rather than each writing
their own.

```cpp
mParticlePanel.install(tools, eng::PanelGroup::Content);   // once
mParticlePanel.setSources(&renderer, &particleLibrary);    // every frame
mParticlePanel.update(dt);                                 // every frame
mParticlePanel.releaseSpawns();                            // before a scene clear
```

`update(dt)` runs whether or not the tab is visible, because it retires the
one-shots the panel spawned; a session that only ticked while the tab was open
would walk away holding every burst it ever fired.

What it does:

- **Library** — filter and select an effect, clone one under a new name, save
  the whole library back to its `particles.toml`.
- **Spawn** — place at a distance along the view ray (with a drop, so a ground
  effect lands on the floor rather than at eye height) or at a fixed world
  point. Per-spawn overrides — size, amount, lifetime, speed, emitter radius,
  tint — are the same `ParticleSpawnOptions` gameplay passes. Stop emitting,
  despawn all, live particle count.
- **Editor** — every field of `ParticleEffectDesc`: the texture picker (with its
  own filter, because the import declares 300+ stems, and a readout of the
  chosen strip's frame count, rate, cell grid and blend), render mode, card
  size, jitters, quota, loop/burst, acceleration, drag, visual role, collision,
  the emitter list, and the colour/size ramps.

Edits write through `ParticleLibrary::reregister`, the same call the TOML
hot-reload uses, once per frame after every widget has had its say — so a panel
edit and a file edit land identically, and dragging a dozen sliders at once does
not rebuild the batch a dozen times.

Two omissions are deliberate. There is no delete: an effect removed from under a
live handle is a crash the panel cannot see coming, and the authored file is one
text editor away. And an existing effect's name is read-only, because the
Renderer keys effects by name and a rename would strand every instance holding
the old id — Clone exists for that.

## Known gaps

- Soft depth fade needs a depth copy target (above).
- ~~Voxel cube winding was unverified.~~ Checked analytically: `buildCube`
  winds each face counter-clockwise seen from outside (the cross product of the
  first triangle's edges points along the face normal), which is the front-face
  convention `cull_hardware clockwise` expects — the same one `psx.material`
  documents as this engine's back-face culling default. Correct as written.
- Ogre's offscreen simulation pause is gone. The CPU pool is a fixed capacity,
  so cost is bounded by capacity rather than by what is on screen; pausing would
  buy little and cost a catch-up pop when an emitter returns to view.
- ~~No effect points at the generated textures.~~ `blood_spray` and
  `blood_drip` now use `texture = "blood_drop"`, which is what surfaced the
  fixed-function bug in the generator described above.
- The shipped placeholder textures are generated by
  `tools/gen_particle_textures.py` and no effect in `particles.toml` points at
  them yet, so they change nothing on screen until one does. `blood.toml`'s
  decals still carry `texture = ""` and render as flat tinted quads; set
  `texture = "blood_splat"` to pick up the generated art.
- ~~`flipbookGrid` is a `float2` and only 4x4 sheets exist, so whether it is
  (cols, rows) or (rows, cols) is untested.~~ Gone. It was never written by
  anything, so *every* flipbook drew its whole sheet; it is now the
  `flipbookCell`/`flipbookOrigin`/`flipbookPerRow` window described above,
  covered by `particle_flipbook` and exercised by 8-, 10- and 12-frame
  non-square strips from the import.
- Additive particles ignore the alpha of their colour ramp.
  `Engine/Particles/SpriteAdditive` is `scene_blend add`, which reads only `src.rgb`,
  so a ramp that fades to zero alpha does not fade an additive sprite — it has
  to fade to black instead. The imported sheets therefore default to `alpha`.
  Fixing it properly means `scene_blend src_alpha one`, which changes every
  shipped additive effect and wants its own visual pass.
