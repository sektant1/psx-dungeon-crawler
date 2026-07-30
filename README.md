<div align="center">

<img src="assets/logo.png" alt="PSX Retro Game Engine" width="360">

# psx-dungeon-crawler

**A first-person PSX-style dungeon crawler, and the C++20 engine written to run it.**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![OGRE 14](https://img.shields.io/badge/OGRE-14-2f6f4f)](https://www.ogre3d.org/)
[![Jolt Physics](https://img.shields.io/badge/physics-Jolt-8a4fbe)](https://github.com/jrouwe/JoltPhysics)
[![EnTT](https://img.shields.io/badge/ECS-EnTT-9a3f3f)](https://github.com/skypjack/entt)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)

</div>

---

OGRE 14 is the rasteriser and nothing else. The scene model, physics integration,
content pipeline, level editor and render profiles are written for this project
rather than borrowed from a general-purpose engine, which is what makes the
presentation reproducible: a fixed-step simulation and a pinnable RNG mean the
same seed and the same frame number produce the same pixels on any machine.

The visual target is a PlayStation-era renderer rebuilt honestly instead of
faked in post: a low internal resolution the game actually renders at, vertex
lighting with banded falloff, affine texture warping, ordered dithering and a
stylize/outline pass. Seven presets ship, from strict console emulation to the
game's own dark-fantasy grade.

<img src="assets/preview.gif" alt="Portal room, running live" width="720">

*No-input loop of the portal room, running live. Same clip as video, without the
GIF's colour quantisation, at [`assets/preview.mp4`](assets/preview.mp4).*

---

## Quick start

```sh
make deps      # toolchain + SDL2 + glm + OGRE, on any usual distro
make run       # build and play
make help      # full target and option reference
```

The first build compiles OGRE from source and takes a while. Every build after
that is incremental. **Never delete `build/`**, and never interrupt a build
mid-link. Either one costs you that first-build time again.

## What is here

| Target | What it is |
|---|---|
| `make run` | the game |
| `make editor` | placement editor: build levels, `F5` to playtest |
| `make material` | editor material staging scene |
| `make demo` | shader sample, for looking at render presets |
| `make cook SCENE=…` | `.scn` to `.map`, the same cooker the editor and CI use |
| `make sim` | headless combat/physics harness, no window |
| `make test` | ctest suite |

## How content flows

```
   .scn  (JSON, authored, committed)
     |
     |   scene_cook  ->  the ONE cooker; editor calls same function
     v
   .map  (binary, derived, never edited by hand)
     |
     v
   game <level>.map
```

`.scn` is the source of truth. `.map` is a build artifact: the editor never
saves one, and a `cook_parity` test asserts that the CLI and the editor emit
identical bytes, so "it worked in the editor" and "it worked in CI" cannot drift
apart.

Levels are assembled from a modular kit (`game/assets/kit.toml`): 4 m cells,
pieces that snap to a cell or a cell edge, all of it data. Adding a piece is a
TOML entry, not a code change.

## The look

The PSX presentation is **frozen**. Dither, vertex lighting with banded
falloff, affine UV warp, the stylize/outline pass and the render presets are a
shipped result, not a work in progress. A refactor has to keep the rendered image
pixel-identical, and `make visual-test` is how that is proven.

Seven presets ship (`PRESET=ps1|ps2|gamecube|n64|pixel-3d|modern-ps1|dungeon`).
The default is `dungeon`, the game's own dark-fantasy grade rather than an era
emulation.

## Command line

The arguments below live on the `game` executable. Everything else is an
environment variable, and `make` exposes both as plain make variables.

| Argument | What it does | Example |
|---|---|---|
| `<file.map>` | Play cooked map instead of procedural dungeon. Any positional arg ending in `.map`. | `./game level.map` |
| `--scene <name>` | Starting scene framing. `portal` poses camera at down-portal showcase and freezes sim. Clips and screenshots shoot from here. | `./game --scene portal` |
| `--render-preset <name>` | Starting preset: `ps1`, `ps2`, `gamecube`, `n64`, `pixel-3d`, `modern-ps1`, `dungeon`, `default`. Starting look only. Console still switches live. Unknown name warns, falls back to default. | `./game --render-preset ps1` |
| `--record <path.gif>` | Record clip, encode GIF, exit. Needs `ffmpeg` on PATH. Pins frame delta to `1/fps`, so clip is reproducible and plays back at speed it was simulated. | `./game --record clip.gif` |
| `--record-frames <n>` | Frames captured (default 120). One rendered frame = one GIF frame. | `--record-frames 60` |
| `--record-fps <n>` | Playback rate, and fixed timestep while recording (default 20). | `--record-fps 24` |
| `--record-start <n>` | First frame captured (default 60). Warm-up before it thrown away, so load hitch cannot land in clip. | `--record-start 120` |
| `--record-width <px>` | Scale GIF to this width, nearest-neighbour, height by aspect. `0` (default) keeps render resolution. | `--record-width 480` |
| `--record-loops <n>` | GIF loop count. `0` (default) loops forever. | `--record-loops 1` |
| `--record-frame-dir <dir>` | Where intermediate PNGs go. Default `<output>.frames`. | `--record-frame-dir /tmp/f` |
| `--record-keep-frames` | Keep PNG sequence after encoding instead of deleting. | `--record-keep-frames` |

Preset precedence: `--render-preset` > `PSX_RENDER_PRESET` > `dungeon`.

Piping the game's output into `grep ... | head` kills the process with SIGPIPE
partway through, which truncates a recording. Redirect to a file instead.

Other executables:

| Command | What it does |
|---|---|
| `mapgen <seed> <out.map>` | Generate `.map` from BSP seed. No window. |
| `scene_cook <file.scn> --kit <kit.toml> [--out <file.map>] [--rewrite <file>] [--validate-only] [--assets <dir>]` | The cooker. Same function editor calls in-process. |
| `game_sim [script.txt]` | Headless combat/physics harness. Built-in smoke script with no arg. |
| `scene_editor [file.scn]` | Placement editor. Opens ritual boss showroom when given nothing. |
| `psx_demo` | Shader sample. |

### Environment variables

Presence-only flags care that the variable is set, not what it is set to.

| Variable | What it does | `make` variable |
|---|---|---|
| `PSX_GEN_SEED` | World seed for procedural dungeon. | `SEED=` |
| `PSX_RENDER_PRESET` | Starting render preset, by name. | `PRESET=` |
| `PSX_SHOWROOM_MAP` | Override editable depth-zero showroom TOML. Layout iteration, no rebuild. | `SHOWROOM=` |
| `PSX_SHOWCASE_PORTAL` | Portal showcase pose. Same as `--scene portal`. | `PORTAL=1` |
| `PSX_NO_SHOWCASE_LABELS` | Kill showroom floating plaques. | |
| `PSX_GEN_DUMP=<seed>` | Print that seed grid to stdout, exit. No window, no OGRE. | |
| `PSX_SCREENSHOT` | Write PNG at `PSX_SCREENSHOT_FRAME`, exit. Also pins 1/60 timestep, which is what makes capture a pixel-diff oracle. | `SHOT=` |
| `PSX_SCREENSHOT_FRAME` | Which frame to capture (default 90). | `FRAME=` |
| `PSX_FIXED_DT` | Fixed frame delta without capturing. Lands on a chosen animation phase. | `FIXED_DT=` |
| `PSX_BENCH_FRAMES` | Sample N frames after 60-frame warm-up, log p50/p95/p99/max, exit. | `BENCH=` |
| `PSX_PROFILE` | Log per-phase frame timings. | `PROFILE=1` |
| `PSX_SHOW_COLLIDERS` | Start with collider overlay on. `F3` toggles live. | `COLLIDERS=1` |
| `PSX_WIREFRAME` | Start with mesh wireframe on. `F2` toggles live. | `WIREFRAME=1` |
| `PSX_DEBUG_UI` | Start with debug console open. | |
| `PSX_FULLSCREEN` | Window fullscreen-desktop. | |
| `PSX_EDITOR_MATERIAL` | Editor starts in material-staging mode (`make material`). | `MATERIAL=1` |
| `PSX_RENDERDOC_FRAME` / `PSX_RENDERDOC_CAPTURE` | Frame to capture, where to write `.rdc`. | `RENDERDOC_FRAME=` / `RENDERDOC_OUT=` |

### Keys

`F1` console, `F2` wireframe, `F3` colliders, `F4` frame times, `F5` playtest (in
editor). Gameplay bindings live in `game/assets/game.toml` under `[bindings]`:
`WASD`/`Space`/`Shift`/`Ctrl`/`C` move, `E` interact, `X` swap weapon,
`F`/`Q`/`R` arrow/spell/beam, `B`/`V`/`G` dodge/deflect/kick.

## Recipes

### Record GIF of portal showcase

```sh
cd build && ./game --scene portal \
    --record ../assets/portal.gif \
    --record-frames 60 --record-fps 20 --record-start 120 --record-width 480
```

Sixty frames at 20 fps is a three-second loop. `--record-start 120` throws away
six seconds of warm-up so no load hitch lands in the clip. Frames go to
`portal.gif.frames/` and are deleted once ffmpeg succeeds; pass
`--record-keep-frames` to keep them, which is also how you inspect a failed
encode.

### Deterministic screenshot

```sh
make screenshot SHOT=/tmp/shot.png FRAME=200
```

The capture pins a fixed timestep and a fixed particle seed, so the same frame
number yields the same pixels run to run, which is what makes it usable as a
regression oracle. A black frame is usually the window being unfocused or
offscreen rather than a regression; step the frame number before chasing it.

### Compare render presets

```sh
./game --render-preset ps1
./game --render-preset gamecube
```

To switch live: `F1`, Render tab, Render Profile. The console seeds its editable
copy from the preset actually applied at startup, so the sliders start from the
running look. Edits apply immediately, and "Copy profile as TOML" in the Shaders
tab dumps the result.

### Iterate dungeon layout, no rebuild

```sh
cp game/assets/showroom.toml /tmp/showroom.toml
$EDITOR /tmp/showroom.toml
make run SHOWROOM=/tmp/showroom.toml
```

The showroom is an ASCII grid: `#` solid, `.` floor, `A` arch, `L` torch,
`S` spawn, `C` world-origin anchor, `X` exit portal, `H`/`B`/`R`/`V` dressing.
Keep every row the same width and exactly one `S`, one `C` and one `X`.

### Reproduce a generated dungeon

```sh
PSX_GEN_DUMP=42 ./game        # print grid for seed 42, no window
make run SEED=42              # play it
make mapgen SEED=42 OUT=42.map && cd build && ./game 42.map
```

### Measure a frame-cost change

```sh
make bench BENCH=300
```

Discards the first 60 frames so shader and texture warm-up cannot masquerade as
steady-state spikes, then logs p50/p95/p99/max.

### Geometry looks wrong

```sh
make run COLLIDERS=1 WIREFRAME=1
```

`F3` and `F2` toggle the same overlays live. The collider overlay draws what
physics actually sees, which is the fastest way to tell a bad mesh from a bad
collider.

## Debugging

Every app is an `eng::Application` driving the same renderer, so the debug
targets take `APP=` and work against whichever is quickest to reproduce in:

```sh
make renderdoc APP=scene_editor      # launch under RenderDoc, capture with F12
make renderdoc APP=game FRAME=200    # headless, capture exactly frame 200
make gdb APP=game BATCH=1            # run to completion, backtrace, exit
make valgrind APP=game FRAME=120     # memcheck, exits on its own
make perf APP=game BENCH=600         # CPU profile + hot paths
make screenshot SHOT=/tmp/x.png      # deterministic capture at fixed timestep
```

See [`docs/debugging-renderdoc.md`](docs/debugging-renderdoc.md) for how to read
a capture of this renderer: which pass is which, and what each render target
holds.

## Layout

```
engine/         the engine. eng_core -> eng_platform -> eng_systems
                -> eng_framework -> eng. A layering violation is a link error,
                and tools/check_layering.py catches the header-only ones.
game/
  src/          the game
  content/      .scn/.map/kit, shared by the game, the cooker and the editor
  editor/       the placement editor (ImGui + ImGuizmo)
  tools/        scene_cook
samples/        the PSX shader demo
docs/design/    GEDD.md is the architecture document
```

## Rules that are load-bearing

1. **Third-party libraries never leak.** Game code includes `eng/*.h` and GLM,
   never an Ogre or SDL header.
2. **One scene model.** The EnTT registry is the truth; the renderer is a view.
3. **Data-driven content.** Weapons, particles, levels, palettes and the kit are
   TOML and components, not hardcoded.
4. **Determinism.** Fixed-step simulation, pinnable RNG, reproducible captures.
5. **The image is frozen.** See above.
