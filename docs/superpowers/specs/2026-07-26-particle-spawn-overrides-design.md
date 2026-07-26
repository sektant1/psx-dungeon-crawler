# Particle Spawn Overrides Design

## Goal

Keep engine particle presets useful as stable defaults while allowing every
spawn to tune its presentation without registering a duplicate preset.
The lobby will use this API to make poison and lava ash legible at showcase
scale without changing how those effects appear in other games or contexts.

## Public API

Add `eng::ParticleSpawnOptions` beside the particle effect descriptors. All
fields are optional in behavior: their identity values preserve the registered
preset exactly.

```cpp
struct ParticleSpawnOptions {
    float sizeScale = 1.0f;
    float amountScale = 1.0f;
    float lifetimeScale = 1.0f;
    float speedScale = 1.0f;
    float radiusScale = 1.0f;
    float emitterRadius = 0.0f;
    glm::vec4 colourTint{1.0f};
    glm::vec3 localOffset{0.0f};
};
```

`Renderer::spawnParticles` receives an overload accepting these options. The
existing overload remains source-compatible and behaves as if identity options
were supplied.

`radiusScale` scales all three dimensions of existing box emitters.
`emitterRadius` optionally spreads a point emitter across a box volume with
that radius; zero preserves the existing point-emitter behavior. This avoids
the ambiguous operation of multiplying a point's zero radius.

`amountScale` changes continuous emission rate for looping effects and total
burst count for one-shot effects. `sizeScale`, `lifetimeScale`, and
`speedScale` multiply the corresponding preset ranges. `colourTint`
component-multiplies emitter/ramp colour. Invalid negative or non-finite
scalars are sanitized to safe identity or minimum values and reported through
the existing logging facility.

## Runtime and Pooling

The registered `ParticleEffectDesc` remains immutable source data. Whenever a
particle system is checked out of the pool, the particle runtime reapplies the
complete preset followed by the requested spawn options. This includes default
dimensions and every emitter's position, dimensions, rate, lifetime, velocity,
and initial colour.

When the instance returns to the pool, no customized state is assumed to be
clean. The next checkout always performs the same complete configuration pass.
This prevents one spawn's larger size, tint, or lifetime from leaking into the
next user of that pooled system.

One-shot retirement uses the overridden maximum lifetime. Continuous effects
retain their existing explicit stop/despawn lifecycle.

## Authored Showcase Data

Showcase exhibits accept a nested table:

```toml
particles = "engine.poison"
particle_offset = [0.0, 0.72, 0.0] # retained as a compatibility alias

[exhibit.particle_options]
size_scale = 1.8
amount_scale = 2.0
lifetime_scale = 1.3
speed_scale = 0.8
radius_scale = 1.5
emitter_radius = 0.35
colour_tint = [0.7, 1.0, 0.3, 1.0]
local_offset = [0.0, 0.72, 0.0]
```

The loader maps this table directly to `ParticleSpawnOptions`.
`particle_offset` remains supported, but `particle_options.local_offset` wins
when both are present.

The poison and lava-ash lobby exhibits will use larger size and amount
multipliers. Engine preset defaults will not be inflated.

## Showcase Labels

`ShowcaseExhibit` gains an authored `label_offset`. The loader reads
`label_offset = [x, y, z]` from each exhibit and label construction adds it to
the existing bounds-derived anchor.

Particle-altars place labels horizontally toward the central aisle and only
slightly above the bowl, keeping billboard plaques beside—not over—the rising
particle columns. Exhibits without an offset retain current placement.

## Testing

Tests cover:

- identity options preserving the descriptor;
- size, amount, lifetime, speed, box-radius scaling, point-emitter radius,
  tint, and offset application;
- continuous and one-shot amount behavior;
- pooled instances receiving fresh defaults before new overrides;
- overridden maximum lifetime controlling one-shot cleanup;
- showcase TOML parsing of nested options and legacy offset compatibility;
- poison and lava exhibits using showcase-only scale/amount overrides;
- particle altar labels defining nonzero horizontal offsets.

The game target, focused particle tests, lobby/level tests, shader validation,
TOML parsing, and `git diff --check` form the completion verification.

## Non-goals

- Runtime mutation of an already-spawned effect.
- Per-particle scripting or arbitrary affectors.
- Duplicating presets solely for lobby presentation.
- Changing global poison or lava-ash defaults.
