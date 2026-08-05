# Authoring a shot

How to make a small scene that plays itself, and how to turn it into a GIF or an
MP4. For the components involved see [ecs.md](ecs.md); this is the workflow.

A *shot* is an ordinary `.scn` that happens to contain a camera. Nothing else
about it is special — same editor, same cooker, same runtime — and that is the
point: the thing you record is the thing the game renders.

## The example

`assets/scenes/cozy_lair.scn` is the one to copy. It is 36 entities and the
whole of it is:

| Entity | Carries | Doing |
|---|---|---|
| `prop_crystal` | a mesh, `spin` 42°/s, `shader` | turns in place |
| `prop_raccoon` | a mesh, `spin` −28°/s, `shader` | turns the other way |
| `camera_main` | `camera` fov 55 priority 10, `orbit` r 5.4 facing centre | circles the subject, looking at it |
| `portal_exit` | `exit` | becomes a portal at runtime |
| `light_torch_*` | `light.animation` flicker, four phases | gutter, out of step |
| `light_key` | `light.animation` pulse | breathes over the crystal |
| `camera_close` | `camera`, `active: false` | a parked second framing |

No C++ anywhere in that list.

### Spin in place vs. orbit around a point

Two components, and the choice is one question — *does it move, or does it just
turn?*

| | |
|---|---|
| **Spin** | turns where it stands. The two props here, at different rates and signs, which is what makes them read as two objects rather than one turntable. |
| **Orbit** | travels a ring: `centre`, `radius`, `axis`, `facing`. On the entity itself — no pivot, no parent. |

The camera used to be a `camera_pivot` carrying a Spin with the camera parented
to it, where the child's z offset *was* the orbit radius. Changing the radius
meant editing a transform that did not say what it was, and the pivot was a row
in the outliner that stood for nothing. It is one entity now.

Two things about `Orbit` worth knowing before placing one:

- **`centre` is what it circles *and* what `facing: centre` looks at.** Put it
  at the subject's height, not on the floor: a ring centred at `y = 0` and
  raised with `height` orbits correctly and spends the whole clip looking at
  tiles.
- **The radius is bounded by the room.** At 5.4 m in a 12 m room the camera
  stays inside; push it past the walls and the shot is a close-up of masonry.
  The editor draws the ring in the viewport for exactly this reason.

They compose. `Orbit(facing: free)` leaves the rotation alone, so an entity with
both is carried round *and* turns on its own axis — a moon.

A pivot is still right when a *group* has to revolve as one: a parent is how
several things share a motion.

```sh
make editor SCENE=assets/scenes/cozy_lair.scn
make scene SCENE=assets/scenes/spin_portal.scn   # cook and play it
```

## Building one

1. **Frame it.** Add an entity, give it the **Camera** component, and place it
   with the gizmo. The viewport draws its frustum at the viewport's aspect —
   the wedge it cuts through the room *is* the fov, which is the only way to
   judge one. `far_clip` is the authored plane; the gizmo clamps what it draws
   to 6 m, because 200 m of frustum is two lines leaving the screen.
2. **Orbit it.** Add **Orbit** to the camera. It starts centred where the camera
   already is, so drag the centre onto the subject, set a radius, and pick
   `look at centre`. The viewport draws the ring; keep it inside the walls.
3. **Move the subject** by putting **Spin** on the prop *itself* — no pivot, it
   turns on the spot. Give each prop a different rate and sign; the spins
   reading against each other is most of what makes a ten-second loop
   watchable.
4. **Light it.** Point lights with `animation.mode = flicker` and a *different*
   `phase` each. Identical phases read as a light switch, not fire.
5. **Park alternates.** Copy the camera, reframe it, untick `active`. Switching
   framings is then a checkbox, and a higher `priority` is how one wins.

A scene may hold as many cameras as you like. The active one with the highest
priority is the shot; the rest are notes.

## Recording

```sh
make new-clip NAME=my_teaser              # copy this scene, open it in the editor
make look SCENE=assets/scenes/my_teaser.scn FRAME=300   # one frame, to judge the framing
make clip SCENE=assets/scenes/my_teaser.scn             # ten seconds, to docs/media/
make clip SCENE=... SECONDS=6 WIDTH=480 MP4=1 OUT=docs/media/teaser
```

`make clip` cooks first, so a clip is always of what the `.scn` currently says,
and it keeps the PNG frames — `MP4=1` re-encodes them rather than running the
game twice. `make look` is the fast loop while framing: edit, run, read the PNG.

Under the hood it is the engine's own recorder, which takes its whole
configuration from the command line, so a clip is reproducible without a
rebuild:

```sh
./build/game assets/scenes/spin_portal.map \
    --record docs/media/spin_portal.gif \
    --record-frames 200 --record-fps 20 --record-start 60 \
    --record-width 320 --record-keep-frames --record-frame-dir /tmp/frames
```

- `--record-fps` **pins the simulation timestep**, so the clip is deterministic:
  the same scene records identically on a fast machine and a slow one.
- `--record-start` drops warm-up frames. The level is fully built before the
  first capture, so a load hitch is not baked into the timing.
- `frames ÷ fps` is the length. 200 at 20 is ten seconds.
- `--record-keep-frames` leaves the PNGs, which is how `MP4=1` gets a second
  encode out of one run:

```sh
ffmpeg -framerate 20 -i build/clip-frames/frame_%05d.png \
    -c:v libx264 -pix_fmt yuv420p -crf 20 \
    -vf "scale=720:-2:flags=neighbor" docs/media/spin_portal.mp4
```

Nearest-neighbour scaling and no dithering, both here and in the built-in GIF
encoder: bilinear and error diffusion each turn a low-resolution retro image
into mush.

On a headless machine, run it under `xvfb-run -a`. Captures on the real display
come out black when the window is not focused.

## The editor never loses the viewport to a scene camera

A document's cameras are content the author is placing, so the editor's preview
world is attached with `drivesCamera = false` and the viewport stays the
`EditorCamera`'s. Without that, every preview rebuild — one per keystroke —
would snap the author into the shot they were composing.

That distinction is per *world*, not per scene: it is a fact about who is
watching, not about the file. The game's world drives the camera; a world that
is merely being looked at does not.

## Which loop runs

`game <file>.map` normally plays the map as a level inside the full game —
enemies, HUD, combat. A map that **authors a camera** goes to the stripped loop
(`MapPlay`) instead, and there the player controller stands down entirely: the
mouse is not grabbed and the scene runs on its own. Placing a camera in a scene
*is* saying "look through this", so it does not need a flag as well.

Both overrides exist for when it is wrong:

| Flag | Effect |
|---|---|
| `--play` | full game, even for a scene with a camera |
| `--walk` / `--scene` | stripped loop, even for a scene without one |

That difference is what separates recording a clip from filming someone
playing.

## A worked example

[levels/turntable.md](levels/turntable.md) is this page applied to one shot: the
model showroom `make demo` runs. It is worth reading for the parts that only
show up once you are composing rather than describing — why the camera has to
stay on the room's axis, why the opening behind the subject has to be wider than
the subject, and why the framing is *derived* from the thing on the plinth
rather than typed in.
