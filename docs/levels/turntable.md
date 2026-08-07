# The turntable — the model showroom {#doc-level-turntable}

A small vaulted stone chamber with one moving part: whatever is standing on the
plinth. It is what `make demo` runs and what the editor opens when no file is
named, because "put an asset in the game's own renderer and look at it" is the
thing most sessions start by wanting.

```sh
make demo                                   # play the scene as it stands
make demo MODEL=kit.prop_chest              # swap the model
make demo MODEL=meshes/props/prop_malenia.obj FIT=2.4
make demo SHOT=/tmp/x.png FRAME=200         # one frame instead of playing
make demo LIST=1                            # every id MODEL= accepts
make editor                                 # open it and drag things
```

| | |
|---|---|
| Scene | `assets/scenes/turntable.scn` |
| Generator | `tools/author_turntable.py` |
| Palette | `[palette.turntable]` in `assets/config/palettes.toml` |
| Size | 12 × 16 m, 6 m to the vault springing, 94 entities |

## Swapping the model

`MODEL=` takes either a kit prefab id from `assets/config/kit.toml` or any mesh
path under `assets/`. Both end up on the plinth at the same size, because the
script measures the mesh rather than trusting it:

- **Scale** comes from `FIT` (default 2.1 m). A prefab authored in centimetres
  and one authored in metres therefore frame identically — the chest at 0.16 m
  gets a 13× scale and lands beside the 1.83 m character at 1.15×.
- **Height** is the bounding box's floor, so nothing pokes through the stone.
- **Centre** is the *median* vertex, not the box centre. See below; this is the
  one measurement in the script that is not the obvious one.
- **Material**, for a bare mesh with none of its own, is
  `Game/Demo/TurntableNeutral` — mid-grey, two-sided. Override with
  `--subject-material`.

A mesh the script cannot read (anything that is not an `.obj`) is placed at
`--scale` with no auto-fit, and it says so.

### Why the median and not the bounding box

A figure holding a spear out to one side has a bounding box two and a half
metres wide around a body forty centimetres wide. Centring the turntable on that
box centre stands the body *off* the plinth, and — because the axis is what
spins — swings the whole model around a point in mid-air beside itself.

The median ignores the spear. A few hundred vertices on a shaft cannot move the
fiftieth percentile of forty thousand vertices in a body, so the axis lands on
the bulk of the mesh, which is what "centred" means to anyone looking at the
screenshot. The box is still right for how tall a thing is and where its feet
go, so both measurements are taken and each is used for what it is good at.

Compound kit prefabs get the same treatment from the other direction: the
composed box (body *plus* attachments) decides the size, and the root piece
alone decides the axis. A boss's sword is part of how tall it stands and is not
part of where its middle is.

## How the shot is built

Three decisions carry the image, and all three were arrived at by reading
screenshots rather than by reasoning:

**The back bay is an opening, not a wall.** A subject photographed against brick
is a dark shape on a mid-grey field. Against an unlit opening it separates
instantly, and no amount of light tuning buys back what one dark rectangle gives
for free. It has to be the whole 4 m bay: an earlier version used a 2 m doorway,
and a 2 m doorway eleven metres back subtends *less* than a 1.3 m subject five
metres from the lens, so the subject covered its own backdrop exactly and the
frame gained nothing. The opening must out-subtend the thing in front of it.

**The lights are short.** Key warm from the front left, fill cold from the right
at a third of the strength, kicker tucked behind. Every range is around 4–6 m in
a 16 m room, so they light the subject and let the wall behind fall away. Widen
one and the shot flattens into a lit box with something standing in it. The
room's own light — torches, the hanging flame, the vault bounce — is separate
and deliberately weak.

**The camera is axial and derived.** Axial because sliding it sideways slides
the opening out from behind the subject: eleven metres of parallax against five.
The three-quarter interest comes from the subject's resting yaw and the offset
key instead, which costs nothing. Derived because `FIT` is a dial — pin the
distance and a 3 m statue overflows while a 0.3 m trinket becomes a speck. The
distance is solved from `FRAME_FILL`, and `FRAME_BIAS` decides where the
subject-and-plinth block sits vertically.

### What is deliberately absent

Both of these were built, screenshotted, and removed, so they are worth naming
before someone adds them back:

- **A chandelier.** A 2.9 m unlit disc directly above the subject ate the top
  quarter of the frame and laid a hard black edge across the vault. What it was
  there for was the light, so `light_hanging` stayed and the geometry went.
- **Candles.** Two rounds went into finding somewhere a candle cluster would not
  read as a pale featureless post — the floor, then up on the plinth step — and
  it read as a pale featureless post in both. `votive_light` is what is left.

Set dressing lives in the *back* half for a related reason. The frame is only
about 5.7 m wide where the subject stands, so a prop against a side wall 5 m out
is not in shot at all until it is roughly 8 m from the lens; put one nearer and
it does not appear beside the subject, it appears as a pale wedge sawn off by
the bottom corner. The foreground is bare floor on purpose.

## Tuning it

Two ways in, and they do not fight as long as you know which is which.

**The script** owns the design. Everything worth turning is in the `STAGE` block
at the top of `tools/author_turntable.py` — room dimensions, the five light
rigs, the torch colour, the camera's fill and bias. Re-running it rewrites the
`.scn`.

**The editor** owns the fiddling. The scene is an ordinary document: open it,
drag a light with the gizmo, save. `make demo` with no arguments cooks and plays
whatever the file currently says, so hand edits survive.

The one place they collide is `make demo MODEL=…`, which re-authors the file
from the script and therefore discards hand edits. If you tuned something you
want to keep, move the value up into the `STAGE` block first.

The look — fog, grade, bloom, the warm/cool split — is not in the script at all.
It is `[palette.turntable]` in `assets/config/palettes.toml`, which is a
screenshot look rather than the dungeon's survival look: ambient up, fog reach
long, vignette restrained, saturation above 1. See the comments there.

## Cameras

Three, of which one is active. Tick `active` on another (and off the hero) to
reframe without moving anything; a higher `priority` is how one wins.

| Entity | |
|---|---|
| `camera_main` | the hero shot, framing derived from the fitted subject |
| `camera_orbit` | parked; circles the plinth at 9°/s looking at the subject |
| `camera_detail` | parked; a 32° close-up from the front right |

Because the scene authors a camera it plays as a shot rather than as a level —
the player controller stands down and the mouse is not grabbed. That is also
what makes it recordable: `make clip SCENE=assets/scenes/turntable.scn` films
ten deterministic seconds of the model turning. See
[authoring-shots.md](../authoring-shots.md).

## Related

- [authoring-shots.md](../authoring-shots.md) — cameras, orbits, clips
- [scene-editor-entities.md](../scene-editor-entities.md) — the components used here
- `make prefab-viewer` — the older, bare turntable: one prefab, four walls, no
  dressing. Still the right tool when the room would be in the way.
