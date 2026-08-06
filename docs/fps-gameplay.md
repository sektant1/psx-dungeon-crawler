# FPS gameplay: movement and weapons

How the player moves, and how a weapon gets from a button press to a hit. For
where the hands sit and how they bob, see
[fps-viewmodel.md](fps-viewmodel.md) — that document is about *presentation*,
this one is about *simulation*. The two meet in exactly one place, named at the
end.

## Movement

`eng::FpsController` is the simulation: one Jolt character capsule, the view
angles, and the locomotion states (run, sprint, walk, crouch, slide, dash). It
does **not** decide where the camera sits — that is an `eng::CameraRig` it
drives, which is what lets the same character be played in first or third
person without the controller learning which.

### The numbers are data

Every tuning value is an `eng::MovementTuning`, authored in
`[player.movement]` in `assets/config/game.toml`:

| Key | What it decides |
|---|---|
| `ground_acceleration` | m/s² toward the input while grounded |
| `ground_friction` | m/s² back to rest with no input |
| `air_acceleration` | m/s² while airborne |
| `jump_velocity` | upward m/s on jump |
| `gravity` | **fallback only** — see below |
| `sprint_multiplier` / `walk_multiplier` / `crouch_multiplier` | scale on the run speed |
| `coyote_time` / `jump_buffer_time` | jump forgiveness, seconds |

Move speed is deliberately **not** in that table. It is `[player] speed`, which
is also what a level's `First-Person Controller` component overrides, and two
places to set one number is how they drift apart.

`gravity` is likewise a fallback: with a physics world attached the player falls
at `[physics] gravity`, because the player and the barrel it walks into have to
agree about which way is down. The tuning value is used only where there is no
world — tests and tools.

A tuning is applied **whole or not at all** (`validMovementTuning`). A rejected
table logs and leaves the defaults, because a controller running new
acceleration against old friction is a feel nobody authored and nobody can
reproduce.

### Tuning it live

**F1 → Viewmodel → Movement.** The sliders edit the live tuning in place, so a
change is felt on the next fixed step — no respawn, no reload. The panel derives
*ms to full speed* and *jump apex* from the current numbers, because those are
what a designer is actually aiming at, and **Copy [player.movement]** produces
the block to paste back into `game.toml`.

It sits in the Viewmodel tab beside Camera rather than in a tab of its own:
FOV, sensitivity, hand placement and how fast you move are one tuning session,
and splitting them across tabs means dragging a slider you cannot see the
effect of.

### Why the air model is Doom-like, not Quake-like

Airborne input steers the velocity toward a *clamped* target at
`air_acceleration`, so air speed can never exceed ground speed and there is no
strafe-jump acceleration to discover.

This was a decision, not an omission. Quake's air acceleration is a different
velocity integrator (it accelerates along the projection of the wish direction,
uncapped), so it could not have been reached by tuning these constants
differently — it is a rewrite of the same function. The brief asked for
"predictable air movement", and a movement tech that rewards a technique the
level design does not teach is the opposite of that.

If it is ever wanted, the place to change is `FpsController::simulate` and the
`approach()` toward `target`, and it should arrive as a *mode* on
`MovementTuning`, not as a retune.

## Weapons

A weapon is a row in `assets/config/weapons.toml`. Nothing about firing is
per-weapon C++, and the ambition is that adding a weapon never becomes any.

```
Input ──> PlayerSystem::sampleWeaponInput   (once per frame, edges captured)
            │
            v
          WeaponController                  (fixed step: cooldown, switch lock,
            │                                ARC spend, buffered input)
            │  returns "weapon index fired"
            v
          CombatSystem::fireWeapon          <── the only branch on weapon kind
            │
    ┌───────┴────────┬──────────────────┐
    v                v                  v
ProjectileSystem   WeaponDeliverySystem (melee)   WeaponDeliverySystem (hitscan)
    │                │                  │
    └────────────────┴──────────────────┘
                     │  one impact callback
                     v
              damage · audio · hit feel · blood
```

Everything above `fireWeapon` runs identically for every weapon; everything
below reports the same impact event. The kind of weapon is visible in exactly
one `switch`.

### Fire modes

`fire_mode` selects the delivery. Omitting it means `projectile`, so weapons
authored before the key keep their behaviour.

| `fire_mode` | Needs | What happens |
|---|---|---|
| `projectile` | `[.projectile]` | a Jolt body is spawned at the muzzle and travels; damage on contact |
| `melee` | `[.melee]` | a sphere is swept ahead of the eye during a timed window; each body hit once |
| `hitscan` | `[.hitscan]` | a ray resolves at once, optionally drawing a beam |

Only the selected delivery's block is validated. A weapon keeps the blocks it is
not using, so retuning one from a bolt into a swing and back is two edits rather
than a rewrite.

