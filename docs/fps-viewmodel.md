# The first-person viewmodel

Where the player's hands are, how they move, and every place those numbers can
be authored. For the weapons themselves — fire modes, projectiles, ARC cost —
see `assets/config/weapons.toml`; this document is about **presentation**.

## The shape of it

```
camera head node                     eng::FpsController::headNode()
  └── first-person-hands node        game::FirstPersonHands
        ├── transform                ← ViewmodelMotion (procedural, per frame)
        ├── skinned mesh + pose      ← eng::animation::SkeletalAnimator (clips)
        └── socket node "right_hand" ← ViewmodelSocketSet, follows joint hand.R
              └── weapon             ← game::WeaponViewmodel (mesh or primitives)
```

Three layers meet on one node and stay separable:

| Layer | Owner | Driven by | Rate |
|---|---|---|---|
| Placement / motion | `ViewmodelMotion` | rig tuning + weapon feel | frame rate |
| Animation | `SkeletalAnimator` | authored clips per weapon | stepped (`viewmodel_fps`) |
| Attachment | `ViewmodelSocketSet` | the skeleton pose + authored sockets | after the pose, every frame |

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

## Sockets: how a weapon gets into the hand

A **socket** is a named point on the rig — `(name, joint, offset, rotation,
scale)` — realised as an ordinary scene-graph node whose local transform is
rewritten each frame from that joint's model matrix. Anything parentable is
therefore attachable, and it rides the animation for free.

They are authored in **`assets/config/viewmodel_hands.toml`**, which also names
the rig itself:

```toml
[hands]
skeleton = "animations/viewmodels/arms/arms_rig.skeleton.ozz"
model    = "meshes/viewmodels/arms_rig.glb"
material = "Game/FirstPersonHands"
idle_animation = "relax"

[[hands.socket]]
name   = "right_hand"
joint  = "hand.R"
offset = [0.0, 0.0, 0.0]
```

Two consequences worth stating. Replacing the player's hands is **one file** —
the socket names stay, so every weapon still hangs where it did. And the socket
names are a **vocabulary**: the editor and the F1 panel offer them in a combo
box, so an author picks `right_hand` instead of typing a Blender joint name.

Socket offsets are conventionally zero. A socket says *where on the skeleton*;
the weapon's `attach_offset` says *where in the hand*. Split one distance across
both and you get a weapon nobody can seat — you drag one number and two things
move.

### What hangs there

`game::WeaponViewmodel` builds one of two presentations, chosen by data:

| `[player_weapon.<id>.viewmodel]` | What is built |
|---|---|
| `model = "meshes/viewmodels/weapons/x.glb"` | that mesh, wearing `material` |
| `model` empty, `[[...part]]` rows authored | the primitives, generated |

Both render with `renderOnTop`, like the hands: a first-person weapon is a
presentation element, not a world object, and must not vanish into a wall the
player is standing against. A model that fails to load falls back to the
primitives and says so in the console, so a bad path is a placeholder rather
than an empty hand.

This is the whole model/sprite/primitive seam. `WeaponController` and
`ViewmodelMotion` never learn which branch was taken, so giving weapon #4 a real
model is a path in a TOML.

## aim ≠ muzzle

The camera ray decides **where the player aims**. The projectile is spawned at
the **muzzle**, which is a socket (`muzzle_socket`) or a raw joint
(`hands_muzzle_joint`) plus an offset (`hands_muzzle_offset`), and travels
toward a point on the aim ray. The muzzle is a socket like any other rather than
a special case; naming a joint directly still works, and resolves through the
same maths with an identity socket. So the shot leaves the weapon and still lands on the crosshair, and the
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

- **Attachment** — the socket the weapon hangs on, and its offset, rotation and
  scale inside it. This is where a weapon is seated in the hand.

### The gizmo

Four targets: **Rig socket**, **Weapon lean**, **Weapon attach**, **Muzzle**
(translate only — a muzzle is a point on a joint, it has no size or orientation
of its own). Move, rotate and scale, local or world axes, with optional
snapping.

**Weapon attach** is the one that differs in kind: its parent frame is a live
joint on the animated skeleton, not a camera-space offset, so the handles sit on
the hand and move with it.

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
- **Viewmodel Preview** — show the hands here, holding a weapon picked from
  `weapons.toml`.

The preview builds the **real** `FirstPersonHands`, the real socket set and the
real `WeaponViewmodel` at the camera entity's place in the world — the same
classes the game runs, for the same reason the palette and the HUD are shared
rather than approximated: an approximation would tell you about the editor.

It is authoring scaffolding, not level data. The cook drops it; no runtime
component corresponds to it. Which weapon an author was looking at while placing
a camera is not something the map should carry.

