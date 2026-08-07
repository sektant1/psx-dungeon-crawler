# Cameras {#doc-camera-systems}

Three shapes of camera, one seam. A scene says which shape it plays in by
carrying one component, and the runtime installs the rig that matches:

| Component (on the camera entity) | Rig | What the scene is |
|---|---|---|
| `FirstPersonController` (or nothing) | `eng::FirstPersonCameraRig` | a world seen from the player's eyes |
| `ThirdPersonCamera` | `eng::ThirdPersonCameraRig` | a world seen over the player's shoulder, with a lock-on |
| `ScreenCamera` | `eng::ScreenCameraRig` | not a world: a 2D screen — menu, HUD plate, dialogue page |

Nothing else in the game changes between them. Same capsule, same weapons, same
level, same fixed step.

## The seam

```
Input ──▶ FpsController ──── simulates the body, owns the view angles
                │
                │  CameraPose (once a frame, already interpolated)
                ▼
           CameraRig ─────── decides where the eye sits
             ├── FirstPersonCameraRig      body ▸ head
             ├── ThirdPersonCameraRig      character │ pivot ▸ boom ▸ eye
             └── ScreenCameraRig           page ▸ eye, fitted to the page
```

`eng::CameraPose` is the whole contract: feet, head offset, view angles, body
facing, roll, speed and strafe ratios, grounded, landing impact. A rig reads it
and writes render nodes. It never reads a physics body, never runs at the
simulation rate, and never writes back into the simulation — *what a camera does
cannot move a capsule*.

Two things flow the other way, and only two:

- `CameraRig::pitchLimitsRadians()` — the rig owns the range (a neck and a boom
  are not constrained by the same thing), the controller enforces it on the
  authoritative angle. Push past the limit and nothing banks up to unwind.
- `CameraRig::viewOverride()` — true only while a lock-on holds the view. The
  controller adopts the angles, so movement stays camera-relative under a lock
  and releasing one is continuous instead of a snap back to the mouse.

Before this, both halves lived in `FpsController::present()`, and a second
camera shape could only have arrived as an `if` in the middle of it.

## First person

The chain is `body` (position + facing yaw) ▸ `head` (bob/crouch offset +
pitch). Unchanged from what the controller used to build itself, so every
viewmodel and carried light still hangs off `headNode()`.

What the rig adds is the feel layer that had nowhere to live:

- **stair smoothing** — the capsule steps up instantly (that is what makes
  stairs walkable) and the eye does not: it keeps its height and catches up
  exponentially. Only while the character stays grounded, because a jump is
  vertical movement the player meant. The single biggest readability win in a
  stepped dungeon.
- **landing dip** — metres of drop per m/s of impact, capped, recovered
  exponentially. Restrained: the viewmodel has the loud version.
- **strafe lean** — a degree or so of roll away from lateral travel.

Each is a named parameter, tunable live in the F1 console's **Camera** tab, and
muting one is setting it to zero rather than editing an expression.

## Third person

Souls-shaped, and the defaults say so: a short boom, a chest-height pivot, a
slow vertical follow, and a lock that frames a point *between* the player and
the target rather than staring at the target alone.

```
character   feet, turned by facingYaw ── an avatar mesh parents here
pivot       smoothed focus + pivot height + shoulder offset
  boom      the orbit: yaw and pitch, nothing else
    eye     +Z along the boom, i.e. behind the look direction; the camera
```

- **Follow** is exponential and frame-rate independent (`1 - e^(-rate·dt)`),
  fast horizontally and deliberately slow vertically. Vertical is where stairs,
  steps and landings live, and a camera that tracks them exactly is the classic
  third-person pumping.
- **Facing** decouples from the view: the body turns towards travel at
  `turnRateDegrees`, or towards the lock-on target — which is what makes
  strafing round an enemy read as circling rather than as walking sideways.
- **The spring arm** is one ray from the pivot back along the boom. It comes in
  *instantly* (a camera that eases into a wall spends those frames inside it)
  and goes back out at `pushOutSpeed`. That asymmetry is what stops a doorway
  strobing, and it is a test, not a claim (`camera_rig` in ctest).
- **Lock-on framing** moves the *pivot* towards the target, capped at 1.35 m, so
  the character never slides off screen; the boom lengthens with range so a
  fight across a room does not shrink both fighters into the same few pixels.

### Lock-on

The rules are the game's (`game::LockOnSystem`), the framing is the engine's.
The engine never learns what a target *is* — it gets a point and a radius.

- **Q** or **middle mouse** toggles. It acquires the candidate nearest to *aim*
  inside the cone, not the nearest body: picking the closest is how a lock ends
  up on the rat at your feet instead of the knight in front of you.