**There is no `spell` mode, deliberately.** A spell is not a fourth way to reach
a target — it is a projectile or a hitscan that costs ARC and wears a school's
glow, and `arc_cost` and `[.viewmodel] glow_school` are keys every weapon
already has. Both shipped weapons are spells by that definition. A separate mode
would have been two code paths differing only in vocabulary, which is how a
fireball and a bolt acquire separate bugs.

This replaced three singleton systems — one fireball, one beam, one melee swing,
each with its own global block in `[combat.*]` — which is why the game could
only ever have one of each. Those files are gone; the history is in
`CombatConfig.h`.

### A melee swing is a window, not an instant

`windup` then `active`, both in seconds. During the active window the shape is
swept every fixed step and each body is recorded so it cannot be hit twice —
without that, a swing does `active / fixedDt` times its authored damage.

The eye and view direction are passed to `fixedUpdate` every step rather than
captured when the swing started, so a swing **tracks the player**: turning
mid-swing turns the sweep, which is what circling an enemy while holding the
button is supposed to mean.

### aim ≠ muzzle, in all three modes

The camera ray decides what the player hits. The muzzle decides where the shot
appears to come from. This holds for every delivery:

- **projectile** — spawned at the muzzle, aimed at the point the camera ray hit
- **hitscan** — the *ray* is cast from the eye, and the *beam* is drawn from the
  muzzle to wherever that ray landed. Casting from the muzzle instead would clip
  the doorframe the player is peeking around
- **melee** — swept from the eye, because a swing has no muzzle

The muzzle itself is a socket on the hand rig; see
[fps-viewmodel.md](fps-viewmodel.md#aim--muzzle).

## How to add a weapon

No C++, in any of the three modes.

1. **Pick a payload.** `[weapon.<id>]` in the same file is the damage row —
   base damage, damage type, knockback, any status effect. Enemies draw from
   this same table, so a player weapon and an enemy attack balance against each
   other by construction.
2. **Add `[player_weapon.<id>]`** with a free `slot` (slots must be contiguous
   from 0; slot 0 is what the player starts holding), a `name`, a `discipline`,
   the `payload` from step 1, a `fire_mode`, `fire_interval`, `arc_cost` and
   `trigger` (`press` or `automatic`).
3. **Add the delivery block** its `fire_mode` needs — `[.projectile]`,
   `[.melee]` or `[.hitscan]`. The commented reference at the top of
   `weapons.toml` lists every key with units.
4. **Add `[.viewmodel]`** — for a model weapon, at minimum a `socket`, the three
   `hands_*_animation` clips, and either a `model` or `[[.viewmodel.part]]`
   primitives. For a sprite weapon, `presentation = "sprite"` and one or more
   `[[.viewmodel.sprite_layer]]` rows. See
   [fps-viewmodel.md](fps-viewmodel.md#how-to-give-a-new-weapon-its-own-presentation).
5. **Run it.** Press the slot key, then tune in the F1 → Viewmodel panel and
   paste the block back.

Slots map to keys `1`, `2`, `3` (`weapon_1..3` in `[bindings]`); adding a fourth
key is a binding, not code.

## Where simulation and presentation meet

Exactly two places, and both are one-directional:

- **The muzzle.** `FirstPersonHands::muzzleWorldPosition` is a point on the
  animated rig, handed to `fireWeapon` as the visual origin. If the rig is not
  up, the delivery falls back to the weapon's camera-space `muzzle_offset` and
  nothing else changes.
- **The kick.** A shot tells `ViewmodelMotion::kick` that it happened. The
  viewmodel never tells the weapon anything.

Aim comes from the camera, never from the viewmodel — so no amount of bob,
sway, recoil or shake can send a shot somewhere the player did not point. That
property is why `setViewShake` is applied inside `present()` and nowhere else.

## Files

| What | Where |
|---|---|
| Character controller + `MovementTuning` | `engine/include/eng/controllers/FpsController.h`, `engine/src/controllers/FpsController.cpp` |
| Movement tuning, authored | `assets/config/game.toml`, `[player]` and `[player.movement]` |
| Weapon definitions (all modes) | `game/src/PlayerWeapons.{h,cpp}`, `assets/config/weapons.toml` |
| Firing timing, switch lock, ARC | `game/src/PlayerWeapons.h` (`WeaponController`) |
| Delivery dispatch | `game/src/CombatSystem.cpp` (`fireWeapon`) |
| Projectiles | `game/src/Projectiles.{h,cpp}` |
| Melee sweeps + hitscan | `game/src/WeaponDelivery.{h,cpp}` |
| Player wiring | `game/src/PlayerSystem.{h,cpp}` |
| Tests | `game/tests/PlayerWeaponTests.cpp`, `game/tests/FpsControllerTests.cpp`, `game/tests/WeaponTimingTests.cpp` |
