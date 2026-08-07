# Enemies {#doc-enemies}

Enemies are data. Adding one is adding a table to `assets/config/enemies.toml`;
there is no C++ to write, no factory to register with, and no `switch` on enemy
type anywhere in the codebase.

## Contents

- [The shape of the system](#the-shape-of-the-system)
- [How to add an enemy](#how-to-add-an-enemy)
- [Archetypes and inheritance](#archetypes-and-inheritance)
- [Every field](#every-field)
- [Spawners](#spawners)
- [The brain](#the-brain)
- [Design decisions, and why](#design-decisions-and-why)
- [Saving and loading](#saving-and-loading)
- [Tuning it live](#tuning-it-live)
- [Tests](#tests)

---

## The shape of the system

```
enemies.toml ──> EnemyLibrary ──> EnemyDef ─┐
                                            │
spawners.toml ─> EnemySpawner ──────────────┼──> EnemySystem::spawn
level markers ──┘                           │
                                            v
                     entity on the combat registry:
                       EnemyTag  EnemyBrain  EnemyMotion  EnemyRender  EnemyOrigin
                       Health    Resistances FactionTag   BodyLink      (combat)
                       Poise     Stamina     ActionState               (feel)

per fixed step:
  CombatSystem::fixedStep   advances ActionState / stamina / poise
  EnemySpawner::update      pacing -> spawn requests
  EnemySystem::fixedStep    senses -> EnemyAI::think -> Intent -> attack + movement
```

| File | Owns |
|---|---|
| `enemy/EnemyDef.h` | the authored row: body, visual, stats, locomotion, perception, behaviour, attacks |
| `enemy/EnemyLibrary.{h,cpp}` | parsing `enemies.toml`, archetype inheritance, resolving damage channels |
| `enemy/EnemyComponents.h` | the components an enemy entity carries |
| `enemy/EnemyAI.{h,cpp}` | the state machine. Pure: senses in, intent out |
| `enemy/EnemySystem.{h,cpp}` | bodies, nodes, components, movement, attack delivery, corpses |
| `enemy/EnemySpawner.{h,cpp}` | pacing rules (pure) |
| `enemy/EnemySpawnerRuntime.cpp` | the part of the spawner that needs a world |
| `enemy/EnemySave.{h,cpp}` | the save format: snapshot struct + pure codec |
| `enemy/EnemySaveRuntime.cpp` | capture/restore against a live world |

The layering rule is worth stating plainly: **`EnemyAI` touches nothing.** It
reads a `Senses` struct and writes an `Intent`. It cannot query physics, cannot
move a body and cannot spawn anything. Every behaviour rule is therefore
testable without an engine, and there is exactly one place (`EnemySystem`) that
turns a decision into a change in the world.

---

## How to add an enemy

Four lines is a real enemy:

```toml
[enemy.bone_thrall]
inherits = "grunt"
name     = "Bone Thrall"
category = "Undead - brittle"
```

It inherits the grunt archetype's body, speed, senses, aggression and move
list. Run the game and spawn it from the debug console's **Enemies** tab.

To make it its own thing, override what differs:

```toml
[enemy.bone_thrall]
inherits = "grunt"
name     = "Bone Thrall"
category = "Undead - brittle, fast"

[enemy.bone_thrall.stats]
health = 30.0

[enemy.bone_thrall.stats.resistances]
pierce = 0.5      # arrows rattle through a skeleton
blunt  = -0.5     # a mace does not

[enemy.bone_thrall.locomotion]
chase_speed = 5.0

[[enemy.bone_thrall.attack]]
id           = "claw"
weapon       = "claw"
max_range    = 2.0
cooldown     = 1.0
windup       = 0.3
recovery     = 0.4
```

Then place it, either as a level marker named `enemy.bone_thrall` (optionally
`enemy.bone_thrall.ambush` to use a pacing preset), or as a `[[spawn]]` entry
in `assets/config/spawners.toml`.

Two rules that catch people out:

- **`[stats.resistances]` and `[[...attack]]` replace, they do not merge.**
  Stating one attack means the enemy has exactly that one, not that one plus
  the archetype's. Anything else would make "this variant has no resistances"
  impossible to write.
- **`weapon` names a row in `weapons.toml`**, which is where damage, damage
  type, crit and crowd-control live. The attack entry owns *range, cadence and
  timing*; the weapon owns *what a landed hit does*. That split is why one
  weapon backs several enemies' moves, and why an enemy hit runs through the
  identical damage pipeline a player hit does.

---

## Archetypes and inheritance

`[archetype.<id>]` rows are templates. They never spawn. `[enemy.<id>]` rows
spawn, and either kind may name any other in `inherits`. Resolution is
field-wise and transitive, so a three-deep chain is normal:

```
base ──> ranged ──> caster ──> [enemy.hex_acolyte]
```

The shipped archetypes are meant to be a complete behavioural vocabulary. Any
enemy you want is a recombination of them, or a small override on one:

| Archetype | The idea | The dials that make it |
|---|---|---|
| `grunt` | trash: numerous, readable, dies fast | low health/poise, high aggression |
| `brute` | heavy: huge windup, breaks your guard | slow turn + high poise + `lunge_speed` |
| `skirmisher` | fast, circles, refuses to stand still | high `circle_chance`, low `aggression` |
| `ranged` | holds distance, kites when crowded | `preferred_range` + `backoff_range` |
| `caster` | slow projectile, applies crowd control | `ranged` + long windup |
| `charger` | closes the gap, all-in | high `lunge_speed`, `circle_chance = 0` |
| `ambusher` | asleep until seen | `starts_dormant` |
| `sentinel` | never moves; turret/statue | `stationary` |
| `boss` | leashed, unstaggerable, mixed move list | `leash_range`, high poise, three moves |

Note what makes a brute a brute: it is not a class, it is `turn_rate_deg = 120`
(so circling it works), `lunge_speed = 3.4` (so circling *late* does not), and
`poise = 110` (so you cannot stagger it out of its windup). Those three numbers
are the design. They live in the file.

---

## Every field

### `[<row>]`

| Key | Default | Meaning |
|---|---|---|
| `name` | `"Enemy"` | shown by the look tooltip |
| `category` | — | flavour line under the name |
| `inherits` | — | archetype or enemy id this row starts from |
| `tier` | `1` | ranking, for pacing/presentation |
| `boss` | `false` | marks it for the HUD |

### `[<row>.body]`

| Key | Default | Meaning |
|---|---|---|
| `radius` / `height` | `0.35` / `1.8` | the collision capsule. `height` is the **total**, caps included -- the same convention as the player's controller and as Unity's capsule collider. The drawn mesh is built from the same two numbers, so the silhouette *is* the hitbox |
| `mass` | `70.0` | corpse weight; affects the death tumble |
| `eye_height` | `1.5` | where line-of-sight rays start and attacks originate |
| `separation_radius` | `0.9` | crowd spacing: how far it pushes off its neighbours |

### `[<row>.visual]`

| Key | Default | Meaning |
|---|---|---|
| `material` | `"Game/Enemy/Red"` | see the palette below |
| `mesh` | — | OBJ path relative to the asset root. Empty = the placeholder cylinder |
| `scale` | `[1,1,1]` | node scale |
| `hit_flash` / `hit_flash_time` | `0.16` / `0.12` | squash amplitude and duration on being hit |
| `cast_shadows` | `true` | stencil shadow casting |
| `blood` | `"human"` | profile in `blood.toml`: spray, gibs, decal and pool together. `"undead_ichor"` for the hollows; `""` for something that does not bleed (see `stone_brute`) |

Placeholder palette, chosen so an enemy is identifiable by silhouette and tint
alone at the resolution this renderer actually draws at:

| Material | Reads as |
|---|---|
| `Game/Enemy/RedPale` | fast / fragile |
| `Game/Enemy/Red` | standard |
| `Game/Enemy/RedDark` | heavy / armoured |
| `Game/Enemy/RedElite` | boss (the only full-chroma red in the palette) |

### `[<row>.stats]`

| Key | Default | Meaning |
|---|---|---|
| `health` | `60.0` | |
| `poise` / `poise_regen` | `40.0` / `30.0` | how hard it is to stagger, and how fast that resets |
| `stamina` / `stamina_regen` | `100.0` / `25.0` | gates its attacks; running dry makes it back off |
| `death_impulse` | `6.0` | how hard the corpse is thrown |
| `corpse_time` | `12.0` | seconds before the corpse despawns. `0` = permanent |

### `[<row>.stats.resistances]`

Channel name → value in `-1 .. 0.9`. Positive resists, negative is a weakness.
Channels come from `magic.toml`; a name that is not defined there is logged and
ignored (and the shipped table is checked for this by a test).

### `[<row>.locomotion]`

| Key | Default | Meaning |
|---|---|---|
| `walk_speed` | `1.6` | patrol / search |
| `chase_speed` | `3.2` | closing |
| `strafe_speed` | `2.0` | circling and backing off |
| `acceleration` | `14.0` | m/s² toward the desired velocity. Low = committed, heavy |
| `turn_rate_deg` | `300.0` | yaw slew. **The single most important dial for whether circling works** |
| `lunge_speed` | `0.0` | forward drift during an attack's committed frames. `0` = none |
| `step_height` | `0.4` | how high a ledge it walks up |

### `[<row>.perception]`

| Key | Default | Meaning |
|---|---|---|
| `sight_range` | `18.0` | |
| `sight_fov_deg` | `140.0` | full cone width |
| `hearing_range` | `5.0` | noticed regardless of facing *and* through walls |
| `lose_sight_time` | `4.0` | seconds without sight before it searches |
| `alert_time` | `0.45` | the reaction beat: it faces you and does nothing |
| `leash_range` | `0.0` | distance from spawn past which it disengages. `0` = follows you anywhere |

### `[<row>.behaviour]`

| Key | Default | Meaning |
|---|---|---|
| `aggression` | `0.6` | 0..1. How readily it commits to an opening |
| `preferred_range` | `0.0` | standoff distance. `0` = 85% of the longest attack range |
| `backoff_range` | `0.0` | inside this it retreats instead of attacking. The ranged-enemy dial |
| `circle_chance` | `0.35` | 0..1 chance to strafe rather than re-close after a swing |
| `reposition_time` | `1.1` | how long a circle/backstep beat lasts |
| `flee_health_pct` | `0.0` | health fraction below which it runs. `0` = fights to the death |
| `dodge_chance` | `0.0` | reserved for sidestepping incoming attacks |
| `think_interval` | `0.2` | seconds between full re-decisions. Bigger = more committed, more readable |
| `starts_dormant` | `false` | asleep until it perceives you |
| `stationary` | `false` | never moves; still turns, telegraphs and attacks |

### `[[<row>.attack]]`

| Key | Default | Meaning |
|---|---|---|
| `id` | `"swing"` | label, for the debug UI |
| `weapon` | `"sword"` | row in `weapons.toml`: the damage payload |
| `min_range` / `max_range` | `0.0` / `2.2` | the band in which this move is selectable |
| `aim_cone_deg` | `45.0` | half-angle the target must be within |
| `cooldown` | `1.6` | seconds before this move is offered again |
| `weight` | `1.0` | relative selection weight among currently valid moves |
| `ranged` | `false` | spawns a projectile at the active frame instead of testing a melee arc |
| `projectile_speed` | `18.0` | m/s, when `ranged` |
| `windup` / `active` / `recovery` | `0.2` / `0.06` / `0.3` | the telegraph, the hit frame, the punish window |
| `stamina_cost` | `15.0` | refused if unaffordable; the enemy backs off instead |
| `poise_damage` | `20.0` | how much of *your* poise a landed hit chips |
| `is_sweep` / `arc` | `false` / `0.0` | wide arc hit; `arc` is the half-angle in radians |

---

## Spawners

A spawn point is **pacing**, not placement. The same struct covers a sleeping
enemy in a room, an ambush that arms when you walk in, an arena that releases
waves, and a corridor that keeps trickling — they differ only in numbers.

```toml
[[spawn]]
enemy             = "hollow"
position          = [0.0, 0.0, -14.0]
count             = 3          # per wave
scatter           = 2.0        # spread, in metres, around `position`
waves             = 1          # 0 = endless
wave_delay        = 0.0        # seconds between waves
clear_before_next_wave = true  # gate the next wave on clearing the last
max_alive         = 8          # cap on this spawner's live enemies
activation_radius = 12.0       # player distance that arms it. 0 = armed at load
deactivation_radius = 0.0      # distance past which it disarms. 0 = never
arm_delay         = 0.35       # the "they heard you" beat before wave one
respawn_delay     = 0.0        # seconds after the last one dies. 0 = gone for good
```

Presets name a pacing shape once:

```toml
[preset.ambush]
count             = 3
waves             = 1
activation_radius = 12.0
arm_delay         = 0.35
```

...and are then referenced either from a spawn entry (`preset = "ambush"`) or
from a **level marker name**: a marker called `enemy.hound.ambush` spawns a
`hound` with the `ambush` pacing. That is how encounter placement stays in the
level file and encounter pacing stays in `spawners.toml`.

Shipped presets: `standing`, `ambush`, `patrol`, `arena`.

---

## The brain

```
                 perceives ──> Alert ──alert_time──> Chase
Dormant ──sees──>    │                                 │
                     │                          in range + off cooldown
Idle ────────────────┘                                 v
  ^                                                  Attack
  │                                                    │ (ActionState owns the timing)
  └── Search <──lose_sight_time── Chase/Circle          v
        │                                    circle_chance / aggression
        └──> arrives at last known ──> Idle              │
                                          Circle <───────┴───────> Reposition
poise broken ──> Stagger (locked; the punish window)
health < flee_health_pct ──> Flee
too far from home ──> walk back ──> Idle
```

Two properties are worth knowing:

**Perception is evaluated every tick, not on the think cadence.** Reacting to
being seen is the one thing that must never lag. Everything else re-decides on
`think_interval`, which is what makes an enemy look committed rather than
twitchy.

**Attacks run on `ActionState`** — the exact component the player's swings run
on. That is not a tidiness point: it means an enemy swing can be deflected,
punished, interrupted and staggered by the systems that already existed, and
that your dodge's invulnerability frames work against enemies without the enemy
code knowing what a dodge is.

---

## Design decisions, and why

**Enemies live on the combat registry, not their own.** An enemy *is* a
combatant that happens to have a brain. A second registry would mean an enemy's
health living somewhere other than where damage is applied, and the two kept in
sync by hand.

**Enemies are capsules, like the player.** A cylinder's hard bottom rim catches
on every step edge and doorway lip, and a player and an enemy of identical
stated size must fit through the same gap. `EnemyBody::straightHeight()` and
`cappedRadius()` derive the Jolt shape, the sweep probe and the drawn mesh from
one place, so they cannot disagree about what `height` meant -- disagreeing is
what produces a hitbox taller than the model.

**Enemies are kinematic bodies, not Jolt character controllers.** Three
reasons, in order of importance:

1. A kinematic body has a `BodyHandle`, and the combat model routes every hit by
   `BodyHandle`. A Jolt character has no body handle, so every enemy would have
   needed a parallel hit-registration path next to the one that already works.
2. A crawler's enemies must hold spacing and telegraph on purpose. A dynamic
   body that the player can shove out of its own windup is not a fight.
3. A kinematic body still blocks the player, still stops arrows, and is still a
   solid `Prop` for every query that already exists.

The cost is that walls become this system's problem: `moveAndSlide` does a
collide-and-slide with a shape cast and a floor snap with a ray. That is ~40
lines, and it buys the three properties above.

**World queries exclude the `Prop` layer.** Not an optimisation. The enemy's own
body is on `Prop`, and a shape cast starting inside its own collider reports
itself at fraction 0 — which reads as "a wall is touching me" and freezes the
enemy where it stands. Excluding `Prop` makes self-hits structurally impossible.
Enemy-vs-enemy spacing is handled by the separation steering instead.

**Enemy projectiles are not Jolt bodies.** A bolt is a point travelling in a
straight line for under a second; a swept ray against the world and a
segment-distance test against the player answer both collision questions
exactly. More decisively: the player is a *character controller* and has no body
to route a physics contact to, so a physics projectile could not have hit them
at all.

**Death is physics, not animation.** The body flips to dynamic and takes the
killing blow's impulse. For a placeholder cylinder that reads better than any
authored pose, and it is the same corpse path the training dummy already proved.

**Arrow impacts are reported by `ProjectileSystem`, not read from raw contacts.**
Only the projectile system knows which half of a contact pair was an arrow.
Reading contacts directly — which is what the code did when the training dummy
was the only target — lets a barrel rolling into an enemy deal arrow damage.

---

## Saving and loading

```cpp
enemysave::writeFile(path, enemysave::capture(enemies, spawner));
if (auto data = enemysave::readFile(path))
    enemysave::restore(ctx, enemies, spawner, *data);
```

Same split as the spawner: a plain `EnemySaveData` snapshot, a **pure** codec
over it, and world-touching capture/restore on top. The codec is what carries
the version and the tests; it round-trips with no registry, renderer or physics
world, which is why it can be tested exhaustively for a few milliseconds a run.

**Saved:** definition id, position/facing/velocity, health, poise, stamina and
their timers, the entire brain (state, timers, last-known target, per-move
cooldowns, and the RNG stream, so a reloaded fight is not a *different* fight),
active status effects, and every spawner's wave/arming state.

**Deliberately not saved**, each for a reason:

| Not saved | Why |
|---|---|
| Corpses | Decoration on a despawn timer. Restoring one means reproducing a ragdoll mid-tumble, which cannot be made to look identical anyway. |
| Projectiles in flight | Sub-second lifetime, and saving them would make the format carry entity references it would then have to remap. |
| The in-progress swing | A save landing between windup and active frame would restore a half-committed attack whose telegraph the player never saw. Enemies reload `Idle`; a saved `Attack` or `Stagger` resolves to `Chase`. |
| `ActiveEffect::source` | Entity ids are not stable across a save, and the status tick never reads source. Kill attribution is lost across a save; nothing else is. |

**Content drift.** A save references definitions and spawners by *id*, never by
index, because it outlives the file it was made against. An enemy whose
definition has since been deleted is dropped with a log rather than guessed at;
a spawner id that no longer exists keeps its authored default state. `decode`
refuses a wrong version outright rather than reading fields that may have
moved, and refuses a truncated blob entirely rather than returning a
half-restored fight.

The debug console's **Enemies** tab has *Save encounter* / *Load encounter*
buttons that go through a real file, so the format itself gets exercised by
hand rather than an in-memory shortcut that would never catch a codec bug.

---

## Tuning it live

The debug console (`` ` ``) has an **Enemies** tab:

- **Spawn** — pick any definition, drop one or five in front of you, clear the
  field.
- **Live** — every enemy with its current AI state, health and distance, plus
  per-row *Kill* (through the real damage path, so the corpse and callbacks
  happen) and *Del*.
- **Tune definition** — sliders over locomotion, perception, behaviour and each
  attack's ranges and timings. Locomotion/perception/behaviour apply **live** to
  everything already on the floor, because those are read from the definition
  every tick. Stats apply to the next spawn, because they were copied into
  components at spawn time.
- **Reload enemies.toml** — re-reads the file. Clears live enemies first: they
  hold pointers into the table.
- **Spawn points** — every spawner's arm/wave/alive state, with a *Trigger*
  button.

---

## Tests

```sh
ctest -R enemy
```

| Test | Covers |
|---|---|
| `enemy_library` | archetype inheritance (field-wise, transitive), replace-not-merge for attacks and resistances, cycle and dangling-`inherits` rejection, derived `preferred_range`, and that the **shipped** `enemies.toml` parses with every resistance naming a real damage channel |
| `enemy_ai` | perception (cone, range, walls, hearing), weighted attack selection with cooldown/range/cone gating, turn rate and wrap, and every state transition: alert beat, chase, commit, stagger lock, search, flee, dormant, stationary, leash, backoff |
| `enemy_save` | round-trip of every field, string interning, and the rejections: bad magic, wrong version, **every** truncation length, absurd element counts, and a zero RNG seed |
| `enemy_spawner` | arming/disarming radii, arm delay, wave gating, pressure waves, `max_alive` throttling, respawn, and marker/preset parsing |

All three run without a renderer, a physics world or a window.