Two implementation notes that matter if you touch it. The rig hangs off a node
`PreviewBridge` owns rather than off the previewed entity's node, because the
ECS preview is torn down and rebuilt on every keystroke and reloading a skeleton
at that rate would make the editor unusable — the node is *placed* to match the
authored camera instead. And a rig that fails to come up is not retried, so a
checkout without the cooked arms does not re-parse a missing skeleton per
keystroke.

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
   - `socket` — which point on the hand rig it hangs off, from
     `viewmodel_hands.toml`. Usually `right_hand`.
   - `model` + `material` — a `.glb` to render. Leave `model` empty and author
     `[[...viewmodel.part]]` rows instead for a primitive placeholder.
   - `attach_offset` / `attach_rotation` / `attach_scale` — where it sits inside
     that socket. These are the seating numbers.
   - `hands_idle_animation` / `hands_draw_animation` / `hands_fire_animation` —
     clips on the shared hand rig.
   - `muzzle_socket` (or `hands_muzzle_joint`) / `hands_muzzle_offset` — where
     its shots leave from.
   - `hands_offset` / `hands_rotation` / `hands_scale` — how it leans out of the
     shared socket. Keep these small; the framing the player learns belongs to
     `[player_viewmodel]`.
   - `recoil_*`, `movement_bob*`, `idle_sway`, `look_sway` — its feel.
2. Run, press its slot key, tune in the panel, **Copy weapon TOML**, paste back.

The rig, the clips, the sockets and the motion layers are shared, so weapon #4
costs a table entry and no renderer or gameplay code.

## How to add a weapon with a real model

The workflow this system exists for, end to end:

1. Drop the mesh at `assets/meshes/viewmodels/weapons/<id>.glb`. Author it with
   its **grip at the origin**, so the attach offset starts near zero.
2. Add `[player_weapon.<id>]` to `weapons.toml` — a slot, a payload, a
   projectile, and under `[...viewmodel]` at minimum:

   ```toml
   socket   = "right_hand"
   model    = "meshes/viewmodels/weapons/<id>.glb"
   material = "Game/ViewModelVesper"
   ```

3. Run the game. Press the weapon's slot key. It is in your hand.
4. **F1 → Viewmodel → Gizmo → Weapon attach.** Drag it until it sits in the
   grip. Motion freezes for the drag, so what you place is what you get.
5. **Copy weapon TOML**, paste back over the section.

No C++, no renderer change, no new component. To judge the placement without
launching the game, put a **Viewmodel Preview** on the scene's camera in the
editor and pick the weapon there.

### If the weapon comes out the wrong size

A socket is a point on the *skeleton*, so a weapon authored in camera space (as
the shipped placeholders were) arrives in the hand at whatever scale the rig
runs at. `attach_scale` is the knob; the two shipped weapons sit at 0.5 and
0.55. This is expected, not a bug — it is the one number a mesh authored to a
different convention always needs.

## GLTF viewmodels, and what is still missing

A `.glb` weapon works today: `model` in the weapon's viewmodel section, hung off
a socket. Nothing in `ViewmodelMotion` or `WeaponController` knows what the
weapon is made of.

What is **not** implemented, and what it would take:

- **Weapon-owned animation.** The weapon mesh is static geometry on a socket; it
  inherits the hand's motion but has no clips of its own. A weapon with a moving
  part needs its own `SkeletalAnimator`, driven from the same fire/draw edges
  `FirstPersonHands` already has.
- **One authored GLTF containing hands + weapon.** Point `[hands] model` at it
  and give the weapon an empty `model` with no parts. Untested; the socket layer
  does not care, but nothing exercises it.
- **Sprite viewmodels.** `WeaponViewmodel` has two branches and would take a
  third; the socket, motion and muzzle layers are unaffected.

The three seams to keep honest are the ones this document names: **placement is
not animation**, **aim is not muzzle**, and **a socket is a point on the
skeleton, not a place in the hand**.

## Files

| What | Where |
|---|---|
| Rig tuning struct (and component) | `game/src/ViewmodelRig.h` |
| Motion composer, TOML load/emit | `game/src/ViewmodelMotion.{h,cpp}` |
| Socket maths + live socket nodes | `game/src/ViewmodelSocket.{h,cpp}` |
| Hands rig definition + loader | `game/src/HandsDefinition.{h,cpp}`, `assets/config/viewmodel_hands.toml` |
| Held weapon: mesh or primitives | `game/src/WeaponViewmodel.{h,cpp}` |
| Editor preview component | `game/src/ViewmodelPreview.h`, `editor/src/viewport/PreviewBridge.cpp` |
| Hand rig, clips, muzzle | `game/src/FirstPersonHands.{h,cpp}` |
| Panel + gizmo | `game/src/DebugOverlay.cpp` |
| Player wiring | `game/src/PlayerSystem.{h,cpp}` |
| Controller component | `engine/include/eng/ecs/components/FirstPersonController.h` |
| Component registration | `engine/src/ecs/ComponentRegistry.cpp`, `game/src/scene/ComponentRegistry.cpp` |
| Editor authoring | `editor/src/ui/ComponentInspector.cpp`, `editor/src/content/Scene{Source,Writer,Cook}.cpp` |
| Tests | `game/tests/ViewmodelRigTests.cpp`, `game/tests/ViewmodelSocketTests.cpp` |
