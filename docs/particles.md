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

1. Drop `ember.png` into `engine/assets/particles/textures/`.
2. Optionally add overrides to `engine/assets/particles/textures.toml`:

```toml
[texture.ember]
blend    = "additive"
flipbook = { rows = 4, cols = 4, fps = 18, loop = true }
nearest  = true
```

3. Reference the stem from an effect: `texture = "ember"`.

No `.material` file is edited. The material `Particles/Auto/ember` is generated
at boot.

## Adding an effect

Append to `game/assets/particles.toml`:

```toml
[[effect]]
name = "stone_debris"
visual_role = "feedback"
texture = "debris"          # or material = "Engine/Particles/Fire"
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

`game/src/BloodSystem.cpp` reads `game/assets/blood.toml` and is the only thing
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
decal = "blood_splat"          # immediate mark under the wound
pool = "blood_pool"
drip_effect = "blood_drip"
drip_hp_fraction = 0.35
amount_scale = 1.0
damage_bias = 0.55             # 0 = along the normal, 1 = along the blow
```

Call it from gameplay:

```cpp
blood.spawnHit(renderer, "human", point, normal, damageDir,
               BloodSeverity::Heavy);
```

`damage_bias` exists because spraying purely along the surface normal makes every
wound look hit head-on, while spraying purely along the blow buries the spray in
the wall behind the target.

## Editor preview

The material staging scene has two modes, chosen by
`engine/assets/material_preview.toml`:

```toml
quad = ["Engine/Particles/*", "Particles/Auto/*", "Editor/FireIcon"]
```

Anything unlisted previews as the lit sphere, unchanged. A listed material
previews as a front-facing unlit quad with a time uniform driving the shader —
an animated inventory icon — because a shader icon is judged on its animation,
not on how it takes light. The RTT thumbnail swatch follows the same mode.

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
- The shipped placeholder textures are generated by
  `tools/gen_particle_textures.py` and no effect in `particles.toml` points at
  them yet, so they change nothing on screen until one does. `blood.toml`'s
  decals still carry `texture = ""` and render as flat tinted quads; set
  `texture = "blood_splat"` to pick up the generated art.
- `flipbookGrid` is a `float2` and only 4x4 sheets exist, so whether it is
  (cols, rows) or (rows, cols) is untested. Check before trusting a non-square
  flipbook.
