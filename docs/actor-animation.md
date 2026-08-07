# Actors: the shared body {#doc-actor-animation}

Every actor in the game — the player's third-person avatar, every enemy, every
NPC — wears the same rigged humanoid and plays from the same clip library. This
document is about that body: where it comes from, how it is posed, and what to
change when you want it to move differently.

For the player's *hands*, which are a separate rig on a separate node with
separate clips, see [`fps-viewmodel.md`](fps-viewmodel.md).

## Why one rig

Before this, three systems each built their own placeholder: the player's avatar
was a capsule primitive, an enemy was a tinted capsule sized from its
definition, an NPC was a capsule with a different default colour. Three code
paths for the same idea, and no animation in any of them.

They now share `game::actor::ActorVisual`: one node, one skin instance over
**one** shared `SkinnedMesh`, and one `ActorAnimator`. A room of thirty
creatures is thirty skin instances over a single upload — which is the reason
the rig is shared rather than loaded per system.

An actor that wants to look different overrides a **material**, not a mesh. The
mannequin's colour is baked into its vertices, so `Game/Enemy/RedDark`
multiplies over the same geometry and costs nothing extra.

## The pipeline

```
assets/source/models/base_player_mesh/Humanoid.blend   the kit's mannequin, unrigged
        │
        │  tools/author_humanoid_rig.py        (Blender; builds rig + clips)
        ▼
assets/source/models/actors/humanoid_rig.glb           rigged, skinned, 20 clips
        │
        │  gltf2ozz  ← assets/config/humanoid_rig.ozz.json
        ▼
assets/animations/actors/humanoid/*.ozz                skeleton + clips (checked in)
assets/meshes/actors/humanoid_rig.glb                  skinned geometry (checked in)
```

Both halves are checked in, so a normal build and a fresh checkout never need
Blender. To regenerate after changing a pose:

```sh
cmake -S . -B build -DENG_BUILD_ANIMATION_TOOLS=ON
cmake --build build --target cook_actor_humanoid
```

or run the authoring step alone, which is faster while iterating:

```sh
tools/author_humanoid_rig.py --preview /tmp/poses --preview-view side \
                             --preview-clips walk_f,run_f
```

`--preview` renders frames straight out of Blender. Use it: a walk reads from
the side and a strafe reads from the front, and the three-quarter view that
looks best hides the errors in both.

### Why the clips are procedural

There is no animator on this project and no motion capture in the repository.
The clips are written as keyframe tables in `author_humanoid_rig.py` — poses
composed from named rotations (`pitch`, `yaw`, `roll` in character axes), eased
and sampled at 30 Hz. That makes them diffable, reproducible, and tunable from
one place. Authored clips can replace any of them later without touching a line
of runtime code: the runtime knows clip *names*, not where they came from.

## Two conventions that bite

**Forward is a node's local −Z.** Two independent places in the engine say so:
the camera's view matrix is `inverse(cameraWorld)` (the GL convention, camera
looks down −Z), and `FpsController::forward()` is `(-sin yaw, 0, -cos yaw)`.
The rig is therefore authored facing −Z (`YAW_FLIP` in the script). The first
build of it faced +Z and every actor walked backwards.

`EnemySystem` has its own local `forwardOf(yaw) = (sin, 0, cos)` — the
opposite. That never showed while enemies were rotationally symmetric capsules.
It is converted at one boundary, in `EnemySystem::syncRender`, rather than by
re-authoring the AI's vector maths that every brain state depends on.

**The anchor is named, not assumed.** Three owners disagree about what an
actor's position means, and each was right about itself:

| Owner | Its position is | `ActorAnchor` |
|---|---|---|
| player | the character node, at the **feet** | `Feet` |
| enemy | a Jolt capsule transform, at the **centre** | `Centre` |
| NPC | a node authored raised by half its height | `Centre` |

`ActorVisualDesc::anchor` states which; `ActorVisual` does the arithmetic once.
Before that each call site converted by hand, and the player was buried to the
waist because it copied a `+0.85` that only made sense for a primitive whose
origin is its centre.

## How a pose is built

`ActorAnimator` composes four layers and owns all the clocks:

```
locomotion   idle/walk/run, phase-locked, direction-blended   full body
posture      jump / fall / land                               full body
action       attack / cast / hit / stagger / death            upper mask + lower mask
look         chest → neck → head                              joint overlay
```

