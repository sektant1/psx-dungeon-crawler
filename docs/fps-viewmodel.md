# The first-person viewmodel

Where the player's hands are, how they move, and every place those numbers can
be authored. For the weapons themselves — fire modes, projectiles, ARC cost —
see `assets/config/weapons.toml`; this document is about **presentation**.

## The shape of it

```
camera head node                     eng::FpsController::headNode()
  └── first-person-hands node        game::FirstPersonHands
        ├── transform                ← ViewmodelMotion (procedural, per frame)
        └── skinned mesh + pose      ← eng::animation::SkeletalAnimator (clips)
```

Two layers meet on one node and stay separable:

| Layer | Owner | Driven by | Rate |
|---|---|---|---|
| Placement / motion | `ViewmodelMotion` | rig tuning + weapon feel | frame rate |
| Animation | `SkeletalAnimator` | authored clips per weapon | stepped (`viewmodel_fps`) |

They run at different rates on purpose. The clips snap on the stop-motion
channel because that is the shipped look; the *placement* runs smooth, because
bob and sway quantised to 24 Hz read as input lag centimetres from the eye —
the same reason the camera itself is never stepped.

`ViewmodelMotion` (`game/src/ViewmodelMotion.h`) is pure math: no renderer, no
ECS, no ImGui. That is what makes it testable (`game/tests/ViewmodelRigTests.cpp`)
and what lets a future sprite or GLTF presentation ride on the same placement.

### Composition

The pose is a sum of independent layers, each with its own accumulator:

```
base (rig socket + weapon lean)
  + movementBob     player speed, normalised against bobReferenceSpeed
  + idleSway        breathing, faded out by movement
  + lookSway        lags a mouse flick, clamped, pulled back to centre
  + recoil          spring, saturating so automatic fire cannot walk it away
  + landing         dip on touchdown
```

Muting one is setting its multiplier to 0. `motionEnabled = false` freezes the
lot at the socket pose, which is how a placement is authored.

## aim ≠ muzzle

The camera ray decides **where the player aims**. The projectile is spawned at
the **muzzle**, which is a joint on the hand rig plus an offset
(`hands_muzzle_joint`, `hands_muzzle_offset`), and travels toward a point on the
aim ray. So the shot leaves the weapon and still lands on the crosshair, and the
distinction survives a change of presentation — a GLTF viewmodel changes which
joint the muzzle hangs off, nothing else.

`FirstPersonHands::muzzleWorldPosition` is the point; `muzzleJointWorld` is the
frame it is expressed in (what the gizmo manipulates).

## Where the numbers live

The same fields, in four places, in order of precedence:

1. **`assets/config/game.toml`, `[player_viewmodel]`** — the game's default
   framing. Loaded at start-up.
2. **A `ViewmodelRig` component on a scene's camera** — a level's override,
   authored in the editor, cooked into the `.map`. Applied on entering that
   level and dropped on leaving it.
3. **`assets/config/weapons.toml`, per weapon** — `hands_offset`,
   `hands_rotation`, `hands_scale` lean out of the shared socket, plus the feel
   numbers (recoil, bob, sway) that weapon wants.
4. **The debug console's Viewmodel panel (F1)** — live tuning, on top of
   whatever the above resolved to. Copy the block out and paste it back into 1
   or 3 to keep it.

The player's movement and lens follow the same rule: `[player]` in `game.toml`,
overridden by an `eng::ecs::FirstPersonController` component on the camera.

Both components are **authored on the camera entity that is the player's eye**.
In first person the camera *is* the head, so "add a first-person controller to
this camera" is the sentence an author means, and the rig hangs off the same
entity.

## The Viewmodel panel (in-game, F1)

Docked in the World group beside the engine's Player tab, because FOV,
sensitivity and hand placement are one tuning session.

- **Camera** — base FOV, sprint FOV kick, sensitivity, head bob. Restrained on
  purpose: move the viewmodel loudly, the camera subtly.
- **Rig socket** — offset (right/up/forward), rotation, scale, framing presets,
  motion freeze, reload from `game.toml`.
