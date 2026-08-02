<div align="center">

<img src="assets/logo.png" alt="Vulkan Retro 3D Engine" width="800">

# Vulkan Retro 3D Engine

**Low resolution. Full control.**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Vulkan 1.3](https://img.shields.io/badge/Vulkan-1.3-A41E22?logo=vulkan&logoColor=white)](https://www.vulkan.org/)
[![Jolt Physics](https://img.shields.io/badge/physics-Jolt-8a4fbe)](https://github.com/jrouwe/JoltPhysics)
[![EnTT](https://img.shields.io/badge/ECS-EnTT-9a3f3f)](https://github.com/skypjack/entt)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue)](LICENSE)

</div>

## What It Is

Vulkan Retro 3D Engine is a C++20 game engine for low-resolution 3D games.

Vulkan 1.3 drives the renderer. The engine owns the frame, the scene pipeline, physics integration, content tools and runtime. Retro rendering is not a filter bolted onto somebody else's engine.

PS1 is the main reference, not a hardware compatibility target. Render profiles can push toward N64, PS2, GameCube, late-1990s PC or a cleaner modern low-resolution style.

The goal is not to compete with general-purpose engines by feature count. The goal is to make this kind of game properly, keep the machinery visible and make every frame explainable in RenderDoc.

## Core

- Vulkan 1.3 renderer and explicit RHI
- Low-resolution internal rendering
- Vertex snapping and affine-style texture warping
- Dithering, color quantization, fog and palette control
- EnTT scene and gameplay layer
- Jolt physics
- SDL2 platform and input layer
- Fixed-step simulation
- Seeded generation and reproducible captures
- Scene editor, material tools and immediate playtesting
- Shared scene cooker for editor and runtime
- Debug UI, profiling and RenderDoc capture support

This is active engine development, not a stable SDK. APIs and content formats can change.

## Footage

<p align="center"><img src="assets/preview.gif" alt="Portal room running in the engine" width="900"></p>

<p align="center"><img src="docs/media/engine-demo.gif" alt="Engine rendering demo" width="900"></p>

<p align="center"><img src="docs/media/engine-demo-debug-ui.gif" alt="Engine debug UI" width="900"></p>

<p align="center"><img src="docs/media/scene-editor.gif" alt="Scene editor" width="900"></p>

<p align="center"><img src="docs/media/model-viewer-turntable.gif" alt="Model viewer turntable" width="600"></p>

<p align="center"><img src="docs/media/ac_vexian.gif" alt="Model viewer turntable" width="600"></p>


## Build

```sh
make deps
make run
```

The first build compiles the dependencies. It will take longer than later builds.

```sh
make editor SCENE=path/to/scene.scn  # edit, then press F5 to playtest
make scene SCENE=path/to/scene.scn   # cook and run a scene
make prefab-viewer                   # inspect a model
make demo                            # run the renderer demo
make test                            # run the test suite
make help                            # show every target and option
```

Choose a render profile:

```sh
make run PRESET=ps1
make run PRESET=n64
make run PRESET=pixel-3d
make run PRESET=modern-ps1
```

The complete profile reference is in [`docs/render-presets.md`](docs/render-presets.md).

## Content Pipeline

```text
.scn source
    |
    | scene cooker
    v
.map runtime data
    |
    v
game
```

`.scn` files are authored and committed. `.map` files are derived output. The editor and command-line tools use the same cooker.

## Project Layout

| Path | Contents |
|---|---|
| [`engine/`](engine/) | Core, platform, rendering, physics and scene systems |
| [`editor/`](editor/) | Scene editor and content tools |
| [`game/`](game/) | Runtime testbed and engine integration |
| [`assets/`](assets/) | Scenes, shaders, materials and configuration |
| [`docs/`](docs/) | Architecture, renderer and authoring docs |

## Rules

1. Vulkan 1.3 owns rendering.
2. Third-party APIs stay behind engine boundaries.
3. The EnTT registry is scene truth. The renderer is a view.
4. Simulation uses a fixed step. Randomness is seedable.
5. Captures are reproducible. Visual changes are testable.
6. Debug tools ship early. Black boxes do not.

## License

Released under the [GNU GPL v3](LICENSE).