Each is a separate method with its own numbers, so a change to how attacks blend
cannot alter how a run reads.

**Locomotion is driven by phase, not by per-clip clocks.** One phase advances
every ground cycle, so `walk_f`, `walk_l` and `run_f` are read at the same point
in their own durations and a diagonal blend keeps both feet on the same beat.
Cadence comes from stride length (`walk_stride`, `run_stride`) — the in-place
equivalent of extracted root motion. Get the stride wrong and the feet skate;
it is the most visible error a locomotion system can have.

**Strafes pair with forward or back, never across.** Strafe cycles are authored
with one leg crossing in front of the other so they blend cleanly with the
forward run. Blended against the *backward* run, that same crossing drives one
leg through the other. So there are two hemispheres, and they cross over through
the near-pure-sideways pose where the pairing does not matter.

**The action layer is masked, and the mask is feathered.** An attack drives the
upper body at full strength while the legs keep running. The mask grades in over
two joints of spine rather than switching on at one — a hard boundary makes the
masked half look bolted onto a lower body that never heard about it.

**Look-at is an overlay, not a clip.** A rotation per joint applied to the
*local* pose, between blending and the local-to-model pass, spread down
chest → neck → head so the turn propagates instead of the head snapping round on
a still neck. Being an overlay on the local pose is what makes it free: the
exact version needs the global pose and a second local-to-model pass.

### The engine underneath

`eng::animation::PoseBlender` (engine/src/animation/SkeletalAnimation.cpp) does
the sampling and blending. It is deliberately stateless between frames — it
holds scratch buffers and the rig, not a playhead — because whoever owns the
state machine owns the clocks. That is what lets locomotion hold a foot phase
across a walk-to-run transition.

`SkeletalAnimator`, the older single-clip crossfading player, is unchanged and
still drives the first-person hands. Two different jobs: a viewmodel plays one
clip at a time, a body never does.

## Tuning

Everything lives in [`assets/config/actors.toml`](../assets/config/actors.toml)
— thresholds, strides, blend times, the look chain and its per-joint share, and
the state → clip name table. Gameplay names a *state*; that table says which
clip plays it, so a creature with its own clip set overrides the rows it has and
inherits the rest.

The locomotion thresholds are validated on load (idle < walk < run, positive
strides): a mis-ordered set is the kind of typo that reads as "animation is
broken" rather than as a bad number.

## Stop motion

Actor poses advance on `StepChannel::Characters`, the same quantised clock that
decides whether the transform is copied this frame. That is the shipped PSX
look, not an oversight — a smoothly animated skeleton on a stepped transform
reads as two creatures fighting over one body.

The player is the one split: its *body* is placed smoothly (quantising your own
movement reads as input lag) while its *pose* steps on the creature channel, so
the avatar animates in the same beats everything else does. The first-person
hands already use exactly this split.

## Failure is survivable

Every consumer checks `ActorRig::valid()` and falls back to the primitive body
it used to draw. A missing rig, a bad `actors.toml`, a failed skin upload — each
costs the game its animation, not its ability to start.

One trap worth knowing: `Renderer::clearScene()` destroys every mesh it holds,
skinned ones included. A rig uploaded once at start-up is a dead handle by the
first frame of the first level. It is reloaded after every level build, next to
`mParticles.reregisterAll` in `main.cpp`.

## Adding a creature

Nothing here needs new C++:

1. Add a row to `enemies.toml` with a `material` and a `body.height`. The rig
   scales to that height, and the drawn silhouette stays inside the capsule
   built from the same number.
2. Leave `visual.mesh` empty. Naming a mesh opts that creature out of the shared
   rig and back onto the old static path, which is still there for anything
   genuinely not humanoid.
3. Run it.

To give one creature its own moves, point `[actor.clips]` rows at different clip
names and cook a rig that has them.

## Known gaps

- **No ragdoll.** Death is an authored collapse and the capsule stays kinematic
  underneath it. The old primitive path threw the corpse as a rigid body, which
  reads well for a cylinder and badly for a humanoid — the capsule would roll
  while the rig stood to attention inside it. A ragdoll is the real answer and
  is not built.
- **No foot IK.** Actors on stairs and slopes plant into the geometry.
- **No additive layers.** Stance variation and locomotion noise are the two
  places they would pay off first.
- **NPCs do not walk.** They stand, look and gesture. The animator already knows
  what a walk is; the input struct is filled with zero velocity.
