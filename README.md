# psx-dungeon-crawler

Hand-built C++20 engine. First-person PSX-style dungeon crawler on top. Not
Unity, not Unreal, not Godot. OGRE 14 rasterises, nothing more. Everything above
it (scene model, physics, content pipeline, editor) written for this one game.

<video src="assets/psx_portal_2x.mp4" width="720" controls loop muted></video>

*No-input loop of portal room. Dithering, banded vertex lighting, affine texture
warp, stylize pass. All live. Player not rendering in your viewer? Clip sits at
[`assets/psx_portal_2x.mp4`](assets/psx_portal_2x.mp4).*

---

## Quick start

```sh
make deps      # toolchain + SDL2 + glm + OGRE, on any usual distro
make run       # build and play
make help      # full target and option reference
```

First build compiles OGRE from source. Slow. Every build after: incremental.
**Never delete `build/`.** Never interrupt a build mid-link. Both cost you that
first-build time again.

## What is here

| Target | What it is |
|---|---|
| `make run` | the game |
| `make editor` | placement editor. Build levels, `F5` to playtest |
| `make material` | editor material staging scene |
| `make demo` | shader sample, for looking at render presets |
| `make cook SCENE=…` | `.scn` to `.map`. Same cooker editor and CI use |
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

`.scn` is truth. `.map` is build artifact. Editor never saves one, and a
`cook_parity` test asserts CLI and editor emit identical bytes. So "worked in
editor" and "worked in CI" cannot drift apart.

Levels assemble from modular kit (`game/assets/kit.toml`). 4 m cells, pieces snap
to cell or cell edge, all data. New piece = TOML entry, not code change.

## The look

PSX presentation is **frozen**. Dither, vertex lighting with banded falloff,
affine UV warp, stylize/outline pass, render presets: shipped result, not work in
progress. Refactor must keep rendered image pixel-identical. `make visual-test`
proves it.

Seven presets ship (`PRESET=ps1|ps2|gamecube|n64|pixel-3d|modern-ps1|dungeon`).
Default is `dungeon`: game own dark-fantasy grade, not era emulation.

## Command line

Args below live on `game` executable. Rest is environment variables. `make`
exposes both as plain make variables.

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

Piping game output into `grep ... | head` kills process with SIGPIPE mid-run.
Truncates recordings. Redirect to file instead.

Other executables:

| Command | What it does |
|---|---|
| `mapgen <seed> <out.map>` | Generate `.map` from BSP seed. No window. |
| `scene_cook <file.scn> --kit <kit.toml> [--out <file.map>] [--rewrite <file>] [--validate-only] [--assets <dir>]` | The cooker. Same function editor calls in-process. |
| `game_sim [script.txt]` | Headless combat/physics harness. Built-in smoke script with no arg. |
| `scene_editor [file.scn]` | Placement editor. Opens ritual boss showroom when given nothing. |
| `psx_demo` | Shader sample. |

### Environment variables

Presence-only flags care that variable is set, not what it is set to.

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

60 frames at 20 fps = 3 second loop. `--record-start 120` throws away 6 seconds
of warm-up, so no load hitch in clip. Frames land in `portal.gif.frames/`,
deleted once ffmpeg succeeds. Want them? `--record-keep-frames`. Also how you
inspect a failed encode.

### Deterministic screenshot

```sh
make screenshot SHOT=/tmp/shot.png FRAME=200
```

Pins fixed timestep and fixed particle seed, so same frame number gives same
pixels run to run. That is what makes it a regression oracle. Some frames come
out black on headless/software GL. Step frame number before blaming your change.

### Compare render presets

```sh
./game --render-preset ps1
./game --render-preset gamecube
```

Live switch: `F1` then Render tab then Render Profile. Console seeds its editable
copy from preset actually applied at startup, so sliders start from running look.
Edits apply immediately. "Copy profile as TOML" in Shaders tab dumps result.

### Iterate dungeon layout, no rebuild

```sh
cp game/assets/showroom.toml /tmp/showroom.toml
$EDITOR /tmp/showroom.toml
make run SHOWROOM=/tmp/showroom.toml
```

Showroom is ASCII grid: `#` solid, `.` floor, `A` arch, `L` torch, `S` spawn,
`C` world-origin anchor, `X` exit portal, `H`/`B`/`R`/`V` dressing. Every row
same width. Exactly one `S`, one `C`, one `X`.

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

Drops first 60 frames so shader and texture warm-up cannot masquerade as
steady-state spikes. Then logs p50/p95/p99/max.

### Geometry looks wrong

```sh
make run COLLIDERS=1 WIREFRAME=1
```

`F3` and `F2` toggle same overlays live. Collider overlay draws what physics
actually sees. Fastest way to tell bad mesh from bad collider.

## Debugging

Every app is an `eng::Application` driving same renderer. Debug targets take
`APP=`, work against whichever is quickest to reproduce in:

```sh
make renderdoc APP=scene_editor      # launch under RenderDoc, capture with F12
make renderdoc APP=game FRAME=200    # headless, capture exactly frame 200
make gdb APP=game BATCH=1            # run to completion, backtrace, exit
make valgrind APP=game FRAME=120     # memcheck, exits on its own
make perf APP=game BENCH=600         # CPU profile + hot paths
make screenshot SHOT=/tmp/x.png      # deterministic capture at fixed timestep
```

See [`docs/debugging-renderdoc.md`](docs/debugging-renderdoc.md) for reading a
capture of this renderer: which pass is which, what each render target holds.

## Layout

```
engine/         the engine. eng_core -> eng_platform -> eng_systems
                -> eng_framework -> eng. Layering violation is a link error,
                and tools/check_layering.py catches header-only ones.
game/
  src/          the game
  content/      .scn/.map/kit, shared by game, cooker and editor
  editor/       placement editor (ImGui + ImGuizmo)
  tools/        scene_cook
samples/        PSX shader demo
docs/design/    GEDD.md is the architecture document
```

## Rules that are load-bearing

1. **Third-party libraries never leak.** Game code includes `eng/*.h` and GLM,
   never an Ogre or SDL header.
2. **One scene model.** EnTT registry is truth. Renderer is a view.
3. **Data-driven content.** Weapons, particles, levels, palettes, kit: TOML and
   components, not hardcoded.
4. **Determinism.** Fixed-step simulation, pinnable RNG, reproducible captures.
5. **The image is frozen.** See above.