- **Flick the mouse** left or right to switch target. A threshold, decayed, so a
  slow drag never adds up to a switch and a tremor never changes target.
- It drops when the target dies (it stops being a candidate), leaves
  `breakRange`, or stays out of line of sight past `occlusionGrace`. Acquire
  range is *narrower* than break range on purpose: that gap is the hysteresis
  that stops a lock flickering as an enemy backs away at exactly the limit.
- Shots follow the lock — `PlayerSystem::aimDirection()` aims at the target, and
  always from the character's own head, never from the boom (which is metres
  behind a wall as often as not).

**F6** switches between first and third person live, because the feel of a
camera is not a thing anyone can judge from a screenshot.

## 2D screens

A scene carrying `ScreenCamera` is not a world. The camera stands square-on to
the XY plane at exactly the distance where `pageHeight` virtual pixels fill the
view, so entities authored in pixels stay that size at every window resolution:
a 32×32 icon is a 32×32 quad.

- `fit = Height` — the page height always fills the screen; a wider window sees
  past the page's sides. What a HUD wants.
- `fit = Contain` — the whole page is always visible, letterboxed. What a menu
  or a dialogue plate wants.
- `origin = TopLeft` gives the UI convention (y down from the corner);
  `ScreenCameraRig::pagePoint()` converts, so a layout copied off a mock-up is
  typed in the mock-up's coordinates.

Two deliberate limits:

- **The projection stays perspective.** Changing it is changing the renderer,
  and the renderer's image is frozen (see `CLAUDE.md`). Anything on the page
  plane is pixel-exact; layers pushed off it by `layerSpacing` are very slightly
  scaled, which reads as depth.
- **This is layout, not widgets.** Text, bars and anything computed per frame
  still belong to `eng::ui::UiCanvas`, which is already pixel-exact and already
  drawn over everything. What a screen scene adds is the ability to *author* a
  screen — to place and nudge a layout in the editor with the same tools that
  place a torch — and to preview and cook it like any other scene.

At runtime, `MapPlay` sees the component, tells the world to stop driving the
camera (`World::setDrivesCamera(false)`) and hands it to the rig.

## Authoring

In the editor, select the camera entity and add one of:

- **First-Person Controller** — how the player moves and what the lens does.
- **Third-Person Camera** — the boom, the follow, the spring arm, the lock-on.
- **2D Screen** — makes the scene a flat page.

They are mirror components: the authored type *is* the runtime one, so a field
added to `eng::ecs::ThirdPersonCamera` shows up in the inspector, the `.scn`,
the cooked `.map` and the game with no translation step to lose it in.

A level that authors nothing runs on `assets/config/game.toml`:

```toml
[camera]
mode = "first_person"   # or "third_person"
distance = 3.4
pivot_height = 1.45
shoulder_offset = 0.42
follow_rate = 18.0
follow_rate_vertical = 7.0

[camera.lock_on]
acquire_range = 16.0
break_range = 22.0
```

A level that authors a `ThirdPersonCamera` plays over the shoulder regardless of
`camera.mode`; the shape is the more specific statement.

## Tuning

`F1` ▸ **Camera** (World group, beside Player and Viewmodel — where the view
sits, what it is framed at and where the hands are is one tuning session):

- mode, and a switch
- lock-on state and its ranges, cone, flick threshold and grace
- every third-person number, applied on the frame you drag it
- the first-person feel layers

## Tests

- `camera_rig` — smoothing is frame-rate independent, the pitch clamp holds, the
  lock swings onto its target and frames between, the spring arm is fast in and
  slow out, the screen fit puts the page across the view.
- `lock_on` — what it acquires, what it refuses, switching without wrapping,
  the occlusion grace, and the acquire/break hysteresis.

Both run without a renderer, a physics world or a window: `solve()`,
`boomLength()` and `fitDistance()` are split out of `present()` exactly so the
model can be checked as arithmetic.

## Files

| Path | What |
|---|---|
| `engine/include/eng/camera/CameraRig.h` | the seam: `CameraPose`, `CameraLockOn`, smoothing helpers |
| `engine/src/camera/FirstPersonCameraRig.cpp` | eyes: bob, stair smoothing, landing dip, lean |
| `engine/src/camera/ThirdPersonCameraRig.cpp` | boom, follow, spring arm, lock framing |
| `engine/src/camera/ScreenCameraRig.cpp` | the page fit |
| `engine/include/eng/ecs/components/*Camera*.h` | the authored tuning |
| `engine/src/controllers/FpsController.cpp` | the body, the view angles, the pose |
| `game/src/LockOn.{h,cpp}` | what may be locked onto, and when it is let go |
| `game/src/PlayerSystem.cpp` | which rig is installed, and the avatar the third-person one frames |
