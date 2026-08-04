# Viewmodel sockets: hanging a weapon off the hands

*2026-08-04*

## The problem

The first-person rig renders **empty hands**. A weapon cannot be put in them,
and the workflow to try is missing at every step.

Three separate facts, found by reading the tree:

1. **`WeaponViewmodelDef::parts` is dead data.** Every weapon in
   `assets/config/weapons.toml` authors procedural primitives
   (`[[player_weapon.*.viewmodel.part]]`), `validPlayerWeaponDefinition`
   *requires* the list to be non-empty — and the only code that consumes it,
   `ViewModel::initPlayerWeapon` (`game/src/ViewModel.cpp:257`), is never
   called. `ViewModel` has no constructor anywhere in `game/`, `editor/` or
   `samples/`. Authors are filling in a table that renders nothing.

2. **There is no way to attach anything to a joint.** `FirstPersonHands`
   computes `muzzleJointWorld` by hand — node world matrix times
   `mAnimator.modelMatrices()[joint]` — and that ad-hoc calculation is the
   *only* place in the engine that reads a joint's frame. There is no node that
   follows a joint, so "put this mesh in that hand" has no primitive under it.
   This is the single missing piece that makes the whole workflow impossible.

3. **The weapon presentation is not authorable.** `ViewmodelRig` (the shared
   socket) is an editor component on the camera, but everything *per weapon*
   lives in `weapons.toml` and is reachable only through the in-game F1 panel.
   The editor draws a "hands" mark — a cross and a line — and nothing else. You
   cannot see the hands, so you certainly cannot see a weapon in them.

The user's summary is accurate: *"add a weapon, link it to the viewmodel entity
(that doesn't exist), add a weapon FPS component (we don't have), attach it to
the hand"* — none of those four steps has an implementation.

## What already works, and stays

The parts of the system that are good are the reason this is a small change and
not a rewrite:

- `ViewmodelMotion` — the layered placement composer (bob, sway, recoil,
  landing) is pure math, tested, and already separates placement from animation.
- `ViewmodelRig` — the shared camera-space socket, authored in four places with
  a defined precedence.
- `SkeletalAnimator` / `AnimationRig` — cooked ozz skeleton with named joints
  and clips, plus `jointIndex(name)` and `modelMatrices()`.
- `aim != muzzle` — the distinction is real and documented.

None of it changes. What is missing is a layer *below* the weapon and *above*
the skeleton: a **socket**.

## The design

### A socket is a named point on the rig

```
camera head node
  └── first-person-hands node          ← ViewmodelMotion writes this transform
        ├── skinned arms mesh + pose   ← SkeletalAnimator writes this
        └── socket node "right_hand"   ← follows joint hand.R, per frame
              └── weapon presentation  ← mesh, or generated primitives
```

A socket is `(name, joint, offset, rotation, scale)`. Each frame the socket
node's **local** transform is set to `jointModelMatrix * offsetTransform`, so it
rides the animated skeleton while remaining an ordinary scene-graph node that
anything can be parented to. The maths is a free function over a `glm::mat4`,
which makes it testable with no renderer and no skeleton.

This one primitive answers the user's actual question — *"entities that have two
models that need to be together"* — and generalises past weapons: a torch, a
shield, a spell effect at a fingertip are all the same node under a different
socket.

The **muzzle becomes a socket** rather than a special case. `muzzleJointWorld`
stops being bespoke arithmetic and becomes "the world transform of the socket
named by this weapon", which is the same code path everything else uses.

### Hands are authored once, in their own file

`assets/config/viewmodel_hands.toml` owns the rig and its socket vocabulary:

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

Two things follow. Replacing the player's hands is one file, not an edit to
`FirstPersonHands.cpp` (Phase 8 of the brief). And the socket names become a
**vocabulary the editor can offer in a combo box**, the same way enemy ids and
script paths already are — an author picks `right_hand`, they do not type a
Blender joint name they had to go and look up.

### A weapon names a socket and a presentation

`[player_weapon.<id>.viewmodel]` gains:

```toml
socket          = "right_hand"
model           = "meshes/viewmodels/weapons/riven_talon.glb"  # optional
material        = "Game/ViewModelTalon"
attach_offset   = [0.0, 0.0, 0.0]
attach_rotation = [0.0, 0.0, 0.0]
attach_scale    = 1.0
muzzle_socket   = "muzzle"   # optional; else hands_muzzle_joint as today
```

`model` set → the mesh is loaded and attached. `model` empty → the existing
`[[...part]]` primitives are generated instead. That is the whole of the
sprite/model/primitive seam the brief's Phase 7 asks for: **one class, the
presentation chosen by data**, no class hierarchy for three weapons. Dropping a
`.glb` next to a TOML line is the "add weapon #4" workflow, and it requires no
C++.

