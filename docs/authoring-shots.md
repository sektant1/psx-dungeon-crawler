# Authoring a shot

How to make a small scene that plays itself, and how to turn it into a GIF or an
MP4. For the components involved see [ecs.md](ecs.md); this is the workflow.

A *shot* is an ordinary `.scn` that happens to contain a camera. Nothing else
about it is special — same editor, same cooker, same runtime — and that is the
point: the thing you record is the thing the game renders.

## The example

`assets/scenes/spin_portal.scn` is the editor's default scene and the one to
copy. It is 92 entities and the whole of it is:

| Entity | Carries | Doing |
|---|---|---|
| `prop_pivot` | `spin` 42°/s | turns |
| `prop_crystal` | `parent: prop_pivot`, a mesh | turns with it |
| `camera_pivot` | `spin` 6°/s, yaw −48° | turns slowly |
| `camera_main` | `parent: camera_pivot`, `camera` fov 52, priority 10 | orbits, looking in |
| `portal_exit` | `exit` | becomes a portal at runtime |
| `light_torch_*` | `light.animation` flicker, four phases | gutter, out of step |
| `light_key` | `light.animation` pulse | breathes over the crystal |
| `camera_close` | `camera`, `active: false` | a parked second framing |

No C++ anywhere in that list. The orbit is composition: the camera is *parented*
to a spinning pivot, so it revolves and keeps looking at the centre because its
facing composes too.

```sh
make editor                      # opens spin_portal.scn
make scene SCENE=assets/scenes/spin_portal.scn   # cook and play it
```

## Building one

1. **Frame it.** Add an entity, give it the **Camera** component, and place it
   with the gizmo. The viewport draws its frustum at the viewport's aspect —
   the wedge it cuts through the room *is* the fov, which is the only way to
   judge one. `far_clip` is the authored plane; the gizmo clamps what it draws
   to 6 m, because 200 m of frustum is two lines leaving the screen.
2. **Move it.** Add an empty entity where the motion should pivot, give it
   **Spin**, and parent the camera to it (drag the row in the Outliner). Sign
   and axis are yours; the camera keeps aiming wherever its own rotation says,
   relative to the pivot.
3. **Move the subject** the same way, at a different rate — the two spins
   reading against each other is most of what makes a ten-second loop watchable.
4. **Light it.** Point lights with `animation.mode = flicker` and a *different*
   `phase` each. Identical phases read as a light switch, not fire.
5. **Park alternates.** Copy the camera, reframe it, untick `active`. Switching
   framings is then a checkbox, and a higher `priority` is how one wins.

A scene may hold as many cameras as you like. The active one with the highest
priority is the shot; the rest are notes.

## Recording

The recorder is engine-side and takes its whole configuration from the command
line, so a clip is reproducible without a rebuild:

```sh
./build/game assets/scenes/spin_portal.map \
    --record docs/media/spin_portal.gif \
    --record-frames 200 --record-fps 20 --record-start 60 \
    --record-width 360 --record-keep-frames --record-frame-dir /tmp/frames
```

- `--record-fps` **pins the simulation timestep**, so the clip is deterministic:
  the same scene records identically on a fast machine and a slow one.
- `--record-start` drops warm-up frames. The level is fully built before the
  first capture, so a load hitch is not baked into the timing.
- `frames ÷ fps` is the length. 200 at 20 is ten seconds.
- `--record-keep-frames` leaves the PNGs, which is how you get a second encode
  out of one run:

```sh
ffmpeg -framerate 20 -i /tmp/frames/frame_%05d.png \
    -c:v libx264 -pix_fmt yuv420p -crf 20 \
    -vf "scale=720:-2:flags=neighbor" docs/media/spin_portal.mp4
```

Nearest-neighbour scaling and no dithering, both here and in the built-in GIF
encoder: bilinear and error diffusion each turn a low-resolution retro image
into mush.

On a headless machine, run it under `xvfb-run -a`. Captures on the real display
come out black when the window is not focused.

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
