<div align="center">

<img src="assets/logo.png" alt="PSX Retro Game Engine" width="360">

# psx-dungeon-crawler

**First-person PSX-style dungeon crawler. Plus C++20 engine written to run it.**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![OGRE 14](https://img.shields.io/badge/OGRE-14-2f6f4f)](https://www.ogre3d.org/)
[![Jolt Physics](https://img.shields.io/badge/physics-Jolt-8a4fbe)](https://github.com/jrouwe/JoltPhysics)
[![EnTT](https://img.shields.io/badge/ECS-EnTT-9a3f3f)](https://github.com/skypjack/entt)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)

<img src="assets/preview.gif" alt="Portal room, running live" width="720">

</div>

## Quick start

```sh
make deps      # toolchain + SDL2 + glm + OGRE, first build is slow
make run       # build and play
make help      # full target and option reference
```

## Targets

```sh
make run                 # the game
make editor SCENE=x.scn  # placement editor, F5 to playtest
make cook SCENE=x.scn    # .scn -> .map, same cooker CI uses
make demo                # shader sample
make sim                 # headless combat/physics harness
make test                # ctest suite
```

## Command line

```sh
./game level.map                    # play a cooked map
./game --scene portal               # portal showcase pose, sim frozen
./game --render-preset ps1          # ps1 ps2 gamecube n64 pixel-3d modern-ps1 dungeon
./game --record clip.gif --record-frames 60 --record-fps 20 \
       --record-start 120 --record-width 480
```

- `--record` also takes `--record-loops`, `--record-frame-dir`,
  `--record-keep-frames`. Needs `ffmpeg`.
- Recording pins the frame delta to `1/fps`. Clip is reproducible.
- Preset precedence: `--render-preset` > `PSX_RENDER_PRESET` > `dungeon`.

## Environment

| Variable | What it does | `make` |
|---|---|---|
| `PSX_GEN_SEED` | world seed | `SEED=` |
| `PSX_RENDER_PRESET` | starting render preset | `PRESET=` |
| `PSX_SHOWROOM_MAP` | override the depth-zero showroom TOML | `SHOWROOM=` |
| `PSX_SCREENSHOT` / `PSX_SCREENSHOT_FRAME` | write a PNG at frame N, exit | `SHOT=` / `FRAME=` |
| `PSX_BENCH_FRAMES` | frame-time percentiles, exit | `BENCH=` |
| `PSX_FIXED_DT` | fixed frame delta, no capture | `FIXED_DT=` |
| `PSX_SHOW_COLLIDERS` / `PSX_WIREFRAME` | debug overlays | `COLLIDERS=` / `WIREFRAME=` |
| `PSX_PROFILE` | per-phase frame timings | `PROFILE=` |
| `PSX_GEN_DUMP=<seed>` | print that seed's grid, exit | |
| `PSX_DEBUG_UI` / `PSX_FULLSCREEN` | console open / fullscreen | |

## Keys

| Key | Action |
|---|---|
| `F1` `F2` `F3` `F4` `F5` | console, wireframe, colliders, frame times, playtest |
| `WASD` `Space` `Shift` `Ctrl` `C` | move, jump, sprint, crouch, slide |
| `E` `X` | interact, swap weapon |
| `F` `Q` `R` | arrow, spell, beam |
| `B` `V` `G` | dodge, deflect, kick |
| | rebind in `game/assets/game.toml` |

## Recipes

```sh
# deterministic capture, same pixels run to run
make screenshot SHOT=/tmp/shot.png FRAME=200

# reproduce a dungeon
PSX_GEN_DUMP=42 ./game && make run SEED=42

# iterate a layout without rebuilding
make run SHOWROOM=/tmp/showroom.toml

# frame cost, first 60 frames dropped as warm-up
make bench BENCH=300

# GPU and native debugging, APP=game|scene_editor|psx_demo
make renderdoc APP=game FRAME=200
make gdb APP=game BATCH=1
make perf APP=game BENCH=600
```

See [`docs/debugging-renderdoc.md`](docs/debugging-renderdoc.md).

## Content

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

## Rules

1. Third-party libraries never leak. Game code includes `eng/*.h` and GLM.
2. EnTT registry is truth. Renderer is a view.
3. Content is TOML and components, not hardcoded.
4. Fixed-step sim, pinnable RNG, reproducible captures.
5. The image is frozen. `make visual-test` proves it.