The dead `parts` path is resurrected rather than deleted, because it is the
placeholder presentation for a weapon whose model does not exist yet — which is
every weapon today.

### The editor gets a viewmodel it can see

A new authored component, **Viewmodel Preview**, on the same camera entity that
carries `FirstPersonController` and `ViewmodelRig`:

```
weapon  = "riven_talon"   (picked from weapons.toml)
visible = true
```

The editor already compiles selected game sources directly (`RenderPalette`,
`GameHud`, `HudModel`), and `FirstPersonHands` depends only on `eng::Renderer`
and a node handle. So the editor instantiates the *real* rig, the *real* socket
set and the *real* weapon presentation under its camera — not an approximation
of them. What you place is what the game renders, which is the property the
existing preview bridge already insists on for the palette and the HUD.

The component is preview-only: the cook drops it, because which weapon the
author was looking at is not level data.

### Tuning: gizmo and TOML round-trip

The F1 panel's gizmo grows a fourth target, **Weapon attach**, beside Rig
socket / Weapon lean / Muzzle, dragging `attach_offset` / `attach_rotation` /
`attach_scale` in the socket's frame. It obeys the two rules the existing
targets already do: handles anchor to the authored pose, and motion freezes for
the duration of a drag.

`viewmodelWeaponToml` emits the new keys, so the existing **Copy weapon TOML**
button round-trips a placement back into `weapons.toml`.

## Files

| Change | Where |
|---|---|
| Socket maths + node set (new) | `game/src/ViewmodelSocket.{h,cpp}` |
| Hands definition + loader (new) | `game/src/HandsDefinition.{h,cpp}` |
| Weapon presentation, mesh or primitives (new) | `game/src/WeaponViewmodel.{h,cpp}` |
| Attachment fields, TOML parse/emit | `game/src/PlayerWeapons.{h,cpp}`, `game/src/ViewmodelMotion.cpp` |
| Sockets + weapon wired into the rig | `game/src/FirstPersonHands.{h,cpp}` |
| Hands asset (new) | `assets/config/viewmodel_hands.toml` |
| Weapon attachment data | `assets/config/weapons.toml` |
| Gizmo target, attach panel | `game/src/DebugOverlay.{h,cpp}` |
| Authored component | `editor/include/editor/content/SceneDocument.h`, `editor/src/content/Scene{Source,Writer,Cook}.cpp`, `editor/src/scene/EntityComponents.cpp`, `editor/src/ui/ComponentInspector.cpp` |
| Editor preview | `editor/src/viewport/PreviewBridge.cpp`, `CMakeLists.txt` |
| Tests (new) | `game/tests/ViewmodelSocketTests.cpp` |
| Docs | `docs/fps-viewmodel.md` |

## As built

Everything above shipped. Three things worth recording that the design did not
anticipate:

**The primitives arrive at the wrong scale, and that is correct.** A socket is a
point on the *skeleton*, so placeholders authored in camera space (0.1–0.34 m,
against a node 0.5 m in front of the eye) land in the hand at the rig's scale.
The two shipped weapons needed `attach_scale` 0.55 and 0.5. This is not a bug to
fix but the one number any mesh authored to a different convention will always
need, and it is documented as such.

**The hand joint's +y runs along the bone.** The first seating attempt rotated
the talon -95° about x to "point the claws forward" and put them through the
floor of the frame. Rotation zero was already right: claws along the fingers.
Offsets that move a weapon "out of the hand" go along **+y**, not -z.

**The editor preview cannot hang off the previewed entity's node.** The ECS
preview is cleared and rebuilt on every document revision — every keystroke —
and re-parenting the rig would mean reloading an ozz skeleton and a skinned mesh
at that rate. It hangs off a `PreviewBridge`-owned node that is *placed* to match
the authored camera instead, which is the same transform by a cheaper route. A
rig that fails to load is also latched, so a checkout without the cooked arms
does not re-parse a missing skeleton per keystroke.

Verified: 131/131 tests pass (including the new `viewmodel_socket` suite); the
weapon renders in the hand in-game; the editor preview accounts for exactly +5
batches and +1376 triangles when switched on, which is the arms mesh plus the
talon's four primitive parts; and a `.scn` carrying `viewmodel_preview`
round-trips through the cooker's rewrite.

## Non-goals

- No renderer change. The image is frozen; a socket is a node and a weapon is a
  mesh, both through APIs that already exist.
- No animation framework. Sockets read the pose the animator already computes.
- No retargeting, no IK, no per-weapon hand skeletons. One rig, named sockets.
- `ViewModel` (the old dead sword/staff/torch class) is left alone rather than
  deleted: removing it is unrelated repository churn, and this change does not
  depend on it.