- **Motion layers** — the global multipliers and each layer's own numbers.
- **Weapon presentation** — per-weapon lean, recoil, bob and sway for the
  equipped weapon (or any other), plus **Test fire**, which kicks the rig even
  while the simulation is frozen.
- **Gizmo** — handles drawn over the game view (below).
- **Copy TOML** — the `[player_viewmodel]` block and the weapon's viewmodel
  block, formatted to paste straight back.

### The gizmo

Three targets: **Rig socket**, **Weapon lean**, **Muzzle** (translate only — a
muzzle is a point on a joint, it has no size or orientation of its own). Move,
rotate and scale, local or world axes, with optional snapping.

Two rules make it usable:

- Handles anchor to the **authored** pose, not the animated node. Bob and sway
  would otherwise drag them out from under the cursor mid-drag.
- Motion is **frozen for the duration of a drag** and restored on release, so
  what you place is what you get.

The muzzle is also marked in the view with a ring and a label, which is the only
way to see that `aim != muzzle` is actually true for the weapon in your hands.

## The editor

`Add Component` on any entity — conventionally the camera — offers:

- **First-Person Controller** — move speed, sensitivity, base FOV, sprint FOV
  kick, head bob.
- **Viewmodel Rig** — socket, rotation, scale, the layer multipliers and each
  layer's numbers.

The camera also draws a **hands** mark in the viewport at the socket, with a
line back to the eye, so the offset is a place you can see rather than three
numbers you have to imagine.

Both are mirror components: the authored type *is* the runtime component, so
the cook is a copy and the `.scn` keys are the component's own field names
(see `assets/schemas/scene.schema.json`).

## How to change the framing of every weapon

1. Run the game, F1, **Viewmodel** tab.
2. Drag the socket (or use the gizmo) until the hands sit right.
3. **Copy [player_viewmodel]**, paste over the block in
   `assets/config/game.toml`.

For one level only, put a `ViewmodelRig` on that scene's camera in the editor
instead, and cook.

## How to give a new weapon its own presentation

Adding a weapon is TOML; none of this needs C++.

1. In `assets/config/weapons.toml`, under
   `[player_weapon.<id>.viewmodel]`:
   - `hands_idle_animation` / `hands_draw_animation` / `hands_fire_animation` —
     clips on the shared hand rig.
   - `hands_muzzle_joint` / `hands_muzzle_offset` — where its shots leave from.
   - `hands_offset` / `hands_rotation` / `hands_scale` — how it leans out of the
     shared socket. Keep these small; the framing the player learns belongs to
     `[player_viewmodel]`.
   - `recoil_*`, `movement_bob*`, `idle_sway`, `look_sway` — its feel.
2. Run, press its slot key, tune in the panel, **Copy weapon TOML**, paste back.

The rig, the clips and the motion layers are shared, so weapon #4 costs a table
entry and no renderer or gameplay code.

## Future: GLTF viewmodels

Nothing in `ViewmodelMotion` or `WeaponController` knows what the hands are made
of. A model presentation replaces what hangs off the rig node and which joint
the muzzle names; the socket, the layers, the panel, the gizmo and the authored
components are unchanged. The two seams to keep honest are the ones this
document names: **placement is not animation**, and **aim is not muzzle**.

## Files

| What | Where |
|---|---|
| Rig tuning struct (and component) | `game/src/ViewmodelRig.h` |
| Motion composer, TOML load/emit | `game/src/ViewmodelMotion.{h,cpp}` |
| Hand rig, clips, muzzle | `game/src/FirstPersonHands.{h,cpp}` |
| Panel + gizmo | `game/src/DebugOverlay.cpp` |
| Player wiring | `game/src/PlayerSystem.{h,cpp}` |
| Controller component | `engine/include/eng/ecs/components/FirstPersonController.h` |
| Component registration | `engine/src/ecs/ComponentRegistry.cpp`, `game/src/scene/ComponentRegistry.cpp` |
| Editor authoring | `editor/src/ui/ComponentInspector.cpp`, `editor/src/content/Scene{Source,Writer,Cook}.cpp` |
| Tests | `game/tests/ViewmodelRigTests.cpp` |
